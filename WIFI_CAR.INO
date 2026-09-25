#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <esp_wifi.h>

// ====================== PINS ======================
#define IN1 2
#define IN2 3
#define IN3 6
#define IN4 7

#define FRONT_LED1 0
#define FRONT_LED2 1
#define BACK_LED1  4
#define BACK_LED2  5

#define TRIG_PIN 9
#define ECHO_PIN 10
#define BATTERY_PIN 8

// ====================== WiFi ======================
const char* ssid     = "WIFI_NAME";
const char* password = "WIFI_PASSWORD";

const char* ap_ssid  = "ESP32-Car";
const char* ap_pass  = "12345678";

// ====================== State ======================
WebServer server(80);

int  leftSpeed     = 180;
int  rightSpeed    = 180;
bool autoPilot     = false;
bool ultrasonicOn  = true;
int  safeDistance  = 25; // cm

// Web UI Inversion toggles (Default F/B & L/R can be flipped here)
bool invertFB = false;
bool invertLR = false;

// Individual LED states
bool ledF1 = false;
bool ledF2 = false;
bool ledB1 = false;
bool ledB2 = false;

unsigned long lastCmdMs = 0;
const unsigned long CMD_TIMEOUT = 1200; // ms

bool motorsRunning = false;
bool isMovingForward = false; // සත්‍ය වශයෙන්ම ඉදිරියට යනවාද යන්න

// LED Pattern system
enum LedPattern { LED_OFF = 0, LED_ON, LED_BLINK, LED_ALTERNATE, LED_CHASE, LED_STROBE };
LedPattern currentLedPattern = LED_OFF;
unsigned long lastLedMs = 0;
int ledStep = 0;

// Movement Pattern system
struct MoveStep {
  char dir; // F B L R S
  int  durationMs;
};

#define MAX_PATTERN_STEPS 16
MoveStep currentPattern[MAX_PATTERN_STEPS];
int patternLen = 0;
int patternIdx = 0;
unsigned long patternStepStart = 0;
bool patternRunning = false;
int selectedPatternId = -1;

// Predefined patterns
const MoveStep PAT_SQUARE[] = {
  {'F', 1200}, {'R', 600}, {'F', 1200}, {'R', 600},
  {'F', 1200}, {'R', 600}, {'F', 1200}, {'R', 600}, {'S', 500}
};
const MoveStep PAT_CIRCLE_LEFT[] = {
  {'L', 300}, {'F', 400}, {'L', 300}, {'F', 400},
  {'L', 300}, {'F', 400}, {'L', 300}, {'F', 400}, {'S', 400}
};
const MoveStep PAT_CIRCLE_RIGHT[] = {
  {'R', 300}, {'F', 400}, {'R', 300}, {'F', 400},
  {'R', 300}, {'F', 400}, {'R', 300}, {'F', 400}, {'S', 400}
};
const MoveStep PAT_ZIGZAG[] = {
  {'F', 800}, {'L', 500}, {'F', 800}, {'R', 500},
  {'F', 800}, {'L', 500}, {'F', 800}, {'R', 500}, {'S', 400}
};
const MoveStep PAT_SPIN_LEFT[]  = {{'L', 2000}, {'S', 500}};
const MoveStep PAT_SPIN_RIGHT[] = {{'R', 2000}, {'S', 500}};
const MoveStep PAT_BACK_FORTH[] = {
  {'F', 1000}, {'S', 300}, {'B', 1000}, {'S', 300},
  {'F', 1000}, {'S', 300}, {'B', 1000}, {'S', 400}
};

// ====================== ULTRASONIC SENSOR ======================
long getDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 25000);
  if (duration == 0) return 999;
  long d = duration * 0.034 / 2;
  if (d < 2) return 999;
  return d;
}

// ====================== MOTOR HARDWARE CONTROL ======================
void stopMotors() {
  analogWrite(IN1, 0);
  analogWrite(IN2, 0);
  analogWrite(IN3, 0);
  analogWrite(IN4, 0);
  motorsRunning = false;
  isMovingForward = false;
}

// සත්‍ය Physical Motors ධාවනය කිරීම:
// ඔබේ Car එකේ Pins අනුව Forward සහ Backward මාරු කර නිවැරදි කර ඇත.
void motorRawForward() {
  analogWrite(IN1, 0);
  analogWrite(IN2, leftSpeed);
  analogWrite(IN3, 0);
  analogWrite(IN4, rightSpeed);
  motorsRunning = true;
  isMovingForward = true; // ඉදිරියට ගමන් කරයි (Ultrasonic Protection Active)
}

void motorRawBackward() {
  analogWrite(IN1, leftSpeed);
  analogWrite(IN2, 0);
  analogWrite(IN3, rightSpeed);
  analogWrite(IN4, 0);
  motorsRunning = true;
  isMovingForward = false; // පසුපසට යයි (Ultrasonic Protection OFF)
}

void motorRawTurnLeft() {
  analogWrite(IN1, 0);
  analogWrite(IN2, 0);
  analogWrite(IN3, 0);
  analogWrite(IN4, rightSpeed);
  motorsRunning = true;
  isMovingForward = false;
}

