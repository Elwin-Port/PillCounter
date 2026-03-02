// firmware-v2.ino
// Dual PN532 Pill Counter — 2 pools, 2 readers, 2 LCD displays
//
// Required libraries (install via Arduino Library Manager):
//   - Adafruit PN532       (>= 1.2.0, needs TwoWire* constructor)
//   - LiquidCrystal I2C    (by Frank de Brabander)
//
// ─── Wiring Summary ─────────────────────────────────────────────────────────
//
//  I2C Bus 1 (Wire, SDA=21 SCL=22):
//    PN532 #1  — SDA/SCL + IRQ=4  + RESET=5
//    LCD  #1   — SDA/SCL, I2C addr 0x27   (set via backpack jumpers)
//    LCD  #2   — SDA/SCL, I2C addr 0x3F   (set via backpack jumpers)
//
//  I2C Bus 2 (Wire1, SDA=16 SCL=17):
//    PN532 #2  — SDA/SCL + IRQ=25 + RESET=26
//
//  PN532 mode switches:  1=ON  0=OFF  (I2C mode)
//
//  NOTE: Both LCDs share Bus 1 — their I2C addresses MUST differ.
//        Common PCF8574 addresses are 0x27 and 0x3F.  If both yours are
//        the same, change one via the A0/A1/A2 solder jumpers on the backpack.
// ────────────────────────────────────────────────────────────────────────────

#include <Wire.h>
#include <Adafruit_PN532.h>
#include <LiquidCrystal_I2C.h>

// ─── I2C Bus Pin Assignments ─────────────────────────────────────────────────
#define I2C1_SDA  21    // Bus 1: Wire  — PN532 #1 + LCD #1 + LCD #2
#define I2C1_SCL  22
#define I2C2_SDA  16    // Bus 2: Wire1 — PN532 #2
#define I2C2_SCL  17

// ─── PN532 IRQ / RESET Pins ──────────────────────────────────────────────────
#define PN532_1_IRQ    4
#define PN532_1_RESET  5
#define PN532_2_IRQ   25
#define PN532_2_RESET 26

// ─── Device Objects ──────────────────────────────────────────────────────────
// Wire1 is the second hardware I2C bus built into the ESP32 Arduino core.
Adafruit_PN532 nfc1(PN532_1_IRQ, PN532_1_RESET, &Wire);
Adafruit_PN532 nfc2(PN532_2_IRQ, PN532_2_RESET, &Wire1);

LiquidCrystal_I2C lcd1(0x27, 16, 2);   // Pool 1 display
LiquidCrystal_I2C lcd2(0x3F, 16, 2);   // Pool 2 display

// ─── Pill Pools ──────────────────────────────────────────────────────────────
int pillsLeft1 = 30;
int pillsLeft2 = 30;

// ─── Per-Reader Debounce + Cycle State ───────────────────────────────────────
struct ReaderState {
  bool          candidate;    // raw read this frame
  unsigned long cSinceMs;     // when candidate last changed
  bool          stable;       // debounced present/absent
  bool          lastStable;   // previous debounced value
  bool          armed;        // tag has been placed at least once this cycle
};

ReaderState r1 = {false, 0, false, false, false};
ReaderState r2 = {false, 0, false, false, false};

const unsigned long DEBOUNCE_MS = 250;

// ─── Helpers ─────────────────────────────────────────────────────────────────
String uidToString(const uint8_t* uid, uint8_t uidLen) {
  String s = "";
  for (uint8_t i = 0; i < uidLen; i++) {
    if (uid[i] < 0x10) s += "0";
    s += String(uid[i], HEX);
  }
  s.toUpperCase();
  return s;
}

bool readFob(Adafruit_PN532 &nfc, String &uidOut) {
  uint8_t uid[7];
  uint8_t uidLen = 0;
  bool ok = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 50);
  if (ok) { uidOut = uidToString(uid, uidLen); return true; }
  uidOut = "";
  return false;
}

// Refresh one LCD: line 0 = label, line 1 = pill count
void updateLCD(LiquidCrystal_I2C &lcd, int poolNum, int pills) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Pool ");
  lcd.print(poolNum);
  lcd.print(" Pills:");
  lcd.setCursor(0, 1);
  lcd.print(pills);
  lcd.print(" remaining");
}

// Init one reader; halts on failure
bool initReader(Adafruit_PN532 &nfc, int num) {
  nfc.begin();
  if (!nfc.getFirmwareVersion()) {
    Serial.print("[ERROR] PN532 #");
    Serial.print(num);
    Serial.println(" not found. Check wiring and I2C mode switches.");
    return false;
  }
  nfc.SAMConfig();
  Serial.print("[OK] PN532 #");
  Serial.print(num);
  Serial.println(" ready.");
  return true;
}

// ─── Per-Reader Cycle Logic ───────────────────────────────────────────────────
// One full cycle (tag present → tag removed) decrements pillsLeft by 1.
void processReader(Adafruit_PN532 &nfc, int poolNum,
                   int &pillsLeft, ReaderState &s,
                   LiquidCrystal_I2C &lcd) {
  String uid = "";
  bool presentNow = readFob(nfc, uid);

  // Debounce: only commit a state change after it holds for DEBOUNCE_MS
  if (presentNow != s.candidate) {
    s.candidate = presentNow;
    s.cSinceMs  = millis();
  }
  if (millis() - s.cSinceMs >= DEBOUNCE_MS) {
    s.stable = s.candidate;
  }

  // Act on transitions only
  if (s.stable != s.lastStable) {
    if (s.stable) {
      // Tag just placed
      s.armed = true;
      Serial.print("[Pool ");
      Serial.print(poolNum);
      Serial.print("] Tag detected. UID = ");
      Serial.println(uid);
    } else {
      // Tag just removed
      Serial.print("[Pool ");
      Serial.print(poolNum);
      Serial.println("] Tag removed.");

      if (s.armed && s.lastStable) {
        if (pillsLeft > 0) pillsLeft--;
        Serial.print("[Pool ");
        Serial.print(poolNum);
        Serial.print("] Pills left = ");
        Serial.println(pillsLeft);
        updateLCD(lcd, poolNum, pillsLeft);
      }
    }
    s.lastStable = s.stable;
  }
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(I2C1_SDA,  I2C1_SCL);
  Wire1.begin(I2C2_SDA, I2C2_SCL);

  lcd1.init(); lcd1.backlight();
  lcd2.init(); lcd2.backlight();

  Serial.println("\n=== Dual PN532 Pill Counter ===");
  Serial.println("Initializing readers...");

  if (!initReader(nfc1, 1)) while (true) delay(10);
  if (!initReader(nfc2, 2)) while (true) delay(10);

  r1.cSinceMs = millis();
  r2.cSinceMs = millis();

  updateLCD(lcd1, 1, pillsLeft1);
  updateLCD(lcd2, 2, pillsLeft2);

  Serial.print("Pool 1 starting pills = "); Serial.println(pillsLeft1);
  Serial.print("Pool 2 starting pills = "); Serial.println(pillsLeft2);
  Serial.println("Place tag on a reader, then remove to count down.");
  Serial.println("--------------------------------------------------");
}

// ─── Loop ────────────────────────────────────────────────────────────────────
void loop() {
  processReader(nfc1, 1, pillsLeft1, r1, lcd1);
  processReader(nfc2, 2, pillsLeft2, r2, lcd2);
  delay(30);
}
