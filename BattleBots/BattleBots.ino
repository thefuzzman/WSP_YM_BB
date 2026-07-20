// This is a comment.
// Comments are ignored when the code
// is compiled and uploaded to the device

// Any segment - such as the next one - that can be modified
// to suit your needs will be prefaced with:
// ==================== EDIT AS NEEDED =====================
// Some descriptive text here
// =========================================================
// And have a line under the editable segment that looks like
// this
// ================= END OF EDITABLE SEGMENT ================

// START OF ACTUAL CODE
// This is a baseline sketch only - it will be usable as-is,
// however you'll likely need to modify to meet what you want
// out of the code
// Have fun! - Adam
// ============================================================

// SELECT YOUR CONNECTION MODE — uncomment exactly ONE:
//
// "Uncommenting" means removing the // at the start of a line.
// The // turns a line into a comment so the compiler skips it.
// Only ONE of these two should be active at a time.
//
// CONNECTION_WIFI = the bot creates its own Wi-Fi network.
//   You connect your phone/browser to it and
//   go to 192.168.4.1 to drive.
//
// CONNECTION_BLE  = the bot advertises over Bluetooth Low Energy.
//   You'd need a custom Android app to connect.
// ============================================================
#define CONNECTION_WIFI
// #define CONNECTION_BLE

// ============================================================
// SELECT YOUR WEAPON TYPE — uncomment exactly ONE:
//
// WEAPON_SERVO = a standard hobby servo (0-180 degrees) drives
//   a mechanical weapon like a flipper or lifter arm.
//
// WEAPON_ESC   = a brushless motor + ESC (Electronic Speed
//   Controller) spins a weapon, like a blade or drum.
//   ESCs are driven with the exact same kind of signal as a
//   servo, but the "angle" is interpreted as a throttle/speed
//   instead of a position. This is the mode you'd use with
//   something like a slim 15A 2S brushless ESC.
// ============================================================
// #define WEAPON_SERVO
#define WEAPON_ESC

// These are "libraries" — pre-written code that saves us from
// having to reinvent the wheel. #include pulls them into our sketch.
#include <ESP32Servo.h> // lets us control servo motors (and ESCs) easily

// The #ifdef / #endif blocks below are "conditional compilation."
// They say: "only include this code if we defined that symbol above."
// So if CONNECTION_WIFI is defined, the Wi-Fi libraries get included.
// If it isn't, they're completely ignored — zero overhead.
#ifdef CONNECTION_WIFI
#include <WiFi.h>       // core Wi-Fi support for ESP32
#include <WebServer.h>  // lets the ESP32 act as a tiny web server
#endif

#ifdef CONNECTION_BLE
#include <BLEDevice.h>  // core BLE support
#include <BLEServer.h>  // lets the ESP32 be a BLE peripheral (the "server" side)
#include <BLEUtils.h>   // helper utilities for BLE
#include <BLE2902.h>    // standard BLE descriptor needed for notifications to work
#endif

// ==================== EDIT AS NEEDED =====================
// Pin numbers tell the ESP32 which physical wire to talk to.
// These match MY board layout — yours might differ.
// If a motor spins backward, swap its IN1 and IN2 pin numbers.
// =========================================================

// NOTE: These are "variables", which is a word or combination of
// letters and numbers thar hold a value that the code uses
// somewhere else. Think of them like storage boxes that
// you can put things in and get things out of

// ================== PINS ==================
const int WEAPON_PIN = 6; // signal wire for the weapon (servo OR ESC) plugs in here

// Left motor: IN1 and IN2 are the two control wires on the DRV8833 driver.
// Driving them high/low/PWM is how we control speed and direction.
const int L_IN1 = 7;
const int L_IN2 = 8;

// Right motor — same idea, different pins
const int R_IN1 = 10;
const int R_IN2 = 9;
// ================= END OF EDITABLE SEGMENT ================

// -------------------------------------------------------------------
// Pin 48 on the ESP32-S3 DevKit has a built-in RGB "NeoPixel" LED.
// We use it as a status light — different colors = different states.
#define RGB_LED 21

// ============================================================
// MOTOR PWM SETTINGS
//
// PWM = "Pulse Width Modulation" — a way to fake variable voltage
// by switching a pin on and off very quickly. Motors see it as
// a speed somewhere between stopped and full throttle.
//
// PWM_FREQ: how many on/off cycles per second. 20,000 Hz is
// above human hearing, so the motors won't whine annoyingly.
//
// PWM_RES: resolution in bits. 8-bit gives us 256 steps (0–255),
// which is plenty of granularity for a combat bot.
// ============================================================
#define PWM_FREQ 20000 // 20 kHz — above human hearing range
#define PWM_RES 8      // 8-bit = duty values 0 (off) to 255 (full speed)

// ================== SHARED STATE ==================
// These variables hold the bot's current condition.
// "unsigned long" is just a non-negative integer big enough to hold
// the value of millis() (milliseconds since boot) without overflowing.

// How long with no control frame before we kill everything automatically.
// The browser sends a frame every 100ms, so this is ~12 missed frames.
// Don't set this too tight: each frame is a full HTTP request/response over
// softAP, and round trips of 100-200ms are normal under load. A timeout
// shorter than a few round trips will trip on latency rather than on an
// actual lost link, which shows up as the bot disarming itself constantly.
const unsigned long FAILSAFE_TIMEOUT = 1200;

bool botActive = false;          // is the bot allowed to move right now?

// stopLatched is the important one. When true, the bot has been HARD STOPPED
// and will refuse every drive/throttle input until someone explicitly hits
// ACTIVATE again. Nothing in the code clears this except handleActivate() /
// the BLE "ACTIVATE" command. A dropped packet, a stale in-flight request, or
// a browser that didn't get the memo cannot un-latch it.
bool stopLatched = true;         // boot up latched — nothing moves until armed

unsigned long lastInputTime = 0; // timestamp of the last control frame

// WEAPON_IDLE is the commanded value that means "weapon is doing nothing".
// It differs by weapon type, which is exactly the kind of detail that caused
// the original "ESC won't stop" bug — so it lives in ONE place now.
#ifdef WEAPON_SERVO
const int WEAPON_IDLE = 90;      // servo: 90 = centered
#endif
#ifdef WEAPON_ESC
const int WEAPON_IDLE = 0;       // ESC: 0 = zero throttle (see note below)
#endif

// These track the current commanded position for each axis.
// 90 = center/neutral for the DRIVE values (0=full reverse, 180=full forward)
int leftPos = 90, rightPos = 90;
int weaponPos = WEAPON_IDLE;

