// firmware-v3.0.ino
// Dual Pill Counter — PN532 (I2C) + RC522 (SPI), 2 pools, 2 OLED displays
//
// Changes from v2.5:
//   - Persistent storage via ESP32 Preferences (counts, maxes, UIDs survive power loss)
//   - Two tactile buttons (GPIO 32 & 33) with full state machine
//   - Set-count mode: hold button 3s → B1=+10, B2=+1, hold to save
//   - Enrollment mode: hold both buttons 5s → scan tags to assign UIDs
//   - Serial command interface: status / reset / setmax / enroll / factory
//
// ─── Button Wiring ───────────────────────────────────────────────────────────
//   Button 1 (Pool 1): GPIO 32 ←→ one leg; GND ←→ diagonal leg. No resistor.
//   Button 2 (Pool 2): GPIO 33 ←→ one leg; GND ←→ diagonal leg. No resistor.
//   (Uses INPUT_PULLUP — button LOW = pressed)
//
// ─── Full Wiring ─────────────────────────────────────────────────────────────
//   I2C Bus 1: PN532 + OLED #1  — SDA=21, SCL=22, IRQ=4, RESET=5
//   I2C Bus 2: OLED #2          — SDA=16, SCL=17
//   SPI:       RC522            — SS=15, SCK=18, MOSI=23, MISO=19, RST=27
// ─────────────────────────────────────────────────────────────────────────────

#include <Wire.h>
#include <SPI.h>
#include <Adafruit_PN532.h>
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>

// ─── Pin Definitions ─────────────────────────────────────────────────────────
#define I2C1_SDA   21
#define I2C1_SCL   22
#define I2C2_SDA   16
#define I2C2_SCL   17
#define PN532_IRQ    4
#define PN532_RESET  5
#define RC522_SS    15
#define RC522_SCK   18
#define RC522_MISO  19
#define RC522_MOSI  23
#define RC522_RST   27
#define BTN1_PIN    32   // Pool 1 button
#define BTN2_PIN    33   // Pool 2 button

// ─── OLED ────────────────────────────────────────────────────────────────────
#define OLED_W       128
#define OLED_H        64
#define OLED_ADDRESS 0x3C
#define OLED_RESET    -1

// ─── Tuning ──────────────────────────────────────────────────────────────────
#define LOW_STOCK_PCT     10     // % remaining that triggers "PICK UP REFILL"
#define DEBOUNCE_MS      250     // NFC debounce
#define BTN_DEBOUNCE_MS   30     // Button debounce
#define BTN_SHORT_MAX_MS 800     // Press < this = short press
#define BTN_HOLD2S      2000     // Hold duration to confirm set mode (ms)
#define BTN_HOLD3S      3000     // Hold duration to enter set mode (ms)
#define BTN_HOLD5S      5000     // Hold duration (both) for enrollment (ms)
#define SET_TIMEOUT_MS 30000     // Auto-cancel set mode if idle this long

// ─── Devices ─────────────────────────────────────────────────────────────────
Adafruit_PN532   nfc1(PN532_IRQ, PN532_RESET, &Wire);
MFRC522          rfid2(RC522_SS, RC522_RST);
Adafruit_SSD1306 oled1(OLED_W, OLED_H, &Wire,  OLED_RESET);
Adafruit_SSD1306 oled2(OLED_W, OLED_H, &Wire1, OLED_RESET);
Preferences      prefs;

// ─── Pill Pools ──────────────────────────────────────────────────────────────
int pillsMax1  = 30;
int pillsLeft1 = 30;
int pillsMax2  = 30;
int pillsLeft2 = 30;

// ─── Tag UIDs (loaded from flash; defaults used if flash is blank) ────────────
String TAG1_UID = "E3E31409";
String TAG2_UID = "4443AB04";

// ─── App State ───────────────────────────────────────────────────────────────
enum AppState { NORMAL, SET_POOL1, SET_POOL2, ENROLL_POOL1, ENROLL_POOL2 };
AppState appState = NORMAL;

