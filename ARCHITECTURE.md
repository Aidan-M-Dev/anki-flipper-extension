# Architecture — anki_review

A Flipper Zero FAP (Flipper Application Package) that turns the device into a
BLE HID keyboard for reviewing Anki flashcards. Targets **Unleashed firmware**
and builds with **ufbt**.

---

## Overview

The app advertises as a Bluetooth HID keyboard. Once paired with a host running
Anki (with a pass/fail–style plugin that maps keyboard shortcuts to review
actions), three physical buttons on the Flipper send the corresponding key
presses:

| Flipper button | HID keycode | Anki action        |
|----------------|-------------|--------------------|
| **Up**         | `Space`     | Flip / show answer |
| **OK**         | `2`         | Yes (remembered)   |
| **Down**       | `1`         | No (forgot)        |

No other buttons produce HID output. **Back** exits the app (standard Flipper
convention).

---

## App States

The app has exactly two states:

```
┌──────────────┐  BLE connected   ┌───────────┐
│ Disconnected │ ───────────────▶ │ Connected │
└──────────────┘ ◀─────────────── └───────────┘
                  BLE disconnected
```

### Disconnected

- BLE HID profile is active and advertising.
- Screen shows pairing/connection instructions.
- Button presses are ignored (no HID output).

### Connected

- A host is paired and the HID connection is live.
- Screen shows a button-label overlay (what each button does).
- Button presses send the mapped HID keycodes.

There is no "pairing" sub-state. From the app's perspective, the BLE stack
handles pairing transparently — the connection callback fires once the link is
established regardless of whether it was a fresh pair or a reconnect.

---

## BLE HID Connection Lifecycle

1. **App start** — the app requests the BLE HID profile via
   `bt_set_status_changed_callback()` on the `Bt` service record and switches
   the profile with `bt_disconnect()` + `bt_keys_storage_set_storage_path()` +
   `furi_hal_bt_change_app()` using `FuriHalBtProfileHidKeyboard`.
2. **Advertising** — the Flipper advertises as an HID keyboard. The device name
   shown to hosts is the Flipper's configured BLE name.
3. **Pairing / reconnect** — handled by the BLE stack. A status-changed callback
   (`BtStatusConnected`) notifies the app.
4. **Sending keys** — on each mapped button press:
   - `furi_hal_bt_hid_kb_press(keycode)` on `InputTypePress`
   - `furi_hal_bt_hid_kb_release(keycode)` on `InputTypeRelease`
5. **Disconnect** — the callback fires with `BtStatusStartedAndShowPairing` (or
   equivalent disconnected status). The app returns to the Disconnected screen.
6. **App exit** — restore the original BLE profile (`bt_set_status_changed_callback(NULL)`,
   restore serial profile via `furi_hal_bt_change_app(FuriHalBtProfileSerial)`).

### Key API Functions

| Purpose                   | Function / Type                                        |
|---------------------------|--------------------------------------------------------|
| Get Bt service            | `furi_record_open(RECORD_BT)`                          |
| Set connection callback   | `bt_set_status_changed_callback(bt, cb, ctx)`          |
| Switch to HID profile     | `furi_hal_bt_change_app(FuriHalBtProfileHidKeyboard, …)` |
| Key press                 | `furi_hal_bt_hid_kb_press(uint16_t keycode)`           |
| Key release               | `furi_hal_bt_hid_kb_release(uint16_t keycode)`         |
| Check connection          | `furi_hal_bt_hid_is_connected()`                       |
| Restore serial profile    | `furi_hal_bt_change_app(FuriHalBtProfileSerial, …)`    |

### HID Keycodes

Defined in `<furi_hal_bt_hid.h>` / USB HID usage tables:

| Key     | Constant                          | Value  |
|---------|-----------------------------------|--------|
| Space   | `HID_KEYBOARD_SPACEBAR`           | `0x2C` |
| 1       | `HID_KEYBOARD_1`                  | `0x1E` |
| 2       | `HID_KEYBOARD_2`                  | `0x1F` |

---

## Input Handling

The Flipper input system delivers `InputEvent` structs via ViewPort callbacks:

```c
typedef struct {
    InputKey key;    // InputKeyUp, InputKeyDown, InputKeyOk, InputKeyBack, …
    InputType type;  // InputTypePress, InputTypeRelease, InputTypeShort, InputTypeLong, …
} InputEvent;
```

The app processes input as follows:

- **InputKeyBack + InputTypeShort** → set a `running` flag to `false`, exit the
  event loop.
- **InputKeyUp / InputKeyOk / InputKeyDown** → if connected:
  - `InputTypePress` → `furi_hal_bt_hid_kb_press(mapped_keycode)`
  - `InputTypeRelease` → `furi_hal_bt_hid_kb_release(mapped_keycode)`