// The last pulse width actually written to the weapon output, in microseconds.
// Reported back to the browser and the serial monitor so you can see what the
// hardware is really being told, rather than inferring it from the UI.
volatile int lastWeaponUs = 1000;

// Set only by the serial BENCH command below. While true, the normal
// "assert idle whenever disarmed" enforcement in loop() stands down so the
// test can drive the output directly. Always returns to false on its own.
bool benchActive = false;

#ifdef WEAPON_SERVO
Servo weaponServo; // object that manages the weapon servo for us
#endif

#ifdef WEAPON_ESC
Servo weaponESC; // ESP32Servo's Servo object also works fine for ESCs —
                 // an ESC just wants a repeating pulse between roughly
                 // 1000-2000 microseconds, exactly like a servo signal.

// ESCs are driven in microseconds, not degrees. IMPORTANT: a slim
// airplane-style ESC like this one is a UNIDIRECTIONAL throttle ESC —
// there's no "neutral center" the way a drive-motor ESC has. It only
// ever spins one way, so the pulse width just means "how fast":
//   1000us = zero throttle / off  <-- this IS the safe idle point
//   2000us = full throttle
// There is no meaningful "1500us = stopped" here — 1500us is roughly
// HALF throttle. Always treat ESC_MIN_US as both "off" and "arm here".
// Check your ESC's manual if it behaves oddly.
//
// The weapon command on the wire is 0-180 for both weapon types, but for
// the ESC that whole span is throttle: 0 = off, 180 = full. (The old code
// threw away the bottom half of the range and used 90 as "off", which meant
// the UI's resting value and the throttle scale disagreed with each other.)
const int ESC_MIN_US = 1000;
const int ESC_MAX_US = 2000;
#endif

// ==================== EDIT AS NEEDED =====================
// The bot broadcasts its own Wi-Fi network. Change the password
// if you don't want randos driving your robot.
// =========================================================
#ifdef CONNECTION_WIFI
// botSSID builds a unique name from part of the chip's MAC address
// which is built-in and unique to each device
// so two bots in the same arena won't have the same network name.
String botSSID = "AntBot-S3-" + String((uint32_t)(ESP.getEfuseMac() >> 32), HEX);
const char* apPassword = "battle123";

// WebServer on port 80 (standard HTTP). The browser talks to this.
WebServer server(80);
// ================= END OF EDITABLE SEGMENT ================

// Declarations are defining something in code prior to using them. These are
// "Forward declarations" — we define these functions later in the file,
// but we tell the compiler they exist NOW so any earlier code can call them.
void handleRoot();
void handleDrive();
void handlePing();
void handleActivate();
void handleStatus();
void handleStop();
void handleDeactivate();
#endif

// ============================================================
// Forward declarations (shared functions used by both modes).
// These must appear BEFORE the BLE callback classes below, because
// those classes call them.
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
// BLE-only globals
// Everything in this block is ignored if you're using Wi-Fi.
// ============================================================
#ifdef CONNECTION_BLE
// UUIDs are like serial numbers that identify BLE services and
// characteristics. Your Android app needs these exact strings.
// You can generate your own at https://www.uuidgenerator.net/
#define BLE_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define BLE_CMD_CHAR_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8" // WRITE (Android -> Bot)
#define BLE_RSP_CHAR_UUID "6e400003-b5a3-f393-e0a9-e50e24dcca9e" // NOTIFY (Bot -> Android)

BLEServer* pServer = nullptr;          // nullptr = "points at nothing yet"
BLECharacteristic* pCmdChar = nullptr; // receive commands from Android
BLECharacteristic* pRspChar = nullptr; // send responses back to Android
bool bleConnected = false;             // is a phone currently connected?

// ---- BLE Command Reference ----
// Android WRITES these strings to BLE_CMD_CHAR_UUID:
//   "ACTIVATE"        -> arm the bot at zero throttle (clears the stop latch)
//   "STOP" / "ABORT"  -> kill motors + weapon and LATCH OFF.
//                        Nothing moves again until ACTIVATE is sent.
//   "PING"            -> keepalive heartbeat (does NOT reset failsafe timer)
//   "D,LLL,RRR,WWW"   -> drive frame, e.g. "D,090,135,000"
//                        LLL/RRR are 0-180 (90 = stop).
//                        WWW is 0-180 weapon: for an ESC 0 = off, 180 = full.
//                        Send one at least every 750ms while armed, even if
//                        everything is zero — that IS the heartbeat.
//
// Bot NOTIFIES on BLE_RSP_CHAR_UUID with these strings:
//   "ACTIVATED"       -> confirmed armed
//   "STOPPED"         -> motors halted
//   "PONG"            -> reply to PING
//   "OK"              -> drive accepted
//   "NOT_ACTIVE"      -> rejected, send ACTIVATE first
//   "STATUS,true|false" -> current armed state

// Convenience wrapper — sends a string as a BLE notification to the phone.
// If nobody's connected, it just does nothing (safe to call anytime).
void bleSend(const String& msg) {
  if (!bleConnected || pRspChar == nullptr) return;
  pRspChar->setValue(msg.c_str());
  pRspChar->notify();
}

// BotServerCallbacks handles connection/disconnection events.
// "class" here is just a way to bundle related functions together.
// BLEServerCallbacks is the base class we're extending with our own behavior.
class BotServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s) override {
    bleConnected = true;
    Serial.println("[BLE] Client connected");
    setRGB(0, 0, 255); // blue = a phone connected, but not armed yet
  }
  void onDisconnect(BLEServer* s) override {
    bleConnected = false;
    Serial.println("[BLE] Client disconnected -- failsafe");
    engageStop("BLE client disconnected");
    // Restart advertising so a new phone can find and connect to the bot
    BLEDevice::startAdvertising();
  }
};

// CmdCallbacks fires whenever the Android app writes a command.
// onWrite() is called automatically by the BLE stack — we don't call it ourselves.
class CmdCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) override {
    // Read the raw bytes into a String, then trim whitespace/newlines
    String cmd = String(pChar->getValue().c_str());
    cmd.trim();
    Serial.print("[BLE] CMD: "); Serial.println(cmd);

    // ---- ACTIVATE ----
    // The only thing that clears the stop latch. Arms at zero throttle.
    if (cmd == "ACTIVATE") {
      killOutputs();
      stopLatched = false;
      botActive = true;
      lastInputTime = millis(); // start the failsafe clock
      setWeapon(WEAPON_IDLE);
      setRGB(0, 255, 0);        // green = ready to drive
      bleSend("ACTIVATED");
      return; // "return" exits the function early — no need to check the rest
    }

