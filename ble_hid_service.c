/* BLE HID GATT service — re-implementation of the firmware-internal
 * ble_svc_hid using only low-level GATT APIs that Unleashed exports to FAPs.
 * Derived from the Unleashed firmware source (lib/ble_profile/extra_services/hid_service.c). */

#include "ble_hid_service.h"
#include <furi.h>
#include <furi_ble/gatt.h>
#include <furi_ble/event_dispatcher.h>
#include <ble/core/ble_defs.h>
#include <ble/core/ble_std.h>

/* STM32WB BLE stack types — not exported in the ufbt SDK headers */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wattributes"
typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t data[];
} hci_uart_pckt;
typedef struct __attribute__((packed)) {
    uint8_t evt;
    uint8_t plen;
    uint8_t data[];
} hci_event_pckt;
typedef struct __attribute__((packed)) {
    uint16_t ecode;
    uint8_t data[];
} evt_blecore_aci;
#pragma GCC diagnostic pop

#define ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE  0x0C01
#define ACI_GATT_SERVER_CONFIRMATION_VSEVT_CODE 0x0C08

/* Standard BLE UUIDs (not in SDK headers) */
#define HUMAN_INTERFACE_DEVICE_SERVICE_UUID 0x1812
#define PROTOCOL_MODE_CHAR_UUID            0x2A4E
#define REPORT_MAP_CHAR_UUID               0x2A4B
#define HID_INFORMATION_CHAR_UUID          0x2A4A
#define HID_CONTROL_POINT_CHAR_UUID        0x2A4C
#define REPORT_CHAR_UUID                   0x2A4D
#define REPORT_REFERENCE_DESCRIPTOR_UUID   0x2908

#define BLE_SVC_HID_REPORT_MAP_MAX_LEN (255)
#define BLE_SVC_HID_REPORT_REF_LEN     (2)
#define BLE_SVC_HID_INFO_LEN           (4)
#define BLE_SVC_HID_CONTROL_POINT_LEN  (1)

#define BLE_SVC_HID_INPUT_REPORT_COUNT (3)

typedef enum {
    HidCharProtocolMode = 0,
    HidCharReportMap,
    HidCharInfo,
    HidCharCtrlPoint,
    HidCharCount,
} HidCharId;

typedef struct {
    uint8_t report_idx;
    uint8_t report_type;
} HidReportId;

_Static_assert(sizeof(HidReportId) == sizeof(uint16_t), "HidReportId must be 2 bytes");

typedef struct {
    const void* data_ptr;
    uint16_t data_len;
} HidDataWrapper;

/* ---- callbacks for GATT characteristic data ---- */

static bool hid_desc_data_cb(const void* context, const uint8_t** data, uint16_t* data_len) {
    const HidReportId* id = context;
    *data_len = sizeof(HidReportId);
    if(data) *data = (const uint8_t*)id;
    return false;
}

static bool hid_report_data_cb(const void* context, const uint8_t** data, uint16_t* data_len) {
    const HidDataWrapper* w = context;
    if(data) {
        *data = w->data_ptr;
        *data_len = w->data_len;
    } else {
        *data_len = BLE_SVC_HID_REPORT_MAP_MAX_LEN;
    }
    return false;
}

/* ---- static characteristic descriptors ---- */

static const Service_UUID_t hid_svc_uuid = {
    .Service_UUID_16 = HUMAN_INTERFACE_DEVICE_SERVICE_UUID,
};

