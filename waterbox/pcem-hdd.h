/* pcem-hdd.h - what the driver needs from the writable hard disks.
 *
 * pcem-hdd.c replaces upstream's hdd_file.c in the waterbox build; these are
 * the extra entry points the Chimera side uses, on top of the six hdd_*
 * functions PCem itself calls.
 */
#ifndef PCEM_HDD_H
#define PCEM_HDD_H

#include <stdint.h>

#define PCEM_HDD_MAX      7      /* PCem's hdc..hdi */
#define PCEM_HDD_NAME_MAX 256

/* How many disks the machine opened, in the order they were opened. An index
 * below is into that list, and it is also the save-data index. */
int         pcem_hdd_count(void);

/* The disk's name: the project file it was seeded from, which is also the name
 * the export carries, so an exported image drops back into the same slot. */
const char *pcem_hdd_export_name(int index);

/* The whole disk as the machine sees it, in bytes - geometry, not file size. */
int64_t     pcem_hdd_export_size(int index);

/* Seed and overlay together, the same bytes the machine reads. Returns how
 * many were written into dst. */
int64_t     pcem_hdd_export_read(int index, int64_t off, uint8_t *dst, int64_t len);

/* For the measurement and the gate: blocks currently held in guest memory
 * (each pcem_hdd_block_bytes() long), and how many block-writes were dropped
 * because they matched the seed exactly. */
int64_t     pcem_hdd_blocks_held(int index);
int64_t     pcem_hdd_writes_matching_seed(int index);
int         pcem_hdd_block_bytes(void);

/* Geometry for a raw image, derived from the image itself. Returns 1 and fills
 * the three out-parameters, or 0 when it cannot tell. */
int         pcem_hdd_derive_geometry(const char *name, int *spt, int *hpc, int *tracks);

#endif
