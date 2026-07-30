// ============================================================
// AntBot-S3 — BLE build (with per-bot PIN + single-controller lock)
//
// This is the original AntBot sketch reduced to Bluetooth Low Energy only,
// with the WiFi web-UI removed and the PIN / auth changes already applied.
// To use it you only need to set two lines per bot (DEVICE_PIN and BOT_NAME),
// pick your weapon type below, and upload.
//
// Drives with the companion Android app "AntBot BLE".
// Have fun! - Adam  (BLE + PIN edits applied afterward)
// ============================================================

// Connection mode is fixed to BLE in this build.
#define CONNECTION_BLE

// ============================================================
// SELECT YOUR WEAPON TYPE — uncomment exactly ONE:
//
// WEAPON_SERVO = a standard hobby servo (0-180 degrees) drives
//   a mechanical weapon like a flipper or lifter arm.
//
// WEAPON_ESC   = a brushless motor + ESC spins a weapon (blade/drum).
//   ESCs take the same pulse signal as a servo, but the "angle" is
//   interpreted as throttle. Use this with a slim 15A 2S brushless ESC.
// ============================================================
// #define WEAPON_SERVO
#define WEAPON_ESC

#include <ESP32Servo.h> // control servos AND ESCs

#include <BLEDevice.h>  // core BLE support
#include <BLEServer.h>  // ESP32 as a BLE peripheral
#include <BLEUtils.h>   // BLE helpers
#include <BLE2902.h>    // descriptor needed for notifications

// ==================== EDIT AS NEEDED =====================
// Pin numbers tell the ESP32 which physical wire to talk to.
// If a motor spins backward, swap its IN1 and IN2 pin numbers.
// =========================================================

// ================== PINS ==================
const int WEAPON_PIN = 6; // signal wire for the weapon (servo OR ESC)

const int L_IN1 = 7;
const int L_IN2 = 8;

const int R_IN1 = 10;
const int R_IN2 = 9;
// ================= END OF EDITABLE SEGMENT ================

// Built-in RGB "NeoPixel" status LED.
#define RGB_LED 21

// ============================================================
// MOTOR PWM SETTINGS
// PWM_FREQ 20 kHz is above hearing; 8-bit gives duty 0..255.
// ============================================================
#define PWM_FREQ 20000
#define PWM_RES 8

// ================== SHARED STATE ==================
// How long with no control frame before we kill everything automatically.
const unsigned long FAILSAFE_TIMEOUT = 1200;

bool botActive = false;          // is the bot allowed to move right now?

// stopLatched: when true the bot is HARD STOPPED and refuses drive/throttle
// input until someone explicitly ACTIVATEs again. Only handleActivate() /
// the BLE "ACTIVATE" command clears it.
bool stopLatched = true;         // boot up latched

unsigned long lastInputTime = 0; // timestamp of the last control frame

#ifdef WEAPON_SERVO
const int WEAPON_IDLE = 90;      // servo: 90 = centered
#endif
#ifdef WEAPON_ESC
const int WEAPON_IDLE = 0;       // ESC: 0 = zero throttle
#endif

int leftPos = 90, rightPos = 90;
int weaponPos = WEAPON_IDLE;

volatile int lastWeaponUs = 1000;

bool benchActive = false;        // set only by the serial BENCH command

#ifdef WEAPON_SERVO
Servo weaponServo;
#endif

#ifdef WEAPON_ESC
Servo weaponESC;
// A slim airplane-style ESC is UNIDIRECTIONAL throttle:
//   1000us = zero throttle / off  <-- safe idle point
//   2000us = full throttle
// The weapon command on the wire is 0-180 = throttle (0 = off, 180 = full).
const int ESC_MIN_US = 1000;
const int ESC_MAX_US = 2000;
#endif

// ============================================================
// Forward declarations (shared functions used below).
// ============================================================
void driveMotor(int in1, int in2, int speed);
void stopMotors();
void setDrive(int leftCmd, int rightCmd);
void setWeapon(int pos);
void forceWeaponIdle();
void killOutputs();
void engageStop(const char* reason);
void rgbOff();
void setRGB(uint8_t r, uint8_t g, uint8_t b);
void updateRGB();

// ============================================================
// BLE globals + callbacks
// ============================================================
#ifdef CONNECTION_BLE
// These must match the Android app.
#define BLE_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_CMD_CHAR_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8" // WRITE (Android -> Bot)
#define BLE_RSP_CHAR_UUID "6e400003-b5a3-f393-e0a9-e50e24dcca9e" // NOTIFY (Bot -> Android)

BLEServer* pServer = nullptr;
BLECharacteristic* pCmdChar = nullptr;
BLECharacteristic* pRspChar = nullptr;
bool bleConnected = false;

