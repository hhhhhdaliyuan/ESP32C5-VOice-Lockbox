# Board pin configuration

This component is the single source of truth for ESP32-S3 peripheral ports and
GPIO assignments. The voiceprint input definitions were migrated from SonKey.
The `kws_wakeup` component owns the GPIO1 hold-to-record registration state
machine and combines voiceprint verification with KWS before publishing wakeup.

## Voiceprint and KWS pins

| Function | GPIO |
| --- | --- |
| Voiceprint record/enroll button | GPIO1 |
| Registration select/back button | GPIO42 |
| EC11 phase A / B | GPIO41 / GPIO39 |
| Confirm button | GPIO40 |
| Admin button | GPIO8 |
| ES8311 I2C SDA / SCL | GPIO17 / GPIO16 |
| ES8311 I2S MCLK / BCLK / WS | GPIO20 / GPIO4 / GPIO5 |
| ESP32 I2S TX GPIO18 -> ES8311 DIN | GPIO18 |
| ES8311 DOUT -> ESP32 I2S RX GPIO19 | GPIO19 |
| Red / green / yellow LED | GPIO3 / GPIO2 / GPIO7 |

KWS has no dedicated GPIO. It consumes PCM captured from ES8311 over I2S.
GPIO20 carries MCLK while GPIO19 receives ES8311 SDOUT. These are also the
ESP32-S3 native USB pins, so native USB must not be used while audio capture
is active.
GPIO39, GPIO40, and GPIO41 share the external JTAG function, so external JTAG
must not be used while those inputs are active.

## Resolved conflicts

The previous LED mapping reused two ES8311 pins:

- Green LED moved from GPIO16 (ES8311 I2C SCL) to GPIO2.
- Yellow LED moved from GPIO18 (ESP32 I2S TX to ES8311 DIN) to GPIO7.

The physical LED signal wires must be moved to GPIO2 and GPIO7 before flashing
this firmware. GPIO3, currently used by the red LED, is an ESP32-S3 strapping
pin and should not be externally forced during reset.
