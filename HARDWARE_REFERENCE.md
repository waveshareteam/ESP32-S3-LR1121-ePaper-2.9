# Hardware reference

Product: ESP32-S3-LR1121-ePaper-2.9  
Product revision: 1.0.0

The current examples identify an ESP32-S3 N16R8 configuration and integrate a
2.9-inch e-paper panel, LR1121 radio, AXP2602 battery gauge, PCF85063A RTC, and
one WS2812 LED.

The example pin assignments are documented in
[`examples/arduino/README.md`](examples/arduino/README.md) and represented in
the ESP-IDF board definitions. The schematic and the existing comprehensive
test both map the 1-bit SDMMC interface to GPIO10/CMD, GPIO11/D0, and
GPIO12/CLK, with the card on the GPIO38-controlled 3.3 V domain. These remain
implementation references rather than a released hardware specification while
the supplied schematic is under review.

The schematic filename identifies PCB document revision Rev1.1, which is
tracked separately from product revision 1.0.0. One e-paper connector block is
labeled for a 2.13-inch panel while the drawing title identifies the 2.9-inch
product. A corrected or confirmed export is required before the schematic can
be promoted from
[`hardware/schematics/pending-review/`](hardware/schematics/pending-review/).

Mechanical dimensions are pending under
[`hardware/dimensions/`](hardware/dimensions/).
