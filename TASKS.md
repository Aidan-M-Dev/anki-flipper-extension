# Tasks — anki_review

Ordered implementation tickets. Each is independently implementable and builds
on the previous ones. Testability is noted per ticket.

---

## T1 — Project scaffold and build smoke test

Create the minimal files needed for ufbt to compile a no-op FAP:

- `application.fam` with app metadata.
- `anki_review.c` with a stub `anki_review_app()` entry point that opens the
  GUI, shows an empty ViewPort, waits for Back, and exits.
- `anki_review_10px.png` placeholder icon (10×10, any content).

**Acceptance criteria:** `ufbt build` succeeds and produces a `.fap` file.

**Testability:** integration-test on hardware (or ufbt build in CI confirms
compilation; runtime requires a Flipper).

---

## T2 — Disconnected screen (GUI drawing)

Implement the ViewPort draw callback. When the app state is `disconnected`,
render the pairing-instructions screen:

- Title: "Anki Review"
- Body: "Waiting for BLE host…" / pairing instructions
- Footer: "[Back] Exit"

Use `canvas_set_font()` and `canvas_draw_str_aligned()` for layout.

**Acceptance criteria:** app launches and displays the disconnected screen.
Back button exits cleanly.

**Testability:** integration-test on hardware (visual verification on the
Flipper's screen).

---

## T3 — Connected screen (GUI drawing)

Add the connected-screen rendering path to the draw callback. When state is
`connected`, render the button-label overlay:

- Title: "Anki Review" / "● Connected"
- Button labels: `[↑] Flip card`, `[OK] Yes`, `[↓] No`
- Footer: "[Back] Exit"

For now, toggle between screens with a temporary flag or button press (BLE is
not wired up yet).

**Acceptance criteria:** both screens render correctly and the draw callback
switches between them based on the `connected` flag.

**Testability:** integration-test on hardware (visual verification; the
temporary toggle mechanism lets you see both screens without BLE).

---

## T4 — BLE HID profile activation

On app start:

1. Open the `Bt` record.
2. Disconnect the current BLE profile.
3. Switch to `FuriHalBtProfileHidKeyboard`.
4. Register a status-changed callback to update the `connected` flag.

On app exit:

1. Restore the serial BLE profile.
2. Close the `Bt` record.

**Acceptance criteria:** the Flipper appears as a Bluetooth keyboard in the
host's device list. Pairing succeeds. The app screen switches from Disconnected
to Connected on BLE connection, and back on disconnection.

**Testability:** integration-test on hardware (pair from a phone or computer,
observe screen state transitions).

---

## T5 — Input mapping and HID key sending

Wire up the input callback:

- `InputKeyUp` → `furi_hal_bt_hid_kb_press(HID_KEYBOARD_SPACEBAR)` / release
- `InputKeyOk` → `furi_hal_bt_hid_kb_press(HID_KEYBOARD_2)` / release
- `InputKeyDown` → `furi_hal_bt_hid_kb_press(HID_KEYBOARD_1)` / release

Only send keys when in the `connected` state. Use `InputTypePress` and
`InputTypeRelease` events (not short-press).

**Acceptance criteria:** with the Flipper paired and Anki open, pressing Up
flips the card, OK marks "yes," and Down marks "no."

**Testability:** integration-test on hardware (pair with a host, open Anki or a
keypress visualizer, verify correct keycodes arrive).

---

## T6 — Input mapping logic (unit-testable extraction)

Extract the pure mapping logic into a standalone function:

```c
// Returns 0 if the key should not produce HID output.
uint16_t anki_map_key(InputKey key);
```

| InputKey       | Return value             |
|----------------|--------------------------|
| `InputKeyUp`   | `HID_KEYBOARD_SPACEBAR`  |
| `InputKeyOk`   | `HID_KEYBOARD_2`         |
| `InputKeyDown`  | `HID_KEYBOARD_1`         |
| anything else  | `0`                      |

Write a test (or at minimum a test-harness-friendly header) that validates the
mapping table independently of the Flipper runtime.

**Acceptance criteria:** mapping function is correct for all `InputKey` values.
Unmapped keys return 0.

**Testability:** unit-testable off-device (pure function, no hardware
dependencies; can be compiled and tested with any C compiler).

---

## T7 — End-to-end QA and polish

Final integration pass:

- Verify clean startup and shutdown (no leaked records, no BLE profile left in
  HID mode after exit).
- Confirm reconnection works (disconnect and reconnect without restarting the
  app).
- Test with actual Anki + pass/fail plugin on at least one platform
  (macOS, Windows, or Linux).
- Check that the app icon appears correctly in the Flipper menu.
- Confirm `ufbt lint` passes (if available) and there are no compiler warnings.

**Acceptance criteria:** the app is fully functional, stable, and ready for
daily use.

**Testability:** integration-test on hardware (manual QA checklist).

---

## Summary

| Ticket | Description                        | Testability                    |
|--------|------------------------------------|--------------------------------|
| T1     | Project scaffold + build smoke     | integration-test on hardware   |
| T2     | Disconnected screen                | integration-test on hardware   |
| T3     | Connected screen                   | integration-test on hardware   |
| T4     | BLE HID profile activation         | integration-test on hardware   |
| T5     | Input mapping + HID key sending    | integration-test on hardware   |
| T6     | Mapping logic extraction + test    | unit-testable off-device       |
| T7     | End-to-end QA and polish           | integration-test on hardware   |
