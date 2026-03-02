# Pill Counter — firmware-v2.1

Dual-pool pill counter running on an ESP32.
Pool 1 uses a **PN532** NFC reader (I2C). Pool 2 uses an **RC522** NFC reader (SPI).
Each pool has its own 16x2 LCD that counts down remaining pills.

---

## How it works

1. Place your NFC tag/keyfob on a reader — the LCD shows the tag was detected.
2. Remove the tag — one pill is subtracted from that pool and the LCD updates.
3. Each full **place → remove** cycle = one pill counted.
4. Pills never go below zero.

---

## Required Libraries

Install all three via **Arduino IDE → Tools → Manage Libraries**:

| Library | Search term | Author |
|---|---|---|
| PN532 driver | `Adafruit PN532` | Adafruit (version >= 1.2.0) |
| RC522 driver | `RFID` | miguelbalboa |
| LCD driver | `LiquidCrystal I2C` | Frank de Brabander |

---

## Hardware

- ESP32 development board
- PN532 NFC/RFID module (Pool 1)
- RC522 RFID module (Pool 2)
- Two 16x2 I2C LCD displays (PCF8574 backpack)
- NFC tags or keyfobs (MIFARE compatible)

---

## Wiring

### Pool 1 — PN532 (I2C)

> PN532 mode switches must be set to I2C mode: **switch 1 = ON, switch 2 = OFF**

| PN532 Pin | ESP32 Pin |
|---|---|
| SDA | 21 |
| SCL | 22 |
| IRQ | 4 |
| RESET | 5 |
| VCC | 3.3V |
| GND | GND |

---

### Pool 2 — RC522 (SPI)

> **3.3V only — do NOT connect RC522 to 5V. It will be damaged.**
> The IRQ pin is not used — leave it unconnected.

| RC522 Board Label | Signal | ESP32 Pin |
|---|---|---|
| SDA | SS (chip select) | 15 |
| SCK | SPI clock | 18 |
| MOSI | MOSI | 23 |
| MISO | MISO | 19 |
| RST | Reset | 27 |
| IRQ | (not used) | — |
| GND | GND | GND |
| 3.3V | Power | 3.3V |

---

### LCD Displays (I2C — both share the same bus as the PN532)

| LCD | I2C Address | ESP32 Pins |
|---|---|---|
| LCD #1 (Pool 1) | 0x27 | SDA=21, SCL=22 |
| LCD #2 (Pool 2) | 0x3F | SDA=21, SCL=22 |

> Both LCDs are on the same I2C bus. Their addresses **must be different**.
> The address is set by the solder jumpers labeled **A0, A1, A2** on the PCF8574 backpack.
> If both LCDs arrived with the same address, change one by bridging a jumper.
>
> To verify your LCD addresses before flashing, upload the I2C scanner sketch:
> **File → Examples → Wire → i2c_scanner**
> Open Serial Monitor at 115200 baud and it will print all detected addresses.

---

## Adjusting Pill Count

The starting pill count for each pool is set at the top of the firmware:

```cpp
int pillsLeft1 = 30;   // Pool 1 starting count
int pillsLeft2 = 30;   // Pool 2 starting count
```

Change these values before flashing to match the actual number of pills in each bottle.

> **Note:** Counts are stored in RAM only. Restarting the ESP32 resets both pools back to the values above. Non-volatile storage (NVS/EEPROM) is planned for a future version.

---

## Serial Monitor

Connect at **115200 baud** to see live output:

```
=== Dual Pill Counter (PN532 + RC522) ===
[OK] PN532 ready.
[OK] RC522 ready.
Pool 1 starting pills = 30
Pool 2 starting pills = 30
--------------------------------------------------
[Pool 1] Tag detected. UID = A3F209B1
[Pool 1] Tag removed.
[Pool 1] Pills left = 29
[Pool 2] Tag detected. UID = 04C71822
[Pool 2] Tag removed.
[Pool 2] Pills left = 29
```

---

## Troubleshooting

**PN532 not found at startup**
- Check mode switches: switch 1 must be ON, switch 2 must be OFF
- Verify SDA/SCL are on pins 21/22
- Confirm 3.3V power to the module

**RC522 not responding**
- Confirm all five SPI wires are connected (SS=15, SCK=18, MOSI=23, MISO=19, RST=27)
- Confirm power is 3.3V, not 5V
- Check that no other SPI device shares the same SS pin

**LCD shows nothing**
- Run the I2C scanner to find the actual address of each backpack
- Update `lcd1(0x27, ...)` or `lcd2(0x3F, ...)` in the firmware to match
- Check that backlight jumper is present on the PCF8574 backpack

**Tag detected but count never decrements**
- The full cycle is required: tag must be **placed then removed**
- Placing and leaving the tag does not count
- Debounce is 250 ms — hold the tag still for at least that long before removing

---

## File Reference

| File | Description |
|---|---|
| `firmware-v1.ino` | Original single-reader, no LCD |
| `firmware-v2.1.ino` | Dual reader (PN532 + RC522), dual LCD |
