# Briefing images and in-game text (investigation, partial)

## Extraction
`--press` scripted run (2 coins at 2000/2030, start at 2600, trigger at 4000..4200) + `--shotseq 5200:15:405:extract/raw/f`
then `tools/dedupe_frames.py extract/raw extract/briefing` keeps the 22 distinct stable frames.
Output is under `extract/` (git-ignored: it is game content).

## What was found about the text
* Captions (briefing, calibration tutorial, "TRIAL SHOOT" etc.) are NOT baked images. The GPU primitive log of a caption frame
  shows one textured quad per character (16x16 for full-width, 8 wide for half-width), taken from 4-bit texture pages
  in VRAM (e.g. tx=512/576, ty=512).
* The VRAM also holds a ready-made image of the whole caption line ("ターゲットの黄大宗です。") — the game renders the line
  into a texture at run time from a glyph source.
* The Japanese strings are in none of the usual encodings, neither in the ROM images (program ROM, banked ROMs, H8) nor in main RAM at the
  moment they are shown: Shift-JIS (cp932), EUC-JP, UTF-8, UTF-16 LE/BE, ISO-2022-JP all give zero matches.
  So the script data is either compressed or uses a private character code.
* English strings found in the clear (program ROM): debug/test menu only ("CREDIT", "TARGET", "CAMERA MENU", ...).
* The on-screen English text seen in the game ("SKIP:START BUTTON", "CREDIT 0/2", "TRIAL SHOOT") is rendered from graphics, not from strings.

## Correction after decoding the ROM graphics (supersedes the two sections above on "per-character" text)
The Japanese text is NOT generated from a font and strings: every caption line, name plate, menu and story title is a
PRE-RENDERED BITMAP stored in the graphics ROM, as an image layout in a tile pack (see below). The "per-character quads" seen in the GPU log
are 32-pixel-wide tiles of those line images. This is why no Japanese string exists in any encoding in ROM/RAM.
Consequence: translating = replacing those bitmaps (same palette/size limits) with images of translated text; no script decoding needed.
Open points: which pack/layout is which line (naming), caption box width limits, and the hook point (substitute pack data when the game
copies it from the banked ROM, or intercept the tile upload).

## Graphics extracted from the ROM data (not screenshots)
`tools/extract_images.py` decodes the banked graphics ROM directly. Pack format (reverse engineered, header `18 00 00 00 18 00 00 00`):
tile pool (32x32, 4 bpp, 512 bytes/tile, id 0 = empty) + 16-colour palettes (15-bit) + layouts (512-wide images assembled from tiles).
57 packs / 412 images found, including the briefing manga panels, digits, TIME, etc. (`extract/images/`, git-ignored).
Found by tracing CPU reads of the banked ROM (`--traceload`): the game copies each pack from ROM to RAM in one sequential run,
then uploads 32x32 tiles to VRAM (GP0 A0). No DMA channel 5 transfer is used for these.
Some images are slightly cropped at the right edge (a few tile ids probably carry flags I do not decode yet).

## Extractor status (final)
Rule that explains every observation (found while chasing the bad right-hand strip of pack 02a75f90 image 02):
**the layout table is one slot ahead of the picture.** The tile shown at the position (x,y) of entry n is the id of entry n-1; the tile at the first
position is the header field F. Tile index = id (0-based) into the pool; id 0xffff is only the end marker (it falls off the end).
Evidence:
* GPU log + VRAM dump (`--primlog`, `--dump-vram`): the game draws the plate "警察幹部 洪" with pool tiles 319..325 = F, then ids 320..324, then 325 (8 px partial);
  the long caption's ids appear one column to the right on screen (x=32..); pool tiles 6 and 68 (the only two uniform tiles) are the shared solid ids.
* In wide portraits the strip at x>=480 joins the left edge of the next row (error ~ inner level) and not column 14 -> it is the left margin, one row down.
* All earlier theories (id-1 bias, filler tiles, row join, seam-based bias) were artefacts of this and are removed from `tools/extract_images.py`.
Result: 60 packs, 703 images, no visible seams; captions, name plates, story titles, manga panels all complete.
