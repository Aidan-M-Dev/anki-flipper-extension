/* Local BLE HID profile — keyboard-only.
 * Derived from the Unleashed firmware source (lib/ble_profile/extra_profiles/hid_profile.c). */

#include "ble_hid_profile.h"
#include "ble_hid_service.h"

#include <furi.h>
#include <furi_hal_version.h>
#include <usb_hid.h>
#include <hid_usage_desktop.h>
#include <hid_usage_keyboard.h>
#include <hid_usage_button.h>
#include <hid_usage_consumer.h>
#include <services/battery_service.h>
#include <services/dev_info_service.h>
#include <gap.h>
#include <ble/core/ble_defs.h>
#include <ble/core/ble_std.h>

#define HUMAN_INTERFACE_DEVICE_SERVICE_UUID 0x1812

#define HID_KB_MAX_KEYS 6

#define HID_INFO_BASE_USB_SPEC 0x0101
#define HID_INFO_COUNTRY_CODE  0x00

enum HidReportId {
    ReportIdKeyboard = 1,
    ReportIdMouse = 2,
    ReportIdConsumer = 3,
};

enum HidInputNumber {
    ReportNumberKeyboard = 0,
    ReportNumberMouse = 1,
    ReportNumberConsumer = 2,
};

typedef struct {
    uint8_t mods;
    uint8_t reserved;
    uint8_t key[HID_KB_MAX_KEYS];
} FURI_PACKED KbReport;

/* Keyboard-only report map (mouse + consumer stubs kept so report IDs match
   the 3-input-report GATT service layout) */
static const uint8_t report_map_data[] = {
    /* Keyboard */
    HID_USAGE_PAGE(HID_PAGE_DESKTOP),
    HID_USAGE(HID_DESKTOP_KEYBOARD),
    HID_COLLECTION(HID_APPLICATION_COLLECTION),
    HID_REPORT_ID(ReportIdKeyboard),
    HID_USAGE_PAGE(HID_DESKTOP_KEYPAD),
    HID_USAGE_MINIMUM(HID_KEYBOARD_L_CTRL),
    HID_USAGE_MAXIMUM(HID_KEYBOARD_R_GUI),
    HID_LOGICAL_MINIMUM(0),
    HID_LOGICAL_MAXIMUM(1),
    HID_REPORT_SIZE(1),
    HID_REPORT_COUNT(8),
    HID_INPUT(HID_IOF_DATA | HID_IOF_VARIABLE | HID_IOF_ABSOLUTE),
    HID_REPORT_COUNT(1),
    HID_REPORT_SIZE(8),
    HID_INPUT(HID_IOF_CONSTANT | HID_IOF_VARIABLE | HID_IOF_ABSOLUTE),
    HID_REPORT_COUNT(HID_KB_MAX_KEYS),
    HID_REPORT_SIZE(8),
    HID_LOGICAL_MINIMUM(0),
    HID_LOGICAL_MAXIMUM(101),
    HID_USAGE_PAGE(HID_DESKTOP_KEYPAD),
    HID_USAGE_MINIMUM(0),
    HID_USAGE_MAXIMUM(101),
    HID_INPUT(HID_IOF_DATA | HID_IOF_ARRAY | HID_IOF_ABSOLUTE),
    HID_END_COLLECTION,
    /* Mouse (stub — keeps report ID 2 valid) */
    HID_USAGE_PAGE(HID_PAGE_DESKTOP),
    HID_USAGE(HID_DESKTOP_MOUSE),
    HID_COLLECTION(HID_APPLICATION_COLLECTION),
    HID_USAGE(HID_DESKTOP_POINTER),
    HID_COLLECTION(HID_PHYSICAL_COLLECTION),
    HID_REPORT_ID(ReportIdMouse),
    HID_USAGE_PAGE(HID_PAGE_BUTTON),
    HID_USAGE_MINIMUM(1),
    HID_USAGE_MAXIMUM(3),
    HID_LOGICAL_MINIMUM(0),
    HID_LOGICAL_MAXIMUM(1),
    HID_REPORT_COUNT(3),
    HID_REPORT_SIZE(1),
    HID_INPUT(HID_IOF_DATA | HID_IOF_VARIABLE | HID_IOF_ABSOLUTE),
    HID_REPORT_SIZE(1),
    HID_REPORT_COUNT(5),
    HID_INPUT(HID_IOF_CONSTANT | HID_IOF_VARIABLE | HID_IOF_ABSOLUTE),
    HID_USAGE_PAGE(HID_PAGE_DESKTOP),
    HID_USAGE(HID_DESKTOP_X),
    HID_USAGE(HID_DESKTOP_Y),
    HID_USAGE(HID_DESKTOP_WHEEL),
    HID_LOGICAL_MINIMUM(-127),
    HID_LOGICAL_MAXIMUM(127),
    HID_REPORT_SIZE(8),
    HID_REPORT_COUNT(3),
    HID_INPUT(HID_IOF_DATA | HID_IOF_VARIABLE | HID_IOF_RELATIVE),
    HID_END_COLLECTION,
    HID_END_COLLECTION,
    /* Consumer (stub — keeps report ID 3 valid) */
    HID_USAGE_PAGE(HID_PAGE_CONSUMER),
    HID_USAGE(HID_CONSUMER_CONTROL),
    HID_COLLECTION(HID_APPLICATION_COLLECTION),
    HID_REPORT_ID(ReportIdConsumer),
    HID_LOGICAL_MINIMUM(0),
    HID_RI_LOGICAL_MAXIMUM(16, 0x3FF),
    HID_USAGE_MINIMUM(0),
    HID_RI_USAGE_MAXIMUM(16, 0x3FF),
    HID_REPORT_COUNT(1),
    HID_REPORT_SIZE(16),
    HID_INPUT(HID_IOF_DATA | HID_IOF_ARRAY | HID_IOF_ABSOLUTE),
    HID_END_COLLECTION,
};

