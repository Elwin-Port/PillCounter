// firmware-v2.ino
// Dual Pill Counter — PN532 (I2C) for Pool 1 + RC522 (SPI) for Pool 2
//
// Required libraries (Arduino Library Manager):
//   - Adafruit PN532        (>= 1.2.0)
//   - MFRC522               (search "RFID" by miguelbalboa)
//   - LiquidCrystal I2C     (by Frank de Brabander)
//
// ─── Wiring ──────────────────────────────────────────────────────────────────
//
//  I2C Bus (Wire, SDA=21 SCL=22):
//    PN532    — SDA=21, SCL=22, IRQ=4, RESET=5
//               Mode switches: switch 1 = ON, switch 2 = OFF  (I2C mode)
//    LCD #1   — SDA=21, SCL=22, I2C addr 0x27
//    LCD #2   — SDA=21, SCL=22, I2C addr 0x3F
//               Both LCDs share the bus — their addresses MUST differ.
//               Address is set via A0/A1/A2 jumpers on the PCF8574 backpack.
//
//  SPI Bus (VSPI):
//    RC522    — SDA(SS)=15, SCK=18, MOSI=23, MISO=19, RST=27
//               IRQ pin — leave unconnected (not used)
//               3.3V ONLY — never connect to 5V
//
// ─────────────────────────────────────────────────────────────────────────────

#include <Wire.h>
#include <SPI.h>
#include <Adafruit_PN532.h>
#include <MFRC522.h>
#include <LiquidCrystal_I2C.h>

// ─── I2C Pins ────────────────────────────────────────────────────────────────
#define I2C_SDA  21
#define I2C_SCL  22

// ─── PN532 Pins (I2C) ────────────────────────────────────────────────────────
#define PN532_IRQ    4
#define PN532_RESET  5

// ─── RC522 Pins (SPI / VSPI) ─────────────────────────────────────────────────
#define RC522_SS    15   // board label: SDA
#define RC522_SCK   18
#define RC522_MISO  19
#define RC522_MOSI  23
#define RC522_RST   27   // board label: RST

// ─── Device Objects ──────────────────────────────────────────────────────────
Adafruit_PN532    nfc1(PN532_IRQ, PN532_RESET, &Wire);
MFRC522           rfid2(RC522_SS, RC522_RST);

LiquidCrystal_I2C lcd1(0x27, 16, 2);   // Pool 1 display
LiquidCrystal_I2C lcd2(0x3F, 16, 2);   // Pool 2 display

// ─── Pill Pools ──────────────────────────────────────────────────────────────
int pillsLeft1 = 30;
int pillsLeft2 = 30;

// ─── Per-Reader Debounce + Cycle State ───────────────────────────────────────
struct ReaderState {
  bool          candidate;   // raw present/absent reading this frame
  unsigned long cSinceMs;    // timestamp when candidate last changed
  bool          stable;      // debounced present/absent
  bool          lastStable;  // previous stable value
  bool          armed;       // true after first tag placement this cycle
};

ReaderState r1 = {false, 0, false, false, false};
ReaderState r2 = {false, 0, false, false, false};

const unsigned long DEBOUNCE_MS = 250;

// ─── RC522 Presence State ────────────────────────────────────────────────────
// The RC522 is non-blocking. After detecting a card, we must poll via WakeupA
// each loop to confirm the card is still in the field. This state tracks that.
bool   rc522InField = false;
String rc522UID     = "";

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

void updateLCD(LiquidCrystal_I2C &lcd, int poolNum, int pills) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Pool "); lcd.print(poolNum); lcd.print(" Pills:");
  lcd.setCursor(0, 1);
  lcd.print(pills); lcd.print(" remaining");
}

// ─── Read Functions ──────────────────────────────────────────────────────────

// Pool 1 — PN532 over I2C.
// Blocks up to 50 ms. Returns true while a tag is in the field.
bool readFob1(String &uidOut) {
  uint8_t uid[7];
  uint8_t uidLen = 0;
  bool ok = nfc1.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 50);
  if (ok) { uidOut = uidToString(uid, uidLen); return true; }
  uidOut = "";
  return false;
}

// Pool 2 — RC522 over SPI.
// Non-blocking. On first detection, saves UID and halts the card.
// Each subsequent call uses WakeupA to confirm the card is still present.
// When WakeupA fails, the card has been removed.
bool readFob2(String &uidOut) {
  if (!rc522InField) {
    // Waiting for a new card
    if (!rfid2.PICC_IsNewCardPresent() || !rfid2.PICC_ReadCardSerial()) {
      uidOut = "";
      return false;
    }
    rc522UID     = uidToString(rfid2.uid.uidByte, rfid2.uid.size);
    rc522InField = true;
    rfid2.PICC_HaltA();
    rfid2.PCD_StopCrypto1();
    uidOut = rc522UID;
    return true;
  }

  // Card was present — ping it via WakeupA to confirm it's still in the field
  byte buf[2];
  byte sz = sizeof(buf);
  rfid2.PCD_StopCrypto1();
  MFRC522::StatusCode status = rfid2.PICC_WakeupA(buf, &sz);
  if (status == MFRC522::STATUS_OK || status == MFRC522::STATUS_COLLISION) {
    rfid2.PICC_HaltA();       // put it back to halt so WakeupA works next loop
    rfid2.PCD_StopCrypto1();
    uidOut = rc522UID;
    return true;
  }

  // No response — card removed
  rc522InField = false;
  rc522UID     = "";
  uidOut       = "";
  return false;
}

// ─── Shared Cycle Logic ───────────────────────────────────────────────────────
// readFn  — pointer to readFob1 or readFob2
// One full cycle (tag placed → tag removed) decrements pillsLeft by 1.
void processReader(bool (*readFn)(String&), int poolNum,
                   int &pillsLeft, ReaderState &s,
                   LiquidCrystal_I2C &lcd) {
  String uid      = "";
  bool presentNow = readFn(uid);

  // Debounce: lock in state only after it holds for DEBOUNCE_MS
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
      s.armed = true;
      Serial.print("[Pool "); Serial.print(poolNum);
      Serial.print("] Tag detected. UID = "); Serial.println(uid);
    } else {
      Serial.print("[Pool "); Serial.print(poolNum);
      Serial.println("] Tag removed.");
      if (s.armed && s.lastStable) {
        if (pillsLeft > 0) pillsLeft--;
        Serial.print("[Pool "); Serial.print(poolNum);
        Serial.print("] Pills left = "); Serial.println(pillsLeft);
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

  // I2C — PN532 + both LCDs
  Wire.begin(I2C_SDA, I2C_SCL);
  lcd1.init(); lcd1.backlight();
  lcd2.init(); lcd2.backlight();

  Serial.println("\n=== Dual Pill Counter (PN532 + RC522) ===");

  // Init PN532
  nfc1.begin();
  if (!nfc1.getFirmwareVersion()) {
    Serial.println("[ERROR] PN532 not found. Check wiring and I2C mode switches.");
    while (true) delay(10);
  }
  nfc1.SAMConfig();
  Serial.println("[OK] PN532 ready.");

  // Init RC522
  SPI.begin(RC522_SCK, RC522_MISO, RC522_MOSI, RC522_SS);
  rfid2.PCD_Init();
  Serial.println("[OK] RC522 ready.");

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
  processReader(readFob1, 1, pillsLeft1, r1, lcd1);
  processReader(readFob2, 2, pillsLeft2, r2, lcd2);
  delay(30);
}
