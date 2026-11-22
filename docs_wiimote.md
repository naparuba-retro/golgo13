# Wiimote + sensor bar (Windows)

Run `golgo13.exe --wiimote` (or set `GOLGO13_WIIMOTE=1`).

1. Pair: remove any old "Nintendo RVL-CNT-01" in Windows Bluetooth settings, then run `golgo13.exe --wiimote-pair` and hold the red SYNC button (or 1+2) until it says Paired. (Windows 11 no longer offers "pair without a code", so the program sends the binary PIN itself.) The Wii will need a new SYNC afterwards.
2. Power the sensor bar with 5 V (USB) or use two candles. Place it above or below the screen, centred.
3. On first use a full-screen 3-step calibration starts (shows whether the bar is detected): top-left mark + A, bottom-right mark + A, then check the red crosshair follows and press A. Home recalibrates. Saved in `wiimote.cal` (user data folder).

Buttons: B = trigger, A = scope zoom, + = start, - = coin, 1 = enter, 2 = test, d-pad up/down. The mouse keeps working; the Wiimote wins while the bar is in view.
Driver: `runtime/platform/wiimote.c` (HID, IR report 0x33, roll-compensated midpoint of the two blobs). Tested with a real Wiimote (pairing, IR pointer, calibration).