    // ---- PING ----
    // PING is just "are you still there?" — it does NOT reset the failsafe
    // timer. Only actual drive frames do that.
    if (cmd == "PING") {
      if (!botActive || stopLatched) { bleSend("NOT_ACTIVE"); return; }
      bleSend("PONG");
      return;
    }

    // ---- STOP / ABORT ----
    // Kills the hardware first, then latches out all further input.
    if (cmd == "STOP" || cmd == "ABORT") {
      engageStop("BLE stop/abort");
      bleSend("STOPPED");
      return;
    }

    // ---- DRIVE "D,LLL,RRR,WWW" ----
    if (cmd.startsWith("D,")) {
      if (!botActive || stopLatched) {
        killOutputs();
        bleSend("NOT_ACTIVE");
        return;
      }
      // Parse the three numbers out of the comma-separated string.
      // indexOf(',', 2) searches for a comma starting at character index 2,
      // skipping the "D," prefix we already know is there.
      int idx1 = cmd.indexOf(',', 2);
      int idx2 = (idx1 >= 0) ? cmd.indexOf(',', idx1 + 1) : -1;
      if (idx1 < 0 || idx2 < 0) {
        bleSend("ERR,BAD_FORMAT"); // malformed command — tell the app
        return;
      }
      // substring() extracts a chunk of the string. toInt() converts
      // the text to a number. constrain() clamps it to a valid range.
      int newLeft = constrain(cmd.substring(2, idx1).toInt(), 0, 180);
      int newRight = constrain(cmd.substring(idx1+1, idx2).toInt(), 0, 180);
      int newWeapon = constrain(cmd.substring(idx2+1).toInt(), 0, 180);

      // Only reset the failsafe timer if there's actual meaningful motion.
      // "5" is a small deadzone — tiny joystick drift shouldn't count.
      leftPos = newLeft;
      rightPos = newRight;
      weaponPos = newWeapon;

      setDrive(leftPos, rightPos);
      setWeapon(weaponPos);
      updateRGB();

      // Every frame refreshes the failsafe clock, including all-zero frames.
      // The app must send a frame at least every 750ms while armed — a
      // heartbeat that only beats when something is moving is not a heartbeat.
      lastInputTime = millis();

      bleSend("OK");
      return;
    }

    // ---- STATUS ----
    if (cmd == "STATUS") {
      bleSend(String("STATUS,") + (botActive ? "true" : "false"));
      return;
    }

    // If we fell through to here, we got a command we don't recognize
    bleSend("ERR,UNKNOWN_CMD");
  }
};
#endif // CONNECTION_BLE

// ============================================================
// (Shared forward declarations now live above the BLE section,
//  since the BLE callback classes call these functions.)
// ============================================================

// ============================================================
// MOTOR CONTROL (shared by both Wi-Fi and BLE)
//
// The DRV8833 motor driver takes two PWM signals per motor.
// The combination of those signals determines direction and speed:
//   IN1=0,     IN2=speed -> forward (speed 0–255)
//   IN1=speed, IN2=0     -> reverse
//   IN1=255,   IN2=255   -> brake (both high = short the motor leads together)
//   IN1=0,     IN2=0     -> coast (motor freewheels)
// ============================================================
void driveMotor(int in1, int in2, int speed) {
  speed = constrain(speed, -255, 255); // safety clamp — never exceed valid range
  if (speed == 0) {
    // Brake: both pins high. The motor resists movement (good for holding position)
    ledcWrite(in1, 255);
    ledcWrite(in2, 255);
  } else if (speed > 0) {
    // Forward: one pin at 0V, other at PWM duty
    ledcWrite(in1, 0);
    ledcWrite(in2, speed);
  } else {
    // Reverse: flip the pins. "-speed" makes the negative number positive.
    ledcWrite(in1, -speed);
    ledcWrite(in2, 0);
  }
}

// Stop both motors by braking them (not coasting)
void stopMotors() {
  ledcWrite(L_IN1, 255); ledcWrite(L_IN2, 255);
  ledcWrite(R_IN1, 255); ledcWrite(R_IN2, 255);
}

// Convert servo-style 0-180 joystick values into actual motor speeds.
// 90 is neutral, 180 is full forward, 0 is full reverse.
// map() scales one numeric range onto another linearly.
void setDrive(int leftCmd, int rightCmd) {
  int leftThrottle = leftCmd - 90; // shift so 0 = neutral, -90 to +90
  int rightThrottle = rightCmd - 90;

  int leftSpeed = map(leftThrottle, -90, 90, -255, 255); // scale to motor range
  int rightSpeed = map(rightThrottle, -90, 90, -255, 255);

  driveMotor(L_IN1, L_IN2, leftSpeed);
  driveMotor(R_IN1, R_IN2, rightSpeed);
}

// ============================================================
// WEAPON CONTROL (shared by both Wi-Fi and BLE)
//
// This is the single place that actually talks to the weapon
// hardware. Every other part of the sketch just calls setWeapon(pos)
// with a familiar 0-180 "servo-style" value and doesn't need to know
// or care whether that's really a servo or an ESC-driven motor.
//
// Whichever WEAPON_SERVO / WEAPON_ESC macro you defined near the top
// of the sketch decides which branch actually compiles.
// ============================================================
void setWeapon(int pos) {
  pos = constrain(pos, 0, 180);

  // HARD GATE. Nothing gets to command the weapon while the bot is stopped
  // or disarmed — not a stale HTTP request that was already in flight when
  // STOP was pressed, not a retransmit, not a bug somewhere else in the
  // sketch. If we're not armed, the only value that reaches the hardware
  // is idle. This single check is what makes STOP actually mean stop.
  if (!botActive || stopLatched) pos = WEAPON_IDLE;

#ifdef WEAPON_SERVO
  weaponServo.write(pos);
  lastWeaponUs = map(pos, 0, 180, 500, 2400);
#endif

#ifdef WEAPON_ESC
  // Straight linear throttle across the whole 0-180 command range.
  //   0   -> 1000us (off)
  //   180 -> 2000us (full)
  int us = map(pos, 0, 180, ESC_MIN_US, ESC_MAX_US);
  weaponESC.writeMicroseconds(us);
  lastWeaponUs = us;
#endif
}

// ============================================================
// SAFETY PRIMITIVES
//
// forceWeaponIdle() bypasses nothing and asks no questions — it writes the
// physical "off" signal to the weapon hardware directly. setWeapon() can be
// reasoned about; this one is the blunt instrument.
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