static const BleGattCharacteristicParams hid_chars[HidCharCount] = {
    [HidCharProtocolMode] =
        {
            .name = "Protocol Mode",
            .data_prop_type = FlipperGattCharacteristicDataFixed,
            .data.fixed.length = 1,
            .uuid.Char_UUID_16 = PROTOCOL_MODE_CHAR_UUID,
            .uuid_type = UUID_TYPE_16,
            .char_properties = CHAR_PROP_READ | CHAR_PROP_WRITE_WITHOUT_RESP,
            .security_permissions = ATTR_PERMISSION_NONE,
            .gatt_evt_mask = GATT_NOTIFY_ATTRIBUTE_WRITE,
            .is_variable = CHAR_VALUE_LEN_CONSTANT,
        },
    [HidCharReportMap] =
        {
            .name = "Report Map",
            .data_prop_type = FlipperGattCharacteristicDataCallback,
            .data.callback.fn = hid_report_data_cb,
            .data.callback.context = NULL,
            .uuid.Char_UUID_16 = REPORT_MAP_CHAR_UUID,
            .uuid_type = UUID_TYPE_16,
            .char_properties = CHAR_PROP_READ,
            .security_permissions = ATTR_PERMISSION_NONE,
            .gatt_evt_mask = GATT_DONT_NOTIFY_EVENTS,
            .is_variable = CHAR_VALUE_LEN_VARIABLE,
        },
    [HidCharInfo] =
        {
            .name = "HID Information",
            .data_prop_type = FlipperGattCharacteristicDataFixed,
            .data.fixed.length = BLE_SVC_HID_INFO_LEN,
            .data.fixed.ptr = NULL,
            .uuid.Char_UUID_16 = HID_INFORMATION_CHAR_UUID,
            .uuid_type = UUID_TYPE_16,
            .char_properties = CHAR_PROP_READ,
            .security_permissions = ATTR_PERMISSION_NONE,
            .gatt_evt_mask = GATT_DONT_NOTIFY_EVENTS,
            .is_variable = CHAR_VALUE_LEN_CONSTANT,
        },
    [HidCharCtrlPoint] =
        {
            .name = "HID Control Point",
            .data_prop_type = FlipperGattCharacteristicDataFixed,
            .data.fixed.length = BLE_SVC_HID_CONTROL_POINT_LEN,
            .uuid.Char_UUID_16 = HID_CONTROL_POINT_CHAR_UUID,
            .uuid_type = UUID_TYPE_16,
            .char_properties = CHAR_PROP_WRITE_WITHOUT_RESP,
            .security_permissions = ATTR_PERMISSION_NONE,
            .gatt_evt_mask = GATT_NOTIFY_ATTRIBUTE_WRITE,
            .is_variable = CHAR_VALUE_LEN_CONSTANT,
        },
};

static const BleGattCharacteristicDescriptorParams hid_report_desc_template = {
    .uuid_type = UUID_TYPE_16,
    .uuid.Char_UUID_16 = REPORT_REFERENCE_DESCRIPTOR_UUID,
    .max_length = BLE_SVC_HID_REPORT_REF_LEN,
    .data_callback.fn = hid_desc_data_cb,
    .security_permissions = ATTR_PERMISSION_NONE,
    .access_permissions = ATTR_ACCESS_READ_WRITE,
    .gatt_evt_mask = GATT_DONT_NOTIFY_EVENTS,
    .is_variable = CHAR_VALUE_LEN_CONSTANT,
};

static const BleGattCharacteristicParams hid_report_template = {
    .name = "Report",
    .data_prop_type = FlipperGattCharacteristicDataCallback,
    .data.callback.fn = hid_report_data_cb,
    .data.callback.context = NULL,
    .uuid.Char_UUID_16 = REPORT_CHAR_UUID,
    .uuid_type = UUID_TYPE_16,
    .char_properties = CHAR_PROP_READ | CHAR_PROP_NOTIFY,
    .security_permissions = ATTR_PERMISSION_NONE,
    .gatt_evt_mask = GATT_DONT_NOTIFY_EVENTS,
    .is_variable = CHAR_VALUE_LEN_VARIABLE,
};

/* ---- service struct ---- */

struct BleHidService {
    uint16_t svc_handle;
    BleGattCharacteristicInstance chars[HidCharCount];
    BleGattCharacteristicInstance input_report_chars[BLE_SVC_HID_INPUT_REPORT_COUNT];
    GapSvcEventHandler* event_handler;
};

