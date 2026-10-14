# Examples

Each example is a separate Arduino sketch. Open the `.ino` file inside the example folder using Arduino IDE.

- `TickrMeter_Clock_LED_Buzzer` - e-paper clock + RGB LED + buzzer + simple OTA page.
- `TickrMeter_Battery_ADC_Buzzer_Test` - ADC1 pin scan + buzzer + display/web output.
- `TickrMeter_BAT32_OTA_v2_3` - working battery ADC baseline confirmed by the user; GPIO32/GPIO33 10k/10k dividers, GPIO4 HIGH, display and OTA. See `docs/battery-adc.md` for measurements and integration. Supersedes v2.2's incorrect GPIO initialization order.
- `TickrMeter_ADC2_GPIO34_Test` - ADC2 boot-time scan and experimental GPIO34 battery testing.
- `TickrMeter_Clock_Web_Settings_OTA` - clock with minute deep sleep, a 5-minute web/OTA window after reset, Lithuanian settings UI, NVS, alarm and RGB/beep controls. See its README for setup and hardware checks.

The ADC/battery sketches are research tools, not production battery gauges. Calibration must be verified on your own board.
