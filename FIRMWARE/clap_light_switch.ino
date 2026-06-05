/*
 * Clap-Activated Light Switch
 * ============================
 * ESP32-C3 SuperMini + INMP441 MEMS Mic + GA12-N20 Motor via H-Bridge
 *
 * Hardware:
 *   - MCU:        ESP32-C3 SuperMini
 *   - Mic:        INMP441 (I2S)
 *   - Motor:      GA12-N20 micro DC motor
 *   - Driver:     Dual H-bridge (IN1-4 / OUT1-4 / EEP / ULT)
 *   - Power:      TPS63020 buck-boost @ 3.3V (EC Buying XL63020-3.3 module)
 *   - Limits:     Salvaged 3D printer endstop switches
 *
 * Pin Assignments:
 *   INMP441  SCK  → GPIO2
 *   INMP441  WS   → GPIO3
 *   INMP441  SD   → GPIO4
 *   Motor    IN1  → GPIO5
 *   Motor    IN2  → GPIO6
 *   Motor    EEP  → GPIO7  (SLEEP/fault-reset pin)
 *   Limit SW 1    → GPIO8  (INPUT_PULLUP, active LOW = fully ON position)
 *   Limit SW 2    → GPIO9  (INPUT_PULLUP, active LOW = fully OFF position)
 */

#include <driver/i2s.h>

// ── I2S / Microphone ──────────────────────────────────────────────────────────
#define I2S_PORT        I2S_NUM_0
#define I2S_SCK_PIN     2
#define I2S_WS_PIN      3
#define I2S_SD_PIN      4
#define SAMPLE_RATE     16000
#define SAMPLE_BUFFER   512

// ── Motor driver ──────────────────────────────────────────────────────────────
#define MOTOR_IN1       5
#define MOTOR_IN2       6
#define MOTOR_EEP       7   // SLEEP / overcurrent-fault reset

// ── Limit switches (active LOW with INPUT_PULLUP) ─────────────────────────────
#define LIMIT_ON        8   // Triggered when light is fully ON
#define LIMIT_OFF       9   // Triggered when light is fully OFF
#define MOTOR_TIMEOUT   3000  // ms before giving up if limit not reached

// ── Clap detection tuning ─────────────────────────────────────────────────────
#define CLAP_THRESHOLD  80000000L
#define AMP_MAP_LOW     0L
#define AMP_MAP_HIGH    60000000L
#define CLAP_MIN_GAP    150   // ms  — minimum gap between two claps
#define CLAP_MAX_GAP    800   // ms  — maximum gap for double-clap
#define COOLDOWN_MS     3000  // ms  — ignore claps after an action

// ── State ─────────────────────────────────────────────────────────────────────
bool lightIsOn = false;

unsigned long lastClapTime   = 0;
unsigned long lastActionTime = 0;
bool          waitingSecond  = false;

// ─────────────────────────────────────────────────────────────────────────────
void setupI2S() {
  i2s_config_t cfg = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate          = SAMPLE_RATE,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format       = I2S_CHANNEL_FMT_ALL_LEFT,   // Required for this ESP32-C3 clone
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 4,
    .dma_buf_len          = SAMPLE_BUFFER,
    .use_apll             = false,
    .tx_desc_auto_clear   = false,
    .fixed_mclk           = 0
  };

  i2s_pin_config_t pins = {
    .bck_io_num   = I2S_SCK_PIN,
    .ws_io_num    = I2S_WS_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = I2S_SD_PIN
  };

  i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
  i2s_set_pin(I2S_PORT, &pins);
}

void setupMotor() {
  pinMode(MOTOR_IN1, OUTPUT);
  pinMode(MOTOR_IN2, OUTPUT);
  pinMode(MOTOR_EEP, OUTPUT);
  resetDriver();
}

// Wake the H-bridge from sleep / clear overcurrent fault
void resetDriver() {
  digitalWrite(MOTOR_EEP, LOW);
  delay(10);
  digitalWrite(MOTOR_EEP, HIGH);
}

void motorStop() {
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, LOW);
}

void motorForward() {
  resetDriver();
  digitalWrite(MOTOR_IN1, HIGH);
  digitalWrite(MOTOR_IN2, LOW);
}

void motorReverse() {
  resetDriver();
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, HIGH);
}

// Drive motor until the target limit switch triggers (or timeout)
void driveToLimit(int limitPin, void (*motorDir)()) {
  unsigned long start = millis();
  motorDir();
  while (digitalRead(limitPin) == HIGH) {
    if (millis() - start > MOTOR_TIMEOUT) {
      Serial.println("⚠ Motor timeout — limit switch not reached");
      break;
    }
    delay(10);
  }
  motorStop();
}

void turnLightOn() {
  Serial.println("→ Turning ON");
  driveToLimit(LIMIT_ON, motorForward);
  lightIsOn = true;
}

void turnLightOff() {
  Serial.println("→ Turning OFF");
  driveToLimit(LIMIT_OFF, motorReverse);
  lightIsOn = false;
}

long readAmplitude() {
  int32_t samples[SAMPLE_BUFFER];
  size_t  bytesRead = 0;

  i2s_read(I2S_PORT, samples, sizeof(samples), &bytesRead, portMAX_DELAY);

  int count = bytesRead / sizeof(int32_t);
  long sum  = 0;
  for (int i = 0; i < count; i++) {
    sum += abs(samples[i] >> 8);   // scale 32-bit sample
  }
  return (count > 0) ? sum / count : 0;
}

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("Clap Light Switch — booting");

  pinMode(LIMIT_ON,  INPUT_PULLUP);
  pinMode(LIMIT_OFF, INPUT_PULLUP);

  setupI2S();
  setupMotor();

  Serial.println("Ready — double-clap to toggle light");
}

void loop() {
  long amp = readAmplitude();

  // Optional: uncomment to calibrate thresholds via Serial Monitor
  // Serial.println(amp);

  if (amp < CLAP_THRESHOLD) return;

  unsigned long now = millis();

  // Ignore claps during cooldown
  if (now - lastActionTime < COOLDOWN_MS) return;

  if (!waitingSecond) {
    // First clap detected — start the double-clap window
    lastClapTime  = now;
    waitingSecond = true;
  } else {
    unsigned long gap = now - lastClapTime;

    if (gap >= CLAP_MIN_GAP && gap <= CLAP_MAX_GAP) {
      // Valid double-clap!
      waitingSecond = false;
      lastActionTime = now;
      Serial.printf("Double-clap! gap=%lums\n", gap);

      if (lightIsOn) {
        turnLightOff();
      } else {
        turnLightOn();
      }
    } else if (gap > CLAP_MAX_GAP) {
      // Window expired — treat this clap as a new first clap
      lastClapTime = now;
    }
    // gap < MIN_GAP: too fast, ignore (stay in waitingSecond state)
  }

  // Reset double-clap window if it expires without a second clap
  if (waitingSecond && (millis() - lastClapTime > CLAP_MAX_GAP)) {
    waitingSecond = false;
  }
}