// Set-mode working values
int           setTemp       = 0;
unsigned long setLastInputMs = 0;

// ─── Button ──────────────────────────────────────────────────────────────────
struct Button {
  uint8_t       pin;
  bool          rawPrev;
  bool          state;            // debounced
  bool          prevState;        // for edge detection
  unsigned long debounceAt;
  unsigned long pressedAt;
  bool          held2sFired;
  bool          held3sFired;
  bool          held5sFired;
  bool          shortFired;

  void begin(uint8_t p) {
    pin = p;
    pinMode(pin, INPUT_PULLUP);
    rawPrev = HIGH; state = HIGH; prevState = HIGH;
    debounceAt = pressedAt = 0;
    held2sFired = held3sFired = held5sFired = shortFired = false;
  }

  // Call every loop iteration
  void update() {
    bool raw = digitalRead(pin);
    if (raw != rawPrev) { debounceAt = millis(); rawPrev = raw; }
    if (millis() - debounceAt < BTN_DEBOUNCE_MS) return;

    bool newState = raw;
    if (newState == state) return;

    if (newState == LOW) {
      // Falling edge — button pressed
      pressedAt    = millis();
      held2sFired  = false;
      held3sFired  = false;
      held5sFired  = false;
      shortFired   = false;
    } else {
      // Rising edge — button released
      unsigned long held = millis() - pressedAt;
      if (held < BTN_SHORT_MAX_MS && !held2sFired && !held3sFired) {
        shortFired = true;
      }
    }
    prevState = state;
    state     = newState;
  }

  bool isDown() { return state == LOW; }

  // One-shot events — consume and return true once
  bool getShortPress() { if (shortFired)  { shortFired  = false; return true; } return false; }
  bool getHeld2s()     { if (isDown() && !held2sFired && millis()-pressedAt >= BTN_HOLD2S) { held2sFired = true; return true; } return false; }
  bool getHeld3s()     { if (isDown() && !held3sFired && millis()-pressedAt >= BTN_HOLD3S) { held3sFired = true; return true; } return false; }
  bool getHeld5s()     { if (isDown() && !held5sFired && millis()-pressedAt >= BTN_HOLD5S) { held5sFired = true; return true; } return false; }
};

Button b1, b2;

// ─── NFC Reader State ─────────────────────────────────────────────────────────
struct ReaderState {
  bool          candidate;
  unsigned long cSinceMs;
  bool          stable;
  bool          lastStable;
  bool          armed;
  String        activeUID;
};

ReaderState r1 = {false, 0, false, false, false, ""};
ReaderState r2 = {false, 0, false, false, false, ""};

bool   rc522InField = false;
String rc522UID     = "";

// ─── Persistence ─────────────────────────────────────────────────────────────
void saveAll() {
  prefs.begin("pillcounter", false);
  prefs.putInt("left1",  pillsLeft1);
  prefs.putInt("left2",  pillsLeft2);
  prefs.putInt("max1",   pillsMax1);
  prefs.putInt("max2",   pillsMax2);
  prefs.putString("uid1", TAG1_UID);
  prefs.putString("uid2", TAG2_UID);
  prefs.end();
  Serial.println("[Prefs] Saved to flash.");
}

void loadAll() {
  prefs.begin("pillcounter", true);   // read-only
  pillsLeft1 = prefs.getInt("left1",  30);
  pillsLeft2 = prefs.getInt("left2",  30);
  pillsMax1  = prefs.getInt("max1",   30);
  pillsMax2  = prefs.getInt("max2",   30);
  TAG1_UID   = prefs.getString("uid1", "E3E31409");
  TAG2_UID   = prefs.getString("uid2", "4443AB04");
  prefs.end();
}

void factoryReset() {
  prefs.begin("pillcounter", false);
  prefs.clear();
  prefs.end();
  Serial.println("[Prefs] Flash cleared. Restarting...");
  delay(500);
  ESP.restart();
}

