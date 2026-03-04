# Pill Counter — Firmware v3.0

A dual-pool digital pill counter built on ESP32. Two NFC readers track which pill tag is scanned, two OLED displays show live counts, and two tactile buttons let users update counts and enroll new tags — no reflashing required.

---

## Hardware

| Component | Part | Notes |
|---|---|---|
| Microcontroller | ESP32 DevKit | Any standard 38-pin DevKit |
| NFC Reader 1 | PN532 | I2C mode (switch 1=ON, switch 2=OFF) |
| NFC Reader 2 | RC522 | SPI mode |
| Display 1 | SSD1306 OLED 128×64 | Pool 1 display |
| Display 2 | SSD1306 OLED 128×64 | Pool 2 display |
| Button 1 | Tactile push button (4-pin) | Pool 1 |
| Button 2 | Tactile push button (4-pin) | Pool 2 |
| NFC Tags | MIFARE ISO14443A | One tag per pill pool |

---

## Wiring

### I2C Bus 1 — PN532 + OLED #1
| Signal | ESP32 Pin |
|---|---|
| SDA | 21 |
| SCL | 22 |
| PN532 IRQ | 4 |
| PN532 RESET | 5 |

### I2C Bus 2 — OLED #2
| Signal | ESP32 Pin |
|---|---|
| SDA | 16 |
| SCL | 17 |

### SPI — RC522
| Signal | ESP32 Pin |
|---|---|
| SS (SDA) | 15 |
| SCK | 18 |
| MOSI | 23 |
| MISO | 19 |
| RST | 27 |
> RC522 is 3.3V only — never connect to 5V.

### Buttons
| Button | ESP32 Pin | Wiring |
|---|---|---|
| Button 1 (Pool 1) | GPIO 32 | One leg → GPIO 32, diagonal leg → GND |
| Button 2 (Pool 2) | GPIO 33 | One leg → GPIO 33, diagonal leg → GND |

No resistors needed — the firmware uses the ESP32's internal pull-up resistors.

For 4-pin tactile buttons: the two legs on each short side of the button are internally connected. Wire one short side to the GPIO pin and the other short side to GND.

---

## Required Libraries

Install all of these via **Arduino IDE → Tools → Manage Libraries**:

| Library | Search For |
|---|---|
| Adafruit PN532 | `Adafruit PN532` |
| MFRC522 | `RFID` by miguelbalboa |
| Adafruit SSD1306 | `Adafruit SSD1306` |
| Adafruit GFX Library | `Adafruit GFX` (SSD1306 dependency) |

`Preferences.h` is built into the ESP32 Arduino core — no install needed.

---

## First-Time Setup

1. Flash `firmware-v3.0.ino` to the ESP32.
2. Open Serial Monitor at **115200 baud**.
3. On first boot, pools start at **30 pills each** with the default tag UIDs hardcoded in the firmware. If your tags differ, run enrollment (see below).
4. Set your actual pill counts using the buttons or the `setmax` serial command.
5. All settings persist to flash — they survive power loss and reboots.

---

## Button Interactions

### Normal Mode

| Action | Result |
|---|---|
| Hold Button 1 (3s) | Enter **set-count mode** for Pool 1 |
| Hold Button 2 (3s) | Enter **set-count mode** for Pool 2 |
| Hold Button 1 + Button 2 (5s) | Enter **enrollment mode** |

### Set-Count Mode

Used to set the current pill count for a pool — for example, after a refill.

| Action | Result |
|---|---|
| Button 1 short press | +10 pills |
| Button 2 short press | +1 pill |
| Hold initiating button (2s) | **Save and exit** |
| No input for 30s | Cancel — count unchanged |

The OLED for that pool shows the live count as you press. When saved, both the current count and the max (used for the low-stock threshold) update to the new value.

**Example — partial refill:** You have 3 pills left and pick up a 60-count bottle (63 total).
1. Hold Button 1 (3s) → set mode starts at 3
2. Press Button 1 six times → +60 = 63
3. Press Button 2 three times → +3 = 66 (if needed)
4. Hold Button 1 (2s) → saved

### Enrollment Mode

Used to assign a new NFC tag to a pool without reflashing.

1. Hold both buttons for 5s → OLED 1 shows **SCAN POOL 1**
2. Scan any tag on either reader → saved as Pool 1's tag
3. OLED 2 shows **SCAN POOL 2**
4. Scan the second tag → saved as Pool 2's tag
5. Both displays return to normal

---

## Display States

Each pool has its own OLED with three possible states:

| State | Trigger | Display |
|---|---|---|
| Normal | Pills > 10% of max | Large pill count + "pills remaining" |
| Low stock | Pills ≤ 10% of max | Count + inverted **"PICK UP REFILL"** banner |
| Empty | Pills = 0 | **"OUT OF PILLS"** + "CONTACT PHARMACY" banner |
| Tag on reader | Tag placed | **"TAG ON"** (clears when tag is removed and count decrements) |

The low-stock threshold is 10% of the pool max. Change `LOW_STOCK_PCT` in the firmware to adjust.

---

## Serial Commands

Connect via USB and open a serial monitor at **115200 baud**.

| Command | Action |
|---|---|
| `status` | Print both pools: count, max, assigned tag UID |
| `reset 1` | Refill Pool 1 to its current max |
| `reset 2` | Refill Pool 2 to its current max |
| `setmax 1 <n>` | Set Pool 1 count and max to `n` (e.g. `setmax 1 60`) |
| `setmax 2 <n>` | Set Pool 2 count and max to `n` |
| `enroll` | Enter enrollment mode (same as holding both buttons) |
| `factory` | Wipe all saved data and restart with defaults |

---

## How Pill Counting Works

- Each NFC tag is assigned to a pool (not a reader). Either reader can see either tag.
- When a tag is placed on a reader → the display flashes **TAG ON**.
- When the tag is removed → the pool count decrements by 1 and saves to flash.
- If the pool is already at 0, no further decrement occurs.
- Tags not in the registry are ignored (UID printed to Serial for reference).

---

## Firmware Version History

| Version | Changes |
|---|---|
| v2.4 | Dual NFC readers (PN532 + RC522), dual OLED displays, tag-to-pool routing |
| v2.5 | Low-stock ("PICK UP REFILL") and empty ("OUT OF PILLS") OLED warnings |
| v3.0 | Persistent storage, tactile buttons, set-count mode, enrollment mode, Serial command interface |
