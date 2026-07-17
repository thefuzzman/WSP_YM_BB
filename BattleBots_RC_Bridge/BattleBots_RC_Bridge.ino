// ==================== EDIT AS NEEDED =====================
// PINS -- match these to your actual wiring.
// =========================================================

// ---- RC receiver input pins ----
// These connect to the WHITE (signal) wire of two receiver
// channels. The receiver's BEC (red) and ground (black) wires
// power the ESP32 as covered in the wiring diagrams earlier --
// they don't connect to these two pins.
const int CH1_STEER_PIN = 4;     // steering channel (e.g. CH1)
const int CH3_THROTTLE_PIN = 5;  // throttle channel (e.g. CH3)

// ---- DRV8833 motor driver pins ----
const int L_IN1 = 7;
const int L_IN2 = 8;
const int R_IN1 = 10;
const int R_IN2 = 9;

// ------------------- Status LED -----------------
// Green = valid RC signal being received. Red = no signal /
// failsafe (motors stopped). This is just a debug convenience,
// not required for the bot to function.
#define RGB_LED 21
// ================= END OF EDITABLE SEGMENT ================

// ============================================================
// PWM MOTOR SETTINGS -- same as the original sketch
// ============================================================
#define PWM_FREQ 20000 // 20kHz, above human hearing
#define PWM_RES 8      // 8-bit = duty values 0-255

// ============================================================
// We measure each channel's pulse width using hardware
// interrupts rather than the simpler pulseIn() function.
// pulseIn() BLOCKS the whole program while it waits for a
// pulse -- with two channels to read every ~20ms, that would
// eat almost all our CPU time and make the motor control feel
// laggy. Interrupts let the ESP32 timestamp the rising and
// falling edges of each signal in the background, so loop()
// stays free to just read the latest values and drive motors.
//
// "volatile" tells the compiler these variables can change at
// any moment from inside an interrupt, so it shouldn't try to
// cache or optimize away repeated reads of them.
// ============================================================
volatile unsigned long ch1RiseTime = 0;
volatile int ch1PulseWidth = 1500;       // last measured pulse width (us)
volatile unsigned long ch1LastEdge = 0;  // millis() of last time this updated

volatile unsigned long ch3RiseTime = 0;
volatile int ch3PulseWidth = 1500;
volatile unsigned long ch3LastEdge = 0;

// IRAM_ATTR keeps this function in fast internal memory -- required
// for interrupt handlers on the ESP32 so they run quickly and
// reliably even while the flash chip is busy doing something else.
void IRAM_ATTR ch1ISR() {
  if (digitalRead(CH1_STEER_PIN) == HIGH) {
    ch1RiseTime = micros();       // pulse just started -- remember when
  } else {
    ch1PulseWidth = micros() - ch1RiseTime; // pulse just ended -- measure it
    ch1LastEdge = millis();
  }
}

void IRAM_ATTR ch3ISR() {
  if (digitalRead(CH3_THROTTLE_PIN) == HIGH) {
    ch3RiseTime = micros();
  } else {
    ch3PulseWidth = micros() - ch3RiseTime;
    ch3LastEdge = millis();
  }
}

// ============================================================
// FAILSAFE
//
// If we haven't seen a fresh pulse on EITHER channel in this
// long, something is wrong -- receiver unbound, unplugged,
// out of range, transmitter off -- so we stop the motors.
//
// Standard RC receivers send a new frame roughly every 14-20ms,
// so 200ms is about 10+ missed frames in a row -- long enough
// to not false-trigger on a single glitched pulse, short enough
// to react quickly to a real signal loss.
//
// NOTE: this is a backup safety net for wiring/power problems.
// Your RECEIVER should ALSO be configured (usually during the
// transmitter binding process) with its own failsafe behavior,
// since many receivers keep sending frames at a fixed "safe"
// value rather than going silent when the transmitter link is
// lost -- that receiver-side failsafe is your first line of
// defense; this timeout is the second.
// ============================================================
const unsigned long RC_SIGNAL_TIMEOUT = 200; // ms

// Small deadzone around center (1500us) so stick drift/jitter
// doesn't cause the bot to creep when both sticks are released.
const int DEADZONE_US = 25;

// ============================================================
// Forward declarations
// ============================================================
void driveMotor(int in1, int in2, int speed);
void stopMotors();
void setRGB(uint8_t r, uint8_t g, uint8_t b);
void rgbOff();

