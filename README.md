# usagepal

A physical Claude Pro usage monitor built on an ESP32-C3 Super Mini with a 1.69" ST7789 display. Shows your session (5h) and weekly usage as progress bars with countdown timers, polling the Anthropic OAuth API directly.

## What it shows

- **Session** — your 5-hour sliding window usage %
- **Weekly** — your 7-day usage %
- Color coding: green → orange (≥75%) → red (100%, bar replaced by large countdown with seconds)
- White marker line on the session bar showing how much of the 5h window has elapsed

## Parts list

| Part                | Spec                             | ~Price |
| ------------------- | -------------------------------- | ------ |
| ESP32-C3 Super Mini | microcontroller with WiFi        | ~$2.50 |
| ST7789 1.54" TFT    | 240×240 SPI color display        | ~$3.00 |
| 8 short wires       | 8–10 cm Dupont / jumper wires    | ~$0.50 |
| 2× M2×6mm screws    | to mount display bezel           | ~$0.10 |
| Double-sided tape   | to secure components inside case | ~$0.10 |
| USB-C cable         | for power                        | —      |
| 3D printed case     | PLA or PETG, ~30g                | ~$0.50 |

**Total: ~$7–8**

---

## Wiring

> ⚠️ Connect VCC to **3.3V only** — never 5V. Use GPIO 8 and 10 for SPI (hardware SPI, fast). Do not use GPIO 6/7 for SPI.

| Display pin | ESP32-C3 GPIO  | Wire color (suggested) |
| ----------- | -------------- | ---------------------- |
| VCC         | 3V3            | Red                    |
| GND         | GND            | Black                  |
| SDA         | GPIO 10 (MOSI) | Orange                 |
| SCL         | GPIO 8 (SCK)   | Green                  |
| RES         | GPIO 2         | Purple                 |
| DC          | GPIO 1         | Blue                   |
| CS          | GPIO 4         | White                  |
| BL          | GPIO 3         | Yellow                 |

---

## Software setup

### Step 1 — Install Arduino IDE

Download [Arduino IDE 2.x](https://www.arduino.cc/en/software) and install it.

### Step 2 — Add ESP32 board support

1. Open Arduino IDE → **File → Preferences**
2. In "Additional boards manager URLs" paste:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Go to **Tools → Board → Boards Manager**, search `esp32`, install **"esp32 by Espressif Systems"**

### Step 3 — Install libraries

Go to **Tools → Library Manager** and install both:

- `Adafruit GFX Library`
- `Adafruit ST7735 and ST7789 Library`

### Step 4 — Configure board settings

Go to **Tools** and set:

| Setting         | Value                   |
| --------------- | ----------------------- |
| Board           | ESP32C3 Dev Module      |
| USB CDC On Boot | **Enabled** ← important |
| CPU Frequency   | 160 MHz                 |
| Upload Speed    | 921600                  |

### Step 5 — Upload the sketch

1. Clone or download this repo
2. Open `app/app.ino` in Arduino IDE
3. Connect the ESP32 via USB-C
4. Select the correct port under **Tools → Port**
5. Click **Upload** (→ arrow button)
6. Wait for "Hard resetting via RTS pin..." — this means success

---

### 2. Configure WiFi

Open `app/app.ino` and set your WiFi credentials in the **CONFIG** section:

```cpp
const char* WIFI_SSID = "your-wifi-name";
const char* WIFI_PASS = "your-wifi-password";
```

### 3. Flash

Upload via Arduino IDE, or with `arduino-cli`:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32c3 app/app.ino
arduino-cli upload  --fqbn esp32:esp32:esp32c3 -p /dev/cu.usbmodem* app/app.ino
```

### 4. Set your token via the web interface

On first boot the display shows a URL — open it in your browser (e.g. `http://192.168.1.x`).

The web interface shows the command to extract your token for each platform:

**macOS**
```bash
security find-generic-password -s "Claude Code-credentials" -a "$(whoami)" -w | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['claudeAiOauth']['accessToken'])"
```

**Windows (PowerShell)**
```powershell
$json = [System.Text.Encoding]::UTF8.GetString((Get-Content "$env:APPDATA\Claude\credentials" -Encoding Byte)); ($json | ConvertFrom-Json).claudeAiOauth.accessToken
```

**Linux**
```bash
secret-tool lookup service 'Claude Code-credentials' account "$USER" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['claudeAiOauth']['accessToken'])"
```

The token starts with `sk-ant-oat01-`. Paste it into the web form — it is saved on the device and you only need to do this once. When it expires (401 error), open the web interface again and update it.

## Optional settings

| Constant | Default | Description |
|---|---|---|
| `SYNC_INTERVAL_SECS` | `300` | How often to call the API (seconds) — ⚠️ **do not set below 300**, the Anthropic OAuth endpoint will return 429 errors |
| `ALERT_THRESHOLD` | `75` | % where color switches green → orange |
| `SHOW_TIME_MARKER` | `true` | White line on session bar showing elapsed time |
| `ACCENT_COLOR` | `0x07E0` | Base color in RGB565 (green) |
| `TEST_MODE` | `false` | Skip API, show dummy data for testing |

Available colors (RGB565):

| Color   | Value  |
|---------|--------|
| Green   | 0x07E0 |
| Cyan    | 0x07FF |
| Blue    | 0x001F |
| Magenta | 0xF81F |
| Orange  | 0xFC00 |
| Coral   | 0xDB8A |
| White   | 0xFFFF |

## Troubleshooting

| Symptom | Fix |
|---|---|
| Black screen | Check SPI wiring — SDA→GPIO10, SCL→GPIO8 |
| No WiFi | Try "Erase All Flash Before Sketch Upload" in Arduino IDE tools |
| Setup screen stuck | Check WiFi credentials in the sketch |
| Error 429 | Rate limited — increase `SYNC_INTERVAL_SECS` or wait a few minutes |
| Error 401 | Token expired — open the web interface and paste a new token |
| Both bars at 0% | Check serial monitor at 115200 baud for the raw API response |

**WiFi on ESP32-C3 Super Mini:** this board can have connection instability at full TX power. The sketch sets `WIFI_POWER_8_5dBm` automatically to fix this.
