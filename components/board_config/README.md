# Board pin configuration

This component is the single source of truth for ESP32-C5 peripheral ports and
GPIO assignments. This branch targets an ESP32-C5-WROOM-1 MCN16R8 board.

## Phase 2: voice wakeup and lid actuator

The C5 voice path validates ES8311 capture, Wi-Fi, KWS, voiceprint
registration, and voiceprint verification. This phase also assigns the SG90
control signal. Display, LEDs, and optional panel controls remain disconnected
and use `GPIO_NUM_NC`.

| Function | C5 GPIO | Notes |
| --- | ---: | --- |
| Voiceprint record/enroll button | GPIO24 | Button connects GPIO24 to GND; internal pull-up |
| ES8311 I2C SDA / SCL | GPIO7 / GPIO6 | Validated on this board |
| ES8311 I2S MCLK / BCLK / WS | GPIO0 / GPIO4 / GPIO5 | GPIO0 has been validated as MCLK |
| C5 I2S TX -> ES8311 DIN | GPIO2 | Do not add external pull circuits |
| ES8311 DOUT -> C5 I2S RX | GPIO3 | Do not add external pull circuits |
| SG90 signal | GPIO10 | 50 Hz PWM configured in firmware; awaiting hardware verification |
| UART console | GPIO11 / GPIO12 | Reserved for the CH340 serial interface |

KWS has no dedicated GPIO. It consumes PCM captured from ES8311 over I2S.
Keep the I2S directions unchanged: C5 DOUT connects to ES8311 DIN, and ES8311
DOUT connects to C5 DIN.

## SG90 wiring

| SG90 wire | Connect to |
| --- | --- |
| Signal (orange/yellow) | C5 GPIO10 |
| VCC (red) | External regulated 5 V supply |
| GND (brown/black) | External supply GND and C5 GND |

Do not power the SG90 from the C5 3.3 V rail. The external 5 V supply and the
C5 must share GND. The controller keeps the lid closed at boot and calls
`servo_open()` only after KWS and voiceprint verification both succeed.

## Later peripherals

The display, LEDs, and optional buttons must receive a separate C5 pin-budget
review before assignment. Do not use GPIO11/GPIO12 during porting, and do not
assign the C5 strapping pins GPIO25 through GPIO28 until reset behavior has
been tested with the final circuit.
