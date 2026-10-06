/*
  Unofficial TickrMeter Rev. 1.6 hardware research example.
  Independently written test code; no original manufacturer firmware/code included.
  Verify GPIO assignments before use on another hardware revision.
*/

/*
  TickrMeter Rev 1.6 - NTP CLOCK + WEB SETTINGS
  ESP32-WROOM-32E + 2.9" e-paper

  Web settings:
    - RGB LED effects ON/OFF
    - minute beep ON/OFF
    - alarm ON/OFF + time + duration
    - home Wi-Fi SSID/password
    - LED / beep / alarm test buttons
    - OTA firmware upload

  Settings are saved in ESP32 NVS using Preferences and survive reboot.

  Confirmed TickrMeter pinout:
    EPD_POWER / EN = GPIO19   ACTIVE LOW: LOW=ON, HIGH=OFF
    BUSY           = GPIO18
    RST / RES#     = GPIO26
    DC / D-C#      = GPIO27
    CS#            = GPIO15
    SCK / SCL      = GPIO13
    MOSI / SDA     = GPIO14

    LED RED        = GPIO23   ACTIVE LOW
    LED GREEN      = GPIO22   ACTIVE LOW
    LED BLUE       = GPIO21   ACTIVE LOW

    BUZZER STBY    = GPIO17   HIGH = ON
    AUDIO SIGNAL   = GPIO25

  OTA AP:
    SSID: TickrMeter-CUSTOM
    PASS: 12345678
    URL : http://192.168.4.1/
*/

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Update.h>
#include <SPI.h>
#include <time.h>
#include <Preferences.h>

#include <GxEPD2_3C.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

// ============================================================
// DEFAULTS
// ============================================================

// Existing Wi-Fi from the old sketch is used only as FIRST-BOOT default.
// After saving through the web page, NVS settings take priority.
const char* DEFAULT_WIFI_SSID = "YOUR_WIFI_SSID";
const char* DEFAULT_WIFI_PASS = "YOUR_WIFI_PASSWORD";

const char* OTA_AP_SSID = "TickrMeter-CUSTOM";
const char* OTA_AP_PASS = "12345678";

// Lithuania: EET/EEST
const char* TZ_LT = "EET-2EEST,M3.5.0/3,M10.5.0/4";

// ============================================================
// PINOUT
// ============================================================

#define EPD_POWER 19
#define EPD_BUSY  18
#define EPD_RST   26
#define EPD_DC    27
#define EPD_CS    15
#define EPD_SCK   13
#define EPD_MOSI  14

#define LED_RED   23
#define LED_GREEN 22
#define LED_BLUE  21

#define BUZZER_STBY 17
#define AUDIO_PIN   25

// ============================================================
// DISPLAY
// ============================================================

GxEPD2_3C<GxEPD2_290_C90c,
          GxEPD2_290_C90c::HEIGHT>
display(
  GxEPD2_290_C90c(
    EPD_CS,
    EPD_DC,
    EPD_RST,
    EPD_BUSY
  )
);

WebServer server(80);
DNSServer dnsServer;
Preferences prefs;

IPAddress apIP(192, 168, 4, 1);
IPAddress apGateway(192, 168, 4, 1);
IPAddress apSubnet(255, 255, 255, 0);

// ============================================================
// SAVED SETTINGS
// ============================================================

String wifiSsid;
String wifiPass;

bool ledEnabled = true;

// Old code had beep disabled, so default is OFF.
bool minuteBeepEnabled = false;

bool alarmEnabled = false;
int alarmHour = 7;
int alarmMinute = 0;

// Alarm sounds for this many seconds.
int alarmDurationSec = 20;

int lastShownMinute = -1;
int lastAlarmStamp = -1;
unsigned long lastReconnectTry = 0;

// ============================================================
// PREFERENCES
// ============================================================

void loadSettings()
{
  prefs.begin("clockcfg", false);

  wifiSsid = prefs.getString("ssid", DEFAULT_WIFI_SSID);
  wifiPass = prefs.getString("pass", DEFAULT_WIFI_PASS);

  ledEnabled = prefs.getBool("led", true);
  minuteBeepEnabled = prefs.getBool("minbeep", false);

  alarmEnabled = prefs.getBool("alarm", false);
  alarmHour = prefs.getInt("alarmH", 7);
  alarmMinute = prefs.getInt("alarmM", 0);
  alarmDurationSec = prefs.getInt("alarmSec", 20);

  alarmHour = constrain(alarmHour, 0, 23);
  alarmMinute = constrain(alarmMinute, 0, 59);
  alarmDurationSec = constrain(alarmDurationSec, 5, 120);
}

