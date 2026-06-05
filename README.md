# DIY-Clap-Light-Switch

An ESP32-C3 powered light switch that toggles on a double-clap, using an INMP441 MEMS microphone for clap detection 
and a GA12-N20 micro DC motor to physically actuate the switch. Designed for a 3D-printed enclosure and powered from 
a single 18650 Li-ion cell on a BreadVolt with 5V and 3.3V.

## Features
- **Double-clap detection** — distinguishes a genuine double-clap from background noise using configurable timing windows and noise max and min settings
- **Physical actuation** — GA12-N20 geared motor drives the switch lever; limit switches define the ON/OFF positions
- **Overcurrent recovery** — automatically resets the motor driver's SLEEP pin after fault conditions
- **Battery powered** — TPS63020 buck-boost regulates 3.3 V across the full 18650 discharge curve
- **Variable cooldown window** — prevents false re-triggers immediately after an action, initially triggered by motor noises next to microphone

## Hardware
| Component       | Part |
|---              |---|
| MCU             | ESP32-C3 SuperMini |
| Microphone      | INMP441 MEMS (I2S) |
| Motor           | GA12-N20 micro DC geared motor |
| Motor driver    | Dual H-bridge (IN1–4 / OUT1–4 / EEP / ULT pinout) |
| Power module    | EC Buying XL63020-3.3 (TPS63020-based buck-boost, 3.3 V fixed) |
| Battery         | 18650 Li-ion cell (external charger workflow) |
| Limit switches  | Salvaged 3D printer endstop switches × 2 |
| Enclosure       | Custom 3D-printed (see CAD) |

### Pin Assignments
| Signal                         | GPIO |
|---                             |---|
| I2S SCK (INMP441)              | 2 |
| I2S WS (INMP441)               | 3 |
| I2S SD (INMP441)               | 4 |
| Motor IN1                      | 5 |
| Motor IN2                      | 6 |
| Motor EEP / SLEEP              | 7 |
| Limit switch — ON position     | 8 |
| Limit switch — OFF position    | 9 |

Limit switches are wired to GND and use INPUT_PULLUP; they read LOW when triggered.

## Firmware
The sketch lives in `/clap_light_switch.ino` and targets the **Arduino IDE 2.x** with the **ESP32 Arduino core**.

### Key Tuning Parameters
```cpp
#define CLAP_THRESHOLD  80000000L   // Amplitude required to register a clap
#define AMP_MAP_LOW     0L          // Amplitude mapping range low
#define AMP_MAP_HIGH    60000000L   // Amplitude mapping range high
#define CLAP_MIN_GAP    150         // ms — minimum gap between the two claps
#define CLAP_MAX_GAP    800         // ms — maximum gap for a valid double-clap
#define COOLDOWN_MS     3000        // ms — lockout after an action
#define MOTOR_TIMEOUT   3000        // ms — fallback if a limit switch is not reached
```

### INMP441 Notes

This specific ESP32-C3 SuperMini clone requires:
- `I2S_CHANNEL_FMT_ALL_LEFT` (standard `RIGHT_LEFT` format produces silence)
- 16000 Hz sample rate

### Building & Flashing
1. Install the [ESP32 Arduino core](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)
2. Select **ESP32C3 Dev Module** as the board
3. Open `firmware/clap_light_switch/clap_light_switch.ino`
4. Flash via USB

======================================================================================

## PCB Design (In Progress)

Target: 2-layer board, ~55 × 90 mm, designed using KiCad.

| Component                          | Mounting |
|---                                 |---|
| ESP32-C3 SuperMini                 | Removable pin headers |
| INMP441                            | Removable pin headers |
| DRV8833 motor driver               | Bare HTSSOP-16 (hand-solderable) |
| TPS63020 power module              | Removable pin headers |
| Motor / battery / limit switches   | 3.5 mm pitch screw terminals |
