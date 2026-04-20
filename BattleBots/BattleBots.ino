#include <ESP32Servo.h>
#include <ESP32Encoder.h>
#include <WiFi.h>
#include <WebServer.h>

// ================== PINS ==================
const int WEAPON_SERVO_PIN = 6;

// DRV8833 - Left Motor
const int L_IN1 = 16;
const int L_IN2 = 17;   // PWM capable

// DRV8833 - Right Motor
const int R_IN1 = 15;
const int R_IN2 = 8;    // PWM capable

// Encoders (Quadrature)
ESP32Encoder encoderLeft;
ESP32Encoder encoderRight;
const int ENC_L_A = 18;   // C1 on left motor
const int ENC_L_B = 21;   // C2 on left motor
const int ENC_R_A = 19;   // C1 on right motor
const int ENC_R_B = 22;   // C2 on right motor

// RGB LED
#define RGB_LED 48

// FAILSAFE
const unsigned long FAILSAFE_TIMEOUT = 2000;   // 2 seconds of no input
bool botActive = false;
unsigned long lastInputTime = 0;

String botSSID = "AntBot-S3-" + String((uint32_t)(ESP.getEfuseMac() >> 32), HEX);
const char* apPassword = "battle123";

WebServer server(80);

Servo weaponServo;

int leftPos = 90, rightPos = 90, weaponPos = 90;

// ====================== MOTOR CONTROL ======================
void driveMotor(int in1, int in2, int speed) {
  speed = constrain(speed, -255, 255);
  
  if (speed > 0) {                    // Forward
    digitalWrite(in1, LOW);
    analogWrite(in2, speed);
  } 
  else if (speed < 0) {               // Reverse
    digitalWrite(in1, HIGH);
    analogWrite(in2, -speed);
  } 
  else {                              // Coast (stop)
    digitalWrite(in1, LOW);
    digitalWrite(in2, LOW);
  }
}

void setDrive(int leftCmd, int rightCmd) {   // 0-180 values from web
  int leftThrottle = leftCmd - 90;           // -90 to +90
  int rightThrottle = rightCmd - 90;
  
  // Scale to full motor range (you can adjust the map range if you want less aggressive power)
  int leftSpeed = map(leftThrottle, -90, 90, -255, 255);
  int rightSpeed = map(rightThrottle, -90, 90, -255, 255);
  
  driveMotor(L_IN1, L_IN2, leftSpeed);
  driveMotor(R_IN1, R_IN2, rightSpeed);
}

// ====================== SETUP ======================
void setup() {
  Serial.begin(115200);
  delay(200);

  // === Motor Driver Pins ===
  pinMode(L_IN1, OUTPUT);
  pinMode(L_IN2, OUTPUT);
  pinMode(R_IN1, OUTPUT);
  pinMode(R_IN2, OUTPUT);
  
  // Stop motors at start
  driveMotor(L_IN1, L_IN2, 0);
  driveMotor(R_IN1, R_IN2, 0);

  // === Encoders ===
  encoderLeft.attachHalfQuad(ENC_L_A, ENC_L_B);
  encoderRight.attachHalfQuad(ENC_R_A, ENC_R_B);
  encoderLeft.clearCount();
  encoderRight.clearCount();

  // === Weapon Servo ===
  weaponServo.attach(WEAPON_SERVO_PIN, 500, 2400);
  weaponServo.write(90);

  // === RGB ===
  pinMode(RGB_LED, OUTPUT);
  rgbOff();

  Serial.println("\n=== AntBot-S3 N20 + DRV8833 + Weapon ===");

  WiFi.softAP(botSSID.c_str(), apPassword);
  Serial.print("AP SSID: ");
  Serial.println(botSSID);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/drive", handleDrive);
  server.on("/activate", handleActivate);
  server.on("/status", handleStatus);

  server.begin();
}

void loop() {
  server.handleClient();

  // FAILSAFE
  if (botActive && (millis() - lastInputTime > FAILSAFE_TIMEOUT)) {
    Serial.println("FAILSAFE TRIGGERED");
    driveMotor(L_IN1, L_IN2, 0);
    driveMotor(R_IN1, R_IN2, 0);
    weaponServo.write(90);
    weaponPos = 90;
    leftPos = rightPos = 90;
    rgbOff();
    botActive = false;
  }

  // Optional: Print encoder counts for debugging (remove or slow down later)
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 500) {
    Serial.printf("Enc L: %d | R: %d\n", 
                  (int)encoderLeft.getCount(), 
                  (int)encoderRight.getCount());
    lastPrint = millis();
  }
}

// RGB
void rgbOff() { neopixelWrite(RGB_LED, 0, 0, 0); }
void setRGB(uint8_t r, uint8_t g, uint8_t b) { neopixelWrite(RGB_LED, r, g, b); }

void updateRGB() {
  int ls = leftPos - 90;
  int rs = rightPos - 90;
  if (ls > 15 && rs > 15)       setRGB(0, 255, 0);
  else if (ls < -15 && rs < -15) setRGB(255, 0, 0);
  else if (abs(ls - rs) > 15)   setRGB(255, 0, 255);
  else                          rgbOff();
}

