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

// This is a baseline sketch only - it will be usable as-is, 
// however you'll likely need to modify to meet what you want
// out of the code

// Have fun! - Adam

// ============================================================
//  SELECT YOUR CONNECTION MODE — uncomment exactly ONE:
//  This means you'll connect with your device to the Bot's
//  built-in Wi-Fi if you leave it as-is (then goto
//  192.168.4.1 in a browser)
//  If you'd rather develop an Android app to control things
//  via Bluetooth, then uncomment that line (i.e.
//  "CONNECTION_BLE" by removing the "//" in front of it) and
//  then add "//" in front of the line with "CONNECTION_WIFI"
//  below
// ============================================================
#define CONNECTION_WIFI
// #define CONNECTION_BLE
// ============================================================

#include <ESP32Servo.h>
#include <ESP32Encoder.h>

#ifdef CONNECTION_WIFI
  #include <WiFi.h>
  #include <WebServer.h>
#endif

#ifdef CONNECTION_BLE
  #include <BLEDevice.h>
  #include <BLEServer.h>
  #include <BLEUtils.h>
  #include <BLE2902.h>
#endif


// ==================== EDIT AS NEEDED =====================
// Your ESP32-S3 may have different pinouts than what I use
// Make sure that you verify the below pins and your board
// =========================================================
// ================== PINS ==================
const int WEAPON_SERVO_PIN = 6;

const int L_IN1 = 8;
const int L_IN2 = 15;
const int R_IN1 = 16;
const int R_IN2 = 17;

ESP32Encoder encoderLeft;
ESP32Encoder encoderRight;
const int ENC_L_A = 18;
const int ENC_L_B = 22;
const int ENC_R_A = 19;
const int ENC_R_B = 21;

// ================= END OF EDITABLE SEGMENT ================

#define RGB_LED 48

// ================== SHARED STATE ==================
const unsigned long FAILSAFE_TIMEOUT = 2000;
bool     botActive    = false;
unsigned long lastInputTime = 0;

int leftPos = 90, rightPos = 90, weaponPos = 90;

Servo weaponServo;

// ==================== EDIT AS NEEDED =====================
// You may choose a different Wi-Fi SSID name and password
// Change "botSSID" and/or "apPassword" as you see fit
// =========================================================
// ============================================================
//  WiFi-only globals
// ============================================================
#ifdef CONNECTION_WIFI
  String      botSSID   = "AntBot-S3-" + String((uint32_t)(ESP.getEfuseMac() >> 32), HEX);
  const char* apPassword = "battle123";
  WebServer   server(80);
// ================= END OF EDITABLE SEGMENT ================

  // Forward declarations for WiFi handlers
  void handleRoot();
  void handleDrive();
  void handlePing();
  void handleActivate();
  void handleStatus();
  void handleStop();
#endif

