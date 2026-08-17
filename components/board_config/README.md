# Board pin configuration

This component is the single source of truth for ESP32-C5 peripheral ports and
GPIO assignments. This branch targets an ESP32-C5-WROOM-1 MCN16R8 board.

## Phase 3: voice wakeup, lid actuator, and status display

The C5 voice path validates ES8311 capture, Wi-Fi, KWS, voiceprint
registration, and voiceprint verification. The SG90 control signal and GC9A01
status display and RGB lid-motion effects are assigned. Optional panel controls
remain disconnected and use `GPIO_NUM_NC`.

| Function | C5 GPIO | Notes |
| --- | ---: | --- |
| Lid close button | GPIO1 | Button connects GPIO1 to GND; internal pull-up |
| Voiceprint record/enroll button | GPIO24 | Button connects GPIO24 to GND; internal pull-up |
| Voiceprint delete button | GPIO25 | Button connects GPIO25 to GND; internal pull-up |
| RGB LED red | GPIO26 | Active-low output; see RGB LED wiring |
| RGB LED green | GPIO27 | Active-low output; see RGB LED wiring |
| RGB LED blue | GPIO28 | Active-low output; see RGB LED wiring |
| ES8311 I2C SDA / SCL | GPIO7 / GPIO6 | Validated on this board |
| ES8311 I2S MCLK / BCLK / WS | GPIO0 / GPIO4 / GPIO5 | GPIO0 has been validated as MCLK |
| C5 I2S TX -> ES8311 DIN | GPIO2 | Do not add external pull circuits |
| ES8311 DOUT -> C5 I2S RX | GPIO3 | Do not add external pull circuits |
| SG90 signal | GPIO10 | 50 Hz PWM; hardware verified on 2026-08-11 |
| UART console | GPIO11 / GPIO12 | Reserved for the CH340 serial interface |
| GC9A01 SCLK / MOSI | GPIO8 / GPIO9 | SPI2 write-only display bus |
| GC9A01 DC / CS / RST | GPIO13 / GPIO14 / GPIO23 | Display control pins |

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

## Lid close button wiring

| Button side | Connect to |
| --- | --- |
| One side | C5 GPIO1 |
| Other side | C5 GND |

The firmware uses the internal pull-up, so the button is active when pressed
(GPIO1 reads low). The close command is ignored unless the current servo state
is `OPENED`; voiceprint enrollment continues to use its separate GPIO24 button.

## Voiceprint delete button wiring

| Button side | Connect to |
| --- | --- |
| One side | C5 GPIO25 |
| Other side | C5 GND |

The delete button is active when pressed (GPIO25 reads low). From the normal
screen, press it once to load the registered speaker list. In the list, short
press moves the cursor. Long-press a registered speaker to enter confirmation,
or select `EXIT MENU` and long-press to return to the normal screen. In
confirmation, short press switches between the Chinese `Confirm` and `Cancel`
choices; long press performs the selected action. The device never sends a
delete request until `Confirm` is selected and long-pressed.

## RGB LED wiring

Use three separate LEDs with three separate resistors. The outputs are
active-low so their reset level stays high:

| LED | C5 GPIO | Connection |
| --- | ---: | --- |
| Red | GPIO26 | `3V3 -> resistor -> LED anode`, LED cathode -> GPIO26 |
| Green | GPIO27 | `3V3 -> resistor -> LED anode`, LED cathode -> GPIO27 |
| Blue | GPIO28 | `3V3 -> resistor -> LED anode`, LED cathode -> GPIO28 |

Use a suitable current-limiting resistor for each LED; 1 kohm is a conservative
starting value at 3.3 V. Do not connect the LED cathodes directly to GND. GPIO27
and GPIO28 are boot-strapping pins and the active-low connection lets them stay
high while the chip resets. The firmware runs red-green-blue in sequence while
opening, blue-green-red while closing, and fast red blinking if an opening PWM
command fails. A mechanical jam cannot be detected without a lid-position or
motor-current sensor.
## GC9A01 wiring

| GC9A01 label | Connect to |
| --- | --- |
| `GND` | C5 `GND` |
| `VCC` | C5 `3V3` |
| `SCL` / `CLK` | C5 `GPIO8` |
| `SDA` / `DIN` | C5 `GPIO9` |
| `DC` | C5 `GPIO13` |
| `CS` | C5 `GPIO14` |
| `RST` / `RES` | C5 `GPIO23` |
| `BL` / `BLK` | C5 `3V3` |

Do not connect the GC9A01 to 5 V. The display uses 3.3 V logic. It has no
MISO connection. The firmware uses a 20 MHz non-DMA SPI path to stay reliable
with Dupont wires on ESP32-C5. GPIO15 must not be used for the display reset:
on the MCN16R8 board it is the module's PSRAM chip-select signal.

At boot the display reports `LID: CLOSED` and `VOICE: STARTING`. After the
voice service starts, it reports `VOICE: LISTENING`. A verified wakeup shows
`OPENING` and then `OPEN`; `OPEN` confirms the servo command completed, not a
physical lid sensor reading. A valid GPIO1 button press shows `CLOSING` and
then returns to `CLOSED`.

## Later peripherals

Do not use GPIO11/GPIO12 during porting. GPIO25 through GPIO28 are C5
strapping pins: the GPIO25 delete button and active-low RGB LED circuit must
keep the final board booting reliably before adding any other loads to them.