// ====================== WEB PAGE ======================
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
    "  </style>\n"
    "</head>\n"
    "<body>\n"
    "  <h1>" + botSSID + "</h1>\n"
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
    "\n"
    "    function activateBot() {\n"
    "      fetch('/activate').then(() => {\n"
    "        botActive = true;\n"
    "        document.getElementById('activateScreen').classList.add('hidden');\n"
    "        document.getElementById('controlScreen').classList.remove('hidden');\n"
    "      });\n"
    "    }\n"
    "\n"
    "    function checkStatus() {\n"
    "      fetch('/status').then(r => r.json()).then(data => {\n"
    "        if (!data.active && botActive) {\n"
    "          botActive = false;\n"
    "          document.getElementById('controlScreen').classList.add('hidden');\n"
    "          document.getElementById('activateScreen').classList.remove('hidden');\n"
    "        }\n"
    "      });\n"
    "    }\n"
    "\n"
    "    function moveKnob(clientX, clientY) {\n"
    "      if (!botActive) return;\n"
    "      isDragging = true;\n"
    "      const rect = base.getBoundingClientRect();\n"
    "      let x = clientX - rect.left - 115;\n"
    "      let y = clientY - rect.top - 115;\n"
    "      const dist = Math.sqrt(x*x + y*y);\n"
    "      if (dist > 115) { x = (x/dist)*115; y = (y/dist)*115; }\n"
    "      knob.style.transform = `translate(${x-37.5}px, ${y-37.5}px)`;\n"
    "      currentX = x / 115;\n"
    "      currentY = -y / 115;\n"
    "      sendDrive();\n"
    "    }\n"
    "\n"
    "    function resetKnob() {\n"
    "      isDragging = false;\n"
    "      knob.style.transform = 'translate(-37.5px, -37.5px)';\n"
    "      currentX = currentY = 0;\n"
    "      sendDrive();\n"
    "    }\n"
    "\n"
    "    function sendDrive() {\n"
    "      if (!botActive) return;\n"
    "      const now = Date.now();\n"
    "      if (now - lastSend < 40) return;   // Fast updates while holding\n"
    "      lastSend = now;\n"
    "\n"
    "      let forward = currentY * 90;\n"
    "      let steer = currentX * 65;          // Slightly stronger steering\n"
    "      let left = Math.round(90 + forward + steer);\n"
    "      let right = Math.round(90 + forward - steer);\n"
    "      left = Math.max(0, Math.min(180, left));\n"
    "      right = Math.max(0, Math.min(180, right));\n"
    "\n"
    "      fetch(`/drive?left=${left}&right=${right}&weapon=${weaponPos}`);\n"
    "    }\n"
    "\n"
    "    function updateWeapon(val) {\n"
    "      if (!botActive) return;\n"
    "      weaponPos = parseInt(val);\n"
    "      document.getElementById('weaponValue').innerText = weaponPos;\n"
    "      sendDrive();\n"
    "    }\n"
    "\n"
    "    function stopAll() {\n"
    "      fetch('/drive?left=90&right=90&weapon=90');\n"
    "      resetKnob();\n"
    "      document.getElementById('weaponSlider').value = 90;\n"
    "      document.getElementById('weaponValue').innerText = 90;\n"
    "    }\n"
    "\n"
    "    // Continuous sending while finger is down\n"
    "    setInterval(() => {\n"
    "      if (isDragging && botActive) sendDrive();\n"
    "    }, 45);\n"
    "\n"
    "    // Touch events\n"
    "    base.addEventListener('touchstart', e => { e.preventDefault(); moveKnob(e.touches[0].clientX, e.touches[0].clientY); });\n"
    "    base.addEventListener('touchmove',  e => { e.preventDefault(); moveKnob(e.touches[0].clientX, e.touches[0].clientY); });\n"
    "    base.addEventListener('touchend',   resetKnob);\n"
    "\n"
    "    // Mouse support\n"
    "    base.addEventListener('mousedown', e => {\n"
    "      const move = ev => moveKnob(ev.clientX, ev.clientY);\n"
    "      document.addEventListener('mousemove', move);\n"
    "      document.addEventListener('mouseup', () => { document.removeEventListener('mousemove', move); resetKnob(); }, {once:true});\n"
    "    });\n"
    "\n"
    "    setInterval(checkStatus, 800);\n"
    "  </script>\n"
    "</body>\n"
    "</html>";

  server.send(200, "text/html", html);
}

void handleActivate() {
  botActive = true;
  lastInputTime = millis();
  Serial.println("Bot ACTIVATED");
  server.send(200, "text/plain", "ACTIVATED");
}

void handleStatus() {
  String json = "{\"active\":" + String(botActive ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void handleDrive() {
  if (!botActive) {
    server.send(200, "text/plain", "NOT_ACTIVE");
    return;
  }

  lastInputTime = millis();

  if (server.hasArg("left")) {
    leftPos = constrain(server.arg("left").toInt(), 0, 180);
  }
  if (server.hasArg("right")) {
    rightPos = constrain(server.arg("right").toInt(), 0, 180);
  }
  if (server.hasArg("weapon")) {
    weaponPos = constrain(server.arg("weapon").toInt(), 0, 180);
    weaponServo.write(weaponPos);
  }

  // Drive the N20 motors using the same 0-180 values the joystick sends
  setDrive(leftPos, rightPos);

  Serial.printf("Drive -> L:%d R:%d W:%d | EncL:%d EncR:%d\n", 
                leftPos, rightPos, weaponPos,
                (int)encoderLeft.getCount(), (int)encoderRight.getCount());

  updateRGB();

  server.send(200, "text/plain", "OK");
}