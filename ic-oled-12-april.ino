/*
 * ============================================================
 * IC TESTER — 7400 / 7408 / 7432 / 7486
 * Board  : Arduino UNO
 * Display: 1.3" I2C OLED — SH1106 (128×64)
 * Library: U8g2 by oliver
 * ============================================================
 *
 * PIN MAPPING
 * ─────────────────────────────────────────────────────────
 * GATE  │  INPUT A  │  INPUT B  │  OUTPUT Y (read)
 * ──────┼───────────┼───────────┼──────────────────────────
 * 1     │   D2      │   D3      │   D4
 * 2     │   D5      │   D6      │   D7
 * 3     │   D8      │   D9      │   D10
 * 4     │   A0      │   A1      │   A2
 * ─────────────────────────────────────────────────────────
 * SELECT button  → D11  (INPUT_PULLUP)
 * START  button  → A3   (INPUT_PULLUP)
 * GREEN  LED     → D12  (via 220Ω to GND)
 * RED    LED     → D13  (via 220Ω to GND)
 * I2C OLED SDA   → A4
 * I2C OLED SCL   → A5
 * ─────────────────────────────────────────────────────────
 */

#include <Wire.h>
#include <U8g2lib.h>

// ── OLED (SH1106, 128×64, I2C) — page mode, UNO RAM-safe ──
U8G2_SH1106_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ── Gate I/O pin arrays (index 0–3 = Gate 1–4) ───────────
const int inA[]  = {  2,  5,  8, A0 };
const int inB[]  = {  3,  6,  9, A1 };
const int outY[] = {  4,  7, 10, A2 };

// ── Control pins ──────────────────────────────────────────
const int SELECT_BTN = 11;
const int START_BTN  = A3;
const int GREEN_LED  = 12;
const int RED_LED    = 13;

// ── IC metadata ───────────────────────────────────────────
const char* IC_NAMES[4] = {
  "7400 NAND",
  "7408  AND",
  "7432   OR",
  "7486  XOR"
};

// Truth table: expected Y for inputs {00, 01, 10, 11}
const int TRUTH[4][4] = {
  { 1, 1, 1, 0 },   // 7400 NAND
  { 0, 0, 0, 1 },   // 7408 AND
  { 0, 1, 1, 1 },   // 7432 OR
  { 0, 1, 1, 0 }    // 7486 XOR
};

// Input combinations: A,B pairs
const int TEST_IN[4][2] = { {0,0}, {0,1}, {1,0}, {1,1} };

// ── State machine ─────────────────────────────────────────
enum State { WELCOME, SELECT_IC, RESULT };
State appState = WELCOME;

// ── Test results ──────────────────────────────────────────
int  currentIC  = 0;
bool testPassed = false;
int  failGate   = -1;
int  failInput  = -1;

// ── Debounce ──────────────────────────────────────────────
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

// ── Helper: text horizontally center kare ─────────────────
void printCentered(const char* str, int y) {
  int w = u8g2.getStrWidth(str);
  u8g2.setCursor((128 - w) / 2, y);
  u8g2.print(str);
}

// ═════════════════════════════════════════════════════════
void setup() {
  u8g2.begin();

  pinMode(SELECT_BTN, INPUT_PULLUP);
  pinMode(START_BTN,  INPUT_PULLUP);
  pinMode(GREEN_LED,  OUTPUT);
  pinMode(RED_LED,    OUTPUT);

  for (int i = 0; i < 4; i++) {
    pinMode(inA[i],  OUTPUT);
    pinMode(inB[i],  OUTPUT);
    pinMode(outY[i], INPUT_PULLUP);
    digitalWrite(inA[i], LOW);
    digitalWrite(inB[i], LOW);
  }

  ledsOff();
  showWelcome();
}