void motorRawTurnRight() {
  analogWrite(IN1, 0);
  analogWrite(IN2, leftSpeed);
  analogWrite(IN3, 0);
  analogWrite(IN4, 0);
  motorsRunning = true;
  isMovingForward = false;
}

// Web එකෙන් හෝ Autopilot එකෙන් දෙන command එක Invert සැකසුම් සමඟ ගැලපීම
char resolveDirection(char requestedDir) {
  char d = requestedDir;
  if (invertFB) {
    if (d == 'F') d = 'B';
    else if (d == 'B') d = 'F';
  }
  if (invertLR) {
    if (d == 'L') d = 'R';
    else if (d == 'R') d = 'L';
  }
  return d;
}

// වාහනය ධාවනය කිරීම සහ Forward එකට පමණක් Ultrasonic ආරක්ෂාව සැපයීම
bool driveCar(char userDir) {
  if (userDir == 'S') {
    stopMotors();
    return true;
  }

  char actualDir = resolveDirection(userDir);

  // කාර් එක ඉදිරියට (Forward) යනවා නම් පමණක් Ultrasonic Sensor එක පරීක්ෂා කෙරේ
  if (actualDir == 'F') {
    if (ultrasonicOn) {
      long dist = getDistance();
      if (dist < safeDistance) {
        stopMotors();
        return false; // බාධකයක් ඇත, නතර කළා
      }
    }
    motorRawForward();
  }
  else if (actualDir == 'B') {
    motorRawBackward(); // Backward යනවිට Ultrasonic බාධාවක් නැත!
  }
  else if (actualDir == 'L') {
    motorRawTurnLeft();
  }
  else if (actualDir == 'R') {
    motorRawTurnRight();
  }
  
  lastCmdMs = millis();
  return true;
}

// Loop එකේදී නිරන්තරයෙන් ඉදිරියට යන විට පමණක් බාධක නිරීක්ෂණය කිරීම
void checkSafety() {
  if (!isMovingForward || !ultrasonicOn) return;
  long dist = getDistance();
  if (dist < safeDistance) {
    stopMotors();
  }
}

// ====================== LED CONTROL ======================
void setHardwareLeds(bool f1, bool f2, bool b1, bool b2) {
  digitalWrite(FRONT_LED1, f1 ? HIGH : LOW);
  digitalWrite(FRONT_LED2, f2 ? HIGH : LOW);
  digitalWrite(BACK_LED1,  b1 ? HIGH : LOW);
  digitalWrite(BACK_LED2,  b2 ? HIGH : LOW);
}

void updateManualLeds() {
  if (currentLedPattern == LED_OFF) {
    setHardwareLeds(ledF1, ledF2, ledB1, ledB2);
  }
}

void runLedPattern() {
  if (currentLedPattern == LED_OFF) {
    updateManualLeds();
    return;
  }

  unsigned long now = millis();
  switch (currentLedPattern) {
    case LED_ON:
      setHardwareLeds(true, true, true, true);
      break;
    case LED_BLINK:
      if (now - lastLedMs > 350) {
        lastLedMs = now;
        ledStep = !ledStep;
        setHardwareLeds(ledStep, ledStep, ledStep, ledStep);
      }
      break;
    case LED_ALTERNATE:
      if (now - lastLedMs > 300) {
        lastLedMs = now;
        ledStep = !ledStep;
        setHardwareLeds(ledStep, ledStep, !ledStep, !ledStep);
      }
      break;
    case LED_CHASE:
      if (now - lastLedMs > 180) {
        lastLedMs = now;
        ledStep = (ledStep + 1) % 4;
        setHardwareLeds(ledStep == 0, ledStep == 1, ledStep == 2, ledStep == 3);
      }
      break;
    case LED_STROBE:
      if (now - lastLedMs > 140) {
        lastLedMs = now;
        ledStep = !ledStep;
        setHardwareLeds(ledStep, !ledStep, !ledStep, ledStep);
      }
      break;
    default:
      break;
  }
}

// ====================== BATTERY ======================
int getBatteryPercent() {
  long sum = 0;
  for (int i = 0; i < 8; i++) {
    sum += analogRead(BATTERY_PIN);
    delay(2);
  }
  float raw = sum / 8.0;
  float voltage = (raw / 4095.0) * 3.3 * 2.0;
  int pct = constrain((int)((voltage - 6.0) / (8.4 - 6.0) * 100.0), 0, 100);
  return pct;
}

