/* SPDX-License-Identifier: MIT */
#include "hal_sflash.h"

/* SDK 1.7 otherwise migrates legacy SYS_CONFIG/RF_CAL during boot. Refuse it:
 * nrc_sf_init_memory_map keeps the existing map or its factory RAM fallback.
 * build_sx.sh weakens only the copied SDK symbol and verifies this ARM body.
 */
uint8_t nrc_sf_migration_1m_to_2m(void) {
    return SF_MIGRATION_RES_ERROR;
}
