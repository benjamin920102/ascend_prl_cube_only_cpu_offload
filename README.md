# `ascend_prl` — the world's first Ascend NPU cryptocurrency miner

[![No Maintenance Intended](http://unmaintained.tech/badge.svg)](http://unmaintained.tech/)

**English** | [中文](README_zh.md)

A from-scratch miner for the [Pearl](https://arxiv.org/abs/2504.09971) Proof-of-**Useful**-Work
coin targeting Huawei Ascend 910B and 910C NPUs.

> **DISCLAMER:** for educational purpose only. Use only with hardware you are authorized to.

This source version runs INT8 matrix multiplication on Cube and all Pearl postprocessing on the CPU. Build from source to use this data path.

## Quickstart

1. **Create a Pearl wallet.** Use the [official wallet](https://github.com/pearl-research-labs/pearl/releases/tag/pearl-wallet-v2.0.0)
   and follow their instructions. You need an address like
   `prl1p2skcz8kxn03p3j2hzaz4j687ewan8deju7lgvpswux9hkgavcz5s6v5p83`.
2. **Choose a pool.** Any standard `stratum` Pearl pool should work. These sources are tested with
   [Kryptex](https://pool.kryptex.com/prl). Note: different pools accept proofs for different M, N, K (see [Performance](#performance-and-shapes)).
3. **Build and run this source version.** Follow [Build](#build), then run
   `ASCEND_RT_VISIBLE_DEVICES=0 WALLET=prl1... POOL=pool.host PORT=7000 ./scripts/launch.sh`.

## Coexistence with other NPU workloads

Run the miner on the same dies as other processes and have it automatically get out of the way:
when another process enters the device, miner stops automatically. No need to restart manually.
Enable it by setting **`PRL_COEXIST_GUARD=1`** when you launch:

```bash
PRL_COEXIST_GUARD=1 ASCEND_RT_VISIBLE_DEVICES=0,1,2,3,4,5,6,7 ./scripts/launch.sh
# Docker: add  -e PRL_COEXIST_GUARD=1  to your run (scripts/run_container.sh passes it through)
```

## Build

Requirements: 
- CANN 9.0.0 toolchain + Ascend-C compiler 
- `gcc` with `-march=armv8-a+crypto` (NEON BLAKE3)
- `cmake ≥ 3.16`.
- BLAKE3 C sources (the `blake3-1.x/c` dir — vendored, or from the cargo registry).
- cargo, The Pearl Rust workspace (`pearl-blake3`, `zk-pow`) for the proof FFI — adjust the paths in
  `proof-ffi/Cargo.toml` to your checkout.

```bash
make                     # fixed rank=128, K=4096; builds all host/kernel/proof artifacts
```

Build flags (`make VAR=...`):

| Flag | Default | Meaning |
| ---- | ------- | ------- |
| `MDIM` | `16384` | 16384 keeps the pool's block-proof fast (non-orphaning). |

## Performance and shapes

Rank is fixed at 128 and K at 4096. Each strip has 128 A rows. The Cube kernel writes
32 independent INT32 products per 64-column B band in `[band][fold][B column][A row]`
order. An asynchronous D2H copy follows the kernel on the same stream. With
`MDIM=16384`, each strip transfers 256 MiB, and a full no-hit scan transfers 32 GiB.
`PIPE_DEPTH=4` allocates 1 GiB of pinned host output plus 256 MiB of device output.
For a smaller pinned footprint, build all components with `make PIPE_DEPTH=2`.

## GZIP v2 + custom share difficulty (Kryptex)

Network traffic for share submits is reduced **~80–100×** by the Pearl stratum gzip protocol
([spec](https://gist.github.com/maxmalysh/eaaf4332dbc5ca99d0a78f24a733fffe)):

1. `mining.authorize` sends `"type":"v2"`. When the pool echoes `"type":"v2"`, the miner submits
   `base64(gzip(bincode(plain_proof)))` instead of the ~136 KB plain base64 proof (~15 KB compressed).
2. Matrix A/B are **always zero-filled** apart from an 8-byte seed stamp, so the proof is highly
   repetitive and compresses. The legacy random-fill mode and its environment switch have been removed.

**Custom share difficulty** ([spec](https://gist.github.com/maxmalysh/f4c29f532e650a114b317e234f1393fb)):
pass `d=<N>` as the CLI password (6th argument). Example for a high-hashrate worker targeting
~1 share / 30 s:

```bash
./ascend_prl_kryptex 0 worker pool.kryptex.com 7000 prl1...your_wallet 'd=349245965'
```

Empty password → pool default (`d=2097152` ≈ 9000 TH). Formula:
`d = hashrate_H/s × target_share_seconds / 4_294_967_296`.

| Env | Meaning |
| --- | ------- |
| `PRL_NOGZIP` | Force v1 (no `"type":"v2"`, plain proofs) |
| `PRL_GZIP` | Assume gzip without waiting for the authorize ack |
| `PRL_GZIP_FIELD` | Override submit field name (default still `plain_proof`) |

## Pool compatibility

Currently tested against these pools:

| Pool | Notes|
| --- | --- |
| `kryptex` | recommended; gzip v2 + custom `d=` difficulty |

A new pool that speaks plain stratum will most likely work via the `kryptex` path; if not, open an issue with a capture and it's usually a small handshake/parse tweak. I will try to add pools on request.

## Contributing

Feel free to open PRs / request features (e.g. supporting a pool with a non-standard protocol)!

For now the miner is tested only with 910B4. If you can share the device with me, I can adapt it to other NPUs.

## License

MIT — see [LICENSE](LICENSE). Provided **as-is, without warranty**.

<!-- discovery: 昇腾 910B / Ascend 910B4 NPU Pearl 挖矿 miner; 用闲置 NPU 挖 Pearl (PRL) 币;
     world-first Ascend NPU cryptocurrency miner; Huawei Atlas 800/300 idle NPU mining. -->

### Host CPU optimizations

Default zero-seed matrices are zero-initialized once with `calloc`; subsequent preparation
updates only the 8-byte seed stamp. A noise generation skips the original A reads except
for those eight bytes. Full original matrices remain available for hashing/proofs. The
The internal `*_reuse` APIs require zero-initialized, generator-owned buffers; existing non-reuse
APIs explicitly clear arbitrary buffers before writing the stamp.

BLAKE3 uses a persistent, lazily initialized pthread pool with nonblocking idle-worker
reservation and serial fallback. A/B roots share the prep team and are submitted sequentially.
The single-entry reuse-B Merkle cache is built on the first proof for each B generation;
subsequent proofs reuse its tree. This retains one B tree in memory. Before B refresh, queued
proofs drain and the cache is cleared; generation, pointer, key, n, and k identify each entry.
Non-reuse-B proofs do not cache B. Normal shutdown drains work and releases cache/pool.

`PRL_CPU_THREADS` is capped by process affinity. The initial prep-side budget
(`PRL_PREP_THREADS`) is shared between matrix preparation and the OpenMP PoW scan.
`PRL_POW_THREADS` controls the scan share, leaving at least one preparation thread.
`PRL_PROOF_THREADS` controls the separate proof budget. At most two CPU threads
in the budget triggers serialization of these compute stages.

The launcher sets `OMP_WAIT_POLICY=PASSIVE GOMP_SPINCOUNT=0` before process startup; use
those environment settings when launching manually too, as some runtimes read them before main.

For the same seed, key and shape, generated matrix bytes and noise arithmetic retain their
previous default zero-filled behavior. The rank-128 NEON byte-index path is the only AArch64
production path; the portable fallback also computes rank 128. No dynamic rank or fill-mode
selection remains.

## CPU transcript and proof of work

For each fold, the CPU adds the raw product to a running matrix modulo 2^32.
It XOR-reduces all 64 B columns and combines A rows in pairs separated by 32
within each group of 64 rows. The transcript update is
`rotl32(word, 13) ^ folded`, at word index `fold % 16`.
Each tile then uses keyed BLAKE3 over 64 little-endian transcript bytes.
The CPU compares the digest as a little-endian 256-bit integer and returns the
first hit in the original scan order with tile index `hi * nbands + band`.

The previous Cube → Vector GM ring, producer/consumer flags and device key/target
uploads were removed. Proof construction uses the existing CPU path.