// ====================== AUTOPILOT ======================
void runAutoPilot() {
  long dist = getDistance();

  if (dist < safeDistance) {
    // බාධකයක් හමුවූ වහාම නතර වී ආපසු හැරීම
    stopMotors();
    delay(100);

    // පසුපසට (Reverse)
    driveCar('B');
    delay(350);
    stopMotors();
    delay(80);

    // අහඹු ලෙස වමට හෝ දකුණට හැරීම
    static bool turnSide = false;
    turnSide = !turnSide;
    if (turnSide) driveCar('L');
    else driveCar('R');
    delay(550 + random(0, 200));

    stopMotors();
    delay(80);
  } else {
    // බාධකයක් නැතිනම් ඉදිරියට (Forward)
    if (dist < safeDistance + 15) {
      int tmpL = leftSpeed, tmpR = rightSpeed;
      leftSpeed  = constrain(leftSpeed  - 40, 80, 255);
      rightSpeed = constrain(rightSpeed - 40, 80, 255);
      driveCar('F');
      leftSpeed  = tmpL;
      rightSpeed = tmpR;
    } else {
      driveCar('F');
    }
  }
}

// ====================== MOVEMENT PATTERNS ======================
void loadPattern(const MoveStep* src, int len) {
  patternLen = min(len, MAX_PATTERN_STEPS);
  for (int i = 0; i < patternLen; i++) {
    currentPattern[i] = src[i];
  }
  patternIdx = 0;
  patternStepStart = millis();
  patternRunning = true;
  autoPilot = false;
  stopMotors();
}

void stopPattern() {
  patternRunning = false;
  patternIdx = 0;
  stopMotors();
}

void runPattern() {
  if (!patternRunning || patternLen == 0) return;

  unsigned long now = millis();
  MoveStep& step = currentPattern[patternIdx];

  if (now - patternStepStart >= (unsigned long)step.durationMs) {
    patternIdx++;
    if (patternIdx >= patternLen) patternIdx = 0;
    patternStepStart = now;
    driveCar(currentPattern[patternIdx].dir);
  } else {
    driveCar(step.dir);
  }

  // Pattern එකෙන් ඉදිරියට යනවිට Ultrasonic බාධකයක් හමුවුවහොත්
  if (isMovingForward && ultrasonicOn) {
    long dist = getDistance();
    if (dist < safeDistance) {
      stopMotors();
      delay(100);
      driveCar('B'); // Reverse
      delay(300);
      stopMotors();
      patternStepStart = millis();
    }
  }
}

