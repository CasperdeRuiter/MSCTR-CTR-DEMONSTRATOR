// ============================================================
//  Interface.ino  -  OLED, rotary encoder, RGB-led, buzzer
// ============================================================
#include "Config.h"

// Menu-items (volgorde = index)
const char* const MENU_ITEMS[] = {
  "Calibrate", "Start Demo", "Status", "Driver Scan"
};
#define MENU_COUNT (sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]))

// ============================================================
//  INIT
// ============================================================
void initInterface() {
  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT,  INPUT_PULLUP);
  pinMode(ENC_SW,  INPUT_PULLUP);
  pinMode(ENC_R,   OUTPUT);
  pinMode(ENC_G,   OUTPUT);
  pinMode(ENC_B,   OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // Eindschakelaar-ingangen: Schmitt-trigger drijft de pin actief,
  // dus geen interne pull-up.
  for (int p = 0; p < LIMIT_PIN_COUNT; p++) pinMode(LIMIT_PIN_FIRST + p, INPUT);

  lastCLK = digitalRead(ENC_CLK);
  setRGB(0, 0, 0);
}

// ============================================================
//  RGB-LED  (common cathode: LOW = aan)
// ============================================================
void setRGB(bool r, bool g, bool b) {
  digitalWrite(ENC_R, r ? LOW : HIGH);
  digitalWrite(ENC_G, g ? LOW : HIGH);
  digitalWrite(ENC_B, b ? LOW : HIGH);
}
void rgbIdle()  { setRGB(0, 1, 0); }   // groen  - klaar / veilig te bedienen
void rgbWait()  { setRGB(1, 0, 0); }   // rood   - wachten / kalibratie / error
void rgbDemo()  { setRGB(0, 0, 1); }   // blauw  - demo
void rgbMotor() { setRGB(1, 1, 0); }   // geel   - handmatige besturing

// ============================================================
//  BUZZER  -  melodieen in PROGMEM
// ============================================================
static const Note MEL_CLICK[] PROGMEM = { {1800, 30} };
static const Note MEL_BEEP[]  PROGMEM = { {1200, 100} };
static const Note MEL_OK[]    PROGMEM = { {1047, 80}, {1319, 80}, {1568, 150} };
static const Note MEL_BOOT[]  PROGMEM = { {523, 80}, {659, 80}, {784, 80}, {1047, 180} };
static const Note MEL_ERROR[] PROGMEM = { {400, 300}, {0, 80}, {300, 400} };
static const Note MEL_HOME[]  PROGMEM = { {784, 80}, {659, 80}, {523, 150} };

void playNote(uint16_t freq) {
  if (freq > 0) tone(BUZZER_PIN, freq);
  else          noTone(BUZZER_PIN);
}

void playMelody(const Note* mel, uint8_t len) {
  melBuffer = mel; melLength = len; melIndex = 0;
  melActive = true; melStart = millis();
  Note n; memcpy_P(&n, &mel[0], sizeof(Note));
  playNote(n.freq);
}

void triggerBuzzer(BuzzerSound s) {
  switch (s) {
    case BUZ_CLICK: playMelody(MEL_CLICK, 1); break;
    case BUZ_BEEP:  playMelody(MEL_BEEP,  1); break;
    case BUZ_OK:    playMelody(MEL_OK,    3); break;
    case BUZ_BOOT:  playMelody(MEL_BOOT,  4); break;
    case BUZ_ERROR: playMelody(MEL_ERROR, 3); break;
    case BUZ_HOME:  playMelody(MEL_HOME,  3); break;
  }
}

// Niet-blokkerende melodie-afhandeling (in loop)
void handleBuzzer() {
  if (!melActive || melBuffer == nullptr) return;
  Note n; memcpy_P(&n, &melBuffer[melIndex], sizeof(Note));
  if (millis() - melStart >= n.dur) {
    melIndex++;
    if (melIndex >= melLength) { melActive = false; noTone(BUZZER_PIN); return; }
    melStart = millis();
    Note next; memcpy_P(&next, &melBuffer[melIndex], sizeof(Note));
    playNote(next.freq);
  }
}

