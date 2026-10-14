# TickrMeter Rev. 1.6: battery and USB voltage measurement

Updated: **2026-10-11**. Tested board: ESP32-WROOM-32E, TickrMeter Rev. 1.6.

## Owner-confirmed findings

The hardware owner reported the updated clock working on 2026-10-11 and
confirmed these channel assignments:

| GPIO | Function | Measurement / control |
|---|---|---|
| 32 | ADC1_CH4, battery voltage divider | Measures the battery with GPIO4 HIGH |
| 33 | ADC1_CH5, USB supply voltage divider | Reports approximately 5 V after divider compensation |
| 4 | Divider MOSFET enable | HIGH enables measurement; LOW disables it |

The divider resistors are nominally **approximately 10 kΩ + 10 kΩ**.
Their exact resistance and tolerance have not been measured in this report.
Use a nominal factor of **2.000** for each channel, then calibrate separately.

The approximately 5 V USB value is the reconstructed upstream voltage,
not a claim that the ESP32 ADC pin directly receives 5 V. For a nominal 1:2
divider, the ADC-side USB voltage would be approximately 2.5 V.
USB supply voltage alone does not establish battery charging status.

## Measurement sequence used by the clock

Source: [`TickrMeter_Clock_Web_Settings_OTA.ino`](../examples/TickrMeter_Clock_Web_Settings_OTA/TickrMeter_Clock_Web_Settings_OTA.ino),
functions `initBattery()` and `sampleBattery()`.

1. Configure GPIO4 as OUTPUT before calling `digitalWrite()`.
2. Configure GPIO32 and GPIO33 as INPUT, 12-bit ADC, `ADC_11db` attenuation.
3. Before each display refresh, drive GPIO4 HIGH and allow 150 ms to settle.
4. For each ADC channel, discard the first RAW conversion, then average
   16 RAW and 16 separate `analogReadMilliVolts()` conversions.
5. Immediately drive GPIO4 LOW when sampling finishes.
6. Hold GPIO4 LOW during deep sleep; release the GPIO hold after waking.

Both channels are sampled before the display load is enabled. The web menu
also supports an explicit fresh measurement. Normal status polling reuses
the latest sample and does not continuously enable the dividers.
The owner explicitly confirmed LOW for disabling GPIO4 between measurements.

RAW and calibrated millivolt samples are independent conversions. Convert as:

```text
nominal_source_mV = averaged_ADC_mV × 2.000
correction[channel] = multimeter_voltage / nominal_source_voltage
displayed_source_mV = nominal_source_mV × correction[channel]
```

## Calibration and display

The clock's **Baterija ir kalibravimas** web menu provides:

- GPIO32 or GPIO33 selection; choose **GPIO32 for actual battery charge**.
- Voltage before correction and after correction.
- A multimeter reference voltage input and a fresh-measurement button.
- Independent calibration coefficients saved in NVS for GPIO32 and GPIO33.
- Reset of the selected channel's correction to 1.000.
- Adjustable empty/full voltage limits, initially 3.00 V and 4.20 V.

Calibration is calculated from the uncorrected sample, so recalibration does
not compound an earlier correction. Changed or older-than-60-second samples
are rejected. Invalid ADC readings show `?` and `--.-- V` instead of fabricated
zero-percent battery values.

Four charge bars represent a linear voltage estimate between the configured
empty/full limits: 1–25 %, 26–50 %, 51–75 %, and 76–100 %. At 0 %, the icon is
empty. This estimate is not a calibrated battery capacity or chemical SOC model.
Selecting GPIO33 displays the USB channel; its approximately 5 V supply must
not be interpreted as the battery's charge level.

## Verification limits

The owner confirmed functional operation and reported the approximately 5 V
USB reading. Paired multimeter/ADC readings, exact resistor measurements,
absolute voltage error, battery capacity accuracy and sleep current are not
recorded here. Previous experimental GPIO34 calibration is not the working
battery-channel mapping and should not be used for this clock.
