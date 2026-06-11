// ============================================================
//  Interface.ino  -  OLED, rotary encoder, RGB-led, buzzer
// ============================================================
#include "Config.h"

// Menu-items (volgorde = index)
const char* const MENU_ITEMS[] = {
  "Kalibreren", "Start Demo", "Status", "Driver Scan"
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
void oledTitle(const char* txt) {
  oled.setCursor(0, OLED_TITLE_Y);
  oled.setTextSize(1);
  oled.print(txt);
}

// Eenvoudig melding-scherm (titel + tot 3 regels)
void oledMessage(const char* title, const char* l1, const char* l2, const char* l3) {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oledTitle(title);
  if (l1) { oled.setCursor(0, OLED_CONTENT_Y);      oled.print(l1); }
  if (l2) { oled.setCursor(0, OLED_CONTENT_Y + 12);  oled.print(l2); }
  if (l3) { oled.setCursor(0, OLED_CONTENT_Y + 24);  oled.print(l3); }
  oled.display();
}

void updateOLED() {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  switch (systemMode) {

    case MODE_CALIB:
      oledTitle("-- KALIBRATIE --");
      for (int i = 0; i < NUM_MOTORS; i++) {
        oled.setCursor(0, OLED_CONTENT_Y + i * 8);
        oled.print(MOTOR_NAMES[i]); oled.print(':');
        if (!motors[i].connected)        oled.print(F("N/A"));
        else switch (motors[i].state) {
          case CAL_IDLE:      oled.print(F("--"));    break;
          case CAL_FIND_ZERO: oled.print(F("Nul.."));  break;
          case CAL_FIND_MAX:  oled.print(F("Max.."));  break;
          case CAL_DONE:      oled.print(F("OK"));     break;
          case CAL_GO_HOME:   oled.print(F("Home"));   break;
        }
      }
      break;

    case MODE_MENU:
      oledTitle("------ MENU ------");
      for (uint8_t i = 0; i < MENU_COUNT; i++) {
        oled.setCursor(0, OLED_CONTENT_Y + i * 9);
        oled.print(i == menuIndex ? F(">") : F(" "));
        oled.print(MENU_ITEMS[i]);
      }
      break;

    case MODE_DEMO:
      oledTitle("------ DEMO ------");
      oled.setCursor(0, OLED_CONTENT_Y);
      oled.print(F("Fase: ")); oled.print(demoStep);
      oled.setCursor(0, OLED_CONTENT_Y + 24);
      oled.print(F("[druk = stop]"));
      break;

    case MODE_ERROR:
      oledTitle("!!!!! FOUT !!!!!");
      oled.setCursor(0, OLED_CONTENT_Y);
      oled.print(errorMsg);
      oled.setCursor(0, OLED_CONTENT_Y + 28);
      oled.print(F("Druk = bevestig"));
      break;

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
