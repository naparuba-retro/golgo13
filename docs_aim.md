# Aim calibration: root cause

Symptom: with MAME's gun ADC mapping, impacts were stretched 1.107x horizontally (about 1.02x vertically) and offset.

Cause (from disassembly of the game, conversion at 0x8034af68, defaults written at 0x8034ad50):
- The game converts raw ADC counts to screen position through a calibration block (0x803b9778) plus a 17x17 correction grid.
- Its factory defaults assume X counts 224/416/608 for screen x 64/256/448 (1.000 count/px, count = x + 160) and
  Y counts 96/256/416 for y 60/240/420 (0.889 count/px, decreasing downward).
- MAME's input mapping spans 122..698 counts over the screen (1.125 count/px), tuned for a real cabinet whose operator
  calibration (stored in EEPROM) compensates. Our EEPROM starts blank, so the factory defaults apply and the mismatch showed up as a 1.125x stretch.

Fix: machine_set_input feeds exactly the characteristic the factory calibration expects (no heuristic in the platform layer;
mouse position in picture pixels maps directly). Measured residual: <= 6 px (mean 3.7 x, 1.7 y), from the grid and star-centroid bias.