// ─── Helpers ─────────────────────────────────────────────────────────────────
String uidToString(const uint8_t* uid, uint8_t len) {
  String s = "";
  for (uint8_t i = 0; i < len; i++) {
    if (uid[i] < 0x10) s += "0";
    s += String(uid[i], HEX);
  }
  s.toUpperCase();
  return s;
}

int poolForUID(const String &uid) {
  if (uid == TAG1_UID) return 1;
  if (uid == TAG2_UID) return 2;
  return 0;
}

// ─── OLED Display ────────────────────────────────────────────────────────────
void updateOLED(Adafruit_SSD1306 &display, int poolNum, int pills, int pillsMax) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("POOL "); display.print(poolNum);
  display.drawLine(0, 11, OLED_W - 1, 11, SSD1306_WHITE);

  if (pills == 0) {
    display.setTextSize(2);
    display.setCursor(4, 16);   display.print("OUT OF");
    display.setCursor(22, 36);  display.print("PILLS");
    display.fillRect(0, 54, OLED_W, 10, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setTextSize(1);
    display.setCursor(14, 56);  display.print("CONTACT PHARMACY");
    display.setTextColor(SSD1306_WHITE);
  } else {
    int threshold = (pillsMax * LOW_STOCK_PCT + 99) / 100;
    if (pills <= threshold) {
      display.setTextSize(3);
      int digits = (pills >= 10) ? 2 : 1;
      display.setCursor((OLED_W - digits * 18) / 2, 14);
      display.print(pills);
      display.fillRect(0, 46, OLED_W, 18, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.setTextSize(1);
      display.setCursor(6, 49); display.print("PICK UP REFILL");
      display.setTextColor(SSD1306_WHITE);
    } else {
      display.setTextSize(4);
      int digits = (pills >= 100) ? 3 : (pills >= 10) ? 2 : 1;
      display.setCursor((OLED_W - digits * 24) / 2, 16);
      display.print(pills);
      display.setTextSize(1);
      display.setCursor(20, 56); display.print("pills remaining");
    }
  }
  display.display();
}

void flashOLED(Adafruit_SSD1306 &display, int poolNum, const char* msg) {
  display.clearDisplay();
  display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0); display.print("POOL "); display.print(poolNum);
  display.drawLine(0, 11, OLED_W - 1, 11, SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(0, 20); display.print(msg);
  display.display();
}

// Shows set-count UI on the pool's OLED
void showSetMode(Adafruit_SSD1306 &display, int poolNum, int tempCount) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Header
  display.setTextSize(1);
  display.setCursor(0, 0); display.print("SET POOL "); display.print(poolNum);
  display.drawLine(0, 11, OLED_W - 1, 11, SSD1306_WHITE);

  // Count — large centered
  display.setTextSize(4);
  int digits = (tempCount >= 100) ? 3 : (tempCount >= 10) ? 2 : 1;
  display.setCursor((OLED_W - digits * 24) / 2, 14);
  display.print(tempCount);

  // Instructions
  display.setTextSize(1);
  display.setCursor(0, 56); display.print("B1+10 B2+1 HOLD:SAVE");

  display.display();
}

// Shows enrollment prompt on the pool's OLED
void showEnrollMode(Adafruit_SSD1306 &display, int poolNum) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0); display.print("ENROLL POOL "); display.print(poolNum);
  display.drawLine(0, 11, OLED_W - 1, 11, SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(8, 18); display.print("SCAN TAG");
  display.setTextSize(1);
  display.setCursor(16, 48); display.print("FOR POOL "); display.print(poolNum);
  display.display();
}

// ─── NFC Read Functions ───────────────────────────────────────────────────────
bool readFob1(String &uidOut) {
  uint8_t uid[7], uidLen = 0;
  if (nfc1.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 50)) {
    uidOut = uidToString(uid, uidLen); return true;
  }
  uidOut = ""; return false;
}