void saveSettings()
{
  prefs.putString("ssid", wifiSsid);
  prefs.putString("pass", wifiPass);

  prefs.putBool("led", ledEnabled);
  prefs.putBool("minbeep", minuteBeepEnabled);

  prefs.putBool("alarm", alarmEnabled);
  prefs.putInt("alarmH", alarmHour);
  prefs.putInt("alarmM", alarmMinute);
  prefs.putInt("alarmSec", alarmDurationSec);
}

// ============================================================
// RGB LED - ACTIVE LOW
// ============================================================

void ledsOff()
{
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, HIGH);
}

// Normal LED action obeys the web setting.
void ledSet(bool redOn, bool greenOn, bool blueOn)
{
  if (!ledEnabled)
  {
    ledsOff();
    return;
  }

  digitalWrite(LED_RED,   redOn   ? LOW : HIGH);
  digitalWrite(LED_GREEN, greenOn ? LOW : HIGH);
  digitalWrite(LED_BLUE,  blueOn  ? LOW : HIGH);
}

// Manual test ignores the LED enable setting.
void manualLedSet(bool redOn, bool greenOn, bool blueOn)
{
  digitalWrite(LED_RED,   redOn   ? LOW : HIGH);
  digitalWrite(LED_GREEN, greenOn ? LOW : HIGH);
  digitalWrite(LED_BLUE,  blueOn  ? LOW : HIGH);
}

void minuteLedTest()
{
  if (!ledEnabled) return;

  ledSet(true, false, false);
  delay(45);

  ledSet(false, true, false);
  delay(45);

  ledSet(false, false, true);
  delay(45);

  ledsOff();
}

void manualLedTest()
{
  manualLedSet(true, false, false);
  delay(180);

  manualLedSet(false, true, false);
  delay(180);

  manualLedSet(false, false, true);
  delay(180);

  ledsOff();
}

// ============================================================
// BUZZER
// ============================================================

void buzzerToneOnce(int freq, int durationMs)
{
  digitalWrite(BUZZER_STBY, HIGH);
  delay(8);

  tone(AUDIO_PIN, freq);
  delay(durationMs);
  noTone(AUDIO_PIN);

  digitalWrite(AUDIO_PIN, LOW);
  delay(5);

  digitalWrite(BUZZER_STBY, LOW);
}

void shortMinuteBeep()
{
  if (!minuteBeepEnabled) return;
  buzzerToneOnce(2200, 35);
}

// Automatic alarm. Alarm is independent from minuteBeepEnabled.
void runAlarm(int durationSec)
{
  unsigned long endAt = millis() + (unsigned long)durationSec * 1000UL;
  bool phase = false;

  while ((long)(endAt - millis()) > 0)
  {
    phase = !phase;

    if (ledEnabled)
    {
      if (phase) ledSet(true, false, false);
      else ledsOff();
    }

    digitalWrite(BUZZER_STBY, HIGH);
    delay(8);

    tone(AUDIO_PIN, phase ? 1900 : 2400);
    delay(120);
    noTone(AUDIO_PIN);

    digitalWrite(AUDIO_PIN, LOW);
    digitalWrite(BUZZER_STBY, LOW);

    // Keep the settings / OTA web server responsive during alarm.
    unsigned long pauseStart = millis();
    while (millis() - pauseStart < 260)
    {
      server.handleClient();
      delay(5);
    }
  }

  noTone(AUDIO_PIN);
  digitalWrite(AUDIO_PIN, LOW);
  digitalWrite(BUZZER_STBY, LOW);
  ledsOff();
}

// ============================================================
// E-PAPER POWER
// ============================================================

void epdPowerOn()
{
  digitalWrite(EPD_POWER, LOW);
  delay(600);
}

void epdPowerOff()
{
  delay(100);
  digitalWrite(EPD_POWER, HIGH);
}

// ============================================================
// 7-SEGMENT STYLE DIGITS
// ============================================================

const uint8_t DIGIT_SEGMENTS[10] =
{
  0b00111111, // 0
  0b00000110, // 1
  0b01011011, // 2
  0b01001111, // 3
  0b01100110, // 4
  0b01101101, // 5
  0b01111101, // 6
  0b00000111, // 7
  0b01111111, // 8
  0b01101111  // 9
};