/* ---- profile instance ---- */

typedef struct {
    FuriHalBleProfileBase base;
    KbReport kb_report;
    BleServiceBattery* battery_svc;
    BleServiceDevInfo* dev_info_svc;
    BleHidService* hid_svc;
} HidProfile;

_Static_assert(offsetof(HidProfile, base) == 0, "Wrong layout");

/* Connection interval: 7.5 ms min, ~45 ms max */
#define CONN_INT_MIN 0x0006
#define CONN_INT_MAX 0x0024

static GapConfig gap_template = {
    .adv_service =
        {
            .UUID_Type = UUID_TYPE_16,
            .Service_UUID_16 = HUMAN_INTERFACE_DEVICE_SERVICE_UUID,
        },
    .appearance_char = GAP_APPEARANCE_KEYBOARD,
    .bonding_mode = true,
    .pairing_method = GapPairingPinCodeVerifyYesNo,
    .conn_param =
        {
            .conn_int_min = CONN_INT_MIN,
            .conn_int_max = CONN_INT_MAX,
            .slave_latency = 0,
            .supervisor_timeout = 0,
        },
};

static FuriHalBleProfileBase* profile_start(FuriHalBleProfileParams params) {
    UNUSED(params);

    HidProfile* p = malloc(sizeof(HidProfile));
    p->base.config = ble_profile_hid_local;
    memset(&p->kb_report, 0, sizeof(p->kb_report));

    p->battery_svc = ble_svc_battery_start(true);
    p->dev_info_svc = ble_svc_dev_info_start();
    p->hid_svc = ble_hid_service_start();
    furi_check(p->hid_svc);

    ble_hid_service_update_report_map(p->hid_svc, report_map_data, sizeof(report_map_data));

    uint8_t hid_info[] = {
        HID_INFO_BASE_USB_SPEC & 0xFF,
        (HID_INFO_BASE_USB_SPEC >> 8) & 0xFF,
        HID_INFO_COUNTRY_CODE,
        0x01 | 0x02, /* remote-wake | normally-connectable */
    };
    ble_hid_service_update_info(p->hid_svc, hid_info);

    return &p->base;
}

static void profile_stop(FuriHalBleProfileBase* base) {
    furi_check(base);
    furi_check(base->config == ble_profile_hid_local);

    HidProfile* p = (HidProfile*)base;
    ble_svc_battery_stop(p->battery_svc);
    ble_svc_dev_info_stop(p->dev_info_svc);
    ble_hid_service_stop(p->hid_svc);
    free(p);
}

static void profile_get_config(GapConfig* config, FuriHalBleProfileParams params) {
    UNUSED(params);
    furi_check(config);
    memcpy(config, &gap_template, sizeof(GapConfig));
    memcpy(config->mac_address, furi_hal_version_get_ble_mac(), sizeof(config->mac_address));
    config->mac_address[2]++;
    snprintf(
        config->adv_name,
        sizeof(config->adv_name),
        "%cAnki %s",
        furi_hal_version_get_ble_local_device_name_ptr()[0],
        furi_hal_version_get_name_ptr());
}

static const FuriHalBleProfileTemplate profile_template = {
    .start = profile_start,
    .stop = profile_stop,
    .get_gap_config = profile_get_config,
};

const FuriHalBleProfileTemplate* ble_profile_hid_local = &profile_template;

/* ---- keyboard press/release ---- */

bool ble_hid_kb_press(FuriHalBleProfileBase* base, uint16_t button) {
    furi_check(base && base->config == ble_profile_hid_local);
    HidProfile* p = (HidProfile*)base;

    for(uint8_t i = 0; i < HID_KB_MAX_KEYS; i++) {
        if(p->kb_report.key[i] == 0) {
            p->kb_report.key[i] = button & 0xFF;
            break;
        }
    }
    p->kb_report.mods |= (button >> 8);
    return ble_hid_service_update_input_report(
        p->hid_svc, ReportNumberKeyboard, (uint8_t*)&p->kb_report, sizeof(KbReport));
}

bool ble_hid_kb_release(FuriHalBleProfileBase* base, uint16_t button) {
    furi_check(base && base->config == ble_profile_hid_local);
    HidProfile* p = (HidProfile*)base;

    for(uint8_t i = 0; i < HID_KB_MAX_KEYS; i++) {
        if(p->kb_report.key[i] == (button & 0xFF)) {
            p->kb_report.key[i] = 0;
            break;
        }
    }
    p->kb_report.mods &= ~(button >> 8);
    return ble_hid_service_update_input_report(
        p->hid_svc, ReportNumberKeyboard, (uint8_t*)&p->kb_report, sizeof(KbReport));
}
