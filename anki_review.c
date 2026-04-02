#include <furi.h>
#include <gui/gui.h>

typedef struct {
    bool running;
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* event_queue;
} AnkiReviewApp;

static void draw_callback(Canvas* canvas, void* ctx) {
    UNUSED(ctx);
    UNUSED(canvas);
}

static void input_callback(InputEvent* input_event, void* ctx) {
    AnkiReviewApp* app = ctx;
    furi_message_queue_put(app->event_queue, input_event, FuriWaitForever);
}

int32_t anki_review_app(void* p) {
    UNUSED(p);

    AnkiReviewApp app;
    app.running = true;
    app.event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    app.view_port = view_port_alloc();
    view_port_draw_callback_set(app.view_port, draw_callback, &app);
    view_port_input_callback_set(app.view_port, input_callback, &app);

    app.gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app.gui, app.view_port, GuiLayerFullscreen);

    InputEvent event;
    while(app.running) {
        if(furi_message_queue_get(app.event_queue, &event, FuriWaitForever) == FuriStatusOk) {
            if(event.key == InputKeyBack && event.type == InputTypeShort) {
                app.running = false;
            }
        }
    }

    gui_remove_view_port(app.gui, app.view_port);
    view_port_free(app.view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(app.event_queue);

    return 0;
}