bool readFob2(String &uidOut) {
  if (!rc522InField) {
    if (!rfid2.PICC_IsNewCardPresent() || !rfid2.PICC_ReadCardSerial()) {
      uidOut = ""; return false;
    }
    rc522UID = uidToString(rfid2.uid.uidByte, rfid2.uid.size);
    rc522InField = true;
    rfid2.PICC_HaltA(); rfid2.PCD_StopCrypto1();
    uidOut = rc522UID; return true;
  }
  byte buf[2]; byte sz = sizeof(buf);
  rfid2.PCD_StopCrypto1();
  MFRC522::StatusCode status = rfid2.PICC_WakeupA(buf, &sz);
  if (status == MFRC522::STATUS_OK || status == MFRC522::STATUS_COLLISION) {
    rfid2.PICC_HaltA(); rfid2.PCD_StopCrypto1();
    uidOut = rc522UID; return true;
  }
  rc522InField = false; rc522UID = ""; uidOut = ""; return false;
}

// Try both readers; return the first UID seen (used in enrollment mode)
bool readAnyFob(String &uidOut) {
  if (readFob1(uidOut) && uidOut.length() > 0) return true;
  if (readFob2(uidOut) && uidOut.length() > 0) return true;
  return false;
}

// ─── Normal Pill-Count Logic ──────────────────────────────────────────────────
void processReader(bool (*readFn)(String&), int readerNum, ReaderState &s) {
  String uid      = "";
  bool presentNow = readFn(uid);

  if (presentNow != s.candidate) { s.candidate = presentNow; s.cSinceMs = millis(); }
  if (millis() - s.cSinceMs >= DEBOUNCE_MS) s.stable = s.candidate;

  if (s.stable != s.lastStable) {
    if (s.stable) {
      int pool = poolForUID(uid);
      if (pool > 0) {
        s.armed = true; s.activeUID = uid;
        if (pool == 1) flashOLED(oled1, 1, "TAG ON");
        else           flashOLED(oled2, 2, "TAG ON");
        Serial.printf("[Reader %d] Tag placed → Pool %d  UID=%s\n", readerNum, pool, uid.c_str());
      } else {
        Serial.printf("[Reader %d] Unknown tag: %s\n", readerNum, uid.c_str());
      }
    } else {
      if (s.armed) {
        int pool = poolForUID(s.activeUID);
        Serial.printf("[Reader %d] Tag removed → Pool %d\n", readerNum, pool);
        if (pool == 1 && pillsLeft1 > 0) {
          pillsLeft1--;
          saveAll();
          Serial.printf("[Pool 1] %d pills left\n", pillsLeft1);
          updateOLED(oled1, 1, pillsLeft1, pillsMax1);
        } else if (pool == 2 && pillsLeft2 > 0) {
          pillsLeft2--;
          saveAll();
          Serial.printf("[Pool 2] %d pills left\n", pillsLeft2);
          updateOLED(oled2, 2, pillsLeft2, pillsMax2);
        }
        s.armed = false; s.activeUID = "";
      }
    }
    s.lastStable = s.stable;
  }
}

