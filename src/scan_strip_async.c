/* Consume raw Cube output and run the Pearl transcript and PoW on the CPU. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "blake3_impl.h"
#include "cpu_budget.h"
#include "pearl_pipeline.h"

enum { WORDS = 16, ROT = 13, HALF_ROWS = PRL_RANK / 2 };

static void fold_band(const int32_t *partials, uint32_t transcript[WORDS][HALF_ROWS])
{
    uint32_t acc[PEARL_PART_ELEMS] = {0};
    memset(transcript, 0, WORDS * HALF_ROWS * sizeof(uint32_t));
    for (int p = 0; p < PEARL_FOLDS; ++p) {
        uint32_t folded[PRL_RANK] = {0};
        for (int col = 0; col < PEARL_BAND_COLS; ++col) {
            for (int row = 0; row < PRL_RANK; ++row) {
                const int i = col * PRL_RANK + row;
                acc[i] += (uint32_t)partials[i];
                folded[row] ^= acc[i];
            }
        }
        for (int hi = 0; hi < HALF_ROWS; ++hi) {
            // Row pairs are (0,32)..(31,63), then (64,96)..(95,127).
            const int row = hi + (hi / 32) * 32;
            uint32_t word = transcript[p % WORDS][hi];
            transcript[p % WORDS][hi] =
                ((word << ROT) | (word >> (32 - ROT))) ^ folded[row] ^ folded[row + 32];
        }
        partials += PEARL_PART_ELEMS;
    }
}

static uint32_t load32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int meets_target(const uint8_t digest[32], const uint32_t target[8])
{
    for (int limb = 7; limb >= 0; --limb) {
        const uint32_t word = load32_le(digest + limb * 4);
        if (word != target[limb]) return word < target[limb];
    }
    return 1;
}

static int scan_band(const int32_t *partials, const uint32_t key_words[8], const uint32_t target[8])
{
    uint32_t transcript[WORDS][HALF_ROWS];
    fold_band(partials, transcript);
    for (int hi = 0; hi < HALF_ROWS; ++hi) {
        uint8_t block[64], xof[64];
        for (int w = 0; w < WORDS; ++w) {
            uint32_t word = transcript[w][hi];
            for (int byte = 0; byte < 4; ++byte)
                block[w * 4 + byte] = (uint8_t)(word >> (8 * byte));
        }
        blake3_compress_xof(key_words, block, sizeof(block), 0,
                            KEYED_HASH | CHUNK_START | CHUNK_END | ROOT, xof);
        if (meets_target(xof, target)) return hi;
    }
    return -1;
}

static int scan_partials(const int32_t *partials, int nbands, int threads,
                         const uint32_t key_words[8], const uint32_t target[8])
{
    // Preserve hit precedence from the original worker layout. This groups
    // CPU work only. The Cube output is indexed by band.
    enum { HIT_GROUPS = 2 * PEARL_GRID };
    const int groups = nbands < HIT_GROUPS ? nbands : HIT_GROUPS;
    int hits[HIT_GROUPS];
    if (threads > groups) threads = groups;

    prl_cpu_enter();
    #pragma omp parallel for num_threads(threads) schedule(static)
    for (int group = 0; group < groups; ++group) {
        hits[group] = -1;
        for (int band = group; band < nbands; band += HIT_GROUPS) {
            const int32_t *band_data = partials + (size_t)band * PEARL_FOLDS * PEARL_PART_ELEMS;
            const int hi = scan_band(band_data, key_words, target);
            if (hi >= 0) {
                hits[group] = hi * nbands + band;
                break;
            }
        }
    }
    prl_cpu_leave();
    for (int group = 0; group < groups; ++group)
        if (hits[group] >= 0) return hits[group];
    return -1;
}

int scan_full(const int8_t *a_noised, int num_strips,
              const uint8_t *key, const uint32_t *target,
              int nbands, int n_hi, int pow_threads,
              int *hit_strip, int *hit_tile)
{
    if (num_strips <= 0) return 0;
    if (n_hi != HALF_ROWS || nbands <= 0 || nbands > INT32_MAX / HALF_ROWS) {
        fputs("[pearl-hlc2] unsupported scan shape\n", stderr);
        exit(2);
    }

    uint32_t key_words[8];
    for (int i = 0; i < 8; ++i) key_words[i] = load32_le(key + i * 4);

    int submitted = 0;
    while (submitted < num_strips && submitted < PEARL_PIPE_DEPTH) {
        pearl_submit_partials(a_noised + (size_t)submitted * PRL_RANK * PRL_K);
        ++submitted;
    }
    for (int completed = 0; completed < num_strips; ++completed) {
        const int32_t *partials = pearl_wait_partials();
        const int tile = scan_partials(partials, nbands, pow_threads, key_words, target);
        if (tile >= 0) {
            *hit_strip = completed;
            *hit_tile = tile;
            // Drain queued transfers before the miner swaps B or reuses A.
            for (int pending = completed + 1; pending < submitted; ++pending)
                pearl_wait_partials();
            return 1;
        }
        if (submitted < num_strips) {
            pearl_submit_partials(a_noised + (size_t)submitted * PRL_RANK * PRL_K);
            ++submitted;
        }
    }
    return 0;
}
