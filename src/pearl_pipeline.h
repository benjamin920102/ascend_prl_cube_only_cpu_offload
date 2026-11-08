#ifndef PEARL_PIPELINE_H
#define PEARL_PIPELINE_H

#include <stdint.h>
#include "prl_config.h"

#ifndef PEARL_PIPE_DEPTH
#define PEARL_PIPE_DEPTH 4
#endif
#if PEARL_PIPE_DEPTH < 1
#error "PEARL_PIPE_DEPTH must be positive"
#endif

#define PEARL_GRID 20
#define PEARL_BAND_COLS 64
#define PEARL_FOLDS (PRL_K / PRL_RANK)
#define PEARL_PART_ELEMS (PEARL_BAND_COLS * PRL_RANK)

#ifdef __cplusplus
extern "C" {
#endif
void pearl_submit_partials(const int8_t *a);
/* FIFO result: [band][fold][B column][A row]. Consume before refilling. */
const int32_t *pearl_wait_partials(void);
#ifdef __cplusplus
}
#endif
#endif
