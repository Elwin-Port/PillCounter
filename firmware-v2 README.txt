What changed and why
Two separate I2C buses (critical)
The PN532 has a fixed I2C address of 0x24 — it cannot be changed. You cannot put two PN532s on the same bus. The solution is the ESP32's second hardware I2C bus (Wire1), which is already defined in the ESP32 Arduino core. No extra library needed.

Bus	Pins	Devices
Wire (Bus 1)	SDA=21, SCL=22	PN532 #1, LCD #1, LCD #2
Wire1 (Bus 2)	SDA=16, SCL=17	PN532 #2
Both LCDs share Bus 1 — they must have different I2C addresses (set via the A0/A1/A2 solder jumpers on the PCF8574 backpack). Typical addresses are 0x27 and 0x3F.

ReaderState struct
Instead of duplicating 5 debounce/state variables for each reader, a struct holds them cleanly. r1 and r2 each get their own independent state — no cross-contamination.

processReader() function
The entire cycle logic lives in one place. Both readers call the same function with their own devices and state. The cycle behavior is identical to v1: place tag → armed=true, remove tag → decrement pool, update LCD.

LCD display
updateLCD() refreshes a single screen whenever a pill is counted. The LCD shows:


Pool 1 Pills:
28 remaining
Before flashing
Install libraries via Arduino Library Manager:

Adafruit PN532 (>= 1.2.0 — needs the TwoWire* constructor)
LiquidCrystal I2C by Frank de Brabander
Check LCD addresses — run an I2C scanner sketch first to confirm what addresses your two LCD backpacks actually report. Adjust 0x27 / 0x3F in the lcd1/lcd2 declarations if needed.

Pin assignments — the new pins (16, 17, 25, 26) are all safe GPIOs on ESP32. Adjust them in the #define block at the top if your wiring differs.