// killOutputs() puts every actuator into its safe state RIGHT NOW.
// It does not touch the arm/latch flags — it is purely "make the hardware stop".
void killOutputs() {
  forceWeaponIdle();  // weapon first: it's the dangerous one
  stopMotors();
  leftPos = rightPos = 90;
  weaponPos = WEAPON_IDLE;
}

// engageStop() is THE emergency stop. The ordering here is deliberate and is
// what you asked for:
//   1. kill the hardware
//   2. only then flip the flags that lock out further input
// Because the web server handlers and loop() all run on the same thread,
// nothing can sneak a command in between those two steps.
void engageStop(const char* reason) {
  killOutputs();      // (1) signal out to the motors and ESC

  botActive = false;  // (2) lock out all further input until re-activation
  stopLatched = true;

  forceWeaponIdle();  // and once more now that the gate is closed
  stopMotors();

  setRGB(255, 0, 0);  // solid red = latched stop, needs re-activation
  Serial.print("[STOP] Latched. Reason: ");
  Serial.println(reason);
}

// ============================================================
// RGB LED (shared)
//
// neopixelWrite() is a built-in ESP32 Arduino function that
// sends the NeoPixel color protocol on any GPIO pin.
// Arguments are (pin, red, green, blue) — each 0–255.
// ============================================================
void rgbOff() { neopixelWrite(RGB_LED, 0, 0, 0); }
void setRGB(uint8_t r, uint8_t g, uint8_t b) { neopixelWrite(RGB_LED, r, g, b); }

// Change LED color based on what the bot is currently doing —
// handy for knowing what's happening without opening the serial monitor.
void updateRGB() {
  int ls = leftPos - 90; // positive = forward, negative = reverse
  int rs = rightPos - 90;
  if (ls > 15 && rs > 15) setRGB(0, 255, 0);              // both forward -> green
  else if (ls < -15 && rs < -15) setRGB(255, 0, 0);       // both reverse -> red
  else if (abs(ls - rs) > 15) setRGB(255, 0, 255);        // turning -> magenta
  else rgbOff();                                          // neutral/idle -> off
}

// ============================================================
// SETUP
// setup() runs exactly once when the board powers on or resets.
// Use it for one-time initialization — hardware config, starting
// services, etc.
// ============================================================
void setup() {
  // Start the serial port so we can print debug messages.
  // Open "Serial Monitor" in Arduino IDE at 115200 baud to see them.
  Serial.begin(115200);
  delay(200); // short pause lets the serial port settle before we print anything

  // ---- Motor LEDC setup ----
  // We attach our motor pins to LEDC (the hardware PWM controller) BEFORE
  // the servo library starts up. This prevents channel conflicts.
  // (Arduino-ESP32 core 3.x API — see the big comment at the top for 2.x)
  ledcAttach(L_IN1, PWM_FREQ, PWM_RES);
  ledcAttach(L_IN2, PWM_FREQ, PWM_RES);
  ledcAttach(R_IN1, PWM_FREQ, PWM_RES);
  ledcAttach(R_IN2, PWM_FREQ, PWM_RES);
  stopMotors(); // make sure everything is stopped at boot

#ifdef WEAPON_SERVO
  // Attach the weapon servo AFTER we've claimed the motor pins.
  // 500–2400 microseconds is the pulse width range for most hobby servos.
  weaponServo.attach(WEAPON_PIN, 500, 2400);
  weaponServo.write(WEAPON_IDLE); // center / safe position on boot
#endif

#ifdef WEAPON_ESC
  // ---- ESC Arming Sequence ----
  // Almost every ESC (including the slim 15A 2S ESC) requires a specific
  // startup sequence before it will actually spin the motor. This is a
  // safety feature — it stops your weapon from spinning up unexpectedly
  // the instant power is applied.
  //
  // The typical sequence is:
  //   1. Attach the signal pin.
  //   2. Immediately send a ZERO-THROTTLE signal and HOLD it. (This ESC
  //      auto-detects its throttle range on power-up, so whatever pulse
  //      width it sees first becomes its reference for "off" — it must
  //      be ESC_MIN_US, never anything higher.)
  //   3. Wait a few seconds for the ESC to beep through its power-on
  //      tones and finish arming.
  //   4. Only THEN is it safe to send real throttle commands.
  //
  // KEEP THE WEAPON CLEAR OF FINGERS/OBJECTS while this runs — some
  // ESCs will twitch the motor briefly during arming.
  weaponESC.attach(WEAPON_PIN, ESC_MIN_US, ESC_MAX_US);
  weaponESC.writeMicroseconds(ESC_MIN_US);
  Serial.println("[ESC] Arming weapon ESC -- keep clear of the weapon!");
  // Hold zero throttle continuously for the whole arming window rather than
  // writing it once and sleeping. Same idea as the loop() enforcement:
  // never assume a single pulse landed.
  for (int i = 0; i < 60; i++) {
    weaponESC.writeMicroseconds(ESC_MIN_US);
    delay(50);
  }
  Serial.println("[ESC] Weapon ESC armed at zero throttle.");
  Serial.println("[ESC] Bot is STOP-LATCHED. Press ACTIVATE to enable controls.");
#endif

  // Configure the RGB LED pin and make sure it's off
  pinMode(RGB_LED, OUTPUT);
  rgbOff();

  Serial.println("\n=== AntBot-S3 | N20 + DRV8833 + Weapon ===");

  // ----------------------------------------------------------
  // Wi-Fi Access Point startup
  // The bot creates its own network — you join it.
  // ----------------------------------------------------------
#ifdef CONNECTION_WIFI
  Serial.println("[MODE] WiFi AP");
  WiFi.softAP(botSSID.c_str(), apPassword); // create the network
  Serial.print(" SSID : "); Serial.println(botSSID);
  Serial.print(" IP   : "); Serial.println(WiFi.softAPIP()); // usually 192.168.4.1
  Serial.println(" Pass : battle123");

  // Register URL routes — when the browser visits a path,
  // call the corresponding handler function.
  server.on("/", handleRoot);             // serves the joystick HTML page
  server.on("/drive", handleDrive);       // move the bot
  server.on("/ping", handlePing);         // keepalive check
  server.on("/activate", handleActivate); // arm the bot
  server.on("/status", handleStatus);     // JSON status query
  server.on("/stop", handleStop);         // emergency stop (latches off)
  server.on("/deactivate", handleDeactivate); // emergency stop + disarm (latches off)
  server.begin();

  setRGB(0, 128, 255); // cyan = Wi-Fi is up, waiting for a browser to connect
#endif

  // ----------------------------------------------------------
  // BLE startup (only if CONNECTION_BLE is defined)
  // ----------------------------------------------------------
#ifdef CONNECTION_BLE
  Serial.println("[MODE] BLE");
  String bleName = "AntBot-" + String((uint32_t)(ESP.getEfuseMac() >> 32), HEX);
  BLEDevice::init(bleName.c_str()); // set the BLE device name
  Serial.print(" BLE name : "); Serial.println(bleName);

  // Create the BLE server and hook up our connection callbacks
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new BotServerCallbacks());

  // A BLE "service" groups related characteristics together
  BLEService* pService = pServer->createService(BLE_SERVICE_UUID);

  // Command characteristic — Android writes drive commands here.
  // WRITE_NR = Write Without Response (faster, no ACK from bot)
  pCmdChar = pService->createCharacteristic(
                 BLE_CMD_CHAR_UUID,
                 BLECharacteristic::PROPERTY_WRITE |
                 BLECharacteristic::PROPERTY_WRITE_NR);
  pCmdChar->setCallbacks(new CmdCallbacks());

  // Response characteristic — bot notifies Android with status/feedback.
  // BLE2902 is a required descriptor that tells the client
  // "yes, you can subscribe to notifications on this characteristic."
  pRspChar = pService->createCharacteristic(
                 BLE_RSP_CHAR_UUID,
                 BLECharacteristic::PROPERTY_NOTIFY);
  pRspChar->addDescriptor(new BLE2902());

  pService->start();

  // Start advertising — this is how nearby devices discover the bot
  BLEAdvertising* pAdv = BLEDevice::getAdvertising();
  pAdv->addServiceUUID(BLE_SERVICE_UUID);
  pAdv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println(" Advertising -- waiting for Android client...");

  setRGB(128, 0, 255); // purple = BLE advertising, no phone connected yet