void hSeg(int x, int y, int w, int t)
{
  display.fillRect(x + t, y, w - 2 * t, t, GxEPD_BLACK);
  display.fillTriangle(x, y + t / 2, x + t, y, x + t, y + t - 1, GxEPD_BLACK);
  display.fillTriangle(
    x + w - 1, y + t / 2,
    x + w - t - 1, y,
    x + w - t - 1, y + t - 1,
    GxEPD_BLACK
  );
}

void vSeg(int x, int y, int h, int t)
{
  display.fillRect(x, y + t, t, h - 2 * t, GxEPD_BLACK);
  display.fillTriangle(x + t / 2, y, x, y + t, x + t - 1, y + t, GxEPD_BLACK);
  display.fillTriangle(
    x + t / 2, y + h - 1,
    x, y + h - t - 1,
    x + t - 1, y + h - t - 1,
    GxEPD_BLACK
  );
}

void drawDigit(int x, int y, int digit)
{
  const int W = 42;
  const int H = 68;
  const int T = 7;
  const int HALF = H / 2;

  uint8_t s = DIGIT_SEGMENTS[digit];

  if (s & (1 << 0)) hSeg(x,         y,               W, T);
  if (s & (1 << 1)) vSeg(x + W - T, y,               HALF, T);
  if (s & (1 << 2)) vSeg(x + W - T, y + HALF,        HALF, T);
  if (s & (1 << 3)) hSeg(x,         y + H - T,        W, T);
  if (s & (1 << 4)) vSeg(x,         y + HALF,         HALF, T);
  if (s & (1 << 5)) vSeg(x,         y,                HALF, T);
  if (s & (1 << 6)) hSeg(x,         y + HALF - T / 2, W, T);
}

void drawColon(int x, int y)
{
  display.fillCircle(x, y + 23, 4, GxEPD_BLACK);
  display.fillCircle(x, y + 47, 4, GxEPD_BLACK);
}

void drawBigTime(int hour, int minute)
{
  const int y = 30;
  const int x0 = 37;

  drawDigit(x0,       y, hour / 10);
  drawDigit(x0 + 47,  y, hour % 10);

  drawColon(x0 + 96, y);

  drawDigit(x0 + 108, y, minute / 10);
  drawDigit(x0 + 155, y, minute % 10);
}

// ============================================================
// SMALL ICONS / FOOTER
// ============================================================

void drawWifiSimple(int x, int y, bool connected)
{
  if (!connected)
  {
    display.drawLine(x, y + 2, x + 12, y + 14, GxEPD_BLACK);
    display.drawLine(x + 12, y + 2, x, y + 14, GxEPD_BLACK);
    return;
  }

  display.fillCircle(x + 6, y + 13, 2, GxEPD_BLACK);

  display.drawLine(x + 2, y + 9, x + 6, y + 6, GxEPD_BLACK);
  display.drawLine(x + 6, y + 6, x + 10, y + 9, GxEPD_BLACK);

  display.drawLine(x, y + 5, x + 6, y + 1, GxEPD_BLACK);
  display.drawLine(x + 6, y + 1, x + 12, y + 5, GxEPD_BLACK);
}

void drawBatteryOutline(int x, int y)
{
  display.drawRect(x, y, 24, 12, GxEPD_BLACK);
  display.fillRect(x + 24, y + 3, 3, 6, GxEPD_BLACK);

  // Placeholder until GPIO34 battery calibration is final.
  display.fillRect(x + 3, y + 3, 9, 6, GxEPD_BLACK);
}

const char* weekdayName(int wday)
{
  static const char* names[] =
  {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
  };

  if (wday < 0 || wday > 6) return "---";
  return names[wday];
}

// ============================================================
// DRAW CLOCK
// ============================================================