// ====================== WEB UI ======================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>ESP32 Smart Car Controller</title>
<style>
  :root {
    --bg: #0b1220;
    --card: #131d31;
    --card-border: rgba(255, 255, 255, 0.08);
    --accent: #38bdf8;
    --green: #22c55e;
    --red: #ef4444;
    --orange: #f59e0b;
    --purple: #a855f7;
    --text: #f1f5f9;
    --dim: #94a3b8;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; user-select: none; -webkit-user-select: none; }
  body {
    font-family: system-ui, -apple-system, sans-serif;
    background: radial-gradient(ellipse at top, #182848 0%, var(--bg) 75%);
    color: var(--text);
    min-height: 100vh;
    padding: 10px 10px 75px 10px;
    display: flex;
    flex-direction: column;
    align-items: center;
  }
  .wrap { width: 100%; max-width: 440px; }
  
  header {
    background: var(--card);
    border-radius: 16px;
    padding: 12px 16px;
    margin-bottom: 12px;
    border: 1px solid var(--card-border);
    box-shadow: 0 4px 18px rgba(0,0,0,0.35);
  }
  .header-top { display: flex; justify-content: space-between; align-items: center; }
  .title { font-size: 1.15rem; font-weight: 800; color: var(--accent); letter-spacing: 0.5px; }
  .status-badge { font-size: 0.75rem; background: rgba(56,189,248,0.15); color: var(--accent); padding: 3px 8px; border-radius: 8px; font-weight: 600; }
  .stats { display: flex; justify-content: space-between; margin-top: 10px; gap: 8px; }
  .stat {
    flex: 1;
    background: rgba(0,0,0,0.3);
    border-radius: 10px;
    padding: 8px 4px;
    text-align: center;
    border: 1px solid rgba(255,255,255,0.03);
  }
  .stat label { font-size: 0.65rem; color: var(--dim); display: block; text-transform: uppercase; font-weight: 600; }
  .stat value { font-size: 1rem; font-weight: 700; color: #fff; }
  .bars { display: flex; gap: 3px; justify-content: center; align-items: flex-end; height: 16px; margin-top: 3px; }
  .bar { width: 5px; background: #334155; border-radius: 2px; }
  .bar.on { background: var(--green); }

  .nav-bar {
    position: fixed;
    bottom: 0;
    left: 0;
    right: 0;
    height: 60px;
    background: #0f172a;
    border-top: 1px solid rgba(255,255,255,0.1);
    display: flex;
    justify-content: space-around;
    align-items: center;
    z-index: 999;
    padding: 0 10px;
  }
  .nav-item {
    flex: 1;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    color: var(--dim);
    font-size: 0.7rem;
    font-weight: 600;
    cursor: pointer;
    height: 100%;
    transition: all 0.2s;
  }
  .nav-item .icon { font-size: 1.25rem; margin-bottom: 2px; }
  .nav-item.active { color: var(--accent); }

  .tab-content { display: none; width: 100%; animation: fadeIn 0.2s ease-in-out; }
  .tab-content.active { display: block; }
  @keyframes fadeIn { from { opacity: 0; transform: translateY(4px); } to { opacity: 1; transform: translateY(0); } }

  .card {
    background: var(--card);
    border-radius: 16px;
    padding: 16px;
    margin-bottom: 12px;
    border: 1px solid var(--card-border);
    box-shadow: 0 4px 16px rgba(0,0,0,0.25);
  }
  .card-title {
    font-size: 0.85rem;
    font-weight: 700;
    color: var(--dim);
    text-transform: uppercase;
    margin-bottom: 12px;
  }

  .dpad {
    display: grid;
    grid-template-columns: 1fr 1fr 1fr;
    grid-template-rows: 76px 76px 76px;
    gap: 10px;
    max-width: 270px;
    margin: 0 auto 15px auto;
  }
  .btn {
    border: none;
    border-radius: 16px;
    background: #1e293b;
    color: white;
    font-size: 1.6rem;
    display: flex;
    align-items: center;
    justify-content: center;
    box-shadow: 0 4px 12px rgba(0,0,0,0.4);
    touch-action: manipulation;
  }
  .btn:active, .btn.pressed { background: var(--accent); color: #0b1220; transform: scale(0.93); }
  .btn-stop { background: #991b1b; font-size: 0.95rem; font-weight: 800; }
  .btn-stop:active { background: var(--red); }
  .btn-up { grid-column: 2; grid-row: 1; }
  .btn-left { grid-column: 1; grid-row: 2; }
  .btn-stop { grid-column: 2; grid-row: 2; }
  .btn-right { grid-column: 3; grid-row: 2; }
  .btn-down { grid-column: 2; grid-row: 3; }

  .grid-2 { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin-top: 8px; }
  .action-btn {
    height: 42px;
    border: none;
    border-radius: 10px;
    background: #24324d;
    color: white;
    font-size: 0.8rem;
    font-weight: 700;
    cursor: pointer;
    transition: background 0.15s, transform 0.1s;
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 4px;
  }
  .action-btn:active { transform: scale(0.96); }
  .action-btn.on { background: var(--orange); color: #000; }
  .action-btn.auto.on { background: var(--green); color: #000; }
  .action-btn.ultra.on { background: var(--purple); color: #fff; }
  .action-btn.invert.on { background: #0284c7; color: #fff; border: 1px solid #38bdf8; }

  .slider-box { margin-top: 14px; }
  .slider-box label { font-size: 0.78rem; color: var(--dim); display: flex; justify-content: space-between; font-weight: 600; }
  input[type=range] { width: 100%; margin-top: 6px; accent-color: var(--accent); }

  .pat-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin-top: 8px; }
  .pbtn {
    height: 38px;
    border: none;
    border-radius: 8px;
    background: #1e293b;
    color: white;
    font-size: 0.75rem;
    font-weight: 600;
  }
  .pbtn.active { background: var(--accent); color: #0b1220; font-weight: 800; }

  .ota-box input[type=file] { width: 100%; margin: 12px 0; font-size: 0.8rem; color: var(--dim); }
  .ota-box button {
    width: 100%;
    height: 44px;
    border: none;
    border-radius: 10px;
    background: var(--accent);
    color: #0b1220;
    font-weight: 800;
    font-size: 0.9rem;
  }
  progress { width: 100%; height: 8px; margin-top: 10px; border-radius: 4px; }
  .hint { font-size: 0.7rem; color: var(--dim); margin-top: 8px; text-align: center; }
</style>
</head>
<body>

<div class="wrap">
  <header>
    <div class="header-top">
      <div class="title">ESP32 CAR</div>
      <div class="status-badge" id="status">Connecting...</div>
    </div>
    <div class="stats">
      <div class="stat">
        <label>Battery</label>
        <value id="bat">--%</value>
      </div>
      <div class="stat">
        <label>Distance</label>
        <value id="dist">-- cm</value>
      </div>
      <div class="stat">
        <label>Signal</label>
        <div class="bars" id="bars">
          <div class="bar" style="height:5px"></div>
          <div class="bar" style="height:8px"></div>
          <div class="bar" style="height:12px"></div>
          <div class="bar" style="height:16px"></div>
        </div>
        <div style="font-size:0.6rem;color:var(--dim);margin-top:2px" id="rssiVal">-- dBm</div>
      </div>
    </div>
  </header>

  <!-- TAB 1: DRIVE -->
  <div id="tab-drive" class="tab-content active">
    <div class="card">
      <div class="card-title">Manual Drive</div>
      <div class="dpad">
        <button class="btn btn-up" data-dir="F">⬆</button>
        <button class="btn btn-left" data-dir="L">⬅</button>
        <button class="btn btn-stop" onclick="send('S')">STOP</button>
        <button class="btn btn-right" data-dir="R">➡</button>
        <button class="btn btn-down" data-dir="B">⬇</button>
      </div>

      <div class="grid-2">
        <button class="action-btn ultra on" id="ultraBtn" onclick="toggleUltra()">Ultrasonic: ON</button>
        <button class="action-btn auto" id="autoBtn" onclick="toggleAuto()">Autopilot: OFF</button>
      </div>

      <div class="slider-box">
        <label>Safe Obstacle Distance: <b id="safeVal">25 cm</b></label>
        <input type="range" min="15" max="50" value="25" id="safeSlider"
               oninput="document.getElementById('safeVal').innerText=this.value+' cm'"
               onchange="setSafe(this.value)">
      </div>
    </div>
  </div>

  <!-- TAB 2: MOTORS -->
  <div id="tab-motors" class="tab-content">
    <div class="card">
      <div class="card-title">Direction Mapping (දිශාවන් මාරු කිරීම)</div>
      <p style="font-size:0.75rem; color:var(--dim); margin-bottom:10px;">
        Forward/Backward හෝ Left/Right අවශ්‍ය නම් මාරු කර ගන්න:
      </p>
      <div class="grid-2">
        <button class="action-btn invert" id="invFbBtn" onclick="toggleInvert('fb')">Invert F/B</button>
        <button class="action-btn invert" id="invLrBtn" onclick="toggleInvert('lr')">Invert L/R</button>
      </div>
      <div class="hint" style="text-align:left; margin-top:8px;">
        * සටහන: Car එක ඉදිරියට (Forward) ධාවනය වන විට පමණක් Ultrasonic Sensor ආරක්ෂාව ක්‍රියාත්මක වේ. Backward (පසුපසට) යනවිට බාධාවක් නොවේ.
      </div>
    </div>

    <div class="card">
      <div class="card-title">Speed Sliders</div>
      <div class="slider-box">
        <label>Left Motor Speed: <b id="leftVal">180</b></label>
        <input type="range" min="80" max="255" value="180" id="leftSlider"
               oninput="document.getElementById('leftVal').innerText=this.value"
               onchange="setSpeed('L',this.value)">
      </div>
      <div class="slider-box">
        <label>Right Motor Speed: <b id="rightVal">180</b></label>
        <input type="range" min="80" max="255" value="180" id="rightSlider"
               oninput="document.getElementById('rightVal').innerText=this.value"
               onchange="setSpeed('R',this.value)">
      </div>
    </div>

    <div class="card">
      <div class="card-title">Auto Movement Patterns</div>
      <div class="pat-grid">
        <button class="pbtn" data-id="0" onclick="startPat(0)">Square</button>
        <button class="pbtn" data-id="1" onclick="startPat(1)">ZigZag</button>
        <button class="pbtn" data-id="2" onclick="startPat(2)">Circle L</button>
        <button class="pbtn" data-id="3" onclick="startPat(3)">Circle R</button>
        <button class="pbtn" data-id="4" onclick="startPat(4)">Spin Left</button>
        <button class="pbtn" data-id="5" onclick="startPat(5)">Spin Right</button>
        <button class="pbtn" data-id="6" onclick="startPat(6)">Back-Forth</button>
        <button class="pbtn" style="background:#7f1d1d;color:#fff" onclick="stopPat()">STOP Pattern</button>
      </div>
    </div>
  </div>

  <!-- TAB 3: LEDS -->
  <div id="tab-leds" class="tab-content">
    <div class="card">
      <div class="card-title">Individual 4-LED Control</div>
      <p style="font-size:0.75rem; color:var(--dim); margin-bottom:10px;">
        LED 4ම වෙන වෙනම Control කරන්න:
      </p>
      <div class="grid-2">
        <button class="action-btn" id="f1Btn" onclick="toggleLed('f1')">Front L1</button>
        <button class="action-btn" id="f2Btn" onclick="toggleLed('f2')">Front L2</button>
        <button class="action-btn" id="b1Btn" onclick="toggleLed('b1')">Back L1</button>
        <button class="action-btn" id="b2Btn" onclick="toggleLed('b2')">Back L2</button>
      </div>
      <div class="grid-2" style="margin-top:10px;">
        <button class="action-btn" onclick="toggleLed('all_on')">All LEDs ON</button>
        <button class="action-btn" onclick="toggleLed('all_off')">All LEDs OFF</button>
      </div>
    </div>

    <div class="card">
      <div class="card-title">Lighting Patterns</div>
      <div class="grid-2">
        <button class="action-btn lbtn" data-pat="0" onclick="setLedPat(0)">Static (Off)</button>
        <button class="action-btn lbtn" data-pat="1" onclick="setLedPat(1)">Full Lights</button>
        <button class="action-btn lbtn" data-pat="2" onclick="setLedPat(2)">All Blink</button>
        <button class="action-btn lbtn" data-pat="3" onclick="setLedPat(3)">Alternate F/B</button>
        <button class="action-btn lbtn" data-pat="4" onclick="setLedPat(4)">Chase (Circle)</button>
        <button class="action-btn lbtn" data-pat="5" onclick="setLedPat(5)">Strobe / Police</button>
      </div>
    </div>
  </div>

  <!-- TAB 4: OTA -->
  <div id="tab-ota" class="tab-content">
    <div class="card ota-box">
      <div class="card-title">OTA Firmware Flash</div>
      <p style="font-size:0.75rem; color:var(--dim);">
        Choose your compiled <code>.bin</code> file and flash wirelessly.
      </p>
      <input type="file" id="binFile" accept=".bin">
      <button onclick="doOTA()">Upload & Flash</button>
      <progress id="prog" value="0" max="100" style="display:none"></progress>
      <div class="hint" id="otaMsg">Select .bin file to begin</div>
    </div>
  </div>
</div>

<nav class="nav-bar">
  <div class="nav-item active" onclick="switchTab('tab-drive', this)">
    <div class="icon">🎮</div>
    <div>Drive</div>
  </div>
  <div class="nav-item" onclick="switchTab('tab-motors', this)">
    <div class="icon">⚙️</div>
    <div>Motors</div>
  </div>
  <div class="nav-item" onclick="switchTab('tab-leds', this)">
    <div class="icon">💡</div>
    <div>LEDs</div>
  </div>
  <div class="nav-item" onclick="switchTab('tab-ota', this)">
    <div class="icon">🚀</div>
    <div>OTA</div>
  </div>
</nav>

<script>
let autoOn = false;
let ultraOn = true;
let holdTimer = null;

function switchTab(tabId, el) {
  document.querySelectorAll('.tab-content').forEach(tc => tc.classList.remove('active'));
  document.querySelectorAll('.nav-item').forEach(ni => ni.classList.remove('active'));
  document.getElementById(tabId).classList.add('active');
  el.classList.add('active');
}

function send(cmd) {
  if (autoOn && cmd !== 'S') return;
  fetch('/move?dir=' + cmd).catch(()=>{});
}

function startHold(dir) {
  send(dir);
  if (holdTimer) clearInterval(holdTimer);
  holdTimer = setInterval(() => send(dir), 180);
}
function stopHold() {
  if (holdTimer) { clearInterval(holdTimer); holdTimer = null; }
  send('S');
}

document.querySelectorAll('[data-dir]').forEach(btn => {
  const dir = btn.dataset.dir;
  btn.addEventListener('pointerdown', e => { e.preventDefault(); btn.classList.add('pressed'); startHold(dir); });
  ['pointerup','pointerleave','pointercancel'].forEach(ev => {
    btn.addEventListener(ev, () => { btn.classList.remove('pressed'); stopHold(); });
  });
});

function toggleLed(id) { fetch('/led?id=' + id); }
function toggleInvert(axis) { fetch('/invert?axis=' + axis); }

function toggleUltra() {
  fetch('/ultra').then(r => r.text()).then(t => {
    ultraOn = (t === '1');
    const b = document.getElementById('ultraBtn');
    b.innerText = ultraOn ? 'Ultrasonic: ON' : 'Ultrasonic: OFF';
    b.classList.toggle('on', ultraOn);
  });
}

function toggleAuto() {
  fetch('/auto').then(r => r.text()).then(t => {
    autoOn = (t === '1');
    const b = document.getElementById('autoBtn');
    b.innerText = autoOn ? 'Autopilot: ON' : 'Autopilot: OFF';
    b.classList.toggle('on', autoOn);
  });
}

function setSafe(v) { fetch('/safe?val=' + v); }
function setSpeed(side, v) { fetch('/speed?side=' + side + '&val=' + v); }

function setLedPat(id) {
  fetch('/ledpat?id=' + id);
  document.querySelectorAll('.lbtn').forEach(b => {
    b.classList.toggle('on', parseInt(b.dataset.pat) === id);
  });
}

function startPat(id) {
  fetch('/pattern?id=' + id);
  document.querySelectorAll('.pbtn[data-id]').forEach(b => {
    b.classList.toggle('active', parseInt(b.dataset.id) === id);
  });
}
function stopPat() {
  fetch('/pattern?id=-1');
  document.querySelectorAll('.pbtn[data-id]').forEach(b => b.classList.remove('active'));
}

function updateBars(rssi) {
  const bars = document.querySelectorAll('#bars .bar');
  let level = 0;
  if (rssi >= -50) level = 4;
  else if (rssi >= -60) level = 3;
  else if (rssi >= -70) level = 2;
  else if (rssi >= -80) level = 1;
  bars.forEach((b, i) => b.classList.toggle('on', i < level));
  document.getElementById('rssiVal').innerText = rssi + ' dBm';
}

setInterval(() => {
  fetch('/status').then(r => r.json()).then(d => {
    document.getElementById('status').innerText = d.ip;
    document.getElementById('dist').innerText = d.dist + ' cm';
    document.getElementById('bat').innerText = d.bat + '%';
    
    document.getElementById('safeVal').innerText = d.safe + ' cm';
    document.getElementById('safeSlider').value = d.safe;
    document.getElementById('leftVal').innerText = d.left;
    document.getElementById('leftSlider').value = d.left;
    document.getElementById('rightVal').innerText = d.right;
    document.getElementById('rightSlider').value = d.right;
    updateBars(d.rssi);

    document.getElementById('ultraBtn').classList.toggle('on', d.ultra);
    document.getElementById('ultraBtn').innerText = d.ultra ? 'Ultrasonic: ON' : 'Ultrasonic: OFF';
    document.getElementById('autoBtn').classList.toggle('on', d.auto);
    document.getElementById('autoBtn').innerText = d.auto ? 'Autopilot: ON' : 'Autopilot: OFF';
    autoOn = d.auto;
    ultraOn = d.ultra;

    // Invert button sync
    const invFb = document.getElementById('invFbBtn');
    invFb.classList.toggle('on', d.invFB);
    invFb.innerText = d.invFB ? 'Invert F/B: ON (Swapped)' : 'Invert F/B: OFF (Normal)';

    const invLr = document.getElementById('invLrBtn');
    invLr.classList.toggle('on', d.invLR);
    invLr.innerText = d.invLR ? 'Invert L/R: ON (Swapped)' : 'Invert L/R: OFF (Normal)';

    // Individual LED button sync
    document.getElementById('f1Btn').classList.toggle('on', d.f1);
    document.getElementById('f2Btn').classList.toggle('on', d.f2);
    document.getElementById('b1Btn').classList.toggle('on', d.b1);
    document.getElementById('b2Btn').classList.toggle('on', d.b2);

    document.querySelectorAll('.lbtn').forEach(b => {
      b.classList.toggle('on', parseInt(b.dataset.pat) === d.ledpat);
    });
  }).catch(() => {
    document.getElementById('status').innerText = 'Offline';
  });
}, 900);

function doOTA() {
  const f = document.getElementById('binFile').files[0];
  if (!f) { document.getElementById('otaMsg').innerText = 'Please select a .bin file'; return; }
  const prog = document.getElementById('prog');
  prog.style.display = 'block';
  prog.value = 0;
  document.getElementById('otaMsg').innerText = 'Uploading firmware...';

  const fd = new FormData();
  fd.append('firmware', f);
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/update');
  xhr.upload.onprogress = e => {
    if (e.lengthComputable) prog.value = Math.round(e.loaded / e.total * 100);
  };
  xhr.onload = () => {
    if (xhr.status === 200) {
      document.getElementById('otaMsg').innerText = 'Done! Restarting ESP32...';
      setTimeout(() => location.reload(), 4000);
    } else {
      document.getElementById('otaMsg').innerText = 'Flash Error: ' + xhr.status;
    }
  };
  xhr.onerror = () => { document.getElementById('otaMsg').innerText = 'Upload failed'; };
  xhr.send(fd);
}
</script>
</body>
</html>
)rawliteral";

// ====================== SETUP ======================
void setup() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  stopMotors();

  Serial.begin(115200);
  delay(200);

  pinMode(FRONT_LED1, OUTPUT);
  pinMode(FRONT_LED2, OUTPUT);
  pinMode(BACK_LED1, OUTPUT);
  pinMode(BACK_LED2, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BATTERY_PIN, INPUT);

  updateManualLeds();

  WiFi.mode(WIFI_AP_STA);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  esp_wifi_set_ps(WIFI_PS_NONE);

  WiFi.softAP(ap_ssid, ap_pass);
  Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());

  WiFi.begin(ssid, password);
  int t = 0;
  while (WiFi.status() != WL_CONNECTED && t < 20) {
    delay(400);
    Serial.print(".");
    t++;
    stopMotors();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nStation IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\nAP Only Mode");
  }

  // ---------- Routes ----------
  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", INDEX_HTML);
  });

  server.on("/move", []() {
    if (autoPilot || patternRunning) {
      server.send(200, "text/plain", "BUSY");
      return;
    }

    String dirStr = server.arg("dir");
    if (dirStr.length() == 0) {
      server.send(400, "text/plain", "BAD_REQ");
      return;
    }

    char rawDir = dirStr.charAt(0);
    bool ok = driveCar(rawDir);

    if (!ok) {
      server.send(200, "text/plain", "OBSTACLE");
    } else {
      server.send(200, "text/plain", "OK");
    }
  });

  server.on("/invert", []() {
    String axis = server.arg("axis");
    if (axis == "fb") invertFB = !invertFB;
    else if (axis == "lr") invertLR = !invertLR;
    server.send(200, "text/plain", "OK");
  });

  server.on("/led", []() {
    String id = server.arg("id");
    currentLedPattern = LED_OFF;

    if (id == "f1") ledF1 = !ledF1;
    else if (id == "f2") ledF2 = !ledF2;
    else if (id == "b1") ledB1 = !ledB1;
    else if (id == "b2") ledB2 = !ledB2;
    else if (id == "all_on") { ledF1 = ledF2 = ledB1 = ledB2 = true; }
    else if (id == "all_off") { ledF1 = ledF2 = ledB1 = ledB2 = false; }

    updateManualLeds();
    server.send(200, "text/plain", "OK");
  });

  server.on("/ledpat", []() {
    int id = server.arg("id").toInt();
    currentLedPattern = (LedPattern)constrain(id, 0, 5);
    ledStep = 0;
    lastLedMs = millis();
    if (currentLedPattern == LED_OFF) updateManualLeds();
    server.send(200, "text/plain", "OK");
  });

  server.on("/ultra", []() {
    ultrasonicOn = !ultrasonicOn;
    server.send(200, "text/plain", ultrasonicOn ? "1" : "0");
  });

  server.on("/auto", []() {
    autoPilot = !autoPilot;
    if (autoPilot) stopPattern();
    else stopMotors();
    lastCmdMs = millis();
    server.send(200, "text/plain", autoPilot ? "1" : "0");
  });

  server.on("/safe", []() {
    safeDistance = constrain(server.arg("val").toInt(), 15, 50);
    server.send(200, "text/plain", "OK");
  });

  server.on("/speed", []() {
    String side = server.arg("side");
    int val = constrain(server.arg("val").toInt(), 80, 255);
    if (side == "L") leftSpeed = val;
    else if (side == "R") rightSpeed = val;
    server.send(200, "text/plain", "OK");
  });

  server.on("/pattern", []() {
    int id = server.arg("id").toInt();
    selectedPatternId = id;
    if (id < 0) {
      stopPattern();
      server.send(200, "text/plain", "STOPPED");
      return;
    }
    switch (id) {
      case 0: loadPattern(PAT_SQUARE, sizeof(PAT_SQUARE)/sizeof(MoveStep)); break;
      case 1: loadPattern(PAT_ZIGZAG, sizeof(PAT_ZIGZAG)/sizeof(MoveStep)); break;
      case 2: loadPattern(PAT_CIRCLE_LEFT, sizeof(PAT_CIRCLE_LEFT)/sizeof(MoveStep)); break;
      case 3: loadPattern(PAT_CIRCLE_RIGHT, sizeof(PAT_CIRCLE_RIGHT)/sizeof(MoveStep)); break;
      case 4: loadPattern(PAT_SPIN_LEFT, sizeof(PAT_SPIN_LEFT)/sizeof(MoveStep)); break;
      case 5: loadPattern(PAT_SPIN_RIGHT, sizeof(PAT_SPIN_RIGHT)/sizeof(MoveStep)); break;
      case 6: loadPattern(PAT_BACK_FORTH, sizeof(PAT_BACK_FORTH)/sizeof(MoveStep)); break;
      default: stopPattern(); break;
    }
    if (patternRunning && patternLen > 0) {
      driveCar(currentPattern[0].dir);
      patternStepStart = millis();
    }
    server.send(200, "text/plain", "OK");
  });

  server.on("/status", []() {
    String ip = "AP: " + WiFi.softAPIP().toString();
    int rssi = -100;
    if (WiFi.status() == WL_CONNECTED) {
      ip = WiFi.localIP().toString();
      rssi = WiFi.RSSI();
    } else {
      rssi = -65;
    }
    long dist = getDistance();
    int bat = getBatteryPercent();

    String json = "{\"ip\":\"" + ip +
                  "\",\"dist\":" + String(dist) +
                  ",\"safe\":" + String(safeDistance) +
                  ",\"bat\":" + String(bat) +
                  ",\"rssi\":" + String(rssi) +
                  ",\"left\":" + String(leftSpeed) +
                  ",\"right\":" + String(rightSpeed) +
                  ",\"invFB\":" + (invertFB ? "true" : "false") +
                  ",\"invLR\":" + (invertLR ? "true" : "false") +
                  ",\"f1\":" + (ledF1 ? "true" : "false") +
                  ",\"f2\":" + (ledF2 ? "true" : "false") +
                  ",\"b1\":" + (ledB1 ? "true" : "false") +
                  ",\"b2\":" + (ledB2 ? "true" : "false") +
                  ",\"ultra\":" + String(ultrasonicOn ? 1 : 0) +
                  ",\"auto\":" + String(autoPilot ? 1 : 0) +
                  ",\"ledpat\":" + String((int)currentLedPattern) +
                  "}";
    server.send(200, "application/json", json);
  });

  // ---------- OTA Handler ----------
  server.on("/update", HTTP_POST,
    []() {
      server.send(Update.hasError() ? 500 : 200, "text/plain", Update.hasError() ? "FAIL" : "OK");
      delay(300);
      ESP.restart();
    },
    []() {
      HTTPUpload& upload = server.upload();
      if (upload.status == UPLOAD_FILE_START) {
        stopMotors();
        autoPilot = false;
        stopPattern();
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
      } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) Serial.printf("OTA Success: %u bytes\n", upload.totalSize);
        else Update.printError(Serial);
      }
    }
  );

  server.begin();
  stopMotors();
  Serial.println("System Ready!");
}

// ====================== LOOP ======================
void loop() {
  server.handleClient();

  // Manual drive safety dead-man timeout
  if (!autoPilot && !patternRunning && motorsRunning && (millis() - lastCmdMs > CMD_TIMEOUT)) {
    stopMotors();
  }

  // Ultrasonic distance safety check during forward motion
  checkSafety();

  // LED Pattern update
  runLedPattern();

  // Modes
  if (patternRunning) {
    runPattern();
    delay(30);
  }
  else if (autoPilot) {
    runAutoPilot();
    delay(50);
  }
  else {
    delay(5);
  }
}