# Anki Review for Flipper Zero

A Flipper Zero app that turns your Flipper into a wireless Bluetooth remote for reviewing Anki flashcards. It advertises as a BLE HID keyboard and sends keypresses mapped to Anki's default shortcuts.

## Button Mapping

| Flipper Button | Key Sent | Anki Action |
|---|---|---|
| Up | Space | Show answer / flip card |
| OK | 2 | "Good" (remembered) |
| Down | 1 | "Again" (forgot) |
| Back | — | Exit app |

## Requirements

- **Flipper Zero** with [Unleashed firmware](https://github.com/DarkFlippers/unleashed-firmware) (should also work on stock OFW)
- **[ufbt](https://github.com/flipperdevices/flipperzero-ufbt)** (Unofficial Flipper Build Tool) installed on your computer
- A phone or computer running **Anki** with Bluetooth enabled

## Building

Set up ufbt for Unleashed firmware (one-time):

```bash
ufbt update --index-url=https://up.unleashedflip.com/directory.json
```

Build the app:

```bash
ufbt build
```

This produces a `.fap` file in the `build/` directory.

## Installing

Connect your Flipper Zero via USB and run:

```bash
ufbt launch
```

This builds, installs, and launches the app in one step.

Alternatively, copy the `.fap` file from `build/` to your Flipper's SD card under `apps/Bluetooth/`.

## Usage

1. Open **Anki Review** on your Flipper (Apps > Bluetooth > Anki Review)
2. On your phone or computer, go to Bluetooth settings and pair with the Flipper
3. The screen will change from "Waiting for BLE host..." to "Connected"
4. Open Anki on your paired device and start a review session
5. Use the Flipper's buttons to control the review — Up to flip, OK for "Good", Down for "Again"
6. Press Back to exit the app and restore normal Bluetooth

## How It Works

The app switches the Flipper's Bluetooth profile from Serial to HID Keyboard, then advertises for pairing. Once connected, button presses are translated to HID keycodes and sent to the host. On exit, the Serial profile is restored so the Flipper's normal BLE functionality (Flipper Mobile App, etc.) works again.

## License

MIT