#endif
}

// ============================================================
// LOOP
// loop() runs over and over, forever, as fast as the CPU allows
// (thousands of times per second). Think of it as the heartbeat
// of the program — check for incoming data, run safety checks,
// send periodic updates.
// ============================================================
void loop() {
  // Let the web server process any pending HTTP requests from the browser.
  // Without this call, the server would never respond to anything.
#ifdef CONNECTION_WIFI
  server.handleClient();
#endif

  // ---- Failsafe check ----
  // If the bot is armed but we haven't received a control frame in a while,
  // cut power immediately. This handles phone battery death, dropped
  // Wi-Fi, browser tab crashes, etc. The browser now sends a frame every
  // 50ms whenever it is armed, so silence for 750ms genuinely means the
  // link is gone — not just "the driver isn't touching anything".
  if (botActive && (millis() - lastInputTime > FAILSAFE_TIMEOUT)) {
    Serial.println("!!! FAILSAFE TRIGGERED !!!");
    engageStop("failsafe timeout");
#ifdef CONNECTION_BLE
    bleSend("FAILSAFE"); // tell the app what happened
#endif
  }

  // ---- Continuous safe-state enforcement ----
  // While we are not armed, we don't just assume the last "off" write stuck.
  // We keep re-asserting zero throttle at 20Hz for as long as the bot is
  // disarmed. If the ESC ever misses or misreads a pulse, the next one is
  // 50ms away. A one-shot write is a hope; this is a guarantee.
  if ((!botActive || stopLatched) && !benchActive) {
    static unsigned long lastAssert = 0;
    if (millis() - lastAssert >= 50) {
      lastAssert = millis();
      forceWeaponIdle();
      stopMotors();
    }
  }

  // ---- Serial bench test ----
  // Type BENCH into the Serial Monitor (115200, newline ending) to ramp the
  // weapon output without any browser involved. This tells you in one step
  // whether a "weapon won't spin" problem is in the ESC/wiring/battery or in
  // the web UI. It ramps to 25% only, and any further serial input aborts it.
  //
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
        if (Serial.available()) break; // any keypress aborts
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


  // "static" means this variable persists between calls to loop() —
  // it's not reset to 0 every time like a normal local variable would be.
  static unsigned long lastPrint = 0;

  // Print a quick status line every 500ms — useful for debugging
  // but not so frequent that it floods the serial monitor.
  if (millis() - lastPrint > 500) {
    Serial.printf("Active:%s Latched:%s L:%d R:%d W:%d (%dus)\n",
                  botActive ? "YES" : "NO",
                  stopLatched ? "YES" : "no",
                  leftPos, rightPos, weaponPos, lastWeaponUs);
    lastPrint = millis();
  }
}

// ============================================================
// Wi-Fi HTTP HANDLERS
// Each of these functions is called by the web server when the
// browser visits the matching URL. server.send() sends back
// the HTTP response — think of it like replying to the browser.
// ============================================================
#ifdef CONNECTION_WIFI

// /ping — the browser asks "are you still there?" every 500ms.
// We respond but do NOT reset the failsafe timer here.
// Only actual drive inputs with real motion reset that clock.
void handlePing() {
  if (!botActive || stopLatched) { server.send(200, "text/plain", "NOT_ACTIVE"); return; }
  server.send(200, "text/plain", "PONG");
}

// /stop — full emergency stop. Both this and /deactivate latch the bot off.
// There is deliberately no longer a "soft stop that stays armed": you asked
// for STOP to mean STOP, so every stop path goes through engageStop().
void handleStop() {
  engageStop("/stop requested");
  server.send(200, "text/plain", "STOPPED");
}

// /deactivate — the "STOP ALL" / abort button. Identical behaviour to /stop.
// Any /drive requests that were already in flight when this landed are
// rejected on arrival, and setWeapon() would clamp them to idle anyway.
void handleDeactivate() {
  engageStop("/deactivate emergency stop");
  server.send(200, "text/plain", "DEACTIVATED");
}