void drawClock(const struct tm& t)
{
  ledSet(false, false, true);

  epdPowerOn();

  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);

  display.init(115200);
  display.setRotation(3);
  display.setFullWindow();

  display.firstPage();

  do
  {
    display.fillScreen(GxEPD_WHITE);

    display.setFont(&FreeSansBold12pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(7, 18);
    display.print("TickrMeter");

    drawWifiSimple(250, 3, WiFi.status() == WL_CONNECTED);

    display.drawLine(5, 24, 291, 24, GxEPD_BLACK);

    drawBigTime(t.tm_hour, t.tm_min);

    display.drawLine(5, 102, 291, 102, GxEPD_BLACK);

    char dateBuf[16];
    snprintf(
      dateBuf,
      sizeof(dateBuf),
      "%04d-%02d-%02d",
      t.tm_year + 1900,
      t.tm_mon + 1,
      t.tm_mday
    );

    display.setFont(&FreeSans9pt7b);
    display.setCursor(7, 123);
    display.print(dateBuf);

    display.setFont(&FreeSansBold9pt7b);
    display.setCursor(118, 123);
    display.print(weekdayName(t.tm_wday));

    // Alarm state in footer.
    display.setFont(&FreeSans9pt7b);
    display.setCursor(163, 123);

    if (alarmEnabled)
    {
      char alarmBuf[12];
      snprintf(
        alarmBuf,
        sizeof(alarmBuf),
        "A %02d:%02d",
        alarmHour,
        alarmMinute
      );
      display.print(alarmBuf);
    }
    else
    {
      display.print("A OFF");
    }

    drawBatteryOutline(262, 110);
  }
  while (display.nextPage());

  display.hibernate();
  epdPowerOff();

  ledSet(false, true, false);
  delay(150);
  ledsOff();
}

// ============================================================
// WIFI / TIME
// ============================================================

void connectHomeWifi()
{
  if (WiFi.status() == WL_CONNECTED)
    return;

  if (wifiSsid.length() == 0)
    return;

  WiFi.disconnect(false, false);
  delay(100);

  WiFi.begin(
    wifiSsid.c_str(),
    wifiPass.c_str()
  );

  unsigned long start = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - start < 15000
  )
  {
    server.handleClient();
    delay(200);
  }
}

void configureNtp()
{
  configTzTime(
    TZ_LT,
    "pool.ntp.org",
    "time.google.com",
    "time.cloudflare.com"
  );
}

bool waitForTime(unsigned long timeoutMs)
{
  unsigned long start = millis();

  while (millis() - start < timeoutMs)
  {
    server.handleClient();

    if (time(nullptr) > 1700000000)
      return true;

    delay(100);
  }

  return false;
}

// ============================================================
// HTML HELPERS
// ============================================================

String htmlEscape(const String& s)
{
  String r;
  r.reserve(s.length() + 12);

  for (size_t i = 0; i < s.length(); i++)
  {
    char c = s[i];

    if (c == '&') r += "&amp;";
    else if (c == '<') r += "&lt;";
    else if (c == '>') r += "&gt;";
    else if (c == '"') r += "&quot;";
    else if (c == '\'') r += "&#39;";
    else r += c;
  }

  return r;
}

String alarmTimeString()
{
  char b[6];
  snprintf(b, sizeof(b), "%02d:%02d", alarmHour, alarmMinute);
  return String(b);
}

String currentTimeString()
{
  time_t now = time(nullptr);

  if (now <= 1700000000)
    return "--:--";

  struct tm t;

  if (!localtime_r(&now, &t))
    return "--:--";

  char b[9];
  strftime(b, sizeof(b), "%H:%M:%S", &t);
  return String(b);
}

// ============================================================
// WEB UI
// ============================================================

String buildMainPage()
{
  String page;
  page.reserve(7000);

  page += R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TickrMeter Clock</title>
<style>
  body{
    margin:0;
    padding:18px;
    font-family:Arial,sans-serif;
    background:#111;
    color:#eee;
  }
  .wrap{
    max-width:560px;
    margin:auto;
  }
  .card{
    background:#1d1d1d;
    border:1px solid #333;
    border-radius:14px;
    padding:18px;
    margin-bottom:14px;
  }
  h2,h3{margin-top:0}
  .status{
    display:grid;
    grid-template-columns:1fr 1fr;
    gap:8px;
    font-size:14px;
  }
  label{
    display:block;
    margin:12px 0 6px;
    color:#bbb;
  }
  input[type=text],
  input[type=password],
  input[type=time],
  input[type=number]{
    width:100%;
    box-sizing:border-box;
    padding:11px;
    font-size:16px;
    border:1px solid #555;
    border-radius:8px;
    background:#0e0e0e;
    color:#fff;
  }
  .switchrow{
    display:flex;
    align-items:center;
    justify-content:space-between;
    gap:16px;
    padding:10px 0;
    border-bottom:1px solid #333;
  }
  input[type=checkbox]{
    width:24px;
    height:24px;
  }
  button,.btn,input[type=submit]{
    display:inline-block;
    box-sizing:border-box;
    min-height:44px;
    padding:10px 14px;
    margin:5px 4px 5px 0;
    border:0;
    border-radius:8px;
    font-size:15px;
    background:#e8e8e8;
    color:#111;
    text-decoration:none;
    cursor:pointer;
  }
  .primary{
    width:100%;
    margin-top:16px;
    font-weight:bold;
  }
  .muted{
    color:#999;
    font-size:13px;
  }
</style>
</head>
<body>
<div class="wrap">

<div class="card">
<h2>TickrMeter Clock</h2>
<div class="status">
<div>Time</div><div><b>)HTML";

  page += currentTimeString();

  page += R"HTML(</b></div>
