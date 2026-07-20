// Native NVS persistence for the last A/C state, replacing the Arduino
// Preferences usage in the old IRController. Only the Goodweather state needs
// to survive reboots now that the protocol is fixed (no runtime learning).
#pragma once
#include "goodweather_ir.h"

#ifdef __cplusplus
extern "C" {
#endif

// Load the persisted A/C state into *out. Returns true if a saved state existed.
bool ac_store_load(gw_state_t *out);

// Persist the given A/C state to NVS.
void ac_store_save(const gw_state_t *state);

#ifdef __cplusplus
}
#endif