// ============================================================
//  BLE-only globals
//  This segment is only used if you chose to comment out
//  the definitions at the top for Wi-Fi and uncomment the BLE
//  portion
// ============================================================
#ifdef CONNECTION_BLE
  // ---- UUIDs ----
  // Copy these into your Android app exactly as shown.
  #define BLE_SERVICE_UUID      "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
  #define BLE_CMD_CHAR_UUID     "beb5483e-36e1-4688-b7f5-ea07361b26a8"  // WRITE (Android → Bot)
  #define BLE_RSP_CHAR_UUID     "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  // NOTIFY (Bot → Android)

  BLEServer*         pServer    = nullptr;
  BLECharacteristic* pCmdChar   = nullptr;   // receive commands
  BLECharacteristic* pRspChar   = nullptr;   // send responses / notifications
  bool               bleConnected = false;
  // The below is just to document what the BLE protocol commands are
  // Since you'll need to write your own Android application, having them 
  // referenced is helpful
  //
  //   "ACTIVATE"          → enable bot controls
  //   "STOP"              → stop motors + weapon, keep active
  //   "PING"              → keepalive (resets failsafe timer)
  //   "D,LLL,RRR,WWW"     → drive  (e.g. "D,090,135,090")
  //                          LLL / RRR / WWW are 0-180 servo-style values
  //
  // Android reads notifications from BLE_RSP_CHAR_UUID:
  //   "ACTIVATED"         → bot is now active
  //   "STOPPED"           → motors stopped
  //   "PONG"              → reply to PING
  //   "OK"                → drive command accepted
  //   "NOT_ACTIVE"        → command rejected, call ACTIVATE first
  //   "STATUS,true|false" → bot active state
  //   "ENC,L,R"           → encoder counts (sent every 500 ms when connected)

  // ---- Helpers ----
  void bleSend(const String& msg) {
    if (!bleConnected || pRspChar == nullptr) return;
    pRspChar->setValue(msg.c_str());
    pRspChar->notify();
  }

  // ---- Server callbacks ----
  class BotServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* s) override {
      bleConnected = true;
      Serial.println("[BLE] Client connected");
      setRGB(0, 0, 255);   // blue = connected, not yet active
    }
    void onDisconnect(BLEServer* s) override {
      bleConnected  = false;
      botActive     = false;
      Serial.println("[BLE] Client disconnected — failsafe");
      stopMotors();
      weaponServo.write(90);
      rgbOff();
      // Restart advertising so a new client can connect
      BLEDevice::startAdvertising();
    }
  };

  // ---- Command characteristic callbacks ----
  class CmdCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pChar) override {
      String cmd = String(pChar->getValue().c_str());
      cmd.trim();
      Serial.print("[BLE] CMD: "); Serial.println(cmd);

      // ---- ACTIVATE ----
      if (cmd == "ACTIVATE") {
        botActive     = true;
        lastInputTime = millis();
        setRGB(0, 255, 0);
        bleSend("ACTIVATED");
        return;
      }

      // ---- PING ----
      if (cmd == "PING") {
        if (!botActive) { bleSend("NOT_ACTIVE"); return; }
        lastInputTime = millis();
        bleSend("PONG");
        return;
      }

      // ---- STOP ----
      if (cmd == "STOP") {
        stopMotors();
        weaponServo.write(90);
        leftPos = rightPos = weaponPos = 90;
        rgbOff();
        bleSend("STOPPED");
        return;
      }

      // ---- DRIVE  "D,LLL,RRR,WWW" ----
      if (cmd.startsWith("D,")) {
        if (!botActive) { bleSend("NOT_ACTIVE"); return; }

        // parse three comma-separated integers after "D,"
        int idx1 = cmd.indexOf(',', 2);
        int idx2 = (idx1 >= 0) ? cmd.indexOf(',', idx1 + 1) : -1;

        if (idx1 < 0 || idx2 < 0) {
          bleSend("ERR,BAD_FORMAT");
          return;
        }

        int newLeft   = constrain(cmd.substring(2,     idx1).toInt(), 0, 180);
        int newRight  = constrain(cmd.substring(idx1+1, idx2).toInt(), 0, 180);
        int newWeapon = constrain(cmd.substring(idx2+1).toInt(),      0, 180);

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

        Serial.printf("[BLE] Drive → L:%d R:%d W:%d [%s]\n",
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

      bleSend("ERR,UNKNOWN_CMD");
    }
  };
#endif  // CONNECTION_BLE

// ============================================================
//  Forward declarations (shared)
// ============================================================
void driveMotor(int in1, int in2, int speed);
void stopMotors();
void setDrive(int leftCmd, int rightCmd);
void rgbOff();
void setRGB(uint8_t r, uint8_t g, uint8_t b);
void updateRGB();

// ============================================================
//  MOTOR CONTROL  (shared)
// ============================================================
void driveMotor(int in1, int in2, int speed) {
  speed = constrain(speed, -255, 255);
  if (speed == 0) {
    // Hard brake — DRV8833 brake mode
    digitalWrite(in1, HIGH);
    digitalWrite(in2, HIGH);
  } else if (speed > 0) {
    digitalWrite(in1, LOW);
    analogWrite(in2, speed);
  } else {
    analogWrite(in1, -speed);
    digitalWrite(in2, LOW);
  }
}

void stopMotors() {
  digitalWrite(L_IN1, HIGH); digitalWrite(L_IN2, HIGH);
  digitalWrite(R_IN1, HIGH); digitalWrite(R_IN2, HIGH);
}

void setDrive(int leftCmd, int rightCmd) {
  int leftThrottle  = leftCmd  - 90;
  int rightThrottle = rightCmd - 90;
  int leftSpeed     = map(leftThrottle,  -90, 90, -255, 255);
  int rightSpeed    = map(rightThrottle, -90, 90, -255, 255);
  driveMotor(L_IN1, L_IN2, leftSpeed);
  driveMotor(R_IN1, R_IN2, rightSpeed);
}

// ============================================================
//  RGB LED  (shared)
// ============================================================
void rgbOff()                              { neopixelWrite(RGB_LED, 0, 0, 0); }
void setRGB(uint8_t r, uint8_t g, uint8_t b) { neopixelWrite(RGB_LED, r, g, b); }

void updateRGB() {
  int ls = leftPos  - 90;
  int rs = rightPos - 90;
  if      (ls > 15 && rs > 15)   setRGB(0, 255, 0);    // forward  → green
  else if (ls < -15 && rs < -15) setRGB(255, 0, 0);    // reverse  → red
  else if (abs(ls - rs) > 15)    setRGB(255, 0, 255);  // turning  → magenta
  else                            rgbOff();
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(200);

  // Motor pins
  pinMode(L_IN1, OUTPUT); pinMode(L_IN2, OUTPUT);
  pinMode(R_IN1, OUTPUT); pinMode(R_IN2, OUTPUT);
  stopMotors();

  // Encoders
  encoderLeft.attachHalfQuad(ENC_L_A, ENC_L_B);
  encoderRight.attachHalfQuad(ENC_R_A, ENC_R_B);
  encoderLeft.clearCount();
  encoderRight.clearCount();

  // Weapon servo
  weaponServo.attach(WEAPON_SERVO_PIN, 500, 2400);
  weaponServo.write(90);

  // RGB
  pinMode(RGB_LED, OUTPUT);
  rgbOff();

  Serial.println("\n=== AntBot-S3  |  N20 + DRV8833 + Weapon ===");

  // ----------------------------------------------------------
  //  WiFi initialisation
  // ----------------------------------------------------------
#ifdef CONNECTION_WIFI
  Serial.println("[MODE] WiFi AP");
  WiFi.softAP(botSSID.c_str(), apPassword);
  Serial.print("  SSID : "); Serial.println(botSSID);
  Serial.print("  IP   : "); Serial.println(WiFi.softAPIP());
  Serial.println("  Pass : battle123");

  server.on("/",         handleRoot);
  server.on("/drive",    handleDrive);
  server.on("/ping",     handlePing);
  server.on("/activate", handleActivate);
  server.on("/status",   handleStatus);
  server.on("/stop",     handleStop);
  server.begin();

  setRGB(0, 128, 255);   // cyan-ish = WiFi ready, waiting for browser
#endif

  // ----------------------------------------------------------
  //  BLE initialisation
  // ----------------------------------------------------------
#ifdef CONNECTION_BLE
  Serial.println("[MODE] BLE");
  String bleName = "AntBot-" + String((uint32_t)(ESP.getEfuseMac() >> 32), HEX);
  BLEDevice::init(bleName.c_str());
  Serial.print("  BLE name : "); Serial.println(bleName);

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new BotServerCallbacks());

  BLEService* pService = pServer->createService(BLE_SERVICE_UUID);

  // Command characteristic — Android WRITES here
  pCmdChar = pService->createCharacteristic(
               BLE_CMD_CHAR_UUID,
               BLECharacteristic::PROPERTY_WRITE |
               BLECharacteristic::PROPERTY_WRITE_NR);
  pCmdChar->setCallbacks(new CmdCallbacks());

  // Response characteristic — Arduino NOTIFIES here
  pRspChar = pService->createCharacteristic(
               BLE_RSP_CHAR_UUID,
               BLECharacteristic::PROPERTY_NOTIFY);
  pRspChar->addDescriptor(new BLE2902());

  pService->start();

  BLEAdvertising* pAdv = BLEDevice::getAdvertising();
  pAdv->addServiceUUID(BLE_SERVICE_UUID);
  pAdv->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("  Advertising — waiting for Android client...");

  setRGB(128, 0, 255);   // purple = BLE advertising
#endif
}

