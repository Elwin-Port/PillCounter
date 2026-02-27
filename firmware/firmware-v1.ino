#include <Wire.h>
#include <Adafruit_PN532.h>

#define I2C_SDA 21
#define I2C_SCL 22

// Use safe GPIOs (NOT 34-39).
// You DO NOT have to wire these yet.
#define PN532_IRQ   4
#define PN532_RESET 5

// This constructor is for I2C mode on the Adafruit library.
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

int pillsLeft = 30;

bool stablePresent = false;
bool lastStablePresent = false;
bool armed = false;

bool candidatePresent = false;
unsigned long candidateSinceMs = 0;
const unsigned long DEBOUNCE_MS = 250;

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

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(I2C_SDA, I2C_SCL);

  Serial.println("\n=== PN532 Pill Counter (Serial Only) ===");
  Serial.println("Using PN532 over I2C (SDA=21 SCL=22)");

  nfc.begin();

  uint32_t version = nfc.getFirmwareVersion();
  if (!version) {
    Serial.println("❌ PN532 NOT FOUND.");
    Serial.println("But since scanner saw 0x24, this is likely still init related.");
    Serial.println("Double-check PN532 is in I2C mode (1,0) and wiring is on SDA/SCL pins.");
    while (true) delay(10);
  }

  nfc.SAMConfig();

  Serial.print("✅ PN532 ready. Starting pillsLeft = ");
  Serial.println(pillsLeft);
  Serial.println("Place keyfob on reader. Remove to subtract.");
  Serial.println("---------------------------------------");

  stablePresent = false;
  lastStablePresent = false;
  armed = false;

  candidatePresent = false;
  candidateSinceMs = millis();
}

void loop() {
  String uid = "";
  bool presentNow = readFobPresentNow(uid);

  if (presentNow != candidatePresent) {
    candidatePresent = presentNow;
    candidateSinceMs = millis();
  }

  if (millis() - candidateSinceMs >= DEBOUNCE_MS) {
    stablePresent = candidatePresent;
  }

  if (stablePresent != lastStablePresent) {

    if (stablePresent) {
      armed = true;
      Serial.print("Keyfob detected. UID = ");
      Serial.println(uid);
    } else {
      Serial.println("Keyfob removed.");

      if (armed && lastStablePresent == true) {
        if (pillsLeft > 0) pillsLeft--;
        if (pillsLeft < 0) pillsLeft = 0;

        Serial.print("Pills left = ");
        Serial.println(pillsLeft);
      }
    }

    lastStablePresent = stablePresent;
  }

  delay(30);
}