// /drive?left=NNN&right=NNN&weapon=NNN
// The browser sends this URL with query parameters every ~40ms while
// the joystick is being held. We parse the values and move the bot.
void handleDrive() {
  // Not armed? Reject, and re-assert the safe state while we're here.
  // We never trust that a previous stop "took" — every rejected frame is
  // another opportunity to push zero throttle at the ESC.
  if (!botActive || stopLatched) {
    killOutputs();
    server.send(200, "text/plain", "NOT_ACTIVE");
    return;
  }

  // server.hasArg() checks if a query parameter exists in the URL.
  // If it's missing, we keep the current value instead of defaulting.
  int newLeft = server.hasArg("left") ? constrain(server.arg("left").toInt(), 0, 180) : leftPos;
  int newRight = server.hasArg("right") ? constrain(server.arg("right").toInt(), 0, 180) : rightPos;
  int newWeapon = server.hasArg("weapon") ? constrain(server.arg("weapon").toInt(), 0, 180) : weaponPos;

  leftPos = newLeft;
  rightPos = newRight;
  weaponPos = newWeapon;

  setDrive(leftPos, rightPos);
  setWeapon(weaponPos);
  updateRGB();

  // THE KEY FIX: every frame we receive refreshes the failsafe clock,
  // including frames that command zero everything. The old code only
  // refreshed the timer when there was "motion", and the browser only
  // bothered sending a frame when there was "motion" — so a command that
  // said "weapon off, motors off" was never sent and never arrived, and
  // the ESC just kept spinning at whatever it was last told.
  // A heartbeat that only beats when something is moving is not a heartbeat.
  lastInputTime = millis();

  server.send(200, "text/plain", String("OK,") + String(lastWeaponUs));
}

// /activate — the ONLY thing in the entire sketch that clears stopLatched.
// We deliberately arm into a known-idle state: motors braked, throttle at
// zero. Whatever the slider happened to be showing is irrelevant — you can
// never re-activate straight into a spinning weapon.
void handleActivate() {
  killOutputs();          // start from zero, always
  stopLatched = false;
  botActive = true;
  lastInputTime = millis();
  setWeapon(WEAPON_IDLE); // now that the gate is open, explicitly write idle
  Serial.println("[WiFi] Bot ACTIVATED (armed at zero throttle)");
  setRGB(0, 255, 0); // green = armed and ready
  server.send(200, "text/plain", "ACTIVATED");
}