// ============================================================
//  ENCODER  -  rotatie (CLK/DT) + drukknop met debounce/flank
// ============================================================
void readEncoder() {
  // --- Rotatie ---
  int clk = digitalRead(ENC_CLK);
  if (clk != lastCLK && clk == LOW) {
    encDelta += (digitalRead(ENC_DT) != clk) ? +1 : -1;
  }
  lastCLK = clk;

  // --- Drukknop ---
  static bool encArmed = false;     // pas accepteren nadat knop 1x los (HIGH) was
  swRaw = digitalRead(ENC_SW);
  if (swRaw == HIGH) encArmed = true;
  if (swRaw != swRawPrev) { swDebounceMs = millis(); swRawPrev = swRaw; }
  if ((millis() - swDebounceMs) >= 25) {
    if (swRaw != swStatePrev) {
      swStatePrev = swRaw;
      if (swRaw == LOW) {            // neergaande flank = ingedrukt
        if (encArmed) { swEdge = true; triggerBuzzer(BUZ_CLICK); }
      } else {                       // stijgende flank = losgelaten
        swRelease = true;
      }
    }
  }
}

// Stop elke lopende (niet-blokkerende) buzzer-toon.
// Aanroepen vóór elke blokkerende beweging zodat een klik-geluidje
// van de encoder niet doorpiept tijdens kalibratie of demo.
void stopBuzzer() {
  noTone(BUZZER_PIN);
  melActive = false;
}

// Korte blokkerende pieptoon (voor gebruik binnen blokkerende bewegingen)
void beep(uint16_t freq, uint16_t ms) {
  tone(BUZZER_PIN, freq);
  delay(ms);
  noTone(BUZZER_PIN);
}

// Wacht tot de encoderknop losgelaten is (met time-out) en wis flanken
void waitButtonRelease() {
  unsigned long t0 = millis();
  while (digitalRead(ENC_SW) == LOW && (millis() - t0) < 3000) delay(5);
  delay(30);
  swEdge = false; swRelease = false; encDelta = 0;
}