// ═════════════════════════════════════════════════════════
void loop() {
  bool selPressed   = debounced(SELECT_BTN, selRaw,   selStable,   selTime);
  bool startPressed = debounced(START_BTN,  startRaw, startStable, startTime);

  // ── WELCOME ───────────────────────────────────────────
  if (appState == WELCOME) {
    if (startPressed) {
      appState = SELECT_IC;
      showSelectIC();
    }
  }

  // ── SELECT IC ─────────────────────────────────────────
  else if (appState == SELECT_IC) {
    if (selPressed) {
      currentIC = (currentIC + 1) % 4;
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

  // ── RESULT ────────────────────────────────────────────
  else if (appState == RESULT) {
    if (startPressed) {        // Same IC dobara test
      ledsOff();
      showTesting();
      runTest();
      updateLEDs();
      showResult();
    }
    else if (selPressed) {     // Wapas menu
      ledsOff();
      appState = SELECT_IC;
      showSelectIC();
    }
  }
}

// ═════════════════════════════════════════════════════════
//  TEST ENGINE
// ═════════════════════════════════════════════════════════
void runTest() {
  testPassed = true;
  failGate   = -1;
  failInput  = -1;

  for (int gate = 0; gate < 4; gate++) {
    for (int t = 0; t < 4; t++) {
      digitalWrite(inA[gate], TEST_IN[t][0]);
      digitalWrite(inB[gate], TEST_IN[t][1]);
      delay(10);

      int got = digitalRead(outY[gate]);
      if (got != TRUTH[currentIC][t]) {
        testPassed = false;
        failGate   = gate + 1;
        failInput  = t;
        resetInputs();
        return;
      }
    }
  }
  resetInputs();
}

// ═════════════════════════════════════════════════════════
//  DISPLAY FUNCTIONS (page mode — firstPage/nextPage loop)
// ═════════════════════════════════════════════════════════

void showWelcome() {
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_ncenB14_tr);
    printCentered("IC TESTER", 22);

    u8g2.drawHLine(0, 28, 128);

    u8g2.setFont(u8g2_font_6x10_tr);
    printCentered("7400/7408/7432/7486", 42);
    printCentered("Press START to begin", 56);
  } while (u8g2.nextPage());
}

void showSelectIC() {
  char header[12];
  snprintf(header, sizeof(header), "IC  %d / 4", currentIC + 1);

  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x10_tr);
    printCentered(header, 12);

    u8g2.drawHLine(0, 16, 128);

    u8g2.setFont(u8g2_font_ncenB14_tr);
    printCentered(IC_NAMES[currentIC], 38);

    u8g2.setFont(u8g2_font_6x10_tr);
    printCentered("SEL=next  START=test", 58);
  } while (u8g2.nextPage());
}

void showTesting() {
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_7x14B_tr);
    printCentered("Testing...", 28);

    u8g2.setFont(u8g2_font_6x10_tr);
    printCentered(IC_NAMES[currentIC], 46);
  } while (u8g2.nextPage());
}

void showResult() {
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_6x10_tr);
    printCentered(IC_NAMES[currentIC], 12);

    u8g2.drawHLine(0, 16, 128);

    if (testPassed) {
      u8g2.setFont(u8g2_font_ncenB14_tr);
      printCentered("** PASS **", 38);
    } else {
      u8g2.setFont(u8g2_font_7x14B_tr);
      printCentered("** FAIL **", 32);

      char buf[24];
      snprintf(buf, sizeof(buf), "Gate %d  Input %d%d",
               failGate,
               TEST_IN[failInput][0],
               TEST_IN[failInput][1]);

      u8g2.setFont(u8g2_font_6x10_tr);
      printCentered(buf, 46);
    }

    u8g2.setFont(u8g2_font_6x10_tr);
    printCentered("START=retest SEL=menu", 62);
  } while (u8g2.nextPage());
}

// ═════════════════════════════════════════════════════════
//  LED CONTROL
// ═════════════════════════════════════════════════════════
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

// ═════════════════════════════════════════════════════════
//  UTILITY
// ═════════════════════════════════════════════════════════
void resetInputs() {
  for (int i = 0; i < 4; i++) {
    digitalWrite(inA[i], LOW);
    digitalWrite(inB[i], LOW);
  }
}