// /status — returns a JSON object the browser can parse.
void handleStatus() {
  String json = String("{\"active\":") + (botActive ? "true" : "false") +
                ",\"latched\":" + (stopLatched ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

// ---- Web UI ----
// This is the entire joystick interface — HTML, CSS, and JavaScript —
// stored as one big string and sent to the browser on request.
// It's not pretty code, but it works without needing any external files.
//
// NOTE: I am not a web developer. Someone could probably take this and make
// it far, far more elegant and usable than I.
// Do what you want with it! — Adam
void handleRoot() {
  // The weapon slider's range depends on the weapon type. For an ESC the
  // whole 0-180 span is throttle and it rests at 0. For a servo it's a
  // position and it rests centered at 90.
#ifdef WEAPON_ESC
  const char* wIdle = "0";
  const char* wLabel = "WEAPON THROTTLE";
#else
  const char* wIdle = "90";
  const char* wLabel = "WEAPON";
#endif

  String html =
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "  <title>AntBot Joystick</title>\n"
    // viewport meta tag prevents mobile browsers from zooming out
    "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no\">\n"
    "  <style>\n"
    "    body { margin:0; padding:0; background:#111; color:#0f0; font-family:Arial,sans-serif; height:100vh; overflow:hidden; touch-action:none; }\n"
    "    h1 { text-align:center; margin:10px 0 5px; font-size:1.6em; }\n"
    "    .activate-screen, .control-screen { position:absolute; top:0; left:0; width:100%; height:100%; display:flex; flex-direction:column; justify-content:center; align-items:center; }\n"
    "    .activate-btn { padding:30px 60px; font-size:2em; background:#0f0; color:#000; border:none; border-radius:20px; font-weight:bold; }\n"
    // The STOP button is deliberately large and always on top of everything.
    "    .stop-btn { position:fixed; top:15px; right:15px; padding:22px 40px; background:#f00; color:white; border:none; border-radius:12px; font-size:1.5em; font-weight:bold; z-index:9999; }\n"
    "    .container { display:flex; height:calc(100vh - 80px); width:100%; }\n"
    "    .left { flex:1; display:flex; justify-content:flex-start; align-items:center; padding-left:50px; }\n"
    "    .right { flex:1; display:flex; flex-direction:column; justify-content:center; align-items:center; padding-right:60px; background:#1a1a1a; }\n"
    "    .joystick-base { width:230px; height:230px; background:rgba(255,255,255,0.08); border:7px solid #0f0; border-radius:50%; position:relative; }\n"
    "    .joystick-knob { width:75px; height:75px; background:#0f0; border-radius:50%; position:absolute; top:50%; left:50%; transform:translate(-50%,-50%); box-shadow:0 0 25px #0f0; }\n"
    "    .weapon-label { font-size:1.5em; margin-bottom:20px; color:#ff0; }\n"
    // Vertical sliders are a mess. The old `orient="vertical"` attribute is
    // Firefox-only, and Chrome dropped `appearance: slider-vertical` in favour
    // of `writing-mode`. Without one of these actually taking effect the
    // control renders as a 70px-WIDE horizontal slider — technically draggable,
    // practically useless, which is why the throttle wouldn't move. All three
    // declarations are here so at least one lands on any given browser.
    "    .weapon-slider { -webkit-appearance:slider-vertical; appearance:slider-vertical; writing-mode:vertical-lr; direction:rtl; width:60px; height:260px; accent-color:#ff0; }\n"
    "    .preset-row { display:flex; gap:8px; margin-top:16px; }\n"
    "    .preset-btn { padding:12px 14px; background:#333; color:#ff0; border:2px solid #ff0; border-radius:8px; font-size:1em; font-weight:bold; }\n"
    "    #usValue { font-size:0.9em; color:#888; }\n"
    "    .value { font-size:2.1em; margin-top:15px; }\n"
    "    .zero-btn { margin-top:18px; padding:14px 26px; background:#ff0; color:#000; border:none; border-radius:10px; font-size:1.1em; font-weight:bold; }\n"
    "    .hidden { display:none !important; }\n"
    // Banner shown if a STOP request has not yet been confirmed by the bot
    "    #stopBanner { position:fixed; top:0; left:0; width:100%; padding:14px; background:#f00; color:#fff; font-size:1.3em; font-weight:bold; text-align:center; z-index:10000; }\n"
    "    #latchNote { color:#f55; font-size:1.1em; margin-top:18px; }\n"
    // small dot in the corner that flashes green on every successful frame
    "    #pingDot { position:fixed; bottom:12px; right:18px; width:10px; height:10px; border-radius:50%; background:#555; }\n"
    "  </style>\n"
    "</head>\n"
    "<body>\n"
    "  <h1>" + botSSID + "</h1>\n"
    "  <div id=\"pingDot\"></div>\n"
    "  <div id=\"stopBanner\" class=\"hidden\">STOP SENT - CONFIRMING...</div>\n"
    "\n"
    // Two screens that swap visibility: activation gate and the actual controls
    "  <div id=\"activateScreen\" class=\"activate-screen\">\n"
    "    <button class=\"activate-btn\" onclick=\"activateBot()\">ACTIVATE BOT</button>\n"
    "    <p style=\"margin-top:30px; font-size:1.3em;\">Press to enable controls</p>\n"
    "    <p id=\"latchNote\">Controls are locked. The weapon will arm at zero throttle.</p>\n"
    "  </div>\n"
    "\n"
    "  <div id=\"controlScreen\" class=\"control-screen hidden\">\n"
    "    <button class=\"stop-btn\" onclick=\"stopAll()\">STOP ALL</button>\n"
    "    <div class=\"container\">\n"
    "      <div class=\"left\">\n"
    "        <div class=\"joystick-base\" id=\"joyBase\"><div class=\"joystick-knob\" id=\"joyKnob\"></div></div>\n"
    "      </div>\n"
    "      <div class=\"right\">\n"
    "        <div class=\"weapon-label\">" + String(wLabel) + "</div>\n"
    // orient=vertical makes this a vertical slider — not all browsers honor it
    "        <input type=\"range\" id=\"weaponSlider\" class=\"weapon-slider\" min=\"0\" max=\"180\" value=\"" + String(wIdle) + "\" orient=\"vertical\" oninput=\"updateWeapon(this.value)\">\n"
    "        <div class=\"value\"><span id=\"weaponValue\">" + String(wIdle) + "</span></div>\n"
    "        <div id=\"usValue\">-- us</div>\n"
    // Tap targets, so commanding throttle never depends on dragging a slider
    // that a given browser may or may not have rendered usefully.
    "        <div class=\"preset-row\">\n"
    "          <button class=\"preset-btn\" onclick=\"setWeaponPct(25)\">25%</button>\n"
    "          <button class=\"preset-btn\" onclick=\"setWeaponPct(50)\">50%</button>\n"
    "          <button class=\"preset-btn\" onclick=\"setWeaponPct(75)\">75%</button>\n"
    "          <button class=\"preset-btn\" onclick=\"setWeaponPct(100)\">100%</button>\n"
    "        </div>\n"
    "        <button class=\"zero-btn\" onclick=\"zeroWeapon()\">WEAPON OFF</button>\n"
    "      </div>\n"
    "    </div>\n"
    "  </div>\n"
    "\n"
    "  <script>\n"
    // ---- JavaScript that runs in the browser ----
    "    const W_IDLE = " + String(wIdle) + ";\n" // weapon value meaning "off"
    "    let botActive = false;\n"      // mirrors the bot's arm state
    "    let stopPending = false;\n"    // a STOP is sent but not yet confirmed
    "    let currentX = 0, currentY = 0, weaponPos = W_IDLE;\n"
    "    let isDragging = false;\n"
    "    const base = document.getElementById('joyBase');\n"
    "    const knob = document.getElementById('joyKnob');\n"
    "    const pingDot = document.getElementById('pingDot');\n"
    "    const banner = document.getElementById('stopBanner');\n"
    "\n"
    // Show the activation screen and reset everything to a safe state
    "    function showActivateScreen() {\n"
    "      botActive = false;\n"
    "      isDragging = false;\n"
    "      currentX = currentY = 0;\n"
    "      weaponPos = W_IDLE;\n"
    "      document.getElementById('weaponSlider').value = W_IDLE;\n"
    "      document.getElementById('weaponValue').innerText = W_IDLE;\n"
    "      knob.style.transform = 'translate(-37.5px, -37.5px)';\n"
    "      document.getElementById('controlScreen').classList.add('hidden');\n"
    "      document.getElementById('activateScreen').classList.remove('hidden');\n"
    "    }\n"
    "\n"
    // Hit /activate, then swap screens if it succeeded. We reset the slider to
    // zero BEFORE arming so the first frame we send can only ever be idle.
    "    function activateBot() {\n"
    "      if (stopPending) return;\n" // never re-arm while a stop is unconfirmed
    "      weaponPos = W_IDLE;\n"
    "      document.getElementById('weaponSlider').value = W_IDLE;\n"
    "      document.getElementById('weaponValue').innerText = W_IDLE;\n"
    "      currentX = currentY = 0;\n"
    "      fetch('/activate').then(r => r.text()).then(t => {\n"
    "        if (t !== 'ACTIVATED') return;\n"
    "        botActive = true;\n"
    "        document.getElementById('activateScreen').classList.add('hidden');\n"
    "        document.getElementById('controlScreen').classList.remove('hidden');\n"
    "      }).catch(() => {});\n"
    "    }\n"
    "\n"
    // ---- THE CONTROL FRAME ----
    // Fixed 100ms timer, sends the CURRENT state unconditionally — including
    // "everything zero". The old code only sent a frame when something was
    // moving, which meant the command that said "weapon off" was the one
    // command that never got sent.
    //
    // inFlight is critical. The ESP32's WebServer handles exactly ONE request
    // at a time, and each fetch is a fresh TCP connection. Fire frames faster
    // than the bot can answer them and they queue up in the browser, latency
    // grows without bound, the bot stops hearing fresh frames, and the failsafe
    // trips — which looks exactly like "the UI bounces back to ACTIVATE".
    // So: never more than one outstanding request. If the previous frame
    // hasn't come back yet, skip this one. The bot's state is unchanged by a
    // skipped frame; only the newest values matter anyway.
    "    let inFlight = false;\n"
    "    function sendFrame() {\n"
    "      if (!botActive || inFlight) return;\n"
    "      const forward = currentY * 90;\n"
    "      const steer = currentX * 65;\n"
    "      let left = Math.max(0, Math.min(180, Math.round(90 + forward + steer)));\n"
    "      let right = Math.max(0, Math.min(180, Math.round(90 + forward - steer)));\n"
    "      inFlight = true;\n"
    // AbortController stops a stalled connection from blocking frames forever
    "      const ac = new AbortController();\n"
    "      const killer = setTimeout(() => ac.abort(), 600);\n"
    "      fetch(`/drive?left=${left}&right=${right}&weapon=${weaponPos}`, {signal: ac.signal})\n"
    "        .then(r => r.text()).then(t => {\n"
    "          clearTimeout(killer); inFlight = false;\n"
    "          if (t.startsWith('NOT_ACTIVE')) { showActivateScreen(); return; }\n"
    "          const parts = t.split(',');\n"
    "          if (parts.length > 1) document.getElementById('usValue').innerText = parts[1] + ' us';\n"
    "          pingDot.style.background = '#0f0';\n"
    "          setTimeout(() => pingDot.style.background = '#555', 60);\n"
    "        })\n"
    "        .catch(() => { clearTimeout(killer); inFlight = false; pingDot.style.background = '#f00'; });\n"
    "    }\n"
    "\n"
    // Calculate where the knob should be based on touch/mouse position and
    // clamp it inside the circular base. No fetch here — the 50ms frame
    // timer is the only thing that talks to the bot, so there is exactly one
    // code path carrying commands and nothing can be dropped by a rate limit.
    "    function moveKnob(clientX, clientY) {\n"
    "      if (!botActive) return;\n"
    "      isDragging = true;\n"
    "      const rect = base.getBoundingClientRect();\n"
    "      let x = clientX - rect.left - 115;\n"
    "      let y = clientY - rect.top - 115;\n"
    "      const dist = Math.sqrt(x*x + y*y);\n"
    "      if (dist > 115) { x = (x/dist)*115; y = (y/dist)*115; }\n"
    "      knob.style.transform = `translate(${x-37.5}px, ${y-37.5}px)`;\n"
    "      currentX = x / 115; currentY = -y / 115;\n"
    "    }\n"
    // Finger/mouse lifted — snap the knob to center. The next frame (<=50ms
    // away) carries the neutral value to the bot.
    "    function resetKnob() {\n"
    "      isDragging = false;\n"
    "      knob.style.transform = 'translate(-37.5px, -37.5px)';\n"
    "      currentX = currentY = 0;\n"
    "    }\n"
    // Called whenever the weapon slider moves
    "    function updateWeapon(val) {\n"
    "      if (!botActive) { document.getElementById('weaponSlider').value = W_IDLE; return; }\n"
    "      weaponPos = parseInt(val);\n"
    "      document.getElementById('weaponValue').innerText = weaponPos;\n"
    "    }\n"
    // One-tap throttle cut that leaves the bot armed and drivable
    "    function zeroWeapon() {\n"
    "      weaponPos = W_IDLE;\n"
    "      document.getElementById('weaponSlider').value = W_IDLE;\n"
    "      document.getElementById('weaponValue').innerText = W_IDLE;\n"
    "    }\n"
    // Preset throttle buttons. pct is 0-100 of the weapon's usable range.
    "    function setWeaponPct(pct) {\n"
    "      if (!botActive) return;\n"
    "      weaponPos = Math.round(W_IDLE + (180 - W_IDLE) * pct / 100);\n"
    "      document.getElementById('weaponSlider').value = weaponPos;\n"
    "      document.getElementById('weaponValue').innerText = weaponPos;\n"
    "    }\n"
    "\n"
    // ---- EMERGENCY STOP ----
    // Local state is killed instantly so this browser stops generating
    // frames, then we hammer /deactivate until the bot actually confirms it.
    // A single un-acknowledged request over Wi-Fi is not an emergency stop;
    // if the one packet is lost, the weapon keeps spinning while the UI
    // cheerfully shows the activation screen. So we retry until we hear back.
    "    function stopAll() {\n"
    "      botActive = false;\n"
    "      isDragging = false;\n"
    "      stopPending = true;\n"
    "      banner.classList.remove('hidden');\n"
    "      banner.innerText = 'STOP SENT - CONFIRMING...';\n"
    "      showActivateScreen();\n"
    "      confirmStop(0);\n"
    "    }\n"
    "    function confirmStop(attempt) {\n"
    "      if (!stopPending) return;\n"
    "      fetch('/deactivate').then(r => r.text()).then(t => {\n"
    "        if (t === 'DEACTIVATED') {\n"
    "          stopPending = false;\n"
    "          banner.classList.add('hidden');\n"
    "        } else { setTimeout(() => confirmStop(attempt+1), 200); }\n"
    "      }).catch(() => {\n"
    "        banner.innerText = 'STOP NOT CONFIRMED - CUT POWER AT THE BOT';\n"
    "        setTimeout(() => confirmStop(attempt+1), 200);\n"
    "      });\n"
    "    }\n"
    "\n"
    // The single command loop. 20Hz, always running, always the same path.
    "    setInterval(sendFrame, 100);\n"
    "\n"
    // If the page is backgrounded, hidden, or closed, treat it as a stop.
    // Mobile browsers throttle timers in background tabs, which would starve
    // the frame loop and trip the failsafe anyway — but we'd rather stop
    // deliberately than rely on a timeout.
    "    document.addEventListener('visibilitychange', () => { if (document.hidden && botActive) stopAll(); });\n"
    "    window.addEventListener('pagehide', () => { navigator.sendBeacon ? navigator.sendBeacon('/deactivate') : fetch('/deactivate'); });\n"
    // NOTE: there is deliberately NO window 'blur' handler here. Blur fires
    // constantly for harmless reasons — tapping the address bar, a notification
    // sliding down, the on-screen keyboard, even hiding the focused ACTIVATE
    // button when we swap screens. Wiring an e-stop to it makes the bot
    // un-armable. visibilitychange and pagehide are the meaningful signals.
    "\n"
    // Spacebar / Escape as a keyboard panic button when driving from a laptop
    "    document.addEventListener('keydown', e => {\n"
    "      if (e.code === 'Space' || e.code === 'Escape') { e.preventDefault(); stopAll(); }\n"
    "    });\n"
    "\n"
    // Touch events (mobile) — preventDefault stops the page from scrolling
    "    base.addEventListener('touchstart', e => { e.preventDefault(); moveKnob(e.touches[0].clientX, e.touches[0].clientY); });\n"
    "    base.addEventListener('touchmove', e => { e.preventDefault(); moveKnob(e.touches[0].clientX, e.touches[0].clientY); });\n"
    "    base.addEventListener('touchend', resetKnob);\n"
    "    base.addEventListener('touchcancel', resetKnob);\n"
    // Mouse events (desktop / testing in a browser on your computer)
    "    base.addEventListener('mousedown', e => {\n"
    "      const move = ev => moveKnob(ev.clientX, ev.clientY);\n"
    "      document.addEventListener('mousemove', move);\n"
    // {once:true} auto-removes this listener after it fires once
    "      document.addEventListener('mouseup', () => { document.removeEventListener('mousemove', move); resetKnob(); }, {once:true});\n"
    "    });\n"
    "  </script>\n"
    "</body>\n"
    "</html>";

  server.send(200, "text/html", html);
}

#endif // CONNECTION_WIFI