// ============================================================
//  LOOP
// ============================================================
void loop() {

#ifdef CONNECTION_WIFI
  server.handleClient();
#endif

  // ---- Shared failsafe ----
  if (botActive && (millis() - lastInputTime > FAILSAFE_TIMEOUT)) {
    Serial.println("!!! FAILSAFE TRIGGERED !!!");
    stopMotors();
    weaponServo.write(90);
    weaponPos = 90;
    leftPos = rightPos = 90;
    rgbOff();
    botActive = false;

#ifdef CONNECTION_BLE
    bleSend("FAILSAFE");
#endif
  }

  // ---- Periodic encoder telemetry ----
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 500) {
    int encL = (int)encoderLeft.getCount();
    int encR = (int)encoderRight.getCount();
    Serial.printf("Enc L:%d | R:%d | Active:%s\n",
                  encL, encR, botActive ? "YES" : "NO");

#ifdef CONNECTION_BLE
    if (bleConnected) {
      bleSend("ENC," + String(encL) + "," + String(encR));
    }
#endif
    lastPrint = millis();
  }
}

// ============================================================
//  Wi-Fi HTTP HANDLERS
// ============================================================
#ifdef CONNECTION_WIFI

void handlePing() {
  if (!botActive) { server.send(200, "text/plain", "NOT_ACTIVE"); return; }
  lastInputTime = millis();
  server.send(200, "text/plain", "PONG");
}

