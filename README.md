# Claude Usage Monitor

A physical Claude Pro usage monitor built on an ESP32-C3 Super Mini with a 1.69" ST7789 display. Shows your session (5h) and weekly usage as progress bars with countdown timers, polling the Anthropic OAuth API directly.

## What it shows

- **Session** — your 5-hour sliding window usage %
- **Weekly** — your 7-day usage %
- Color coding: green → orange (≥75%) → red (100%, bar replaced by large countdown with seconds)
- White marker line on the session bar showing how much of the 5h window has elapsed

## Hardware

### Parts

- ESP32-C3 Super Mini (AI-Thinker)
- 1.69" ST7789 TFT display, 240×280, SPI

### Wiring

| Display pin | ESP32-C3 GPIO |
|-------------|---------------|
| SDA (MOSI)  | 10            |
| SCL (SCK)   | 8             |
| RES         | 2             |
| DC          | 1             |
| CS          | 4             |
| BL          | 3             |
| VCC         | 3V3           |
| GND         | GND           |

## Software setup

### 1. Arduino IDE

Install board support:
1. Open **Preferences** → add to Additional Boards Manager URLs:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
2. **Boards Manager** → search `esp32` → install **esp32 by Espressif**
3. Select board: **Tools → Board → ESP32C3 Dev Module**

Install libraries via **Sketch → Include Library → Manage Libraries**:
- `Adafruit GFX Library`
- `Adafruit ST7735 and ST7789 Library`

### 2. Get your OAuth token

The token comes from Claude Code's local credentials. Run this in your terminal (macOS):

```bash
security find-generic-password -s "Claude Code-credentials" -a "YOUR_USERNAME" -w | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['claudeAiOauth']['accessToken'])"
```

Replace `YOUR_USERNAME` with your macOS username. The token starts with `sk-ant-oat01-`.

> The token is the same one Claude Code CLI uses. It expires periodically — if you get 401 errors, run the command again and update the sketch.

### 3. Configure the sketch

Open `app/app.ino` and fill in the **CONFIG** section at the top:

```cpp
const char* WIFI_SSID   = "your-wifi-name";
const char* WIFI_PASS   = "your-wifi-password";
const char* OAUTH_TOKEN = "sk-ant-oat01-...";
```

Optional settings:

| Constant | Default | Description |
|---|---|---|
| `SYNC_INTERVAL_SECS` | `300` | How often to call the API (seconds) |
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

### 4. Flash

Upload via Arduino IDE, or with `arduino-cli`:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32c3 app/app.ino
arduino-cli upload  --fqbn esp32:esp32:esp32c3 -p /dev/cu.usbmodem* app/app.ino
```

Monitor serial output at **115200 baud** to see the raw API response and parsed values.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Black screen | Check SPI wiring — SDA→GPIO10, SCL→GPIO8 |
| No WiFi | Try "Erase All Flash Before Sketch Upload" in Arduino IDE tools; also check TX power note below |
| Error 429 | Rate limited — increase `SYNC_INTERVAL_SECS` or wait a few minutes |
| Error 401 | Token expired — re-run the `security` command and update `OAUTH_TOKEN` |
| Both bars at 0% | Check serial monitor for the raw API response |

**WiFi on ESP32-C3 Super Mini:** this board can have connection instability at full TX power. The sketch sets `WIFI_POWER_8_5dBm` automatically to fix this.