<div>Home Wi-Fi</div><div><b>)HTML";

  page += (WiFi.status() == WL_CONNECTED ? "CONNECTED" : "OFFLINE");

  page += R"HTML(</b></div>
<div>STA IP</div><div>)HTML";

  page += WiFi.localIP().toString();

  page += R"HTML(</div>
<div>OTA AP</div><div>192.168.4.1</div>
</div>
</div>

<form method="POST" action="/save">

<div class="card">
<h3>Clock options</h3>

<div class="switchrow">
<span>RGB LED effects</span>
<input type="checkbox" name="led" )HTML";

  if (ledEnabled) page += "checked";

  page += R"HTML(>
</div>

<div class="switchrow">
<span>Beep every minute</span>
<input type="checkbox" name="minbeep" )HTML";

  if (minuteBeepEnabled) page += "checked";

  page += R"HTML(>
</div>

<div class="switchrow">
<span>Alarm enabled</span>
<input type="checkbox" name="alarm" )HTML";

  if (alarmEnabled) page += "checked";

  page += R"HTML(>
</div>

<label>Alarm time</label>
<input type="time" name="alarmtime" value=")HTML";

  page += alarmTimeString();

  page += R"HTML(">

<label>Alarm duration, seconds</label>
<input type="number" name="alarmsec" min="5" max="120" value=")HTML";

  page += String(alarmDurationSec);

  page += R"HTML(">
</div>

<div class="card">
<h3>Home Wi-Fi</h3>

<label>SSID</label>
<input type="text" name="ssid" maxlength="32" value=")HTML";

  page += htmlEscape(wifiSsid);

  page += R"HTML(">

<label>Password</label>
<input type="password" name="pass" maxlength="64"
       placeholder="Leave blank to keep current password">

<p class="muted">
TickrMeter-CUSTOM OTA access point stays enabled even if home Wi-Fi fails.
</p>

<input class="primary" type="submit" value="SAVE SETTINGS + RESTART">
</div>

</form>

<div class="card">
<h3>Tests</h3>
<a class="btn" href="/test-led">Test RGB</a>
<a class="btn" href="/test-beep">Test beep</a>
<a class="btn" href="/test-alarm">Test alarm 3s</a>
<a class="btn" href="/refresh">Refresh screen</a>
</div>

<div class="card">
<h3>Firmware update</h3>
<form method="POST" action="/update" enctype="multipart/form-data">
<input type="file" name="update" accept=".bin" required>
<br><br>
<input type="submit" value="UPLOAD .BIN">
</form>
</div>

</div>
</body>
</html>
)HTML";

  return page;
}

