#pragma once

#include <stdint.h>
#include <stdbool.h>

/* BLE HID GATT service — keyboard-only, reimplemented from low-level GATT
 * APIs so the FAP works on Unleashed firmware (which does not export
 * ble_profile_hid to external apps). */

typedef struct BleHidService BleHidService;

BleHidService* ble_hid_service_start(void);
void ble_hid_service_stop(BleHidService* service);

bool ble_hid_service_update_report_map(BleHidService* svc, const uint8_t* data, uint16_t len);
bool ble_hid_service_update_input_report(
    BleHidService* svc,
    uint8_t input_report_num,
    uint8_t* data,
    uint16_t len);
bool ble_hid_service_update_info(BleHidService* svc, uint8_t* data);
