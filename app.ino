/*
 * ╔══════════════════════════════════════════════════════════════╗
 *   CLAUDE USAGE — ESP32-C3 Super Mini + ST7789 1.69" 240×280
 *
 *   Wiring:
 *     SDA → GPIO 10  (MOSI)   SCL → GPIO 8  (SCK)
 *     RES → GPIO 2            DC  → GPIO 1
 *     CS  → GPIO 4            BL  → GPIO 3
 *     VCC → 3V3               GND → GND
 * ╚══════════════════════════════════════════════════════════════╝
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>

// ════════════════════════════════════════════════════════════════
//   CONFIG
// ════════════════════════════════════════════════════════════════
const char* WIFI_SSID = "238";
const char* WIFI_PASS = "238conchita";

#define SYNC_INTERVAL_SECS  300

#define TEST_MODE        false   // true = skip API, use dummy data
#define SHOW_TIME_MARKER true   // true = show elapsed-time marker on session bar
#define ALERT_THRESHOLD  75    // % above which bars/icons turn red

// Accent color for bars, percentages and icons — RGB565
// Green:   0x07E0  Cyan:    0x07FF  Blue:    0x001F
// Magenta: 0xF81F  Red:     0xF800  Orange:  0xFC00  White: 0xFFFF
// Coral:   0xDB8A  (#DE7356)
#define ACCENT_COLOR  0x07E0
// ════════════════════════════════════════════════════════════════

// ── Pins ──────────────────────────────────────────────────────
#define TFT_CS  4
#define TFT_DC  1
#define TFT_RST 2
#define TFT_BLK 3

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
WebServer        server(80);
Preferences      prefs;

// ── Display (landscape: 280×240) ──────────────────────────────
#define DISP_W 280
#define DISP_H 240

// ── Colors ────────────────────────────────────────────────────
#define C_BG      0x0000        // black
#define C_WHITE   0xFFFF
#define C_GRAY    0x8C51        // countdown text
#define C_DIMGRAY 0x4208        // status text
#define C_GREEN   ACCENT_COLOR
#define C_TRACK   0x18E3        // progress bar background

// ── Layout ────────────────────────────────────────────────────
#define MARGIN   14
#define BAR_W   (DISP_W - 2 * MARGIN)
#define BAR_H    12
#define BAR_R     6
#define ICON_W   24

// Block 1 — Session  (percentage at textSize 5 = 40px tall)
#define B1_Y      12
#define B1_CNT_Y  60
#define B1_BAR_Y  84

// Block 2 — Weekly
#define B2_Y     128
#define B2_CNT_Y 176
#define B2_BAR_Y 200

// ── State ─────────────────────────────────────────────────────
int  sessionPct       = 0;
int  weeklyPct        = 0;
long sessionSecsLeft  = -1;
long weeklySecsLeft   = -1;
unsigned long sessionReceivedAt = 0;
unsigned long weeklyReceivedAt  = 0;
time_t weeklyResetEpoch = 0;

unsigned long lastSyncMs      = 0;
unsigned long lastCountdownMs = 0;
bool ntpSynced = false;
bool sessionResetting = false;
bool weeklyResetting  = false;
int  retryCount       = 0;
static const unsigned long RETRY_DELAYS[] = {30, 60, 120};  // seconds before falling back to SYNC_INTERVAL_SECS
char activeToken[220];  // runtime token — loaded from NVS or falls back to OAUTH_TOKEN

// ── Time utilities ────────────────────────────────────────────
time_t parseISO8601(const char* s) {
  int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
  sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &sec);
  static const int MD[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  long days = 0;
  for (int yr = 1970; yr < y; yr++)
    days += (yr%4==0 && (yr%100!=0 || yr%400==0)) ? 366 : 365;
  bool leap = (y%4==0 && (y%100!=0 || y%400==0));
  for (int m = 1; m < mo; m++) { days += MD[m-1]; if (m==2 && leap) days++; }
  days += d - 1;
  return (time_t)(days * 86400L + h * 3600L + mi * 60L + sec);
}

long currentLeft(long recvSecs, unsigned long recvAt) {
  if (recvSecs < 0) return -1;
  return max(recvSecs - (long)((millis() - recvAt) / 1000UL), 0L);
}

// ── JSON helpers ──────────────────────────────────────────────
float extractJsonFloat(const String& body, const char* key) {
  int idx = body.indexOf(key);
  if (idx < 0) return -1.0f;
  idx += strlen(key);
  while (idx < (int)body.length() && body[idx] != '-' && !isDigit(body[idx])) idx++;
  return body.substring(idx).toFloat();
}

time_t parseResetsAtEpoch(const String& section) {
  const char* key = "\"resets_at\":\"";
  int idx = section.indexOf(key);
  if (idx < 0) return 0;
  int q1 = idx + strlen(key);
  int q2 = section.indexOf('"', q1);
  if (q2 < 0) return 0;
  return parseISO8601(section.substring(q1, q2).c_str());
}

// ── Icons ─────────────────────────────────────────────────────
uint16_t accentColor(int pct) {
  if (pct >= 100)            return 0xF800;  // red
  if (pct >= ALERT_THRESHOLD) return 0xFC00;  // orange
  return C_GREEN;
}

void drawIconClock(int x, int y, uint16_t color) {
  tft.drawCircle(x + 10, y + 10, 9, color);
  tft.drawLine(x + 10, y + 10, x + 10, y + 4,  color);  // hour hand
  tft.drawLine(x + 10, y + 10, x + 15, y + 10, color);  // minute hand
  tft.drawCircle(x + 10, y + 10, 1, color);
}

void drawIconBars(int x, int y, uint16_t color) {
  tft.fillRect(x + 1,  y + 13, 5, 7,  color);
  tft.fillRect(x + 8,  y + 8,  5, 12, color);
  tft.fillRect(x + 15, y + 3,  5, 17, color);
}

// ── Progress bar ──────────────────────────────────────────────
void drawProgressBar(int y, int pct, uint16_t color) {
  tft.fillRect(MARGIN, y, BAR_W, BAR_H, C_TRACK);
  if (pct > 0) {
    int fw = (int)((long)pct * BAR_W / 100);
    if (fw > 0) tft.fillRect(MARGIN, y, fw, BAR_H, color);
  }
}

// ── Right-aligned text ────────────────────────────────────────
void printRight(const char* str, int y, uint16_t color, uint8_t size = 1) {
  tft.setTextSize(size);
  tft.setTextColor(color);
  int16_t tx, ty; uint16_t tw, th;
  tft.getTextBounds(str, 0, 0, &tx, &ty, &tw, &th);
  tft.setCursor(DISP_W - MARGIN - tw, y);
  tft.print(str);
}

// ── Time formatting ───────────────────────────────────────────
void fmtSessionReset(char* buf, long secs) {
  if (secs < 0)  { strcpy(buf, "--h --m"); return; }
  if (secs == 0) { strcpy(buf, "resetting!"); return; }
  int h = secs / 3600, m = (secs % 3600) / 60;
  snprintf(buf, 16, "%dh %02dm", h, m);
}

void fmtWeeklyReset(char* buf, long secs) {
  if (secs < 0)  { strcpy(buf, "--d --h --m"); return; }
  if (secs == 0) { strcpy(buf, "resetting!"); return; }
  int d = secs / 86400, h = (secs % 86400) / 3600, m = (secs % 3600) / 60;
  snprintf(buf, 20, "%dd %dh %02dm", d, h, m);
}

// ── Draw a full block ─────────────────────────────────────────
void drawBlock(int titleY, int cntY, int barY,
               const char* title, int pct, bool isSession) {
  tft.fillRect(0, titleY - 2, DISP_W, barY - titleY + BAR_H + 6, C_BG);

  uint16_t accent = accentColor(pct);

  // Icon — aligned with title row
  if (isSession) drawIconClock(MARGIN, titleY + 9, accent);
  else           drawIconBars(MARGIN, titleY + 9, accent);

  // Title — vertically centered within the 40px percentage height
  tft.setTextSize(2);
  tft.setTextColor(C_WHITE);
  tft.setCursor(MARGIN + ICON_W + 2, titleY + 12);
  tft.print(title);

  // Percentage — right-aligned, large
  char pctBuf[8];
  snprintf(pctBuf, sizeof(pctBuf), "%d%%", pct);
  printRight(pctBuf, titleY, accent, 5);

  // Progress bar — hidden at 100%
  if (pct < 100) {
    drawProgressBar(barY, pct, accent);

    // Time marker on session bar — vertical line showing elapsed time in the 5h window
    if (SHOW_TIME_MARKER && isSession && sessionSecsLeft >= 0) {
      long secsLeft = currentLeft(sessionSecsLeft, sessionReceivedAt);
      long elapsed  = secsLeft == 0 ? 0L : constrain(18000L - secsLeft, 0L, 18000L);
      int mx = MARGIN + (int)(elapsed * BAR_W / 18000L);
      tft.drawFastVLine(mx, barY - 5, BAR_H + 10, C_WHITE);
    }
  }
}

// ── Update countdown texts (called every second) ──────────────
void drawCountdown(const char* buf, int cntY, int barY, bool full, uint16_t color, uint8_t sz) {
  int zoneTop = cntY - 2;
  int zoneH   = full ? (barY + BAR_H + 4 - zoneTop) : 18;  // height, not absolute y
  tft.fillRect(0, zoneTop, DISP_W, zoneH, C_BG);
  if (full) {
    tft.setTextSize(sz);
    tft.setTextColor(color);
    int16_t tx, ty; uint16_t tw, th;
    tft.getTextBounds(buf, 0, 0, &tx, &ty, &tw, &th);
    int cy = zoneTop + (zoneH - th) / 2;
    tft.setCursor((DISP_W - tw) / 2, cy);
    tft.print(buf);
  } else {
    printRight(buf, cntY, color, sz);
  }
}

void fmtWithSecs(char* buf, long secs, bool isSession) {
  if (secs < 0)  { strcpy(buf, "--h --m --s"); return; }
  if (secs == 0) { strcpy(buf, "resetting!"); return; }
  int s = secs % 60, m = (secs % 3600) / 60, h = (secs % 86400) / 3600;
  int d = secs / 86400;
  if (d > 0)
    snprintf(buf, 24, "%dd %dh %02dm %02ds", d, h, m, s);
  else if (h > 0)
    snprintf(buf, 20, "%dh %02dm %02ds", h, m, s);
  else if (m > 0)
    snprintf(buf, 12, "%dm %02ds", m, s);
  else
    snprintf(buf, 8, "%ds", s);
}

void updateResetTexts() {
  long sl = currentLeft(sessionSecsLeft, sessionReceivedAt);
  long wl = currentLeft(weeklySecsLeft,  weeklyReceivedAt);
  char buf[28];

  // Simulate reset when countdown hits 0 — restart from full interval until next real sync
  if (sessionSecsLeft >= 0 && sl == 0 && !sessionResetting) {
    sessionResetting  = true;
    sessionPct        = 0;
    sessionSecsLeft   = 18000L;   // 5h
    sessionReceivedAt = millis();
    sl = 18000L;
    drawBlock(B1_Y, B1_CNT_Y, B1_BAR_Y, "Session", 0, true);
  }
  if (weeklySecsLeft >= 0 && wl == 0 && !weeklyResetting) {
    weeklyResetting  = true;
    weeklyPct        = 0;
    weeklySecsLeft   = 7L * 86400L;  // 7 days
    weeklyReceivedAt = millis();
    wl = 7L * 86400L;
    drawBlock(B2_Y, B2_CNT_Y, B2_BAR_Y, "Weekly", 0, false);
  }

  if (sessionPct >= 100) fmtWithSecs(buf, sl, true);
  else                   fmtSessionReset(buf, sl);
  drawCountdown(buf, B1_CNT_Y, B1_BAR_Y, sessionPct >= 100, sessionPct >= 100 ? 0xF800 : C_GRAY, sessionPct >= 100 ? 3 : 2);

  if (weeklyPct >= 100) fmtWithSecs(buf, wl, false);
  else                  fmtWeeklyReset(buf, wl);
  drawCountdown(buf, B2_CNT_Y, B2_BAR_Y, weeklyPct >= 100, weeklyPct >= 100 ? 0xF800 : C_GRAY, weeklyPct >= 100 ? 3 : 2);
}

// ── Full redraw ───────────────────────────────────────────────
void redrawAll() {
  tft.fillScreen(C_BG);
  tft.drawFastHLine(MARGIN, DISP_H / 2, BAR_W, 0x2104);  // center divider

  drawBlock(B1_Y, B1_CNT_Y, B1_BAR_Y, "Session", sessionPct, true);
  drawBlock(B2_Y, B2_CNT_Y, B2_BAR_Y, "Weekly",  weeklyPct,  false);

  updateResetTexts();
}

// ── Status line ───────────────────────────────────────────────
void showStatus(const char* msg, uint16_t color = C_DIMGRAY) {
  tft.fillRect(0, DISP_H - 11, DISP_W, 11, C_BG);
  if (msg[0] == 0) return;
  tft.setTextSize(1);
  tft.setTextColor(color);
  int16_t tx, ty; uint16_t tw, th;
  tft.getTextBounds(msg, 0, 0, &tx, &ty, &tw, &th);
  tft.setCursor((DISP_W - tw) / 2, DISP_H - 11);
  tft.print(msg);
}

void showIP() {}

// ── Web server ────────────────────────────────────────────────
void handleRoot() {
  // Build progress bar HTML
  auto pctBar = [](int pct) -> String {
    String col = pct >= 100 ? "#F800" : pct >= 75 ? "#FC00" : "#07E0";
    return "<div style='background:#222;border-radius:4px;height:6px;margin:4px 0 10px'>"
           "<div style='background:" + col + ";width:" + String(pct) + "%;height:6px;border-radius:4px'></div></div>";
  };


  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>usagepal</title>"
    "<style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
    "background:#0d0d0d;color:#e0e0e0;max-width:480px;margin:0 auto;padding:24px 16px 48px}"
    "h1{font-size:1.4em;font-weight:700;color:#fff;letter-spacing:-.5px}"
    "h1 span{color:#00e000}"
    ".sub{color:#555;font-size:.8em;margin-top:2px;margin-bottom:24px}"
    ".card{background:#161616;border:1px solid #2a2a2a;border-radius:12px;padding:18px;margin-bottom:14px}"
    ".card-title{font-size:.7em;font-weight:600;letter-spacing:.1em;text-transform:uppercase;"
    "color:#555;margin-bottom:14px}"
    ".row{display:flex;justify-content:space-between;align-items:center;margin-bottom:2px}"
    ".row label{color:#888;font-size:.85em}"
    ".row .v{font-size:.9em;font-weight:600;color:#e0e0e0}"
    ".green{color:#00e000!important}.orange{color:#FC00!important}.red{color:#F800!important}"
    ".chip{font-size:.72em;padding:2px 8px;border-radius:20px;font-weight:600}"
    ".sep{border:none;border-top:1px solid #222;margin:12px 0}"
    "input[type=text]{width:100%;background:#0d0d0d;border:1px solid #2a2a2a;border-radius:8px;"
    "color:#e0e0e0;padding:10px 12px;font-size:.85em;font-family:monospace;margin-top:8px;"
    "outline:none;transition:border .2s}"
    "input[type=text]:focus{border-color:#00e000}"
    ".btn{display:block;width:100%;padding:11px;border:none;border-radius:8px;font-weight:700;"
    "font-size:.9em;cursor:pointer;margin-top:8px;transition:opacity .15s}"
    ".btn:hover{opacity:.85}"
    ".btn-primary{background:#00e000;color:#000}"
    ".btn-ghost{background:#1e1e1e;color:#888;border:1px solid #2a2a2a}"
    ".btn-dl{background:#1a1a2e;color:#07E0;border:1px solid #07E030;text-decoration:none;"
    "display:block;text-align:center;padding:10px;border-radius:8px;font-size:.85em;"
    "font-weight:600;margin-top:8px}"
    ".btn-dl:hover{background:#07E015;color:#000}"
    "code{display:block;background:#0a0a0a;border:1px solid #1e1e1e;border-radius:6px;"
    "padding:10px 12px;font-size:.75em;color:#888;word-break:break-all;margin-top:8px;"
    "line-height:1.5}"
    ".step{display:flex;gap:10px;margin:6px 0;font-size:.82em;color:#777;align-items:flex-start}"
    ".step-n{background:#222;color:#00e000;border-radius:50%;width:18px;height:18px;min-width:18px;"
    "display:flex;align-items:center;justify-content:center;font-size:.75em;font-weight:700}"
    "ic{font-style:normal}"
    ".ostab{background:#1e1e1e;color:#666;border:1px solid #2a2a2a;border-radius:6px;"
    "padding:5px 12px;font-size:.78em;font-weight:600;cursor:pointer}"
    ".ostab.active{background:#00e000;color:#000;border-color:#00e000}"
    "</style></head><body>";

  html += "<h1>usage<span>pal</span></h1><div class='sub'>ESP32 desk companion &bull; " + WiFi.localIP().toString() + "</div>";

  // ── Token card ──
  html += "<div class='card'>"
    "<div class='card-title'>Token</div>"
    "<div style='font-size:.8em;color:#666;margin-bottom:8px'>Run in your terminal to get the token:</div>"
    "<div style='display:flex;gap:6px;margin-bottom:8px'>"
    "<button onclick=\"showOs('mac')\" id='tab-mac' class='ostab active'>macOS</button>"
    "<button onclick=\"showOs('win')\" id='tab-win' class='ostab'>Windows</button>"
    "<button onclick=\"showOs('lin')\" id='tab-lin' class='ostab'>Linux</button>"
    "</div>"
    "<code id='cmd-mac'>security find-generic-password -s \"Claude Code-credentials\" -a \"$(whoami)\" -w | "
    "python3 -c \"import sys,json; d=json.load(sys.stdin); print(d['claudeAiOauth']['accessToken'])\"</code>"
    "<code id='cmd-win' style='display:none'>$json = [System.Text.Encoding]::UTF8.GetString("
    "(Get-Content \"$env:APPDATA\\Claude\\credentials\" -Encoding Byte)); "
    "($json | ConvertFrom-Json).claudeAiOauth.accessToken</code>"
    "<code id='cmd-lin' style='display:none'>secret-tool lookup service 'Claude Code-credentials' account \"$USER\" | "
    "python3 -c \"import sys,json; d=json.load(sys.stdin); print(d['claudeAiOauth']['accessToken'])\"</code>"
    "<script>"
    "function showOs(os){"
    "['mac','win','lin'].forEach(function(o){"
    "document.getElementById('cmd-'+o).style.display=o===os?'block':'none';"
    "document.getElementById('tab-'+o).classList.toggle('active',o===os)"
    "})"
    "}"
    "</script>"
    "<form method='POST' action='/token'>"
    "<input type='text' name='token' value='" + String(activeToken) + "' autocomplete='off' spellcheck='false'>"
    "<button class='btn btn-primary' type='submit'>Save token &amp; sync now</button>"
    "</form></div>";


  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleTokenUpdate() {
  if (server.hasArg("token")) {
    String tok = server.arg("token");
    tok.trim();
    if (tok.length() > 10 && tok.startsWith("sk-ant-")) {
      bool firstToken = (activeToken[0] == '\0');
      tok.toCharArray(activeToken, sizeof(activeToken));
      prefs.begin("usagepal", false);
      prefs.putString("token", tok);
      prefs.end();
      server.sendHeader("Location", "/");
      server.send(302);
      // If this was the first token, boot into normal operation
      if (firstToken) {
        syncWithAnthropic();
        lastSyncMs = millis();
      } else {
        lastSyncMs = 0;
      }
      return;
    }
  }
  server.send(400, "text/plain", "Invalid token");
}


// ── Test mode ─────────────────────────────────────────────────
void loadTestData() {
  sessionPct        = 75;
  weeklyPct         = 100;
  sessionSecsLeft   = 2 * 3600 + 17 * 60;   // 2h 17m
  weeklySecsLeft    = 18 * 3600 + 23 * 60 + 45;  // 18h 23m 45s
  sessionReceivedAt = millis();
  weeklyReceivedAt  = millis();
  redrawAll();
  showStatus("test mode");
}

// ── Anthropic OAuth usage API ─────────────────────────────────
void syncWithAnthropic() {
  if (TEST_MODE) { loadTestData(); return; }
  showStatus("syncing...");

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  if (!http.begin(client, "https://api.anthropic.com/api/oauth/usage")) {
    showStatus("http begin error", 0xF800);
    return;
  }

  char auth[256];
  snprintf(auth, sizeof(auth), "Bearer %s", activeToken);
  http.addHeader("Authorization",  auth);
  http.addHeader("anthropic-beta", "oauth-2025-04-20");
  http.setTimeout(20000);

  int code = http.GET();
  Serial.printf("HTTP %d\n", code);

  String resp = http.getString();
  Serial.println(resp);

  if (code == 200) {
    // five_hour block
    int fhS = resp.indexOf("\"five_hour\":");
    if (fhS >= 0) {
      String fh = resp.substring(fhS, resp.indexOf('}', fhS) + 1);
      float u = extractJsonFloat(fh, "\"utilization\":");
      if (u >= 0) sessionPct = (int)constrain(u, 0, 100);
      time_t ep = parseResetsAtEpoch(fh);
      if (ep > 0 && ntpSynced) {
        sessionSecsLeft   = max((long)(ep - time(nullptr)), 0L);
        sessionReceivedAt = millis();
      }
    }

    // seven_day block
    int sdS = resp.indexOf("\"seven_day\":");
    if (sdS >= 0) {
      String sd = resp.substring(sdS, resp.indexOf('}', sdS) + 1);
      float u = extractJsonFloat(sd, "\"utilization\":");
      if (u >= 0) weeklyPct = (int)constrain(u, 0, 100);
      time_t ep = parseResetsAtEpoch(sd);
      if (ep > 0 && ntpSynced) {
        weeklyResetEpoch = ep;
        weeklySecsLeft   = max((long)(ep - time(nullptr)), 0L);
        weeklyReceivedAt = millis();
      }
    }

    Serial.printf("session=%d%%  secs=%ld  weekly=%d%%  wsecs=%ld\n",
                  sessionPct, sessionSecsLeft, weeklyPct, weeklySecsLeft);

    sessionResetting = false;
    weeklyResetting  = false;
    retryCount = 0;
    redrawAll();
    showIP();
  } else {
    Serial.printf("error: %s\n", resp.substring(0, 200).c_str());
    char msg[32];
    snprintf(msg, sizeof(msg), "error %d", code);
    showStatus(msg, 0xF800);
    // Schedule retry with backoff
    int idx = retryCount < 3 ? retryCount : 2;
    lastSyncMs = millis() - (unsigned long)SYNC_INTERVAL_SECS * 1000UL
                 + RETRY_DELAYS[idx] * 1000UL;
    retryCount++;
  }

  http.end();
}

// ── Setup screen (no token configured) ───────────────────────
void showSetupScreen() {
  tft.fillScreen(C_BG);
  String ip = WiFi.localIP().toString();

  // Title
  tft.setTextSize(2);
  tft.setTextColor(C_GREEN);
  tft.setCursor(14, 18);
  tft.print("usagepal");

  // Divider
  tft.drawFastHLine(14, 44, DISP_W - 28, 0x2104);

  // Message
  tft.setTextSize(1);
  tft.setTextColor(C_WHITE);
  tft.setCursor(14, 58);
  tft.print("No token configured.");
  tft.setCursor(14, 72);
  tft.print("Open this URL to set it up:");

  // IP — large, centered, green
  tft.setTextSize(2);
  tft.setTextColor(C_GREEN);
  String url = "http://" + ip;
  int16_t tx, ty; uint16_t tw, th;
  tft.getTextBounds(url.c_str(), 0, 0, &tx, &ty, &tw, &th);
  tft.setCursor((DISP_W - tw) / 2, 100);
  tft.print(url);

  // Divider
  tft.drawFastHLine(14, 132, DISP_W - 28, 0x2104);

  // Hint
  tft.setTextSize(1);
  tft.setTextColor(C_DIMGRAY);
  tft.setCursor(14, 144);
  tft.print("Token is saved on device.");
  tft.setCursor(14, 156);
  tft.print("You only need to do this once.");
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // Load token from NVS
  prefs.begin("usagepal", true);
  String saved = prefs.getString("token", "");
  prefs.end();
  if (saved.length() > 10)
    saved.toCharArray(activeToken, sizeof(activeToken));
  else
    activeToken[0] = '\0';  // no token — will show setup screen

  pinMode(TFT_BLK, OUTPUT);
  digitalWrite(TFT_BLK, HIGH);

  SPI.begin(8, -1, 10, TFT_CS);
  delay(100);
  tft.init(240, 280, SPI_MODE3);  // physical panel dimensions
  delay(50);
  tft.setRotation(1);              // landscape: 280×240
  tft.fillScreen(C_BG);
  showStatus("connecting WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  delay(200);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 60) {
    delay(500); tries++;
    Serial.printf("WiFi status: %d  attempt %d\n", WiFi.status(), tries);
  }

  if (WiFi.status() != WL_CONNECTED) {
    showStatus("no WiFi", 0xF800);
    return;
  }

  configTime(0, 0, "pool.ntp.org", "time.google.com");
  showStatus("syncing time...");
  struct tm t;
  if (getLocalTime(&t, 5000)) ntpSynced = true;

  server.on("/",      HTTP_GET,  handleRoot);
  server.on("/token", HTTP_POST, handleTokenUpdate);
  server.begin();
  Serial.printf("Web server started at http://%s\n", WiFi.localIP().toString().c_str());

  if (activeToken[0] == '\0') {
    showSetupScreen();
  } else {
    syncWithAnthropic();
    lastSyncMs = millis();
  }
}

// ── Loop ──────────────────────────────────────────────────────
void loop() {
  server.handleClient();
  if (activeToken[0] != '\0' && millis() - lastSyncMs >= (unsigned long)SYNC_INTERVAL_SECS * 1000UL) {
    lastSyncMs = millis();
    syncWithAnthropic();
  }
  if (activeToken[0] != '\0' && millis() - lastCountdownMs >= 1000) {
    lastCountdownMs = millis();
    updateResetTexts();
  }
}
