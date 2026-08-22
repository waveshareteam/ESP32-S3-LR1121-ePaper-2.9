# Terminus Font Asset

The e-paper UI uses an ASCII subset of Terminus Bold 8x14 from Terminus Font
4.48. The source glyphs came from `ter-u14b.bdf`; rows were retained as
1-bit bitmaps without antialiasing and only U+0020 through U+007E were
embedded in `src/lv_font_terminus_14_bold.c`.

Upstream: https://terminus-font.sourceforge.net/

Source package: https://packages.debian.org/source/bookworm/xfonts-terminus

Terminus Font is distributed under the SIL Open Font License, Version 1.1.
The complete license and reserved-font-name notice are in `OFL.txt`.

This component is the single repository source for the e-paper text font used
by demos 3 through 8. Each demo firmware links one copy of the font object.
