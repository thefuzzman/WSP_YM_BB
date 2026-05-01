//    This is a comment.  
//    Comments are ignored when the code
//    is compiled and uploaded to the device
//    Any segment - such as the next one - that can be modified
//    to suit your needs will be prefaced with:

// ==================== EDIT AS NEEDED =====================
// Some descriptive text here
// =========================================================

//    And have a line under the editable segment that looks like
//    this

// ================= END OF EDITABLE SEGMENT ================



//  START OF ACTUAL CODE
//    This is a baseline sketch only - it will be usable as-is, 
//    however you'll likely need to modify to meet what you want
//    out of the code
//    Have fun! - Adam

// ============================================================
//  SELECT YOUR CONNECTION MODE — uncomment exactly ONE:
//
//  "Uncommenting" means removing the // at the start of a line.
//  The // turns a line into a comment so the compiler skips it.
//  Only ONE of these two should be active at a time.
//
//  CONNECTION_WIFI  = the bot creates its own Wi-Fi network.
//                     You connect your phone/browser to it and
//                     go to 192.168.4.1 to drive.
//
//  CONNECTION_BLE   = the bot advertises over Bluetooth Low Energy.
//                     You'd need a custom Android app to connect.
// ============================================================
#define CONNECTION_WIFI
// #define CONNECTION_BLE
// ============================================================

// These are "libraries" — pre-written code that saves us from
// having to reinvent the wheel.  #include pulls them into our sketch.
#include <ESP32Servo.h>    // lets us control servo motors easily
#include <ESP32Encoder.h>  // lets us read encoder pulses from motors (optional)

// The #ifdef / #endif blocks below are "conditional compilation."
// They say: "only include this code if we defined that symbol above."
// So if CONNECTION_WIFI is defined, the Wi-Fi libraries get included.
// If it isn't, they're completely ignored — zero overhead.
#ifdef CONNECTION_WIFI
  #include <WiFi.h>        // core Wi-Fi support for ESP32
  #include <WebServer.h>   // lets the ESP32 act as a tiny web server
#endif

#ifdef CONNECTION_BLE
  #include <BLEDevice.h>   // core BLE support
  #include <BLEServer.h>   // lets the ESP32 be a BLE peripheral (the "server" side)
  #include <BLEUtils.h>    // helper utilities for BLE
  #include <BLE2902.h>     // standard BLE descriptor needed for notifications to work
#endif


// ==================== EDIT AS NEEDED =====================
// Pin numbers tell the ESP32 which physical wire to talk to.
// These match MY board layout — yours might differ.
// If a motor spins backward, swap its IN1 and IN2 pin numbers.
// =========================================================
// NOTE: These are "variables", which is a word or combination of
//       letters and numbers thar hold a value that the code uses
//       somewhere else.  Think of them like storage boxes that 
//       you can put things in and get things out of

// ================== PINS ==================
const int WEAPON_SERVO_PIN = 6;   // signal wire of the weapon servo plugs in here

// Left motor: IN1 and IN2 are the two control wires on the DRV8833 driver.
// Driving them high/low/PWM is how we control speed and direction.
const int L_IN1 = 7;
const int L_IN2 = 8;

// Right motor — same idea, different pins
const int R_IN1 = 10;
const int R_IN2 = 9;

// Encoders are optional sensors built into some N20 motors that
// count how far each wheel has turned.  Each encoder has two signal
// wires (A and B) that we read to get direction + distance.
// You may not need the below if your N20 motors lack encoders, but
// leaving this in won't affect the outcome if they are not present
ESP32Encoder encoderLeft;
ESP32Encoder encoderRight;
const int ENC_L_A = 11;
const int ENC_L_B = 12;
const int ENC_R_A = 13;
const int ENC_R_B = 16;
// ================= END OF EDITABLE SEGMENT ================

// -------------------------------------------------------------------
// Pin 48 on the ESP32-S3 DevKit has a built-in RGB "NeoPixel" LED.
// We use it as a status light — different colors = different states.
 #define RGB_LED 21

