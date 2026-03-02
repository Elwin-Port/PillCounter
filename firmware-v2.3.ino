// firmware-v2.2.ino
// Dual Pill Counter — PN532 (I2C) + RC522 (SPI), 2 pools, 2 LCDs
//
// Changes from v2.1:
//   - LCD library changed to hd44780 (LiquidCrystal_I2C has timing issues on ESP32)
//   - Tags are now locked to pools by UID, not by which reader detected them.
//     Tag E3E31409 always decrements Pool 1.
//     Tag 4443AB04 always decrements Pool 2.
//     Unknown tags are ignored.
//
// Required libraries (Arduino IDE → Tools → Manage Libraries):
//   - Adafruit PN532        (>= 1.2.0)
//   - MFRC522               (search "RFID" by miguelbalboa)
//   - hd44780               (by Bill Perry — use this instead of LiquidCrystal_I2C)
//
// ─── Wiring ──────────────────────────────────────────────────────────────────
//
//  I2C Bus (Wire, SDA=21 SCL=22):
//    PN532    — SDA=21, SCL=22, IRQ=4, RESET=5
//               Mode switches: switch 1=ON, switch 2=OFF  (I2C mode)
//    LCD #1   — SDA=21, SCL=22, I2C addr 0x27
//    LCD #2   — SDA=21, SCL=22, I2C addr 0x3F
//               Both LCDs share the bus — addresses MUST differ.
//               Address is set by A0/A1/A2 solder jumpers on the PCF8574 backpack.
//               Run File→Examples→Wire→i2c_scanner to confirm your addresses.
//
//  SPI Bus (VSPI):
//    RC522    — SDA(SS)=15, SCK=18, MOSI=23, MISO=19, RST=27
//               IRQ — leave unconnected
//               3.3V ONLY — never connect to 5V
//
// ─────────────────────────────────────────────────────────────────────────────

#include <Wire.h>
#include <SPI.h>
#include <Adafruit_PN532.h>
#include <MFRC522.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

// ─── I2C Pins ────────────────────────────────────────────────────────────────
#define I2C_SDA  21
#define I2C_SCL  22

// ─── PN532 Pins ──────────────────────────────────────────────────────────────
#define PN532_IRQ    4
#define PN532_RESET  5

// ─── RC522 Pins ──────────────────────────────────────────────────────────────
#define RC522_SS    15   // board label: SDA
#define RC522_SCK   18
#define RC522_MISO  19
#define RC522_MOSI  23
#define RC522_RST   27   // board label: RST

// ─── Devices ─────────────────────────────────────────────────────────────────
Adafruit_PN532 nfc1(PN532_IRQ, PN532_RESET, &Wire);
MFRC522        rfid2(RC522_SS, RC522_RST);

hd44780_I2Cexp lcd1(0x27);   // Pool 1 display — change address if your backpack differs
hd44780_I2Cexp lcd2(0x3F);   // Pool 2 display — change address if your backpack differs

// ─── Tag-to-Pool Registry ────────────────────────────────────────────────────
// A tag always routes to its assigned pool regardless of which reader sees it.
// Add more tags here as needed.
const String TAG1_UID = "E3E31409";   // → Pool 1
const String TAG2_UID = "4443AB04";   // → Pool 2

// ─── Pill Pools ──────────────────────────────────────────────────────────────
int pillsLeft1 = 30;
int pillsLeft2 = 30;

// ─── Per-Reader Debounce + Cycle State ───────────────────────────────────────
struct ReaderState {
  bool          candidate;   // raw present/absent reading this frame
  unsigned long cSinceMs;    // timestamp when candidate last changed
  bool          stable;      // debounced present/absent
  bool          lastStable;  // previous stable value
  bool          armed;       // true once a known tag has been placed
  String        activeUID;   // UID of the tag that armed this reader
};

ReaderState r1 = {false, 0, false, false, false, ""};
ReaderState r2 = {false, 0, false, false, false, ""};

const unsigned long DEBOUNCE_MS = 250;

// ─── RC522 Presence Tracking ─────────────────────────────────────────────────
// RC522 is non-blocking. After first detection we poll via WakeupA each loop
// to confirm the card is still in the field.
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