// ==================== EDIT AS NEEDED =====================
// Per-bot PIN. Give EVERY bot a different PIN and give each driver only their
// bot's PIN. The bot ignores everything except STOP until it receives the
// correct "AUTH,<pin>" on a connection.
const char* DEVICE_PIN = "4471";
// Friendly name shown in the app's bot picker. Make it unique per bot, e.g.
// "AntBot-1", "AntBot-2". Leave "" to fall back to the MAC-based name.
const char* BOT_NAME = "AntBot-1";
// ================= END OF EDITABLE SEGMENT ================

bool authorized = false;   // has THIS connection sent the correct PIN?

// ---- BLE Command Reference ----
// Android WRITES to BLE_CMD_CHAR_UUID:
//   "AUTH,<pin>"      -> must succeed once per connection before anything else
//   "ACTIVATE"        -> arm at zero throttle (clears the stop latch)
//   "STOP" / "ABORT"  -> kill motors + weapon and LATCH OFF (allowed w/o PIN)
//   "PING"            -> keepalive (does NOT reset failsafe)
//   "D,LLL,RRR,WWW"   -> drive frame, e.g. "D,090,135,000" (0-180; 90 = stop;
//                        WWW is weapon throttle, ESC 0 = off, 180 = full)
//   "STATUS"          -> query armed state
//
// Bot NOTIFIES on BLE_RSP_CHAR_UUID:
//   "AUTH_OK" / "AUTH_FAIL"  -> PIN result
//   "NOT_AUTH"        -> a command arrived before a valid PIN
//   "ACTIVATED" / "STOPPED" / "PONG" / "OK" / "NOT_ACTIVE"
//   "STATUS,true|false" / "FAILSAFE"

// Sends a string as a BLE notification. Safe to call anytime.
void bleSend(const String& msg) {
  if (!bleConnected || pRspChar == nullptr) return;
  pRspChar->setValue(msg.c_str());
  pRspChar->notify();
}

// Handles connect/disconnect events.
class BotServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s) override {
    bleConnected = true;
    authorized = false;             // must re-enter PIN every connection
    BLEDevice::stopAdvertising();   // bot disappears while held: no 2nd phone can grab it
    Serial.println("[BLE] Client connected -- awaiting PIN");
    setRGB(0, 0, 255); // blue = connected, not yet authorized
  }
  void onDisconnect(BLEServer* s) override {
    bleConnected = false;
    authorized = false;
    Serial.println("[BLE] Client disconnected -- failsafe");
    engageStop("BLE client disconnected");
    // Restart advertising so the next phone can find the (now free) bot
    BLEDevice::startAdvertising();
  }
};

// Fires whenever the Android app writes a command.
class CmdCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) override {
    String cmd = String(pChar->getValue().c_str());
    cmd.trim();
    Serial.print("[BLE] CMD: "); Serial.println(cmd);

    // ---- AUTH,<pin> ----  must succeed once per connection before anything else
    if (cmd.startsWith("AUTH,")) {
      String pin = cmd.substring(5); pin.trim();
      if (pin == String(DEVICE_PIN)) {
        authorized = true;
        Serial.println("[BLE] AUTH ok");
        bleSend("AUTH_OK");
      } else {
        authorized = false;
        Serial.println("[BLE] AUTH failed");
        bleSend("AUTH_FAIL");
      }
      return;
    }

    // STOP / ABORT work even without a PIN -- stopping is never unsafe.
    if (cmd == "STOP" || cmd == "ABORT") {
      engageStop("BLE stop/abort");
      bleSend("STOPPED");
      return;
    }

    // Everything past here requires a valid PIN on this connection.
    if (!authorized) {
      killOutputs();
      bleSend("NOT_AUTH");
      return;
    }

    // ---- ACTIVATE ----  the only thing that clears the stop latch.
    if (cmd == "ACTIVATE") {
      killOutputs();
      stopLatched = false;
      botActive = true;
      lastInputTime = millis(); // start the failsafe clock
      setWeapon(WEAPON_IDLE);
      setRGB(0, 255, 0);        // green = ready to drive
      bleSend("ACTIVATED");
      return;
    }

    // ---- PING ----  does NOT reset the failsafe timer.
    if (cmd == "PING") {
      if (!botActive || stopLatched) { bleSend("NOT_ACTIVE"); return; }
      bleSend("PONG");
      return;
    }

