#include <furi.h>
#include <furi_hal_bt.h>
#include <furi_hal_bt_hid.h>
#include <bt/bt_service/bt.h>
#include <gui/gui.h>
#include <storage/storage.h>

#define BT_KEYS_PATH APP_DATA_PATH(".bt_hid.keys")

typedef struct {
    bool running;
    bool connected;
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* event_queue;
    Bt* bt;
} AnkiReviewApp;

/* Pure mapping function — unit-testable off-device (T6).
 * Returns 0 if the key should not produce HID output. */
uint16_t anki_map_key(InputKey key) {
    switch(key) {
    case InputKeyUp:
        return HID_KEYBOARD_SPACEBAR;
    case InputKeyOk:
        return HID_KEYBOARD_2;
    case InputKeyDown:
        return HID_KEYBOARD_1;
    default:
        return 0;
    }
}

static void bt_status_changed(BtStatus status, void* context) {
    AnkiReviewApp* app = context;
    app->connected = (status == BtStatusConnected);
    view_port_update(app->view_port);
}

static void draw_callback(Canvas* canvas, void* ctx) {
    AnkiReviewApp* app = ctx;
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, "Anki Review");

    if(!app->connected) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 18, AlignCenter, AlignTop, "Waiting for BLE host...");
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignTop, "Pair this Flipper via");
        canvas_draw_str_aligned(canvas, 64, 42, AlignCenter, AlignTop, "your Bluetooth settings.");
    } else {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 14, AlignCenter, AlignTop, "\x95 Connected");
        canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignTop, "[^] Flip card  (Space)");
        canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignTop, "[OK] Yes        (2)");
        canvas_draw_str_aligned(canvas, 64, 48, AlignCenter, AlignTop, "[v] No          (1)");
    }

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 127, 63, AlignRight, AlignBottom, "[Back] Exit");
}

static void input_callback(InputEvent* input_event, void* ctx) {
    AnkiReviewApp* app = ctx;
    furi_message_queue_put(app->event_queue, input_event, FuriWaitForever);
}

int32_t anki_review_app(void* p) {
    UNUSED(p);

    AnkiReviewApp app;
    app.running = true;
    app.connected = false;
    app.event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    /* GUI setup */
    app.view_port = view_port_alloc();
    view_port_draw_callback_set(app.view_port, draw_callback, &app);
    view_port_input_callback_set(app.view_port, input_callback, &app);
    app.gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app.gui, app.view_port, GuiLayerFullscreen);

    /* BLE HID setup */
    app.bt = furi_record_open(RECORD_BT);
    bt_disconnect(app.bt);
    furi_delay_ms(200);
    bt_keys_storage_set_storage_path(app.bt, BT_KEYS_PATH);
    furi_check(furi_hal_bt_change_app(FuriHalBtProfileHidKeyboard, NULL, NULL));
    furi_hal_bt_start_advertising();
    bt_set_status_changed_callback(app.bt, bt_status_changed, &app);

    /* Main event loop */
    InputEvent event;
    while(app.running) {
        if(furi_message_queue_get(app.event_queue, &event, FuriWaitForever) == FuriStatusOk) {
            if(event.key == InputKeyBack && event.type == InputTypeShort) {
                app.running = false;
            } else if(app.connected) {
                uint16_t hid_key = anki_map_key(event.key);
                if(hid_key) {
                    if(event.type == InputTypePress) {
                        furi_hal_bt_hid_kb_press(hid_key);
                    } else if(event.type == InputTypeRelease) {
                        furi_hal_bt_hid_kb_release(hid_key);
                    }
                }
            }
        }
    }

    /* Teardown BLE */
    bt_set_status_changed_callback(app.bt, NULL, NULL);
    bt_disconnect(app.bt);
    furi_delay_ms(200);
    bt_keys_storage_set_default_path(app.bt);
    furi_check(furi_hal_bt_change_app(FuriHalBtProfileSerial, NULL, NULL));
    furi_record_close(RECORD_BT);

    /* Teardown GUI */
    gui_remove_view_port(app.gui, app.view_port);
    view_port_free(app.view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(app.event_queue);

    return 0;
}
