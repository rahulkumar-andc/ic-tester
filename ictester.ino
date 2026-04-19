/*
 * ============================================================
 *  IC TESTER — 7400 / 7402 / 7408 / 7432 / 7486   (SH1106 OLED v3)
 *  Board  : Arduino UNO
 *  Display: 1.3" I2C OLED — SH1106 controller (128×64 px)
 *  Flow   : WELCOME → SELECT IC → TESTING → RESULT → SELECT IC
 * ============================================================
 *
 *  LIBRARIES NEEDED (install via Arduino IDE → Manage Libraries)
 *  ─────────────────────────────────────────────────────────
 *  1. Adafruit SH110X   (search: "Adafruit SH110X")
 *  2. Adafruit GFX      (search: "Adafruit GFX Library")
 *     (GFX installs automatically as a dependency of SH110X)
 *  ─────────────────────────────────────────────────────────
 *
 *  PIN MAPPING  (for 7400 / 7408 / 7432 / 7486)
 *  ─────────────────────────────────────────────────────────
 *  GATE  │  INPUT A  │  INPUT B  │  OUTPUT Y (read)
 *  ──────┼───────────┼───────────┼──────────────────────────
 *    1   │   D2      │   D3      │   D4
 *    2   │   D5      │   D6      │   D7
 *    3   │   D8      │   D9      │   D10
 *    4   │   A0      │   A1      │   A2
 *  ─────────────────────────────────────────────────────────
 *
 *  NOTE — 7402 NOR has a DIFFERENT pinout (Y,A,B vs A,B,Y).
 *  The software automatically remaps pins when testing 7402.
 *  Same hardware wiring — no changes needed.
 *  ─────────────────────────────────────────────────────────
 *  SELECT button  → D11  (INPUT_PULLUP — no external resistor)
 *  START  button  → A3   (INPUT_PULLUP — no external resistor)
 *  GREEN  LED     → D12  (via 220Ω to GND)
 *  RED    LED     → D13  (via 220Ω to GND)
 *  OLED SDA       → A4
 *  OLED SCL       → A5
 *  OLED VCC       → 3.3V  (some modules accept 5V — check yours)
 *  OLED GND       → GND
 *  ─────────────────────────────────────────────────────────
 *
 *  !! IMPORTANT — IC POWER PINS (connect FIRST, always) !!
 *  ─────────────────────────────────────────────────────────
 *  IC Pin 14  →  Arduino 5V    (VCC)
 *  IC Pin  7  →  Arduino GND   (GND)
 *
 *  If these are missing, the IC outputs nothing and EVERY
 *  gate will show FAIL — even on a perfectly good IC.
 *  ─────────────────────────────────────────────────────────
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

// ── OLED setup ───────────────────────────────────────────────
// SH1106 is 128×64.  Reset pin = -1 (uses I2C reset, not a GPIO).
// I2C address is 0x3C for most SH1106 modules.
// If display stays blank try 0x3D.
#define OLED_ADDR   0x3C
#define SCREEN_W    128
#define SCREEN_H     64

Adafruit_SH1106G oled(SCREEN_W, SCREEN_H, &Wire, -1);

// ── Gate I/O pin arrays (index 0–3 = Gate 1–4) ───────────────
// Default mapping: works for 7400, 7408, 7432, 7486
const int inA[]  = { 2,  5,  8, A0 };
const int inB[]  = { 3,  6,  9, A1 };
const int outY[] = { 4,  7, 10, A2 };

// ── 7402 NOR remapped pins ───────────────────────────────────
// 7402 swaps output & input positions on the IC package.
// Same physical wires, but Arduino must swap pin roles.
//
//  Gate │ Drive A │ Drive B │ Read Y
//  ─────┼─────────┼─────────┼──────────
//    1  │  D3     │  D4     │  D2
//    2  │  D6     │  D7     │  D5
//    3  │  D10    │  D8     │  D9
//    4  │  A2     │  A0     │  A1
//
const int norInA[]  = {  3,  6, 10, A2 };   // drives NOR input A
const int norInB[]  = {  4,  7,  8, A0 };   // drives NOR input B
const int norOutY[] = {  2,  5,  9, A1 };   // reads  NOR output Y

// ── Control pins ─────────────────────────────────────────────
const int SELECT_BTN = 11;
const int START_BTN  = A3;
const int GREEN_LED  = 12;
const int RED_LED    = 13;

// ── IC metadata ──────────────────────────────────────────────
const char* IC_NAMES[5] = {
  "7400 NAND",
  "7402  NOR",
  "7408  AND",
  "7432   OR",
  "7486  XOR"
};

// Truth table: expected Y for inputs {00, 01, 10, 11}
const int TRUTH[5][4] = {
  { 1, 1, 1, 0 },   // 7400 NAND
  { 1, 0, 0, 0 },   // 7402 NOR
  { 0, 0, 0, 1 },   // 7408 AND
  { 0, 1, 1, 1 },   // 7432 OR
  { 0, 1, 1, 0 }    // 7486 XOR
};

const int TEST_IN[4][2] = { {0,0}, {0,1}, {1,0}, {1,1} };

// ── State machine ─────────────────────────────────────────────
enum State { WELCOME, SELECT_IC, RESULT };
State appState = WELCOME;

// ── Test results ─────────────────────────────────────────────
int  currentIC  = 0;
bool testPassed = false;
int  failGate   = -1;
int  failInput  = -1;

// ── millis()-based debounce ───────────────────────────────────
#define DEBOUNCE_MS 50

int  selRaw   = HIGH, selStable   = HIGH;
int  startRaw = HIGH, startStable = HIGH;
unsigned long selTime = 0, startTime = 0;

bool debounced(int pin, int &raw, int &stable, unsigned long &t) {
  int reading = digitalRead(pin);
  if (reading != raw) {
    raw = reading;
    t   = millis();
  }
  if ((millis() - t) >= DEBOUNCE_MS) {
    if (stable == HIGH && raw == LOW) {
      stable = LOW;
      return true;
    }
    stable = raw;
  }
  return false;
}

// ═════════════════════════════════════════════════════════════
void setup() {
  // Give OLED 500ms to boot up before sending I2C commands
  delay(500);
  
  // OLED init
  oled.begin(OLED_ADDR, true);   // true = reset sequence
  oled.setTextWrap(false);
  oled.setTextColor(SH110X_WHITE);

  pinMode(SELECT_BTN, INPUT_PULLUP);
  pinMode(START_BTN,  INPUT_PULLUP);
  pinMode(GREEN_LED,  OUTPUT);
  pinMode(RED_LED,    OUTPUT);

  // Gate pins — start as INPUT_PULLUP (high-impedance, safe state).
  // This prevents short circuits if any IC (especially 7402 NOR,
  // which has outputs where 7400 has inputs) is already in the
  // socket at power-on.  Pins are switched to OUTPUT only during
  // the actual test, then restored to INPUT_PULLUP afterwards.
  for (int i = 0; i < 4; i++) {
    pinMode(inA[i],  INPUT_PULLUP);
    pinMode(inB[i],  INPUT_PULLUP);
    pinMode(outY[i], INPUT_PULLUP);
  }

  ledsOff();
  showWelcome();
}

// ═════════════════════════════════════════════════════════════
void loop() {
  bool selPressed   = debounced(SELECT_BTN, selRaw,   selStable,   selTime);
  bool startPressed = debounced(START_BTN,  startRaw, startStable, startTime);

  // ── WELCOME ────────────────────────────────────────────────
  if (appState == WELCOME) {
    if (startPressed) {
      appState = SELECT_IC;
      showSelectIC();
    }
  }

  // ── SELECT IC ─────────────────────────────────────────────
  else if (appState == SELECT_IC) {
    if (selPressed) {
      currentIC = (currentIC + 1) % 5;
      showSelectIC();
    }
    else if (startPressed) {
      ledsOff();
      showTesting();
      runTest();
      updateLEDs();
      appState = RESULT;
      showResult();
    }
  }

  // ── RESULT ────────────────────────────────────────────────
  else if (appState == RESULT) {
    if (startPressed) {
      ledsOff();
      appState = SELECT_IC;
      showSelectIC();
    }
  }
}

// ═════════════════════════════════════════════════════════════
//  TEST ENGINE
// ═════════════════════════════════════════════════════════════

// Configure pins for standard ICs (7400/7408/7432/7486)
void setupPinsForStandard() {
  for (int i = 0; i < 4; i++) {
    pinMode(inA[i],  OUTPUT);
    pinMode(inB[i],  OUTPUT);
    pinMode(outY[i], INPUT_PULLUP);
    digitalWrite(inA[i], LOW);
    digitalWrite(inB[i], LOW);
  }
}

// Reconfigure pins for 7402 NOR (swap INPUT ↔ OUTPUT roles)
void setupPinsForNOR() {
  for (int i = 0; i < 4; i++) {
    pinMode(norInA[i],  OUTPUT);
    pinMode(norInB[i],  OUTPUT);
    pinMode(norOutY[i], INPUT_PULLUP);
    digitalWrite(norInA[i], LOW);
    digitalWrite(norInB[i], LOW);
  }
}

// Restore ALL gate pins to safe INPUT_PULLUP (high-impedance)
// Called after every test to prevent short circuits with any IC.
void safePinsIdle() {
  for (int i = 0; i < 4; i++) {
    pinMode(inA[i],  INPUT_PULLUP);
    pinMode(inB[i],  INPUT_PULLUP);
    pinMode(outY[i], INPUT_PULLUP);
  }
}

void runTest() {
  testPassed = true;
  failGate   = -1;
  failInput  = -1;

  // 7402 NOR needs remapped pins due to different IC pinout
  bool isNOR = (currentIC == 1);
  const int* pA = isNOR ? norInA  : inA;
  const int* pB = isNOR ? norInB  : inB;
  const int* pY = isNOR ? norOutY : outY;

  if (isNOR) {
    setupPinsForNOR();
  } else {
    setupPinsForStandard();
  }

  for (int gate = 0; gate < 4; gate++) {
    for (int t = 0; t < 4; t++) {
      digitalWrite(pA[gate], TEST_IN[t][0]);
      digitalWrite(pB[gate], TEST_IN[t][1]);
      delay(10);

      int got = digitalRead(pY[gate]);
      if (got != TRUTH[currentIC][t]) {
        testPassed = false;
        failGate   = gate + 1;
        failInput  = t;
        break;
      }
    }
    if (!testPassed) break;
  }

  // Restore all pins to safe high-impedance state
  safePinsIdle();
}

// ═════════════════════════════════════════════════════════════
//  OLED DISPLAY HELPERS
//
//  SH1106 is a buffered display:
//    1. oled.clearDisplay()   — clear the RAM buffer
//    2. draw text / graphics  — write into buffer
//    3. oled.display()        — push buffer to screen
//
//  Text sizes used:
//    setTextSize(1) → 6×8 px per char  (21 chars per row)
//    setTextSize(2) → 12×16 px per char (10 chars per row)
//    setTextSize(3) → 18×24 px per char (~7 chars per row)
// ═════════════════════════════════════════════════════════════

// Helper: draw a horizontal divider line
void drawLine(int y) {
  oled.drawFastHLine(0, y, SCREEN_W, SH110X_WHITE);
}

// ── WELCOME screen ───────────────────────────────────────────
// ┌────────────────────────┐
// │      IC TESTER         │  ← size 2, bold look
// │   7400 7408 7432 7486  │  ← size 1, supported ICs
// │  ─────────────────     │
// │    Press START         │  ← size 1, blinking hint
// └────────────────────────┘
void showWelcome() {
  oled.clearDisplay();

  // Title — size 2 (12×16 px)
  oled.setTextSize(2);
  oled.setCursor(8, 4);
  oled.print("IC TESTER");

  // Supported ICs — size 1
  oled.setTextSize(1);
  oled.setCursor(4, 28);
  oled.print("7400 02 08 32 86");

  // Divider
  drawLine(38);

  // Prompt
  oled.setCursor(14, 46);
  oled.print("Press START btn");

  oled.display();
}

// ── SELECT IC screen ─────────────────────────────────────────
// ┌────────────────────────┐
// │  Select IC  [ 2 / 4 ]  │  ← size 1 header
// │  ─────────────────     │
// │    > 7408  AND         │  ← size 2, current IC
// │                        │
// │  SEL=next  STA=test    │  ← size 1 hint
// └────────────────────────┘
void showSelectIC() {
  oled.clearDisplay();

  // Header row
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("Select IC  [");
  oled.print(currentIC + 1);
  oled.print("/5]");

  drawLine(10);

  // IC name — size 2
  oled.setTextSize(2);
  oled.setCursor(0, 16);
  oled.print("> ");
  oled.print(IC_NAMES[currentIC]);

  // Bottom hint — size 1
  oled.setTextSize(1);
  oled.setCursor(0, 56);
  oled.print("SEL=next  STA=test");

  oled.display();
}

// ── TESTING screen ───────────────────────────────────────────
// ┌────────────────────────┐
// │   Testing...           │
// │                        │
// │     7408  AND          │  ← size 2
// │                        │
// │  Gate 1... 2... 3...   │
// └────────────────────────┘
void showTesting() {
  oled.clearDisplay();

  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print("Testing...");

  drawLine(10);

  oled.setTextSize(2);
  oled.setCursor(4, 18);
  oled.print(IC_NAMES[currentIC]);

  oled.setTextSize(1);
  oled.setCursor(0, 56);
  oled.print("Please wait...");

  oled.display();
}

// ── RESULT screen — PASS ─────────────────────────────────────
// ┌────────────────────────┐
// │  7408  AND             │
// │  ─────────────────     │
// │                        │
// │   *** PASS ***         │  ← size 2
// │                        │
// │  STA = test again      │
// └────────────────────────┘
//
// ── RESULT screen — FAIL ─────────────────────────────────────
// ┌────────────────────────┐
// │  7408  AND             │
// │  ─────────────────     │
// │   FAIL                 │  ← size 2
// │   Gate:2  Input:01     │  ← size 1, detail
// │  STA = test again      │
// └────────────────────────┘
void showResult() {
  oled.clearDisplay();

  // IC name header
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.print(IC_NAMES[currentIC]);

  drawLine(10);

  if (testPassed) {
    // PASS — big text centered
    oled.setTextSize(2);
    oled.setCursor(16, 20);
    oled.print("** PASS **");

  } else {
    // FAIL — big text + detail
    oled.setTextSize(2);
    oled.setCursor(0, 14);
    oled.print("!! FAIL !!");

    // Detail line — gate number and which input combo failed
    // Show A,B values for the failing input combo
    oled.setTextSize(1);
    oled.setCursor(0, 36);
    oled.print("Gate: ");
    oled.print(failGate);
    oled.print("   Input: ");
    oled.print(TEST_IN[failInput][0]);
    oled.print(",");
    oled.print(TEST_IN[failInput][1]);

    // Expected vs got hint
    oled.setCursor(0, 46);
    oled.print("Expected Y=");
    oled.print(TRUTH[currentIC][failInput]);
  }

  // Bottom hint
  oled.setTextSize(1);
  oled.setCursor(0, 56);
  oled.print("START = test again");

  oled.display();
}

// ═════════════════════════════════════════════════════════════
//  LED CONTROL
// ═════════════════════════════════════════════════════════════

void updateLEDs() {
  if (testPassed) {
    digitalWrite(GREEN_LED, HIGH);
    digitalWrite(RED_LED,   LOW);
  } else {
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(RED_LED,   HIGH);
  }
}

void ledsOff() {
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED,   LOW);
}