    // ---- DRIVE "D,LLL,RRR,WWW" ----
    if (cmd.startsWith("D,")) {
      if (!botActive || stopLatched) {
        killOutputs();
        bleSend("NOT_ACTIVE");
        return;
      }
      int idx1 = cmd.indexOf(',', 2);
      int idx2 = (idx1 >= 0) ? cmd.indexOf(',', idx1 + 1) : -1;
      if (idx1 < 0 || idx2 < 0) {
        bleSend("ERR,BAD_FORMAT");
        return;
      }
      int newLeft = constrain(cmd.substring(2, idx1).toInt(), 0, 180);
      int newRight = constrain(cmd.substring(idx1+1, idx2).toInt(), 0, 180);
      int newWeapon = constrain(cmd.substring(idx2+1).toInt(), 0, 180);

      leftPos = newLeft;
      rightPos = newRight;
      weaponPos = newWeapon;

      setDrive(leftPos, rightPos);
      setWeapon(weaponPos);
      updateRGB();

      // Every frame refreshes the failsafe clock, including all-zero frames.
      lastInputTime = millis();

      bleSend("OK");
      return;
    }

    // ---- STATUS ----
    if (cmd == "STATUS") {
      bleSend(String("STATUS,") + (botActive ? "true" : "false"));
      return;
    }

    bleSend("ERR,UNKNOWN_CMD");
  }
};
#endif // CONNECTION_BLE

// ============================================================
// MOTOR CONTROL
//   IN1=0,     IN2=speed -> forward (0-255)
//   IN1=speed, IN2=0     -> reverse
//   IN1=255,   IN2=255   -> brake
//   IN1=0,     IN2=0     -> coast
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

// Convert 0-180 joystick values into motor speeds (90 = neutral).
void setDrive(int leftCmd, int rightCmd) {
  int leftThrottle = leftCmd - 90;
  int rightThrottle = rightCmd - 90;

  int leftSpeed = map(leftThrottle, -90, 90, -255, 255);
  int rightSpeed = map(rightThrottle, -90, 90, -255, 255);

  driveMotor(L_IN1, L_IN2, leftSpeed);
  driveMotor(R_IN1, R_IN2, rightSpeed);
}

// ============================================================
// WEAPON CONTROL — single place that talks to the weapon hardware.
// ============================================================
void setWeapon(int pos) {
  pos = constrain(pos, 0, 180);

  // HARD GATE: nothing commands the weapon while stopped or disarmed.
  if (!botActive || stopLatched) pos = WEAPON_IDLE;

#ifdef WEAPON_SERVO
  weaponServo.write(pos);
  lastWeaponUs = map(pos, 0, 180, 500, 2400);
#endif

#ifdef WEAPON_ESC
  int us = map(pos, 0, 180, ESC_MIN_US, ESC_MAX_US);
  weaponESC.writeMicroseconds(us);
  lastWeaponUs = us;
#endif
}

// ============================================================
// SAFETY PRIMITIVES
// ============================================================
void forceWeaponIdle() {
#ifdef WEAPON_SERVO
  weaponServo.write(WEAPON_IDLE);
  lastWeaponUs = 1500;
#endif
#ifdef WEAPON_ESC
  weaponESC.writeMicroseconds(ESC_MIN_US);
  lastWeaponUs = ESC_MIN_US;
#endif
}

// Put every actuator into its safe state RIGHT NOW.
void killOutputs() {
  forceWeaponIdle();  // weapon first
  stopMotors();
  leftPos = rightPos = 90;
  weaponPos = WEAPON_IDLE;
}

// THE emergency stop: kill hardware, THEN latch out further input.
void engageStop(const char* reason) {
  killOutputs();

  botActive = false;
  stopLatched = true;

  forceWeaponIdle();
  stopMotors();

  setRGB(255, 0, 0);  // solid red = latched stop
  Serial.print("[STOP] Latched. Reason: ");
  Serial.println(reason);
}

// ============================================================
// RGB LED
// ============================================================
void rgbOff() { neopixelWrite(RGB_LED, 0, 0, 0); }
void setRGB(uint8_t r, uint8_t g, uint8_t b) { neopixelWrite(RGB_LED, r, g, b); }