void handleStop() {
  stopMotors();
  weaponServo.write(90);
  leftPos = rightPos = weaponPos = 90;
  rgbOff();
  server.send(200, "text/plain", "STOPPED");
}

void handleDrive() {
  if (!botActive) { server.send(200, "text/plain", "NOT_ACTIVE"); return; }

  int newLeft   = server.hasArg("left")   ? constrain(server.arg("left").toInt(),   0, 180) : leftPos;
  int newRight  = server.hasArg("right")  ? constrain(server.arg("right").toInt(),  0, 180) : rightPos;
  int newWeapon = server.hasArg("weapon") ? constrain(server.arg("weapon").toInt(), 0, 180) : weaponPos;

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

  Serial.printf("[WiFi] Drive → L:%d R:%d W:%d [%s] | EncL:%d EncR:%d\n",
                leftPos, rightPos, weaponPos,
                hasMotion ? "MOTION" : "neutral",
                (int)encoderLeft.getCount(),
                (int)encoderRight.getCount());

  server.send(200, "text/plain", "OK");
}

void handleActivate() {
  botActive     = true;
  lastInputTime = millis();
  Serial.println("[WiFi] Bot ACTIVATED");
  setRGB(0, 255, 0);
  server.send(200, "text/plain", "ACTIVATED");
}

