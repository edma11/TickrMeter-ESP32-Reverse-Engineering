# TickrMeter ESP32 Reverse Engineering

Unofficial community hardware research and test code for **TickrMeter Rev. 1.6**.

This repository documents independently discovered GPIO connections and contains independently written ESP32 test sketches. It does **not** contain the original TickrMeter firmware, extracted firmware, decompiled source code, manufacturer schematics, or other proprietary files.

> **Unofficial project.** Not affiliated with or endorsed by TickrMeter or its manufacturer.

## Tested hardware

- TickrMeter PCB revision: **Rev. 1.6**
- MCU: **ESP32-WROOM-32E**
- Display: **2.9 inch, 296 x 128, B/W/R e-paper**
- Display family/controller: **SSD1680 family**
- Arduino display driver tested successfully: **GxEPD2_290_C90c**
- Interface: **4-wire SPI**

Hardware revisions may differ. Verify the PCB before using these pin assignments.

## Confirmed GPIO pinout

| Function | ESP32 GPIO | Notes |
|---|---:|---|
| E-paper power / EN | GPIO19 | **Active LOW**: LOW = ON, HIGH = OFF |
| E-paper BUSY | GPIO18 | FPC pin 9 |
| E-paper RST / RES# | GPIO26 | FPC pin 10 |
| E-paper DC / D-C# | GPIO27 | FPC pin 11 |
| E-paper CS# | GPIO15 | FPC pin 12 |
| E-paper SCK / SCL | GPIO13 | FPC pin 13 |
| E-paper MOSI / SDA | GPIO14 | FPC pin 14 |
| RGB LED red | GPIO23 | **Active LOW** |
| RGB LED green | GPIO22 | **Active LOW** |
| RGB LED blue | GPIO21 | **Active LOW** |
| Buzzer / amplifier STBY | GPIO17 | HIGH = ON |
| Audio signal | GPIO25 | Tone / audio drive |
| Battery divider ADC | GPIO32 | ADC1_CH4; battery voltage through an approximately 10 kΩ / 10 kΩ divider |
| USB divider ADC | GPIO33 | ADC1_CH5; USB supply voltage, approximately 5 V after divider compensation |
| Divider enable | GPIO4 | HIGH = enable measurement; LOW = disable, including deep sleep |

More details: [`docs/pinout.md`](docs/pinout.md)

Battery ADC findings and measurements: [`docs/battery-adc.md`](docs/battery-adc.md).
On **2026-10-11**, the hardware owner confirmed the updated clock working:
GPIO32 measures the battery when GPIO4 is HIGH, and GPIO33 measures the USB
supply (approximately 5 V after divider compensation). The dividers are
nominally approximately 10 kΩ / 10 kΩ; use a starting conversion factor of 2.000
and calibrate each channel independently against a multimeter.

## Examples

### 1. Clock + RGB LED + buzzer
[`examples/TickrMeter_Clock_LED_Buzzer`](examples/TickrMeter_Clock_LED_Buzzer)

Basic hardware demonstration using the e-paper display, RGB LED and buzzer. Also contains a simple web OTA page.

### 2. Battery / ADC / buzzer test
[`examples/TickrMeter_Battery_ADC_Buzzer_Test`](examples/TickrMeter_Battery_ADC_Buzzer_Test)

Tests ADC1-capable pins and displays the readings on the e-paper screen and web page.

### 3. ADC2 + GPIO34 test
[`examples/TickrMeter_ADC2_GPIO34_Test`](examples/TickrMeter_ADC2_GPIO34_Test)

Experimental ADC mapping sketch. ADC2 is sampled before Wi-Fi is started because classic ESP32 ADC2 cannot be used reliably while Wi-Fi is active.

**Important:** battery calibration values in this test are experimental and are not a final voltage-divider calibration.

### 4. Clock + web settings + OTA
[`examples/TickrMeter_Clock_Web_Settings_OTA`](examples/TickrMeter_Clock_Web_Settings_OTA)

More complete demo with:

- captive portal / local configuration page
- Wi-Fi settings stored in NVS
- RGB LED enable/disable and test
- beep enable/disable and test
- alarm settings
- OTA firmware upload
- NTP clock
- web/AP/OTA available for 5 minutes after reset; timer wakes do not reopen it
- deep sleep between minute updates and hourly NTP synchronization
- Lithuanian web UI, validated settings, OTA upload progress and revised display layout
- battery voltage and four charge bars at the upper left of the display
- GPIO32/GPIO33 selection, separate saved calibration per channel and adjustable empty/full voltage limits

Hardware owner reported the updated clock working on 2026-10-11.
Choose GPIO32 for actual battery charge; GPIO33 measures USB supply voltage.

Setup, display refresh limits, and hardware checks: [`clock README`](examples/TickrMeter_Clock_Web_Settings_OTA/README.md).

The Wi-Fi credentials in this public repository are placeholders. Enter your own values or configure them through the web interface.

## Arduino IDE setup

1. Install the **ESP32 Arduino core**.
2. Select **ESP32 Dev Module**.
3. Install the **GxEPD2** library using Arduino Library Manager.
4. Open one example `.ino` file.
5. Check the GPIO assignments against your hardware revision.
6. Compile and upload normally.

For the OTA-enabled examples, the test AP is configured as:

- SSID: `TickrMeter-CUSTOM`
- Password: `12345678`
- Address: `http://192.168.4.1/`

This password is intentionally only a test/default value. Change it before using the device in a permanent installation.

## Hardware recovery / UART test points

Visible PCB test points found on Rev. 1.6 include:

- `BOOT`, `RTS`, `RX`, `GND`
- `RST`, `GND`, `5V`, `TX`

Use **3.3 V UART logic**. The `5V` pad is a power rail and is **not** a 5 V UART logic indication.

## Safety notes

E-paper panels contain analog/high-voltage drive nodes. Do not treat all FPC pins as ESP32 GPIO. In particular, nodes such as VGH, VGL, VSH, VSL, VCOM, GDR and RESE should not be connected directly to ESP32 GPIO pins.

Incorrect wiring can damage the ESP32, display panel, power circuitry, USB/UART adapter or battery.

## Project origin

The pinout was determined by hardware inspection, continuity measurements and purpose-built test sketches. The code in this repository was written independently for testing and experimentation.

No original manufacturer firmware or source code is included.

## License

The code and original documentation in this repository are released under the [MIT License](LICENSE).

Third-party library licenses (for example GxEPD2 and its dependencies) remain with their respective authors.