void startWebServer()
{
  server.on("/", HTTP_GET, []()
  {
    server.send(200, "text/html", buildMainPage());
  });

  server.on("/save", HTTP_POST, []()
  {
    ledEnabled = server.hasArg("led");
    minuteBeepEnabled = server.hasArg("minbeep");
    alarmEnabled = server.hasArg("alarm");

    if (server.hasArg("alarmtime"))
    {
      String a = server.arg("alarmtime");

      if (a.length() >= 5 && a.charAt(2) == ':')
      {
        int h = a.substring(0, 2).toInt();
        int m = a.substring(3, 5).toInt();

        if (h >= 0 && h <= 23 && m >= 0 && m <= 59)
        {
          alarmHour = h;
          alarmMinute = m;
        }
      }
    }

    if (server.hasArg("alarmsec"))
    {
      alarmDurationSec =
        constrain(server.arg("alarmsec").toInt(), 5, 120);
    }

    if (server.hasArg("ssid"))
    {
      String newSsid = server.arg("ssid");
      newSsid.trim();

      if (newSsid.length() > 0)
        wifiSsid = newSsid;
    }

    // Blank password means: preserve current password.
    if (server.hasArg("pass"))
    {
      String newPass = server.arg("pass");

      if (newPass.length() > 0)
        wifiPass = newPass;
    }

    saveSettings();

    server.send(
      200,
      "text/html",
      "<html><body style='font-family:Arial;text-align:center'>"
      "<h2>Settings saved</h2>"
      "<p>TickrMeter is restarting...</p>"
      "</body></html>"
    );

    delay(1000);
    ESP.restart();
  });

  server.on("/test-led", HTTP_GET, []()
  {
    manualLedTest();
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/test-beep", HTTP_GET, []()
  {
    // Manual test ignores minute-beep setting.
    buzzerToneOnce(2200, 100);
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/test-alarm", HTTP_GET, []()
  {
    runAlarm(3);
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/refresh", HTTP_GET, []()
  {
    time_t now = time(nullptr);
    struct tm t;

    if (now > 1700000000 && localtime_r(&now, &t))
      drawClock(t);

    server.sendHeader("Location", "/");
    server.send(303);
  });

  // Keep old /update URL useful.
  server.on("/update", HTTP_GET, []()
  {
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on(
    "/update",
    HTTP_POST,

    []()
    {
      if (Update.hasError())
      {
        server.send(500, "text/plain", "UPDATE FAILED");
      }
      else
      {
        server.send(200, "text/plain", "UPDATE OK - REBOOT");
        delay(1000);
        ESP.restart();
      }
    },

    []()
    {
      HTTPUpload& upload = server.upload();

      if (upload.status == UPLOAD_FILE_START)
      {
        if (!Update.begin(UPDATE_SIZE_UNKNOWN))
          Update.printError(Serial);
      }
      else if (upload.status == UPLOAD_FILE_WRITE)
      {
        if (
          Update.write(
            upload.buf,
            upload.currentSize
          ) != upload.currentSize
        )
        {
          Update.printError(Serial);
        }
      }
      else if (upload.status == UPLOAD_FILE_END)
      {
        if (!Update.end(true))
          Update.printError(Serial);
      }
    }
  );

  // Captive portal: any unknown URL goes back to settings page.
  server.onNotFound([]()
  {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  });

  server.begin();
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
  Serial.begin(115200);
  delay(500);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  ledsOff();

  pinMode(BUZZER_STBY, OUTPUT);
  digitalWrite(BUZZER_STBY, LOW);

  pinMode(AUDIO_PIN, OUTPUT);
  digitalWrite(AUDIO_PIN, LOW);

  pinMode(EPD_POWER, OUTPUT);
  digitalWrite(EPD_POWER, HIGH);

  loadSettings();

  // OTA AP is always available.
  WiFi.mode(WIFI_AP_STA);

  // Force a known AP address.
  WiFi.softAPConfig(apIP, apGateway, apSubnet);
  WiFi.softAP(OTA_AP_SSID, OTA_AP_PASS);

  // Captive portal DNS.
  dnsServer.start(53, "*", apIP);

  startWebServer();

  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  connectHomeWifi();
  configureNtp();
  waitForTime(10000);

  // Startup feedback obeys settings.
  minuteLedTest();
  shortMinuteBeep();

  time_t now = time(nullptr);
  struct tm t;

  if (now > 1700000000 && localtime_r(&now, &t))
  {
    drawClock(t);
    lastShownMinute = t.tm_min;
  }
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
  dnsServer.processNextRequest();
  server.handleClient();

  if (
    WiFi.status() != WL_CONNECTED &&
    millis() - lastReconnectTry > 60000
  )
  {
    lastReconnectTry = millis();
    connectHomeWifi();
  }

  time_t now = time(nullptr);

  if (now > 1700000000)
  {
    struct tm t;

    if (localtime_r(&now, &t))
    {
      bool newMinute = (t.tm_min != lastShownMinute);

      int alarmStamp =
        t.tm_yday * 1440 +
        t.tm_hour * 60 +
        t.tm_min;

      bool alarmNow =
        alarmEnabled &&
        t.tm_hour == alarmHour &&
        t.tm_min == alarmMinute &&
        alarmStamp != lastAlarmStamp;

      if (newMinute)
      {
        lastShownMinute = t.tm_min;

        minuteLedTest();

        if (alarmNow)
        {
          lastAlarmStamp = alarmStamp;
          runAlarm(alarmDurationSec);
        }
        else
        {
          shortMinuteBeep();
        }

        drawClock(t);
      }
    }
  }

  delay(50);
}