static BleEventAckStatus hid_event_handler(void* event, void* context) {
    UNUSED(context);
    hci_event_pckt* ep = (hci_event_pckt*)(((hci_uart_pckt*)event)->data);
    evt_blecore_aci* bc = (evt_blecore_aci*)ep->data;
    if(ep->evt == HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE) {
        if(bc->ecode == ACI_GATT_ATTRIBUTE_MODIFIED_VSEVT_CODE ||
           bc->ecode == ACI_GATT_SERVER_CONFIRMATION_VSEVT_CODE) {
            return BleEventAckFlowEnable;
        }
    }
    return BleEventNotAck;
}

BleHidService* ble_hid_service_start(void) {
    BleHidService* svc = malloc(sizeof(BleHidService));

    svc->event_handler = ble_event_dispatcher_register_svc_handler(hid_event_handler, svc);

    if(!ble_gatt_service_add(
           UUID_TYPE_16,
           &hid_svc_uuid,
           PRIMARY_SERVICE,
           2 + (4 * BLE_SVC_HID_INPUT_REPORT_COUNT) + 1 + 2 + 2 + 2,
           &svc->svc_handle)) {
        free(svc);
        return NULL;
    }

    /* Protocol Mode */
    ble_gatt_characteristic_init(
        svc->svc_handle, &hid_chars[HidCharProtocolMode], &svc->chars[HidCharProtocolMode]);
    uint8_t protocol_mode = 1; /* Report Protocol */
    ble_gatt_characteristic_update(
        svc->svc_handle, &svc->chars[HidCharProtocolMode], &protocol_mode);

    /* Input Reports */
    BleGattCharacteristicDescriptorParams desc;
    BleGattCharacteristicParams report_char;
    HidReportId report_id;

    memcpy(&desc, &hid_report_desc_template, sizeof(desc));
    memcpy(&report_char, &hid_report_template, sizeof(report_char));

    desc.data_callback.context = &report_id;
    report_char.descriptor_params = &desc;
    report_id.report_type = 0x01; /* Input */

    for(uint8_t i = 0; i < BLE_SVC_HID_INPUT_REPORT_COUNT; i++) {
        report_id.report_idx = i + 1;
        ble_gatt_characteristic_init(svc->svc_handle, &report_char, &svc->input_report_chars[i]);
    }

    /* Remaining characteristics */
    for(size_t i = HidCharReportMap; i < HidCharCount; i++) {
        ble_gatt_characteristic_init(svc->svc_handle, &hid_chars[i], &svc->chars[i]);
    }

    return svc;
}

void ble_hid_service_stop(BleHidService* svc) {
    furi_assert(svc);
    ble_event_dispatcher_unregister_svc_handler(svc->event_handler);

    for(size_t i = 0; i < HidCharCount; i++) {
        ble_gatt_characteristic_delete(svc->svc_handle, &svc->chars[i]);
    }
    for(uint8_t i = 0; i < BLE_SVC_HID_INPUT_REPORT_COUNT; i++) {
        ble_gatt_characteristic_delete(svc->svc_handle, &svc->input_report_chars[i]);
    }

    ble_gatt_service_delete(svc->svc_handle);
    free(svc);
}

bool ble_hid_service_update_report_map(BleHidService* svc, const uint8_t* data, uint16_t len) {
    furi_assert(svc);
    furi_assert(data);
    HidDataWrapper w = {.data_ptr = data, .data_len = len};
    return ble_gatt_characteristic_update(svc->svc_handle, &svc->chars[HidCharReportMap], &w);
}

bool ble_hid_service_update_input_report(
    BleHidService* svc,
    uint8_t input_report_num,
    uint8_t* data,
    uint16_t len) {
    furi_assert(svc);
    furi_assert(data);
    furi_assert(input_report_num < BLE_SVC_HID_INPUT_REPORT_COUNT);
    HidDataWrapper w = {.data_ptr = data, .data_len = len};
    return ble_gatt_characteristic_update(
        svc->svc_handle, &svc->input_report_chars[input_report_num], &w);
}

bool ble_hid_service_update_info(BleHidService* svc, uint8_t* data) {
    furi_assert(svc);
    furi_assert(data);
    return ble_gatt_characteristic_update(svc->svc_handle, &svc->chars[HidCharInfo], &data);
}
