// firmware-v2.4.ino
// Dual Pill Counter — PN532 (I2C) + RC522 (SPI), 2 pools, 2 OLED displays
//
// Changes from v2.3:
//   - Replaced LCD (hd44780) with OLED SSD1306 displays
//   - Each OLED is on its own I2C bus — address conflicts are impossible
//
// Required libraries (Arduino IDE → Tools → Manage Libraries):
//   - Adafruit PN532        (>= 1.2.0)
//   - MFRC522               (search "RFID" by miguelbalboa)
//   - Adafruit SSD1306      (by Adafruit)
//   - Adafruit GFX Library  (by Adafruit — SSD1306 depends on this)
//
// ─── Wiring ──────────────────────────────────────────────────────────────────
//
//  I2C Bus 1 (Wire):
//    PN532     — SDA=21, SCL=22, IRQ=4, RESET=5
//               Mode switches: switch 1=ON, switch 2=OFF  (I2C mode)
//    OLED #1   — SDA=21, SCL=22, VCC=3.3V, GND=GND
//
//  I2C Bus 2 (Wire1):
//    OLED #2   — SDA=16, SCL=17, VCC=3.3V, GND=GND
//
//    Both OLEDs are on separate buses — address conflicts impossible.
//    If your OLED doesn't show up, run File→Examples→Wire→i2c_scanner
//    on each bus to confirm the address and update OLED_ADDRESS below.
//
//  SPI Bus (VSPI):
//    RC522     — SDA(SS)=15, SCK=18, MOSI=23, MISO=19, RST=27
//               IRQ — leave unconnected
//               3.3V ONLY — never connect to 5V
//
// ─────────────────────────────────────────────────────────────────────────────

#include <Wire.h>
#include <SPI.h>
#include <Adafruit_PN532.h>
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ─── I2C Bus Pins ────────────────────────────────────────────────────────────
#define I2C1_SDA  21    // Bus 1: PN532 + OLED #1
#define I2C1_SCL  22
#define I2C2_SDA  16    // Bus 2: OLED #2
#define I2C2_SCL  17

// ─── PN532 Pins ──────────────────────────────────────────────────────────────
#define PN532_IRQ    4
#define PN532_RESET  5

// ─── RC522 Pins ──────────────────────────────────────────────────────────────
#define RC522_SS    15   // board label: SDA
#define RC522_SCK   18
#define RC522_MISO  19
#define RC522_MOSI  23
#define RC522_RST   27   // board label: RST

// ─── OLED Settings ───────────────────────────────────────────────────────────
#define OLED_W        128
#define OLED_H         64
#define OLED_ADDRESS 0x3C   // most OLEDs default to 0x3C — change to 0x3D if needed
#define OLED_RESET    -1    // no reset pin used

// ─── Devices ─────────────────────────────────────────────────────────────────
Adafruit_PN532   nfc1(PN532_IRQ, PN532_RESET, &Wire);
MFRC522          rfid2(RC522_SS, RC522_RST);

Adafruit_SSD1306 oled1(OLED_W, OLED_H, &Wire,  OLED_RESET);  // Pool 1
Adafruit_SSD1306 oled2(OLED_W, OLED_H, &Wire1, OLED_RESET);  // Pool 2

// ─── Tag-to-Pool Registry ────────────────────────────────────────────────────
// A tag always routes to its assigned pool regardless of which reader sees it.
const String TAG1_UID = "E3E31409";   // → Pool 1
const String TAG2_UID = "4443AB04";   // → Pool 2

// ─── Pill Pools ──────────────────────────────────────────────────────────────
int pillsLeft1 = 30;
int pillsLeft2 = 30;

// ─── Per-Reader State ────────────────────────────────────────────────────────
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

const unsigned long DEBOUNCE_MS = 250;

// ─── RC522 Presence Tracking ─────────────────────────────────────────────────
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

int poolForUID(const String &uid) {
  if (uid == TAG1_UID) return 1;
  if (uid == TAG2_UID) return 2;
  return 0;
}