// ============================================================
// MOTOR CONTROL -- identical logic to the original sketch
//
// speed ranges from -255 (full reverse) to +255 (full forward).
// The DRV8833 takes two PWM signals per motor:
//   IN1=0,     IN2=speed -> forward
//   IN1=speed, IN2=0     -> reverse
//   IN1=255,   IN2=255   -> brake
// ============================================================
void driveMotor(int in1, int in2, int speed) {
  speed = constrain(speed, -255, 255);
  if (speed == 0) {
    ledcWrite(in1, 255);
    ledcWrite(in2, 255);
  } else if (speed > 0) {
    ledcWrite(in1, 0);
    ledcWrite(in2, speed);
  } else {
    ledcWrite(in1, -speed);
    ledcWrite(in2, 0);
  }
}

void stopMotors() {
  ledcWrite(L_IN1, 255); ledcWrite(L_IN2, 255);
  ledcWrite(R_IN1, 255); ledcWrite(R_IN2, 255);
}

void setRGB(uint8_t r, uint8_t g, uint8_t b) { neopixelWrite(RGB_LED, r, g, b); }
void rgbOff() { neopixelWrite(RGB_LED, 0, 0, 0); }

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== AntBot-S3 | RC Bridge Mode ===");
  Serial.println("No WiFi/BLE -- all control from the RC transmitter.");

  // ---- Motor driver PWM setup ----
  ledcAttach(L_IN1, PWM_FREQ, PWM_RES);
  ledcAttach(L_IN2, PWM_FREQ, PWM_RES);
  ledcAttach(R_IN1, PWM_FREQ, PWM_RES);
  ledcAttach(R_IN2, PWM_FREQ, PWM_RES);
  stopMotors();

  // ---- RC receiver input setup ----
  // INPUT_PULLDOWN keeps the pin from floating (reading random
  // noise as pulses) if the receiver is ever briefly disconnected.
  pinMode(CH1_STEER_PIN, INPUT_PULLDOWN);
  pinMode(CH3_THROTTLE_PIN, INPUT_PULLDOWN);
  // CHANGE means the interrupt fires on BOTH the rising edge
  // (pulse starts) and falling edge (pulse ends) -- we need both
  // to measure the pulse's width.
  attachInterrupt(digitalPinToInterrupt(CH1_STEER_PIN), ch1ISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(CH3_THROTTLE_PIN), ch3ISR, CHANGE);

  pinMode(RGB_LED, OUTPUT);
  setRGB(255, 100, 0); // amber = waiting for first valid RC signal

  Serial.println("Waiting for RC signal...");
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  // ---- Safely copy the volatile values ----
  // Reading a single int is effectively atomic on the ESP32, but
  // we copy both before doing any math so an interrupt firing
  // mid-calculation can't hand us a half-updated pair of numbers.
  int steerUs = ch1PulseWidth;
  int throttleUs = ch3PulseWidth;
  unsigned long steerAge = millis() - ch1LastEdge;
  unsigned long throttleAge = millis() - ch3LastEdge;

  bool signalFresh = (steerAge < RC_SIGNAL_TIMEOUT) &&
                      (throttleAge < RC_SIGNAL_TIMEOUT);

  if (!signalFresh) {
    // Lost signal (or never had one yet) -- stop and wait.
    stopMotors();
    setRGB(255, 0, 0); // red = no signal
  } else {
    // ---- Deadzone: snap near-center readings to exactly center ----
    if (abs(steerUs - 1500) < DEADZONE_US) steerUs = 1500;
    if (abs(throttleUs - 1500) < DEADZONE_US) throttleUs = 1500;

    // ---- Clamp to the valid RC pulse range before mapping ----
    // Protects against a stray glitched reading briefly sending
    // an out-of-range value to map().
    steerUs = constrain(steerUs, 1000, 2000);
    throttleUs = constrain(throttleUs, 1000, 2000);

    // ---- Convert 1000-2000us pulses into a -255..+255 range ----
    int steer = map(steerUs, 1000, 2000, -255, 255);
    int throttle = map(throttleUs, 1000, 2000, -255, 255);

    // ---- Tank/skid-steer mixing ----
    // Same idea as the joystick mixing in the WiFi version:
    // both wheels get the throttle value, then steering ADDS to
    // one side and SUBTRACTS from the other so the bot turns.
    int leftSpeed = constrain(throttle + steer, -255, 255);
    int rightSpeed = constrain(throttle - steer, -255, 255);

    driveMotor(L_IN1, L_IN2, leftSpeed);
    driveMotor(R_IN1, R_IN2, rightSpeed);
    setRGB(0, 255, 0); // green = good signal, driving normally
  }

  // ---- Periodic debug output ----
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 250) {
    Serial.printf("CH1(steer):%dus  CH3(throttle):%dus  signal:%s\n",
                  steerUs, throttleUs, signalFresh ? "OK" : "LOST");
    lastPrint = millis();
  }
}