void updateRGB() {
  int ls = leftPos - 90;
  int rs = rightPos - 90;
  if (ls > 15 && rs > 15) setRGB(0, 255, 0);              // both forward -> green
  else if (ls < -15 && rs < -15) setRGB(255, 0, 0);       // both reverse -> red
  else if (abs(ls - rs) > 15) setRGB(255, 0, 255);        // turning -> magenta
  else rgbOff();                                          // idle -> off
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  // ---- Motor LEDC setup (before the servo library starts) ----
  ledcAttach(L_IN1, PWM_FREQ, PWM_RES);
  ledcAttach(L_IN2, PWM_FREQ, PWM_RES);
  ledcAttach(R_IN1, PWM_FREQ, PWM_RES);
  ledcAttach(R_IN2, PWM_FREQ, PWM_RES);
  stopMotors();

#ifdef WEAPON_SERVO
  weaponServo.attach(WEAPON_PIN, 500, 2400);
  weaponServo.write(WEAPON_IDLE);
#endif

#ifdef WEAPON_ESC
  // ---- ESC Arming Sequence ----
  // Send zero throttle and HOLD it while the ESC powers up and arms.
  // KEEP THE WEAPON CLEAR while this runs.
  weaponESC.attach(WEAPON_PIN, ESC_MIN_US, ESC_MAX_US);
  weaponESC.writeMicroseconds(ESC_MIN_US);
  Serial.println("[ESC] Arming weapon ESC -- keep clear of the weapon!");
  for (int i = 0; i < 60; i++) {
    weaponESC.writeMicroseconds(ESC_MIN_US);
    delay(50);
  }
  Serial.println("[ESC] Weapon ESC armed at zero throttle.");
  Serial.println("[ESC] Bot is STOP-LATCHED. Send ACTIVATE to enable controls.");
#endif

  pinMode(RGB_LED, OUTPUT);
  rgbOff();

  Serial.println("\n=== AntBot-S3 | N20 + DRV8833 + Weapon | BLE ===");

  // ----------------------------------------------------------
  // BLE startup
  // ----------------------------------------------------------
#ifdef CONNECTION_BLE
  Serial.println("[MODE] BLE");
  String bleName = (strlen(BOT_NAME) > 0)
                   ? String(BOT_NAME)
                   : ("AntBot-" + String((uint32_t)(ESP.getEfuseMac() >> 32), HEX));
  BLEDevice::init(bleName.c_str());
  Serial.print(" BLE name : "); Serial.println(bleName);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new BotServerCallbacks());

  BLEService* pService = pServer->createService(BLE_SERVICE_UUID);

  pCmdChar = pService->createCharacteristic(
                 BLE_CMD_CHAR_UUID,
                 BLECharacteristic::PROPERTY_WRITE |
                 BLECharacteristic::PROPERTY_WRITE_NR);
  pCmdChar->setCallbacks(new CmdCallbacks());

  pRspChar = pService->createCharacteristic(
                 BLE_RSP_CHAR_UUID,
                 BLECharacteristic::PROPERTY_NOTIFY);
  pRspChar->addDescriptor(new BLE2902());

  pService->start();

  BLEAdvertising* pAdv = BLEDevice::getAdvertising();
  pAdv->addServiceUUID(BLE_SERVICE_UUID);
  pAdv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println(" Advertising -- waiting for Android client...");

  setRGB(128, 0, 255); // purple = advertising, no phone connected yet
#endif
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  // ---- Failsafe check ----
  // If armed but no control frame for a while, cut power immediately.
  if (botActive && (millis() - lastInputTime > FAILSAFE_TIMEOUT)) {
    Serial.println("!!! FAILSAFE TRIGGERED !!!");
    engageStop("failsafe timeout");
#ifdef CONNECTION_BLE
    bleSend("FAILSAFE");
#endif
  }

  // ---- Continuous safe-state enforcement ----
  // While disarmed, re-assert zero throttle at 20Hz. A one-shot write is a
  // hope; this is a guarantee.
  if ((!botActive || stopLatched) && !benchActive) {
    static unsigned long lastAssert = 0;
    if (millis() - lastAssert >= 50) {
      lastAssert = millis();
      forceWeaponIdle();
      stopMotors();
    }
  }

  // ---- Serial bench test ----
  // Type BENCH (115200, newline) to ramp the weapon to 25% with no phone.
  // REMOVE THE WEAPON BLADE/DRUM BEFORE RUNNING THIS.
  if (Serial.available()) {
    String c = Serial.readStringUntil('\n');
    c.trim();
    if (c == "BENCH") {
#ifdef WEAPON_ESC
      Serial.println("[BENCH] Weapon removed? Ramping to 25% in 3s...");
      delay(3000);
      benchActive = true;
      for (int us = ESC_MIN_US; us <= ESC_MIN_US + 250; us += 5) {
        weaponESC.writeMicroseconds(us);
        lastWeaponUs = us;
        Serial.printf("[BENCH] %d us\n", us);
        delay(100);
        if (Serial.available()) break;
      }
      delay(1500);
      weaponESC.writeMicroseconds(ESC_MIN_US);
      lastWeaponUs = ESC_MIN_US;
      benchActive = false;
      Serial.println("[BENCH] Done, throttle back to zero.");
#else
      Serial.println("[BENCH] Only available in WEAPON_ESC mode.");
#endif
    } else if (c == "STOP") {
      engageStop("serial STOP command");
    }
  }

  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 500) {
    Serial.printf("Active:%s Latched:%s Auth:%s L:%d R:%d W:%d (%dus)\n",
                  botActive ? "YES" : "NO",
                  stopLatched ? "YES" : "no",
#ifdef CONNECTION_BLE
                  authorized ? "YES" : "no",
#else
                  "n/a",
#endif
                  leftPos, rightPos, weaponPos, lastWeaponUs);
    lastPrint = millis();
  }
}
