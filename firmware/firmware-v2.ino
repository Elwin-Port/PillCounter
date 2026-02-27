#include <Wire.h>
#include <Adafruit_PN532.h>

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ===== ESP32 I2C pins =====
#define I2C_SDA 21
#define I2C_SCL 22

// ===== PN532 pins (same as your working firmware-v1.ino) =====
// You do NOT need to wire these for I2C polling to work,
// but using this constructor prevents the GPIO input-only error.
#define PN532_IRQ   4
#define PN532_RESET 5
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

// ===== OLED =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ===== Counter =====
int pillsLeft = 30;

// ===== State machine =====
bool stablePresent = false;
bool lastStablePresent = false;
bool armed = false;

bool candidatePresent = false;
unsigned long candidateSinceMs = 0;
const unsigned long DEBOUNCE_MS = 250;

String statusLine = "Booting...";

String uidToString(const uint8_t* uid, uint8_t uidLen) {
  String s = "";
  for (uint8_t i = 0; i < uidLen; i++) {
    if (uid[i] < 0x10) s += "0";
    s += String(uid[i], HEX);
  }
  s.toUpperCase();
  return s;
}

bool readFobPresentNow(String &uidOut) {
  uint8_t uid[7];
  uint8_t uidLen = 0;

  bool success = nfc.readPassiveTargetID(
    PN532_MIFARE_ISO14443A,
    uid,
    &uidLen,
    50
  );

  if (success) {
    uidOut = uidToString(uid, uidLen);
    return true;
  }

  uidOut = "";
  return false;
}

void drawScreen(const String& debugLine, int pills) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("PILLS LEFT");

  display.setTextSize(3);
  display.setCursor(0, 14);
  display.println(pills);

  display.setTextSize(1);
  display.setCursor(0, 52);
  display.println(debugLine);

  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(I2C_SDA, I2C_SCL);

  // OLED init (most common address is 0x3C)
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed. If your OLED is 0x3D, change 0x3C to 0x3D.");
    while (true) delay(10);
  }

  statusLine = "Init NFC...";
  drawScreen(statusLine, pillsLeft);

  // PN532 init (same pattern as your working version)
  nfc.begin();

  uint32_t version = nfc.getFirmwareVersion();
  if (!version) {
    Serial.println("❌ PN532 NOT FOUND.");
    statusLine = "PN532 NOT FOUND";
    drawScreen(statusLine, pillsLeft);
    while (true) delay(10);
  }

  nfc.SAMConfig();

  Serial.println("✅ PN532 ready.");
  Serial.print("Starting pillsLeft = ");
  Serial.println(pillsLeft);

  stablePresent = false;
  lastStablePresent = false;
  armed = false;

  candidatePresent = false;
  candidateSinceMs = millis();

  statusLine = "Place keyfob";
  drawScreen(statusLine, pillsLeft);
}

void loop() {
  String uid = "";
  bool presentNow = readFobPresentNow(uid);

  // Debounce
  if (presentNow != candidatePresent) {
    candidatePresent = presentNow;
    candidateSinceMs = millis();
  }

  if (millis() - candidateSinceMs >= DEBOUNCE_MS) {
    stablePresent = candidatePresent;
  }

  // Only act on stable transitions
  if (stablePresent != lastStablePresent) {
    if (stablePresent) {
      armed = true;
      statusLine = "Keyfob detected";
      Serial.print("Keyfob detected UID=");
      Serial.println(uid);
    } else {
      statusLine = "Keyfob removed";
      Serial.println("Keyfob removed");

      if (armed && lastStablePresent == true) {
        if (pillsLeft > 0) pillsLeft--;
        if (pillsLeft < 0) pillsLeft = 0;

        Serial.print("Pills left = ");
        Serial.println(pillsLeft);
      }
    }

    drawScreen(statusLine, pillsLeft);
    lastStablePresent = stablePresent;
  }

  delay(30);
}