// Returns 1, 2, or 0 (unknown) for a given UID string
int poolForUID(const String &uid) {
  if (uid == TAG1_UID) return 1;
  if (uid == TAG2_UID) return 2;
  return 0;
}

void updateLCD(hd44780_I2Cexp &lcd, int poolNum, int pills) {
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
// Non-blocking. Detects placement via PICC_IsNewCardPresent, then polls
// WakeupA each loop to confirm the card is still present.
bool readFob2(String &uidOut) {
  if (!rc522InField) {
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

  // Card was present — confirm it's still in the field
  byte buf[2];
  byte sz = sizeof(buf);
  rfid2.PCD_StopCrypto1();
  MFRC522::StatusCode status = rfid2.PICC_WakeupA(buf, &sz);
  if (status == MFRC522::STATUS_OK || status == MFRC522::STATUS_COLLISION) {
    rfid2.PICC_HaltA();
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

// ─── Cycle Logic ─────────────────────────────────────────────────────────────
// Routing is determined by the TAG UID, not by which reader saw it.
// - Tag placed:   look up UID → if known, arm the reader and store the UID
// - Tag removed:  look up stored UID → decrement that pool, update that LCD
// - Unknown tags: ignored entirely
void processReader(bool (*readFn)(String&), int readerNum, ReaderState &s) {
  String uid      = "";
  bool presentNow = readFn(uid);

  // Debounce
  if (presentNow != s.candidate) {
    s.candidate = presentNow;
    s.cSinceMs  = millis();
  }
  if (millis() - s.cSinceMs >= DEBOUNCE_MS) {
    s.stable = s.candidate;
  }

  if (s.stable != s.lastStable) {
    if (s.stable) {
      // ── Tag placed ──
      int pool = poolForUID(uid);
      if (pool > 0) {
        s.armed     = true;
        s.activeUID = uid;
        Serial.print("[Reader "); Serial.print(readerNum);
        Serial.print("] Tag detected → Pool "); Serial.print(pool);
        Serial.print("  UID = "); Serial.println(uid);
      } else {
        Serial.print("[Reader "); Serial.print(readerNum);
        Serial.print("] Unknown tag — ignored.  UID = "); Serial.println(uid);
      }

    } else {
      // ── Tag removed ──
      if (s.armed) {
        int pool = poolForUID(s.activeUID);
        Serial.print("[Reader "); Serial.print(readerNum);
        Serial.print("] Tag removed → Pool "); Serial.println(pool);

        if (pool == 1 && pillsLeft1 > 0) {
          pillsLeft1--;
          Serial.print("[Pool 1] Pills left = "); Serial.println(pillsLeft1);
          updateLCD(lcd1, 1, pillsLeft1);
        } else if (pool == 2 && pillsLeft2 > 0) {
          pillsLeft2--;
          Serial.print("[Pool 2] Pills left = "); Serial.println(pillsLeft2);
          updateLCD(lcd2, 2, pillsLeft2);
        }

        s.armed     = false;
        s.activeUID = "";
      }
    }

    s.lastStable = s.stable;
  }
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(I2C_SDA, I2C_SCL);

  // Init LCDs — hd44780 begin() returns 0 on success, non-zero on failure
  if (lcd1.begin(16, 2) != 0) {
    Serial.println("[ERROR] LCD #1 failed. Check address (0x27) and wiring.");
    while (true) delay(10);
  }
  lcd1.backlight();

  if (lcd2.begin(16, 2) != 0) {
    Serial.println("[ERROR] LCD #2 failed. Check address (0x3F) and wiring.");
    while (true) delay(10);
  }
  lcd2.backlight();

  Serial.println("\n=== Dual Pill Counter v2.2 (PN532 + RC522) ===");

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
  Serial.print("Tag 1 (Pool 1) UID = "); Serial.println(TAG1_UID);
  Serial.print("Tag 2 (Pool 2) UID = "); Serial.println(TAG2_UID);
  Serial.println("--------------------------------------------------");
}

// ─── Loop ────────────────────────────────────────────────────────────────────
void loop() {
  processReader(readFob1, 1, r1);
  processReader(readFob2, 2, r2);
  delay(30);
}