// Draws the pill count screen on one OLED
void updateOLED(Adafruit_SSD1306 &display, int poolNum, int pills) {
  display.clearDisplay();

  // Pool label — small text, top
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("POOL ");
  display.print(poolNum);

  // Divider line
  display.drawLine(0, 11, OLED_W - 1, 11, SSD1306_WHITE);

  // Pill count — large centered number
  display.setTextSize(4);
  int digits = (pills >= 100) ? 3 : (pills >= 10) ? 2 : 1;
  int xPos   = (OLED_W - digits * 24) / 2;
  display.setCursor(xPos, 16);
  display.print(pills);

  // "pills remaining" label — small text, bottom
  display.setTextSize(1);
  display.setCursor(20, 56);
  display.print("pills remaining");

  display.display();
}

// Flashes a brief status message when a tag is placed
void flashOLED(Adafruit_SSD1306 &display, int poolNum, const char* msg) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("POOL ");
  display.print(poolNum);
  display.drawLine(0, 11, OLED_W - 1, 11, SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(0, 20);
  display.print(msg);
  display.display();
}

// ─── Read Functions ──────────────────────────────────────────────────────────

// Reader 1 — PN532 over I2C. Blocks up to 50 ms.
bool readFob1(String &uidOut) {
  uint8_t uid[7];
  uint8_t uidLen = 0;
  bool ok = nfc1.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 50);
  if (ok) { uidOut = uidToString(uid, uidLen); return true; }
  uidOut = "";
  return false;
}

// Reader 2 — RC522 over SPI. Non-blocking.
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

  rc522InField = false;
  rc522UID     = "";
  uidOut       = "";
  return false;
}

// ─── Cycle Logic ─────────────────────────────────────────────────────────────
// Tag placed  → arm + flash OLED "TAG ON"
// Tag removed → decrement pool, update OLED count
// Unknown tag → ignored
void processReader(bool (*readFn)(String&), int readerNum, ReaderState &s) {
  String uid      = "";
  bool presentNow = readFn(uid);

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
        if (pool == 1) flashOLED(oled1, 1, "TAG ON");
        else           flashOLED(oled2, 2, "TAG ON");
        Serial.print("[Reader "); Serial.print(readerNum);
        Serial.print("] Tag placed -> Pool "); Serial.print(pool);
        Serial.print("  UID = "); Serial.println(uid);
      } else {
        Serial.print("[Reader "); Serial.print(readerNum);
        Serial.print("] Unknown tag ignored.  UID = "); Serial.println(uid);
      }

    } else {
      // ── Tag removed ──
      if (s.armed) {
        int pool = poolForUID(s.activeUID);
        Serial.print("[Reader "); Serial.print(readerNum);
        Serial.print("] Tag removed -> Pool "); Serial.println(pool);

        if (pool == 1 && pillsLeft1 > 0) {
          pillsLeft1--;
          Serial.print("[Pool 1] Pills left = "); Serial.println(pillsLeft1);
          updateOLED(oled1, 1, pillsLeft1);
        } else if (pool == 2 && pillsLeft2 > 0) {
          pillsLeft2--;
          Serial.print("[Pool 2] Pills left = "); Serial.println(pillsLeft2);
          updateOLED(oled2, 2, pillsLeft2);
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

  Wire.begin(I2C1_SDA,  I2C1_SCL);
  Wire1.begin(I2C2_SDA, I2C2_SCL);

  if (!oled1.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("[ERROR] OLED #1 not found. Check wiring on SDA=21 SCL=22.");
    while (true) delay(10);
  }
  if (!oled2.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("[ERROR] OLED #2 not found. Check wiring on SDA=16 SCL=17.");
    while (true) delay(10);
  }

  Serial.println("\n=== Dual Pill Counter v2.4 (PN532 + RC522 + OLED) ===");

  nfc1.begin();
  if (!nfc1.getFirmwareVersion()) {
    Serial.println("[ERROR] PN532 not found. Check wiring and I2C mode switches.");
    while (true) delay(10);
  }
  nfc1.SAMConfig();
  Serial.println("[OK] PN532 ready.");

  SPI.begin(RC522_SCK, RC522_MISO, RC522_MOSI, RC522_SS);
  rfid2.PCD_Init();
  Serial.println("[OK] RC522 ready.");

  r1.cSinceMs = millis();
  r2.cSinceMs = millis();

  updateOLED(oled1, 1, pillsLeft1);
  updateOLED(oled2, 2, pillsLeft2);

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
