/* Fixed production consensus parameters; not build or runtime options. */
#ifndef PRL_CONFIG_H
#define PRL_CONFIG_H
#define PRL_RANK 128
#define PRL_K 4096
/* Cube matmul width is independent of the protocol's transcript band. */
#define PRL_CUBE_COLS 256
#define PRL_BAND_COLS 64
#endif