// ============================================================
//  OLED
// ============================================================
// Titelbalk: volledige witte balk met zwarte titel (identiteitselement).
void oledTitle(const char* txt) {
  oled.fillRect(0, 0, 128, 11, SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  oled.setCursor(3, 2);
  oled.print(txt);
  oled.setTextColor(SSD1306_WHITE);          // klaar voor body-tekst
}

// Rechts-uitgelijnde context-tag op de titelbalk (bv "RUN", "STOP").
void oledHeaderTag(const char* tag) {
  int x = 125 - 6 * (int)strlen(tag);
  oled.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  oled.setCursor(x, 2);
  oled.print(tag);
  oled.setTextColor(SSD1306_WHITE);
}

// Merk-glyph: drie geneste ringen = de drie buizen T1>T2>T3.
void oledBrandGlyph(int cx, int cy, uint16_t col) {
  oled.drawCircle(cx, cy, 4, col);
  oled.drawCircle(cx, cy, 2, col);
  oled.drawPixel(cx, cy, col);
}

// Menu-pictogram (10x10) in rij-kleur 'col'. cy0 = bovenkant van de cel.
void oledMenuIcon(uint8_t item, int cy0, uint16_t col) {
  switch (item) {
    case 0:                                    // Calibrate - datum/home
      oled.drawCircle(8, cy0 + 4, 4, col);
      oled.fillCircle(8, cy0 + 4, 1, col);
      oled.drawLine(8, cy0 - 1, 8, cy0,      col);
      oled.drawLine(8, cy0 + 8, 8, cy0 + 9,  col);
      oled.drawLine(2, cy0 + 4, 3, cy0 + 4,  col);
      oled.drawLine(13, cy0 + 4, 14, cy0 + 4, col);
      break;
    case 1:                                    // Start Demo - play
      oled.fillTriangle(4, cy0 + 1, 4, cy0 + 9, 12, cy0 + 5, col);
      break;
    case 2: {                                  // Status - ECG
      int cyM = cy0 + 5;
      oled.drawLine(3, cyM, 5, cyM, col);
      oled.drawLine(5, cyM, 7, cy0 + 1, col);
      oled.drawLine(7, cy0 + 1, 9, cy0 + 9, col);
      oled.drawLine(9, cy0 + 9, 10, cyM, col);
      oled.drawLine(10, cyM, 12, cyM, col);
      break;
    }
    case 3:                                    // Driver Scan - vergrootglas
      oled.drawCircle(7, cy0 + 3, 3, col);
      oled.drawLine(9, cy0 + 5, 12, cy0 + 9, col);
      oled.drawPixel(7, cy0 + 3, col);
      break;
  }
}

// Geanimeerde opstart-splash: badge-logo + links-naar-rechts vullende balk.
void bootSplash() {
  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.drawRect(0, 0, 128, 64, SSD1306_WHITE);         // badge-kader

  oled.drawCircle(64, 12, 7, SSD1306_WHITE);           // geneste ringen
  oled.drawCircle(64, 12, 4, SSD1306_WHITE);
  oled.fillCircle(64, 12, 1, SSD1306_WHITE);
  oled.drawLine(64, 12, 72, 6, SSD1306_WHITE);         // gestuurde tip

  oled.setTextSize(2);
  oled.setCursor(34, 22);
  oled.print(F("MSCTR"));
  oled.setTextSize(1);
  oled.setCursor(1, 40);
  oled.print(F("Concentric Tube Robot"));
  oled.setCursor(1, 49);
  oled.print(F("Benchmark  Saxion  UT"));

  oled.drawRect(8, 57, 112, 6, SSD1306_WHITE);         // balk-omtrek
  oled.display();

  triggerBuzzer(BUZ_BOOT);
  for (int w = 0; w <= 108; w += 6) {                  // ~1.3 s vullen
    oled.fillRect(10, 59, w, 2, SSD1306_WHITE);
    oled.display();
    handleBuzzer();
    delay(70);
  }
}

// Eenvoudig melding-scherm (titelbalk + tot 3 regels body).
void oledMessage(const char* title, const char* l1, const char* l2, const char* l3) {
  oled.clearDisplay();
  oled.setTextSize(1);
  oledTitle(title);
  oled.setTextColor(SSD1306_WHITE);
  if (l1) { oled.setCursor(3, 18); oled.print(l1); }
  if (l2) { oled.setCursor(3, 30); oled.print(l2); }
  if (l3) { oled.setCursor(3, 42); oled.print(l3); }
  oled.display();
}

void updateOLED() {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  switch (systemMode) {

    case MODE_CALIB: {
      oledTitle("HOMING");
      oledHeaderTag("T1 T2 T3");
      oled.setTextColor(SSD1306_WHITE);
      oled.setCursor(3, 14);
      oled.print(F("Homing all axes"));

      const int   traIdx[3] = { 1, 3, 5 };     // M2/M4/M6 = translatie
      const char* lbl[3]    = { "T1 Translate", "T2 Translate", "T3 Translate" };
      const int   axisY[3]  = { 25, 35, 45 };
      for (int k = 0; k < 3; k++) {
        int i = traIdx[k];
        oled.setTextColor(SSD1306_WHITE);
        oled.setCursor(3, axisY[k]);
        oled.print(lbl[k]);
        const char* tok;
        if (!motors[i].connected) tok = "N/A";
        else switch (motors[i].state) {
          case CAL_FIND_ZERO: tok = "zero"; break;
          case CAL_FIND_MAX:  tok = "max";  break;
          case CAL_GO_HOME:   tok = "home"; break;
          case CAL_DONE:      tok = "OK";   break;
          default:            tok = "--";   break;
        }
        oled.drawRect(86, axisY[k] - 1, 40, 9, SSD1306_WHITE);
        int tx = 124 - 6 * (int)strlen(tok);
        oled.setCursor(tx, axisY[k]);
        oled.print(tok);
      }
      oled.drawLine(0, 54, 127, 54, SSD1306_WHITE);
      oled.setCursor(3, 56);
      oled.print(F("Please wait..."));
      break;
    }

    case MODE_MENU:
      oledTitle("MAIN MENU");
      oledBrandGlyph(118, 5, SSD1306_BLACK);
      for (uint8_t i = 0; i < MENU_COUNT; i++) {
        int rowY = 14 + 12 * i;
        uint16_t col;
        if (i == menuIndex) {
          oled.fillRect(0, rowY, 128, 12, SSD1306_WHITE);
          oled.fillRect(125, rowY + 1, 2, 10, SSD1306_BLACK);
          oled.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
          col = SSD1306_BLACK;
        } else {
          oled.setTextColor(SSD1306_WHITE);
          col = SSD1306_WHITE;
        }
        oledMenuIcon(i, rowY + 1, col);
        oled.setCursor(17, rowY + 2);
        oled.print(MENU_ITEMS[i]);
      }
      break;

    case MODE_DEMO:
      oledTitle("DEMO RUNNING");
      oledHeaderTag("RUN");
      oled.setTextColor(SSD1306_WHITE);
      // Geneste-buis-graphic: T1>T2>T3 telescopisch uitschuivend
      oled.drawLine(8, 18, 84, 18, SSD1306_WHITE);     // T1 buiten
      oled.drawLine(8, 30, 84, 30, SSD1306_WHITE);
      oled.drawLine(8, 18, 8, 30, SSD1306_WHITE);
      oled.drawLine(84, 18, 84, 21, SSD1306_WHITE);
      oled.drawLine(84, 27, 84, 30, SSD1306_WHITE);
      oled.drawLine(20, 21, 72, 21, SSD1306_WHITE);    // T2 midden
      oled.drawLine(20, 27, 72, 27, SSD1306_WHITE);
      oled.drawLine(30, 23, 64, 23, SSD1306_WHITE);    // T3 binnen
      oled.drawLine(30, 25, 64, 25, SSD1306_WHITE);
      oled.fillCircle(66, 24, 2, SSD1306_WHITE);       // gestuurde tip
      oled.drawLine(66, 24, 72, 20, SSD1306_WHITE);
      oled.setCursor(92, 17); oled.print(F("T1"));
      oled.setCursor(92, 26); oled.print(F("T2"));
      oled.setCursor(92, 35); oled.print(F("T3"));
      oled.setCursor(3, 43); oled.print(F("Phase ")); oled.print(demoStep);
      oled.fillRect(0, 54, 128, 10, SSD1306_WHITE);    // sterke stop-footer
      oled.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
      oled.setCursor(31, 55); oled.print(F("PUSH = STOP"));
      oled.setTextColor(SSD1306_WHITE);
      break;

    case MODE_ERROR: {
      oledTitle("ERROR");
      oledHeaderTag("STOP");
      oled.setTextColor(SSD1306_WHITE);
      oled.drawTriangle(16, 16, 6, 38, 26, 38, SSD1306_WHITE);  // gevarendriehoek
      oled.fillRect(15, 23, 2, 8, SSD1306_WHITE);
      oled.fillRect(15, 33, 2, 2, SSD1306_WHITE);
      String m = errorMsg;                              // errorMsg over max 2 regels
      if (m.length() <= 15) {
        oled.setCursor(34, 24); oled.print(m);
      } else {
        int sp = m.lastIndexOf(' ', 15);
        if (sp < 1) sp = 15;
        oled.setCursor(34, 20); oled.print(m.substring(0, sp));
        oled.setCursor(34, 30); oled.print(m.substring(m.charAt(sp) == ' ' ? sp + 1 : sp));
      }
      oled.fillRect(0, 54, 128, 10, SSD1306_WHITE);     // sterke confirm-footer
      oled.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
      oled.setCursor(22, 55); oled.print(F("PUSH = CONFIRM"));
      oled.setTextColor(SSD1306_WHITE);
      break;
    }

    default:
      oledTitle("MSCTR");
      break;
  }
  oled.display();
}

// Monitor encoder-knop op raw pinniveau — zet g_stopFlag zodra knop ingedrukt is.
// Draait in loop() zodat blokkerende functies (demo, fout) de vlag kunnen lezen.
void monitorEncoderButton() {
  static bool lastState = HIGH;
  static unsigned long lowSince = 0;
  bool cur = digitalRead(ENC_SW);

  // Log bij elke statuswijziging
  if (cur != lastState) {
    lastState = cur;
    Serial.print(F("[ENC_SW] pin 44 -> "));
    Serial.println(cur == LOW ? F("LOW (ingedrukt)") : F("HIGH (losgelaten)"));
  }

  // Zet stop-vlag na 30 ms aaneengesloten LOW (debounce)
  if (cur == LOW) {
    if (lowSince == 0) lowSince = millis();
    if (millis() - lowSince >= 30) { g_stopFlag = true; lowSince = 0; }
  } else {
    lowSince = 0;
  }
}

// OLED-verversing: alleen hertekenen als de getoonde inhoud verandert.
// oled.display() is een blokkerende I2C-overdracht (~23 ms) die het stappen
// onderbreekt; tijdens beweging dus NIET periodiek hertekenen -> vloeiend.
void refreshOLED() {
  static unsigned long last    = 0;
  static uint32_t      lastSig = 0xFFFFFFFFUL;

  uint32_t sig = (uint32_t)systemMode * 1311u
               + (uint32_t)menuIndex  * 17u
               + (uint32_t)demoStep   * 7u;
  for (int i = 0; i < NUM_MOTORS; i++) sig = sig * 31u + (uint32_t)motors[i].state;

  // Beweging draait blokkerend (buiten loop), dus hier alleen in rust:
  // hertekenen bij wijziging of rustige heartbeat.
  if (sig == lastSig && (millis() - last) < 750) return;
  lastSig = sig;
  last    = millis();
  updateOLED();
}
