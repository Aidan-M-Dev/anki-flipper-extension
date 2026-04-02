#pragma once

#include <furi_ble/profile_interface.h>

/* Local BLE HID profile for Unleashed firmware.
 * Provides the same functionality as ble_profile_hid but built from
 * low-level GATT APIs that Unleashed exports to FAPs. */

extern const FuriHalBleProfileTemplate* ble_profile_hid_local;

bool ble_hid_kb_press(FuriHalBleProfileBase* profile, uint16_t button);
bool ble_hid_kb_release(FuriHalBleProfileBase* profile, uint16_t button);