- All other keys/types are ignored.

Using press/release (rather than short-press) gives the host a realistic key
event pair and avoids issues with held-key repeat behavior.

---

## GUI

The app uses a single **ViewPort** attached to the system **Gui** service.
There is no need for ViewDispatcher or scene management — two static screens
are sufficient.

### Disconnected Screen

```
┌────────────────────────────┐
│      Anki Review           │
│                            │
│  Waiting for BLE host...   │
│                            │
│  Pair this Flipper from    │
│  your device's Bluetooth   │
│  settings.                 │
│                            │
│              [Back] Exit   │
└────────────────────────────┘
```

### Connected Screen

```
┌────────────────────────────┐
│      Anki Review           │
│      ● Connected           │
│                            │
│  [↑]  Flip card  (Space)   │
│  [OK] Yes        (2)       │
│  [↓]  No         (1)       │
│                            │
│              [Back] Exit   │
└────────────────────────────┘
```

The draw callback checks the `connected` flag and renders the appropriate
screen. State changes trigger `view_port_update(view_port)` to force a redraw.

---

## File Structure

```
anki-flipper-extension/
├── application.fam          # FAP manifest (appid, name, entry point, icon, etc.)
├── anki_review.c            # All application logic (single source file)
├── anki_review_10px.png     # 10×10 app icon for the Flipper menu
├── ARCHITECTURE.md          # This document
├── TASKS.md                 # Implementation tickets
└── .gitignore
```

### application.fam

```python
App(
    appid="anki_review",
    name="Anki Review",
    apptype=FlipperAppType.EXTERNAL,
    entry_point="anki_review_app",
    requires=["bt", "gui"],
    stack_size=1 * 1024,
    fap_icon="anki_review_10px.png",
    fap_category="Bluetooth",
    fap_author="Aidan",
    fap_version="0.1",
)
```

A single `.c` file is appropriate for the scope of this app — roughly 200–300
lines covering init, teardown, input dispatch, draw, and the event loop. There
is no need for a multi-file split.

---

## Unleashed vs Stock OFW

The BLE HID API surface (`furi_hal_bt_hid_*`, `Bt` service, `FuriHalBtProfile`
enum) is **identical** between Unleashed and stock OFW as of early 2025.
Unleashed re-exports the same HAL headers and does not modify the BLE HID
profile implementation.

The only practical difference is the SDK source: ufbt must be configured to pull
the Unleashed index:

```
ufbt update --index-url=https://up.unleashedflip.com/directory.json
```

This ensures the compiled `.fap` is linked against the correct firmware ABI.
The C source code itself is portable across both firmwares.

---

## Decision Log

| # | Decision | Rationale |
|---|----------|-----------|
| 1 | **Two states only (Disconnected / Connected)** | BLE pairing, bonding, and reconnection are handled entirely by the Flipper's BLE stack. The app only needs to know "can I send keys or not." Adding intermediate states (Pairing, Reconnecting) would require hooking into lower-level BLE events with no user-facing benefit. |
| 2 | **Press/Release rather than Short-press for HID output** | Sending `InputTypePress` → `kb_press` and `InputTypeRelease` → `kb_release` mirrors real keyboard behavior. Using `InputTypeShort` would require synthesizing both press and release in one handler, adding a delay or relying on instant press+release, which some hosts may drop. |
| 3 | **Space / 2 / 1 key mappings** | These match the default Anki pass/fail plugin bindings. Space is Anki's built-in "show answer" key. 1 and 2 map to the "Again" and "Good" buttons in a 2-button (pass/fail) review layout. No configuration UI is needed — the mappings are fixed to keep the app minimal. |
| 4 | **Single source file** | The entire app is ~250 lines: init, teardown, two draw screens, input dispatch, event loop. Splitting into multiple files would add overhead (headers, build complexity) for no clarity benefit at this scale. |
| 5 | **ViewPort instead of ViewDispatcher / Scenes** | ViewDispatcher and the scene framework are designed for multi-view apps with complex navigation. This app has two static screens selected by a boolean. A raw ViewPort with a draw callback is the simplest correct approach. |
| 6 | **Back button exits immediately** | Standard Flipper UX convention. Long-press Back is reserved by the system. Short-press Back in a simple app means "go back / exit." No confirmation dialog is needed — there is no state to lose. |
| 7 | **No USB HID fallback** | The use case is wireless review from a distance (e.g., couch, bed). USB HID would require a cable, defeating the purpose. If USB is ever needed, it can be added later behind a config toggle, but it is out of scope. |
| 8 | **Targeting Unleashed firmware** | Unleashed provides the same BLE HID API as stock OFW but is the user's chosen firmware. Building against the Unleashed SDK via ufbt ensures ABI compatibility. The source remains portable to stock OFW with no changes. |