// ─── Button State Machine ─────────────────────────────────────────────────────
void handleButtons() {
  b1.update();
  b2.update();

  switch (appState) {

    case NORMAL: {
      // Both held 5s → enrollment
      if (b1.isDown() && b2.isDown()) {
        // Use b1.pressedAt as the start — enrollment fires when both have been
        // down long enough. We check both are still down after 5s.
        unsigned long downSince = max(b1.pressedAt, b2.pressedAt);
        if (!b1.held5sFired && !b2.held5sFired &&
            millis() - downSince >= BTN_HOLD5S) {
          b1.held5sFired = b2.held5sFired = true;
          appState = ENROLL_POOL1;
          showEnrollMode(oled1, 1);
          oled2.clearDisplay(); oled2.display();  // blank while waiting
          Serial.println("[Enroll] Mode started — scan tag for Pool 1");
        }
        break;
      }

      // B1 held 3s → set mode Pool 1
      if (b1.getHeld3s()) {
        setTemp       = pillsLeft1;
        setLastInputMs = millis();
        appState      = SET_POOL1;
        showSetMode(oled1, 1, setTemp);
        Serial.printf("[Set] Pool 1 set mode entered. Current: %d\n", setTemp);
        break;
      }

      // B2 held 3s → set mode Pool 2
      if (b2.getHeld3s()) {
        setTemp       = pillsLeft2;
        setLastInputMs = millis();
        appState      = SET_POOL2;
        showSetMode(oled2, 2, setTemp);
        Serial.printf("[Set] Pool 2 set mode entered. Current: %d\n", setTemp);
        break;
      }
      break;
    }

    case SET_POOL1: {
      bool changed = false;

      if (b1.getShortPress()) { setTemp += 10; changed = true; }   // +10
      if (b2.getShortPress()) { setTemp  += 1; changed = true; }   // +1
      if (setTemp < 0) setTemp = 0;

      if (changed) {
        setLastInputMs = millis();
        showSetMode(oled1, 1, setTemp);
        Serial.printf("[Set] Pool 1 temp count: %d\n", setTemp);
      }

      // Hold B1 2s → save
      if (b1.getHeld2s()) {
        pillsLeft1 = pillsMax1 = setTemp;
        saveAll();
        appState = NORMAL;
        updateOLED(oled1, 1, pillsLeft1, pillsMax1);
        Serial.printf("[Set] Pool 1 saved: %d pills\n", pillsLeft1);
        break;
      }

      // Timeout → cancel
      if (millis() - setLastInputMs >= SET_TIMEOUT_MS) {
        appState = NORMAL;
        updateOLED(oled1, 1, pillsLeft1, pillsMax1);
        Serial.println("[Set] Pool 1 set mode cancelled (timeout).");
      }
      break;
    }

    case SET_POOL2: {
      bool changed = false;

      if (b1.getShortPress()) { setTemp += 10; changed = true; }   // +10
      if (b2.getShortPress()) { setTemp  += 1; changed = true; }   // +1
      if (setTemp < 0) setTemp = 0;

      if (changed) {
        setLastInputMs = millis();
        showSetMode(oled2, 2, setTemp);
        Serial.printf("[Set] Pool 2 temp count: %d\n", setTemp);
      }

      // Hold B2 2s → save
      if (b2.getHeld2s()) {
        pillsLeft2 = pillsMax2 = setTemp;
        saveAll();
        appState = NORMAL;
        updateOLED(oled2, 2, pillsLeft2, pillsMax2);
        Serial.printf("[Set] Pool 2 saved: %d pills\n", pillsLeft2);
        break;
      }

      // Timeout → cancel
      if (millis() - setLastInputMs >= SET_TIMEOUT_MS) {
        appState = NORMAL;
        updateOLED(oled2, 2, pillsLeft2, pillsMax2);
        Serial.println("[Set] Pool 2 set mode cancelled (timeout).");
      }
      break;
    }

    case ENROLL_POOL1: {
      String uid = "";
      if (readAnyFob(uid) && uid.length() > 0) {
        TAG1_UID = uid;
        Serial.printf("[Enroll] Pool 1 UID saved: %s\n", uid.c_str());
        flashOLED(oled1, 1, "SAVED!");
        delay(1000);
        showEnrollMode(oled2, 2);
        appState = ENROLL_POOL2;
        Serial.println("[Enroll] Scan tag for Pool 2");
      }
      break;
    }

    case ENROLL_POOL2: {
      String uid = "";
      if (readAnyFob(uid) && uid.length() > 0) {
        TAG2_UID = uid;
        Serial.printf("[Enroll] Pool 2 UID saved: %s\n", uid.c_str());
        flashOLED(oled2, 2, "SAVED!");
        saveAll();
        delay(1000);
        appState = NORMAL;
        updateOLED(oled1, 1, pillsLeft1, pillsMax1);
        updateOLED(oled2, 2, pillsLeft2, pillsMax2);
        Serial.println("[Enroll] Complete. Returning to normal.");
      }
      break;
    }
  }
}

