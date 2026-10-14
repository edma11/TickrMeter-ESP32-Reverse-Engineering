# TickrMeter Rev. 1.6 pinout

These connections were independently identified and tested on one **TickrMeter Rev. 1.6** unit with an **ESP32-WROOM-32E**.

## ESP32 connections

| Function | GPIO | Logic / notes |
|---|---:|---|
| EPD power / EN | 19 | Active LOW: LOW = display power ON |
| EPD BUSY | 18 | Input, FPC 9 |
| EPD RST / RES# | 26 | FPC 10 |
| EPD DC / D-C# | 27 | FPC 11 |
| EPD CS# | 15 | FPC 12 |
| EPD SCK / SCL | 13 | FPC 13 |
| EPD MOSI / SDA | 14 | FPC 14 |
| RGB LED red | 23 | Active LOW |
| RGB LED green | 22 | Active LOW |
| RGB LED blue | 21 | Active LOW |
| Buzzer / audio STBY | 17 | HIGH enables audio stage |
| Audio signal | 25 | Tested with `tone()` |
| Battery divider ADC | 32 | ADC1_CH4; battery voltage through nominal approximately 10 kΩ / 10 kΩ divider; owner confirmed working on 2026-10-11 |
| USB divider ADC | 33 | ADC1_CH5; USB supply voltage through divider; owner reported approximately 5 V after compensation on 2026-10-11 |
| Divider MOSFET enable | 4 | HIGH enables measurement; LOW disables it and is held during deep sleep; call `pinMode(OUTPUT)` before `digitalWrite()` |

Battery measurements, known limitations, and future firmware integration:
[`battery-adc.md`](battery-adc.md).

## E-paper FPC observations

| FPC pin | Signal | Observation |
|---:|---|---|
| 8 | BS1 | LOW selects 4-wire SPI on this display family |
| 9 | BUSY | GPIO18 |
| 10 | RES# | GPIO26 |
| 11 | D/C# | GPIO27 |
| 12 | CS# | GPIO15 |
| 13 | SCL / SPI clock | GPIO13 |
| 14 | SDA / SPI data | GPIO14 |
| 15 | VDDIO | ~3.3 V when GPIO19 is LOW |
| 16 | VCI | ~3.3 V when GPIO19 is LOW |
| 17 | VSS / GND | Ground |

The working Arduino display class is:

```cpp
GxEPD2_3C<GxEPD2_290_C90c, GxEPD2_290_C90c::HEIGHT>
```

with the ESP32 SPI bus initialized as:

```cpp
SPI.begin(13, -1, 14, 15); // SCK, MISO unused, MOSI, CS
```

## 24-pin FPC reference

This is a reference for the SSD1680-family panel connection observed during testing. Some pins are panel-specific.

| FPC | Signal |
|---:|---|
| 1 | NC / panel-specific |
| 2 | GDR |
| 3 | RESE |
| 4 | NC |
| 5 | VSH2 |
| 6 | TSCL |
| 7 | TSDA |
| 8 | BS1 |
| 9 | BUSY |
| 10 | RES# |
| 11 | D/C# |
| 12 | CS# |
| 13 | SCL / SPI clock |
| 14 | SDA / SPI data |
| 15 | VDDIO |
| 16 | VCI |
| 17 | VSS / GND |
| 18 | VDD |
| 19 | VPP / OTP-test |
| 20 | VSH1 |
| 21 | VGH |
| 22 | VSL |
| 23 | VGL |
| 24 | VCOM |

## Warning

`VSH1`, `VSH2`, `VGH`, `VGL`, `VSL`, `VCOM`, `GDR` and `RESE` are e-paper analog/high-voltage-related nodes, not ordinary ESP32 GPIO signals. Do not connect them directly to ESP32 pins.

## UART / recovery pads

Visible test points:

```text
BOOT  RTS  RX  GND
RST   GND  5V  TX
```

Use **3.3 V UART logic**. `5V` is a power pad.

## Revision warning

This documentation is confirmed only for the tested Rev. 1.6 board. Other TickrMeter revisions may use different GPIO assignments or display hardware.