void handleStatus() {
  String json = "{\"active\":" + String(botActive ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

// ---- Web UI (served to the browser over WiFi AP) ----
// NOTE: I am not a web developer.  Someone could probably take this and make
// it far, far more elegant and usable than I
// Do what you want with it!
void handleRoot() {
  String html =
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "  <title>AntBot Joystick</title>\n"
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
    "    #pingDot { position:fixed; bottom:12px; right:18px; width:10px; height:10px; border-radius:50%; background:#555; }\n"
    "  </style>\n"
    "</head>\n"
    "<body>\n"
    "  <h1>" + botSSID + "</h1>\n"
    "  <div id=\"pingDot\"></div>\n"
    "\n"
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
    "        <input type=\"range\" id=\"weaponSlider\" class=\"weapon-slider\" min=\"0\" max=\"180\" value=\"90\" orient=\"vertical\" oninput=\"updateWeapon(this.value)\">\n"
    "        <div class=\"value\"><span id=\"weaponValue\">90</span>°</div>\n"
    "      </div>\n"
    "    </div>\n"
    "  </div>\n"
    "\n"
    "  <script>\n"
    "    let botActive = false;\n"
    "    let lastSend = 0;\n"
    "    let currentX = 0, currentY = 0, weaponPos = 90;\n"
    "    let isDragging = false;\n"
    "    const base = document.getElementById('joyBase');\n"
    "    const knob = document.getElementById('joyKnob');\n"
    "    const pingDot = document.getElementById('pingDot');\n"
    "\n"
    "    function activateBot() {\n"
    "      fetch('/activate').then(() => {\n"
    "        botActive = true;\n"
    "        document.getElementById('activateScreen').classList.add('hidden');\n"
    "        document.getElementById('controlScreen').classList.remove('hidden');\n"
    "      });\n"
    "    }\n"
    "    function sendPing() {\n"
    "      if (!botActive) return;\n"
    "      fetch('/ping').then(r => r.text()).then(t => {\n"
    "        if (t === 'NOT_ACTIVE') {\n"
    "          botActive = false;\n"
    "          document.getElementById('controlScreen').classList.add('hidden');\n"
    "          document.getElementById('activateScreen').classList.remove('hidden');\n"
    "        }\n"
    "        pingDot.style.background = '#0f0';\n"
    "        setTimeout(() => pingDot.style.background = '#555', 200);\n"
    "      }).catch(() => { pingDot.style.background = '#f00'; });\n"
    "    }\n"
    "    function moveKnob(clientX, clientY) {\n"
    "      if (!botActive) return;\n"
    "      isDragging = true;\n"
    "      const rect = base.getBoundingClientRect();\n"
    "      let x = clientX - rect.left - 115;\n"
    "      let y = clientY - rect.top  - 115;\n"
    "      const dist = Math.sqrt(x*x + y*y);\n"
    "      if (dist > 115) { x = (x/dist)*115; y = (y/dist)*115; }\n"
    "      knob.style.transform = `translate(${x-37.5}px, ${y-37.5}px)`;\n"
    "      currentX = x / 115; currentY = -y / 115;\n"
    "      sendDrive();\n"
    "    }\n"
    "    function resetKnob() {\n"
    "      isDragging = false;\n"
    "      knob.style.transform = 'translate(-37.5px, -37.5px)';\n"
    "      currentX = currentY = 0;\n"
    "      fetch('/stop');\n"
    "    }\n"
    "    function sendDrive() {\n"
    "      if (!botActive) return;\n"
    "      const now = Date.now();\n"
    "      if (now - lastSend < 40) return;\n"
    "      lastSend = now;\n"
    "      const forward = currentY * 90;\n"
    "      const steer   = currentX * 65;\n"
    "      let left  = Math.max(0, Math.min(180, Math.round(90 + forward + steer)));\n"
    "      let right = Math.max(0, Math.min(180, Math.round(90 + forward - steer)));\n"
    "      if (Math.abs(left-90)>5 || Math.abs(right-90)>5 || Math.abs(weaponPos-90)>5)\n"
    "        fetch(`/drive?left=${left}&right=${right}&weapon=${weaponPos}`);\n"
    "    }\n"
    "    function updateWeapon(val) {\n"
    "      if (!botActive) return;\n"
    "      weaponPos = parseInt(val);\n"
    "      document.getElementById('weaponValue').innerText = weaponPos;\n"
    "      sendDrive();\n"
    "    }\n"
    "    function stopAll() {\n"
    "      fetch('/stop');\n"
    "      isDragging = false;\n"
    "      knob.style.transform = 'translate(-37.5px, -37.5px)';\n"
    "      currentX = currentY = 0;\n"
    "      weaponPos = 90;\n"
    "      document.getElementById('weaponSlider').value = 90;\n"
    "      document.getElementById('weaponValue').innerText = 90;\n"
    "    }\n"
    "    setInterval(() => { if (isDragging && botActive) sendDrive(); }, 45);\n"
    "    setInterval(sendPing, 500);\n"
    "    base.addEventListener('touchstart', e => { e.preventDefault(); moveKnob(e.touches[0].clientX, e.touches[0].clientY); });\n"
    "    base.addEventListener('touchmove',  e => { e.preventDefault(); moveKnob(e.touches[0].clientX, e.touches[0].clientY); });\n"
    "    base.addEventListener('touchend',   resetKnob);\n"
    "    base.addEventListener('mousedown', e => {\n"
    "      const move = ev => moveKnob(ev.clientX, ev.clientY);\n"
    "      document.addEventListener('mousemove', move);\n"
    "      document.addEventListener('mouseup', () => { document.removeEventListener('mousemove', move); resetKnob(); }, {once:true});\n"
    "    });\n"
    "  </script>\n"
    "</body>\n"
    "</html>";

  server.send(200, "text/html", html);
}

#endif  // CONNECTION_WIFI