// ─── Serial Command Interface ─────────────────────────────────────────────────
void handleSerial() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toLowerCase();

  if (cmd == "status") {
    Serial.println("=== Pill Counter Status ===");
    Serial.printf("Pool 1: %d / %d pills  UID=%s\n", pillsLeft1, pillsMax1, TAG1_UID.c_str());
    Serial.printf("Pool 2: %d / %d pills  UID=%s\n", pillsLeft2, pillsMax2, TAG2_UID.c_str());
    Serial.printf("Low-stock threshold: %d%%\n", LOW_STOCK_PCT);

  } else if (cmd == "reset 1") {
    pillsLeft1 = pillsMax1;
    saveAll();
    updateOLED(oled1, 1, pillsLeft1, pillsMax1);
    Serial.printf("[Reset] Pool 1 refilled to %d\n", pillsLeft1);

  } else if (cmd == "reset 2") {
    pillsLeft2 = pillsMax2;
    saveAll();
    updateOLED(oled2, 2, pillsLeft2, pillsMax2);
    Serial.printf("[Reset] Pool 2 refilled to %d\n", pillsLeft2);

  } else if (cmd.startsWith("setmax 1 ")) {
    int n = cmd.substring(9).toInt();
    if (n > 0) {
      pillsMax1 = pillsLeft1 = n;
      saveAll();
      updateOLED(oled1, 1, pillsLeft1, pillsMax1);
      Serial.printf("[SetMax] Pool 1 → %d\n", n);
    }

  } else if (cmd.startsWith("setmax 2 ")) {
    int n = cmd.substring(9).toInt();
    if (n > 0) {
      pillsMax2 = pillsLeft2 = n;
      saveAll();
      updateOLED(oled2, 2, pillsLeft2, pillsMax2);
      Serial.printf("[SetMax] Pool 2 → %d\n", n);
    }

  } else if (cmd == "enroll") {
    appState = ENROLL_POOL1;
    showEnrollMode(oled1, 1);
    oled2.clearDisplay(); oled2.display();
    Serial.println("[Enroll] Mode started via Serial — scan tag for Pool 1");

  } else if (cmd == "factory") {
    Serial.println("[Factory] Wiping flash...");
    factoryReset();   // restarts ESP32

  } else {
    Serial.println("Commands: status | reset 1 | reset 2 | setmax 1 <n> | setmax 2 <n> | enroll | factory");
  }
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(I2C1_SDA, I2C1_SCL);
  Wire1.begin(I2C2_SDA, I2C2_SCL);

  if (!oled1.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("[ERROR] OLED #1 not found.");
    while (true) delay(10);
  }
  if (!oled2.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("[ERROR] OLED #2 not found.");
    while (true) delay(10);
  }

  nfc1.begin();
  if (!nfc1.getFirmwareVersion()) {
    Serial.println("[ERROR] PN532 not found.");
    while (true) delay(10);
  }
  nfc1.SAMConfig();
  Serial.println("[OK] PN532 ready.");

  SPI.begin(RC522_SCK, RC522_MISO, RC522_MOSI, RC522_SS);
  rfid2.PCD_Init();
  Serial.println("[OK] RC522 ready.");

  b1.begin(BTN1_PIN);
  b2.begin(BTN2_PIN);

  loadAll();

  r1.cSinceMs = r2.cSinceMs = millis();

  updateOLED(oled1, 1, pillsLeft1, pillsMax1);
  updateOLED(oled2, 2, pillsLeft2, pillsMax2);

  Serial.println("\n=== Dual Pill Counter v3.0 ===");
  Serial.printf("Pool 1: %d / %d pills  UID=%s\n", pillsLeft1, pillsMax1, TAG1_UID.c_str());
  Serial.printf("Pool 2: %d / %d pills  UID=%s\n", pillsLeft2, pillsMax2, TAG2_UID.c_str());
  Serial.println("Type 'status' for help.");
  Serial.println("--------------------------------------------------");
}

// ─── Loop ────────────────────────────────────────────────────────────────────
void loop() {
  handleButtons();

  if (appState == NORMAL) {
    processReader(readFob1, 1, r1);
    processReader(readFob2, 2, r2);
  }

  handleSerial();
  delay(20);
}