// ============================================================
//  MOTOR PWM SETTINGS
//
//  PWM = "Pulse Width Modulation" — a way to fake variable voltage
//  by switching a pin on and off very quickly.  Motors see it as
//  a speed somewhere between stopped and full throttle.
//
//  PWM_FREQ: how many on/off cycles per second.  20,000 Hz is
//  above human hearing, so the motors won't whine annoyingly.
//
//  PWM_RES: resolution in bits.  8-bit gives us 256 steps (0–255),
//  which is plenty of granularity for a combat bot.
// ============================================================
#define PWM_FREQ  20000   // 20 kHz — above human hearing range
#define PWM_RES   8       // 8-bit = duty values 0 (off) to 255 (full speed)

// ================== SHARED STATE ==================
// These variables hold the bot's current condition.
// "unsigned long" is just a non-negative integer big enough to hold
// the value of millis() (milliseconds since boot) without overflowing.

// How long with no input before we kill the motors automatically.
// 750ms is plenty of time for normal Wi-Fi latency but stops the
// bot quickly if your phone drops off the network or the browser closes.
const unsigned long FAILSAFE_TIMEOUT = 750;

bool     botActive    = false;  // is the bot allowed to move right now?
unsigned long lastInputTime = 0; // timestamp of the last real control input

// These track the current commanded position for each axis.
// 90 = center/neutral for servo-style values (0=full reverse, 180=full forward)
int leftPos = 90, rightPos = 90, weaponPos = 90;

Servo weaponServo;  // object that manages the weapon servo for us


// ==================== EDIT AS NEEDED =====================
// The bot broadcasts its own Wi-Fi network.  Change the password
// if you don't want randos driving your robot.
// =========================================================
#ifdef CONNECTION_WIFI
  // botSSID builds a unique name from part of the chip's MAC address
  // which is built-in and unique to each device
  // so two bots in the same arena won't have the same network name.
  String      botSSID   = "AntBot-S3-" + String((uint32_t)(ESP.getEfuseMac() >> 32), HEX);
  const char* apPassword = "battle123";

  // WebServer on port 80 (standard HTTP).  The browser talks to this.
  WebServer   server(80);
// ================= END OF EDITABLE SEGMENT ================

  // Declarations are defining something in code prior to using them.  These are
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
//     BLE-only globals
//     Everything in this block is ignored if you're using Wi-Fi.
// ============================================================
#ifdef CONNECTION_BLE
  // UUIDs are like serial numbers that identify BLE services and
  // characteristics.  Your Android app needs these exact strings.
  // You can generate your own at https://www.uuidgenerator.net/
  #define BLE_SERVICE_UUID      "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
  #define BLE_CMD_CHAR_UUID     "beb5483e-36e1-4688-b7f5-ea07361b26a8"  // WRITE (Android -> Bot)
  #define BLE_RSP_CHAR_UUID     "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  // NOTIFY (Bot -> Android)

  BLEServer*         pServer    = nullptr;   // nullptr = "points at nothing yet"
  BLECharacteristic* pCmdChar   = nullptr;   // receive commands from Android
  BLECharacteristic* pRspChar   = nullptr;   // send responses back to Android
  bool               bleConnected = false;   // is a phone currently connected?

  // ---- BLE Command Reference ----
  // Android WRITES these strings to BLE_CMD_CHAR_UUID:
  //   "ACTIVATE"        -> arm the bot (enables drive commands)
  //   "STOP"            -> stop motors + weapon, stay armed
  //   "PING"            -> keepalive heartbeat (does NOT reset failsafe timer)
  //   "D,LLL,RRR,WWW"  -> drive command, e.g. "D,090,135,090"
  //                        LLL/RRR/WWW are 0-180 servo-style integers
  //
  // Bot NOTIFIES on BLE_RSP_CHAR_UUID with these strings:
  //   "ACTIVATED"         -> confirmed armed
  //   "STOPPED"           -> motors halted
  //   "PONG"              -> reply to PING
  //   "OK"                -> drive accepted
  //   "NOT_ACTIVE"        -> rejected, send ACTIVATE first
  //   "STATUS,true|false" -> current armed state
  //   "ENC,L,R"           -> encoder counts, sent every 500 ms

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
      setRGB(0, 0, 255);   // blue = a phone connected, but not armed yet
    }
    void onDisconnect(BLEServer* s) override {
      bleConnected  = false;
      botActive     = false;
      Serial.println("[BLE] Client disconnected -- failsafe");
      stopMotors();
      weaponServo.write(90);   // center/retract the weapon
      rgbOff();
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
      if (cmd == "ACTIVATE") {
        botActive     = true;
        lastInputTime = millis();   // start the failsafe clock
        setRGB(0, 255, 0);         // green = ready to drive
        bleSend("ACTIVATED");
        return;   // "return" exits the function early — no need to check the rest
      }

      // ---- PING ----
      // PING is just "are you still there?" — it does NOT reset the failsafe
      // timer.  Only actual drive commands with real motion do that.
      if (cmd == "PING") {
        if (!botActive) { bleSend("NOT_ACTIVE"); return; }
        bleSend("PONG");
        return;
      }

      // ---- STOP ----
      if (cmd == "STOP") {
        stopMotors();
        weaponServo.write(90);
        leftPos = rightPos = weaponPos = 90;   // reset all positions to neutral
        rgbOff();
        bleSend("STOPPED");
        return;
      }

      // ---- DRIVE  "D,LLL,RRR,WWW" ----
      if (cmd.startsWith("D,")) {
        if (!botActive) { bleSend("NOT_ACTIVE"); return; }

        // Parse the three numbers out of the comma-separated string.
        // indexOf(',', 2) searches for a comma starting at character index 2,
        // skipping the "D," prefix we already know is there.
        int idx1 = cmd.indexOf(',', 2);
        int idx2 = (idx1 >= 0) ? cmd.indexOf(',', idx1 + 1) : -1;

        if (idx1 < 0 || idx2 < 0) {
          bleSend("ERR,BAD_FORMAT");   // malformed command — tell the app
          return;
        }

        // substring() extracts a chunk of the string.  toInt() converts
        // the text to a number.  constrain() clamps it to a valid range.
        int newLeft   = constrain(cmd.substring(2,     idx1).toInt(), 0, 180);
        int newRight  = constrain(cmd.substring(idx1+1, idx2).toInt(), 0, 180);
        int newWeapon = constrain(cmd.substring(idx2+1).toInt(),      0, 180);

        // Only reset the failsafe timer if there's actual meaningful motion.
        // "5" is a small deadzone — tiny joystick drift shouldn't count.
        bool hasMotion = (abs(newLeft   - 90) > 5) ||
                         (abs(newRight  - 90) > 5) ||
                         (abs(newWeapon - 90) > 5);

        leftPos   = newLeft;
        rightPos  = newRight;
        weaponPos = newWeapon;

        setDrive(leftPos, rightPos);
        weaponServo.write(weaponPos);
        updateRGB();

        if (hasMotion) lastInputTime = millis();

        Serial.printf("[BLE] Drive -> L:%d R:%d W:%d [%s]\n",
                      leftPos, rightPos, weaponPos,
                      hasMotion ? "MOTION" : "neutral");
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
#endif  // CONNECTION_BLE


// ============================================================
//     Forward declarations (shared functions used by both modes)
// ============================================================
void driveMotor(int in1, int in2, int speed);
void stopMotors();
void setDrive(int leftCmd, int rightCmd);
void rgbOff();
void setRGB(uint8_t r, uint8_t g, uint8_t b);
void updateRGB();


// ============================================================
//     MOTOR CONTROL  (shared by both Wi-Fi and BLE)
//
//  The DRV8833 motor driver takes two PWM signals per motor.
//  The combination of those signals determines direction and speed:
//    IN1=0,   IN2=speed  -> forward (speed 0–255)
//    IN1=speed, IN2=0    -> reverse
//    IN1=255, IN2=255    -> brake (both high = short the motor leads together)
//    IN1=0,   IN2=0      -> coast (motor freewheels)
// ============================================================
void driveMotor(int in1, int in2, int speed) {
  speed = constrain(speed, -255, 255);   // safety clamp — never exceed valid range

  if (speed == 0) {
    // Brake: both pins high.  The motor resists movement (good for holding position)
    ledcWrite(in1, 255);
    ledcWrite(in2, 255);
  } else if (speed > 0) {
    // Forward: one pin at 0V, other at PWM duty
    ledcWrite(in1, 0);
    ledcWrite(in2, speed);
  } else {
    // Reverse: flip the pins.  "-speed" makes the negative number positive.
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
  int leftThrottle  = leftCmd  - 90;   // shift so 0 = neutral, -90 to +90
  int rightThrottle = rightCmd - 90;
  int leftSpeed     = map(leftThrottle,  -90, 90, -255, 255);  // scale to motor range
  int rightSpeed    = map(rightThrottle, -90, 90, -255, 255);
  driveMotor(L_IN1, L_IN2, leftSpeed);
  driveMotor(R_IN1, R_IN2, rightSpeed);
}


// ============================================================
//     RGB LED  (shared)
//
//  neopixelWrite() is a built-in ESP32 Arduino function that
//  sends the NeoPixel color protocol on any GPIO pin.
//  Arguments are (pin, red, green, blue) — each 0–255.
// ============================================================
void rgbOff()                              { neopixelWrite(RGB_LED, 0, 0, 0); }
void setRGB(uint8_t r, uint8_t g, uint8_t b) { neopixelWrite(RGB_LED, r, g, b); }

// Change LED color based on what the bot is currently doing —
// handy for knowing what's happening without opening the serial monitor.
void updateRGB() {
  int ls = leftPos  - 90;   // positive = forward, negative = reverse
  int rs = rightPos - 90;
  if      (ls > 15 && rs > 15)   setRGB(0, 255, 0);    // both forward  -> green
  else if (ls < -15 && rs < -15) setRGB(255, 0, 0);    // both reverse  -> red
  else if (abs(ls - rs) > 15)    setRGB(255, 0, 255);  // turning       -> magenta
  else                            rgbOff();              // neutral/idle  -> off
}


// ============================================================
//     SETUP
//  setup() runs exactly once when the board powers on or resets.
//  Use it for one-time initialization — hardware config, starting
//  services, etc.
// ============================================================
void setup() {
  // Start the serial port so we can print debug messages.
  // Open "Serial Monitor" in Arduino IDE at 115200 baud to see them.
  Serial.begin(115200);
  delay(200);   // short pause lets the serial port settle before we print anything

  // ---- Motor LEDC setup ----
  // We attach our motor pins to LEDC (the hardware PWM controller) BEFORE
  // the servo library starts up.  This prevents channel conflicts.
  // (Arduino-ESP32 core 3.x API — see the big comment at the top for 2.x)
  ledcAttach(L_IN1, PWM_FREQ, PWM_RES);
  ledcAttach(L_IN2, PWM_FREQ, PWM_RES);
  ledcAttach(R_IN1, PWM_FREQ, PWM_RES);
  ledcAttach(R_IN2, PWM_FREQ, PWM_RES);
  stopMotors();   // make sure everything is stopped at boot

  // Set up encoders.  "HalfQuad" mode counts every other pulse,
  // which halves resolution but doubles noise immunity — good enough for us.
  encoderLeft.attachHalfQuad(ENC_L_A, ENC_L_B);
  encoderRight.attachHalfQuad(ENC_R_A, ENC_R_B);
  encoderLeft.clearCount();    // reset counters to 0 at startup
  encoderRight.clearCount();

  // Attach the weapon servo AFTER we've claimed the motor pins.
  // 500–2400 microseconds is the pulse width range for most hobby servos.
  weaponServo.attach(WEAPON_SERVO_PIN, 500, 2400);
  weaponServo.write(90);   // center / safe position on boot

  // Configure the RGB LED pin and make sure it's off
  pinMode(RGB_LED, OUTPUT);
  rgbOff();

  Serial.println("\n=== AntBot-S3  |  N20 + DRV8833 + Weapon ===");

  // ----------------------------------------------------------
  //     Wi-Fi Access Point startup
  //     The bot creates its own network — you join it.
  // ----------------------------------------------------------
#ifdef CONNECTION_WIFI
  Serial.println("[MODE] WiFi AP");
  WiFi.softAP(botSSID.c_str(), apPassword);   // create the network
  Serial.print("  SSID : "); Serial.println(botSSID);
  Serial.print("  IP   : "); Serial.println(WiFi.softAPIP());   // usually 192.168.4.1
  Serial.println("  Pass : battle123");

  // Register URL routes — when the browser visits a path,
  // call the corresponding handler function.
  server.on("/",            handleRoot);      // serves the joystick HTML page
  server.on("/drive",       handleDrive);     // move the bot
  server.on("/ping",        handlePing);      // keepalive check
  server.on("/activate",    handleActivate);  // arm the bot
  server.on("/status",      handleStatus);    // JSON status query
  server.on("/stop",        handleStop);      // stop motors, stay armed
  server.on("/deactivate",  handleDeactivate); // emergency stop + disarm
  server.begin();

  setRGB(0, 128, 255);   // cyan = Wi-Fi is up, waiting for a browser to connect
#endif

  // ----------------------------------------------------------
  //     BLE startup (only if CONNECTION_BLE is defined)
  // ----------------------------------------------------------
#ifdef CONNECTION_BLE
  Serial.println("[MODE] BLE");
  String bleName = "AntBot-" + String((uint32_t)(ESP.getEfuseMac() >> 32), HEX);
  BLEDevice::init(bleName.c_str());   // set the BLE device name
  Serial.print("  BLE name : "); Serial.println(bleName);

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
  Serial.println("  Advertising -- waiting for Android client...");

  setRGB(128, 0, 255);   // purple = BLE advertising, no phone connected yet
#endif
}


// ============================================================
//     LOOP
//  loop() runs over and over, forever, as fast as the CPU allows
//  (thousands of times per second).  Think of it as the heartbeat
//  of the program — check for incoming data, run safety checks,
//  send periodic updates.
// ============================================================
void loop() {

  // Let the web server process any pending HTTP requests from the browser.
  // Without this call, the server would never respond to anything.
#ifdef CONNECTION_WIFI
  server.handleClient();
#endif

  // ---- Failsafe check ----
  // If the bot is armed but we haven't received real input in a while,
  // cut power immediately.  This handles phone battery death, dropped
  // Wi-Fi, browser tab crashes, etc.
  if (botActive && (millis() - lastInputTime > FAILSAFE_TIMEOUT)) {
    Serial.println("!!! FAILSAFE TRIGGERED !!!");
    stopMotors();
    weaponServo.write(90);   // retract/center weapon
    weaponPos = 90;
    leftPos = rightPos = 90;
    rgbOff();
    botActive = false;   // require re-activation before moving again

#ifdef CONNECTION_BLE
    bleSend("FAILSAFE");   // tell the app what happened
#endif
  }

  // ---- Periodic encoder telemetry ----
  // "static" means this variable persists between calls to loop() —
  // it's not reset to 0 every time like a normal local variable would be.
  static unsigned long lastPrint = 0;

  // Print encoder values every 500ms — useful for debugging
  // but not so frequent that it floods the serial monitor.
  if (millis() - lastPrint > 500) {
    int encL = (int)encoderLeft.getCount();
    int encR = (int)encoderRight.getCount();
    Serial.printf("Enc L:%d | R:%d | Active:%s\n",
                  encL, encR, botActive ? "YES" : "NO");

#ifdef CONNECTION_BLE
    // If a phone is connected over BLE, send it the counts too
    if (bleConnected) {
      bleSend("ENC," + String(encL) + "," + String(encR));
    }
#endif
    lastPrint = millis();
  }
}


// ============================================================
//     Wi-Fi HTTP HANDLERS
//  Each of these functions is called by the web server when the
//  browser visits the matching URL.  server.send() sends back
//  the HTTP response — think of it like replying to the browser.
// ============================================================
#ifdef CONNECTION_WIFI

// /ping — the browser asks "are you still there?" every 500ms.
// We respond but do NOT reset the failsafe timer here.
// Only actual drive inputs with real motion reset that clock.
void handlePing() {
  if (!botActive) { server.send(200, "text/plain", "NOT_ACTIVE"); return; }
  server.send(200, "text/plain", "PONG");
}

// /stop — joystick released.  Halts motors but keeps the bot armed
// so the driver can immediately move again without re-activating.
void handleStop() {
  stopMotors();
  weaponServo.write(90);
  leftPos = rightPos = weaponPos = 90;
  rgbOff();
  server.send(200, "text/plain", "STOPPED");
}

// /deactivate — the "STOP ALL" button.  Halts motors AND disarms
// the bot.  Any in-flight /drive requests that arrive after this
// will be rejected with NOT_ACTIVE.
void handleDeactivate() {
  stopMotors();
  weaponServo.write(90);
  leftPos = rightPos = weaponPos = 90;
  botActive = false;   // require ACTIVATE before driving again
  rgbOff();
  Serial.println("[WiFi] DEACTIVATED via emergency stop");
  server.send(200, "text/plain", "DEACTIVATED");
}

// /drive?left=NNN&right=NNN&weapon=NNN
// The browser sends this URL with query parameters every ~40ms while
// the joystick is being held.  We parse the values and move the bot.
void handleDrive() {
  if (!botActive) { server.send(200, "text/plain", "NOT_ACTIVE"); return; }

  // server.hasArg() checks if a query parameter exists in the URL.
  // If it's missing, we keep the current value instead of defaulting to 90.
  int newLeft   = server.hasArg("left")   ? constrain(server.arg("left").toInt(),   0, 180) : leftPos;
  int newRight  = server.hasArg("right")  ? constrain(server.arg("right").toInt(),  0, 180) : rightPos;
  int newWeapon = server.hasArg("weapon") ? constrain(server.arg("weapon").toInt(), 0, 180) : weaponPos;

  // Deadzone check — small values near 90 are joystick noise, not real input
  bool hasMotion = (abs(newLeft   - 90) > 5) ||
                   (abs(newRight  - 90) > 5) ||
                   (abs(newWeapon - 90) > 5);

  leftPos   = newLeft;
  rightPos  = newRight;
  weaponPos = newWeapon;

  setDrive(leftPos, rightPos);
  weaponServo.write(weaponPos);
  updateRGB();

  // Only real motion resets the failsafe timer
  if (hasMotion) lastInputTime = millis();

  Serial.printf("[WiFi] Drive -> L:%d R:%d W:%d [%s] | EncL:%d EncR:%d\n",
                leftPos, rightPos, weaponPos,
                hasMotion ? "MOTION" : "neutral",
                (int)encoderLeft.getCount(),
                (int)encoderRight.getCount());

  server.send(200, "text/plain", "OK");
}

// /activate — arms the bot so drive commands are accepted.
// Having a separate activation step prevents accidental movement
// just from loading the page.
void handleActivate() {
  botActive     = true;
  lastInputTime = millis();
  Serial.println("[WiFi] Bot ACTIVATED");
  setRGB(0, 255, 0);   // green = armed and ready
  server.send(200, "text/plain", "ACTIVATED");
}

// /status — returns a JSON object the browser can parse.
// Useful for the UI to check whether the bot thinks it's still armed.
void handleStatus() {
  String json = "{\"active\":" + String(botActive ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

// ---- Web UI ----
// This is the entire joystick interface — HTML, CSS, and JavaScript —
// stored as one big string and sent to the browser on request.
// It's not pretty code, but it works without needing any external files.
//
// NOTE: I am not a web developer.  Someone could probably take this and make
// it far, far more elegant and usable than I.
// Do what you want with it!  — Adam
void handleRoot() {
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
    "    .stop-btn { position:fixed; top:15px; right:15px; padding:12px 28px; background:#f00; color:white; border:none; border-radius:12px; font-size:1.2em; }\n"
    "    .container { display:flex; height:calc(100vh - 80px); width:100%; }\n"
    "    .left { flex:1; display:flex; justify-content:flex-start; align-items:center; padding-left:50px; }\n"
    "    .right { flex:1; display:flex; justify-content:flex-end; align-items:center; padding-right:60px; background:#1a1a1a; }\n"
    "    .joystick-base { width:230px; height:230px; background:rgba(255,255,255,0.08); border:7px solid #0f0; border-radius:50%; position:relative; }\n"
    "    .joystick-knob { width:75px; height:75px; background:#0f0; border-radius:50%; position:absolute; top:50%; left:50%; transform:translate(-50%,-50%); box-shadow:0 0 25px #0f0; }\n"
    "    .weapon-label { font-size:1.5em; margin-bottom:20px; }\n"
    "    .weapon-slider { width:70px; height:340px; accent-color:#ff0; }\n"
    "    .value { font-size:2.1em; margin-top:15px; }\n"
    "    .hidden { display:none !important; }\n"
    // small dot in the corner that flashes green on every successful ping
    "    #pingDot { position:fixed; bottom:12px; right:18px; width:10px; height:10px; border-radius:50%; background:#555; }\n"
    "  </style>\n"
    "</head>\n"
    "<body>\n"
    "  <h1>" + botSSID + "</h1>\n"
    "  <div id=\"pingDot\"></div>\n"
    "\n"
    // Two screens that swap visibility: activation gate and the actual controls
    "  <div id=\"activateScreen\" class=\"activate-screen\">\n"
    "    <button class=\"activate-btn\" onclick=\"activateBot()\">ACTIVATE BOT</button>\n"
    "    <p style=\"margin-top:30px; font-size:1.3em;\">Press to enable controls</p>\n"
    "  </div>\n"
    "\n"
    "  <div id=\"controlScreen\" class=\"control-screen hidden\">\n"
    "    <button class=\"stop-btn\" onclick=\"stopAll()\">STOP ALL</button>\n"
    "    <div class=\"container\">\n"
    "      <div class=\"left\">\n"
    "        <div class=\"joystick-base\" id=\"joyBase\"><div class=\"joystick-knob\" id=\"joyKnob\"></div></div>\n"
    "      </div>\n"
    "      <div class=\"right\">\n"
    "        <div class=\"weapon-label\">WEAPON</div>\n"
    // orient=vertical makes this a vertical slider — not all browsers honor it
    "        <input type=\"range\" id=\"weaponSlider\" class=\"weapon-slider\" min=\"0\" max=\"180\" value=\"90\" orient=\"vertical\" oninput=\"updateWeapon(this.value)\">\n"
    "        <div class=\"value\"><span id=\"weaponValue\">90</span>°</div>\n"
    "      </div>\n"
    "    </div>\n"
    "  </div>\n"
    "\n"
    "  <script>\n"
    // ---- JavaScript that runs in the browser ----
    "    let botActive = false;\n"         // mirrors the bot's arm state on the browser side
    "    let lastSend = 0;\n"              // timestamp of last /drive fetch
    "    let currentX = 0, currentY = 0, weaponPos = 90;\n"  // current joystick position (-1 to 1) and weapon
    "    let isDragging = false;\n"        // is the user's finger/mouse currently on the joystick?
    "    const base = document.getElementById('joyBase');\n"
    "    const knob = document.getElementById('joyKnob');\n"
    "    const pingDot = document.getElementById('pingDot');\n"
    "\n"
    // Show the activation screen and reset everything to a safe state
    "    function showActivateScreen() {\n"
    "      botActive = false;\n"
    "      isDragging = false;\n"
    "      currentX = currentY = 0;\n"
    "      knob.style.transform = 'translate(-37.5px, -37.5px)';\n"  // knob back to center
    "      document.getElementById('controlScreen').classList.add('hidden');\n"
    "      document.getElementById('activateScreen').classList.remove('hidden');\n"
    "    }\n"
    "\n"
    // Hit /activate, then swap screens if it succeeded
    "    function activateBot() {\n"
    "      fetch('/activate').then(() => {\n"
    "        botActive = true;\n"
    "        document.getElementById('activateScreen').classList.add('hidden');\n"
    "        document.getElementById('controlScreen').classList.remove('hidden');\n"
    "      });\n"
    "    }\n"
    // Heartbeat — called every 500ms.  If the bot replies NOT_ACTIVE,
    // the failsafe must have fired on the bot side, so we return to the
    // activation screen to keep both sides in sync.
    "    function sendPing() {\n"
    "      if (!botActive) return;\n"
    "      fetch('/ping').then(r => r.text()).then(t => {\n"
    "        if (t === 'NOT_ACTIVE') { showActivateScreen(); return; }\n"
    "        pingDot.style.background = '#0f0';\n"                        // flash green
    "        setTimeout(() => pingDot.style.background = '#555', 200);\n" // back to gray
    "      }).catch(() => { pingDot.style.background = '#f00'; });\n"     // red = network error
    "    }\n"
    // Calculate where the knob should be based on touch/mouse position,
    // clamp it inside the circular base, then immediately send a drive command.
    "    function moveKnob(clientX, clientY) {\n"
    "      if (!botActive) return;\n"
    "      isDragging = true;\n"
    "      const rect = base.getBoundingClientRect();\n"  // get the joystick base position on screen
    "      let x = clientX - rect.left - 115;\n"         // offset so center = (0,0)
    "      let y = clientY - rect.top  - 115;\n"
    "      const dist = Math.sqrt(x*x + y*y);\n"         // distance from center (Pythagorean theorem)
    "      if (dist > 115) { x = (x/dist)*115; y = (y/dist)*115; }\n"  // clamp to circle radius
    "      knob.style.transform = `translate(${x-37.5}px, ${y-37.5}px)`;\n"
    "      currentX = x / 115; currentY = -y / 115;\n"  // normalize to -1..1  (Y flipped: up = positive)
    "      sendDrive();\n"
    "    }\n"
    // Finger/mouse lifted — snap the knob to center and tell the bot to stop
    "    function resetKnob() {\n"
    "      isDragging = false;\n"
    "      knob.style.transform = 'translate(-37.5px, -37.5px)';\n"
    "      currentX = currentY = 0;\n"
    "      fetch('/stop');\n"
    "    }\n"
    // Convert normalized joystick X/Y into left/right motor values and send them.
    // Rate-limited to once per 40ms (~25 Hz) so we don't flood the bot.
    "    function sendDrive() {\n"
    "      if (!botActive) return;\n"
    "      const now = Date.now();\n"
    "      if (now - lastSend < 40) return;\n"  // too soon — skip this call
    "      lastSend = now;\n"
    // Tank-style mixing: forward/back from Y, steering from X
    "      const forward = currentY * 90;\n"  // max ±90 from center
    "      const steer   = currentX * 65;\n"  // slightly less than forward for smoother turns
    "      let left  = Math.max(0, Math.min(180, Math.round(90 + forward + steer)));\n"
    "      let right = Math.max(0, Math.min(180, Math.round(90 + forward - steer)));\n"
    // Skip the fetch if everything is at neutral — no point sending a no-op
    "      if (Math.abs(left-90)>5 || Math.abs(right-90)>5 || Math.abs(weaponPos-90)>5)\n"
    "        fetch(`/drive?left=${left}&right=${right}&weapon=${weaponPos}`);\n"
    "    }\n"
    // Called whenever the weapon slider moves
    "    function updateWeapon(val) {\n"
    "      if (!botActive) return;\n"
    "      weaponPos = parseInt(val);\n"
    "      document.getElementById('weaponValue').innerText = weaponPos;\n"
    "      sendDrive();\n"
    "    }\n"
    // Emergency stop: disarm the bot, reset slider, go back to activation screen
    "    function stopAll() {\n"
    "      fetch('/deactivate');\n"
    "      weaponPos = 90;\n"
    "      document.getElementById('weaponSlider').value = 90;\n"
    "      document.getElementById('weaponValue').innerText = 90;\n"
    "      showActivateScreen();\n"
    "    }\n"
    // While dragging, keep firing sendDrive even if the finger hasn't moved —
    // this keeps the failsafe timer alive on the bot side.
    "    setInterval(() => { if (isDragging && botActive) sendDrive(); }, 45);\n"
    "    setInterval(sendPing, 500);\n"

    // Touch events (mobile) — preventDefault stops the page from scrolling
    "    base.addEventListener('touchstart',  e => { e.preventDefault(); moveKnob(e.touches[0].clientX, e.touches[0].clientY); });\n"
    "    base.addEventListener('touchmove',   e => { e.preventDefault(); moveKnob(e.touches[0].clientX, e.touches[0].clientY); });\n"
    "    base.addEventListener('touchend',    resetKnob);\n"
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

#endif  // CONNECTION_WIFI
