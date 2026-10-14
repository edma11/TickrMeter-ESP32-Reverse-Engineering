/*
  Unofficial TickrMeter Rev. 1.6 hardware research example.
  Independently written test code; no original manufacturer firmware/code included.
  Verify GPIO assignments before use on another hardware revision.
*/

/*
  TickrMeter Rev 1.6 - NTP CLOCK + WEB SETTINGS + DEEP SLEEP
  ESP32-WROOM-32E + 2.9" e-paper

  Web settings:
    - RGB LED effects ON/OFF
    - minute beep ON/OFF
    - alarm ON/OFF + time + duration
    - home Wi-Fi SSID/password
    - LED / beep / alarm test buttons
    - OTA firmware upload

  Settings are saved in ESP32 NVS using Preferences and survive reboot.
  Web/AP/OTA: 5 minutes after RESET/power-on/software restart only.
  Timer wake: radio off except hourly NTP; redraw once per minute.
  GxEPD2_290_C90c uses full refresh (~27 seconds); no fast partial refresh.

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
#include <esp_sleep.h>
#include <esp_sntp.h>
#include <driver/gpio.h>
#include <sys/time.h>
#include <math.h>
#include "ClockLogic.h"

#include <GxEPD2_3C.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

// ============================================================
// DEFAULTS
// ============================================================

// Existing Wi-Fi from the old sketch is used only as FIRST-BOOT default.
// After saving through the web page, NVS settings take priority.
const char* DEFAULT_WIFI_SSID = "";
const char* DEFAULT_WIFI_PASS = "";

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

// Confirmed battery divider: GPIO32 ADC1, 10k/10k; GPIO4 HIGH enables it.
// User confirmed GPIO4 may be switched off. HIGH only while measuring;
// LOW between measurements and held LOW throughout deep sleep.
constexpr uint8_t PIN_BAT_EN = 4;
constexpr float BAT_DIVIDER_FACTOR = 2.000f;
const uint8_t BAT_ADC_PINS[] = {32, 33};
uint8_t batteryPin = 32;
float batteryCorrection[2] = {1.0f, 1.0f};
float batteryEmptyV = 3.0f, batteryFullV = 4.2f;
uint16_t batteryRaw[2] = {0, 0};
uint32_t batteryNominalMv[2] = {0, 0};
bool batteryChannelValid[2] = {false, false};
uint32_t batterySampleId = 0, batterySampleAt = 0;
bool batterySampleRequested = false;
uint32_t batteryMilliVolts = 0;
bool batteryValid = false;
int batteryPercent = -1;

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

const uint32_t WEB_WINDOW_MS = ClockLogic::windowMs;
const time_t VALID_TIME = 1700000000;
RTC_DATA_ATTR time_t lastShownStamp = 0;
RTC_DATA_ATTR int lastAlarmDay = -1;
RTC_DATA_ATTR time_t lastNtpSync = 0;
bool webActive = false, uploadActive = false, uploadOK = false;
bool refreshRequested = false, restartPending = false;
bool drawing = false;
uint32_t webStarted = 0, uploadActivity = 0, restartAt = 0;
uint32_t alarmStarted = 0, alarmLength = 0;
bool alarmRunning = false;
volatile bool ntpSynced = false;
String screenTitle = "KUPISKIS";
String alarmTimeString();
void serviceWeb();
void stopWeb();
void serviceAlarm();
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

  batteryPin = prefs.getUChar("batPin", 32);
  if (batteryPin != 32 && batteryPin != 33) batteryPin = 32;
  batteryCorrection[0] = prefs.getFloat("batCal32", 1.0f);
  batteryCorrection[1] = prefs.getFloat("batCal33", 1.0f);
  for (float& factor : batteryCorrection)
    if (!isfinite(factor) || factor < 0.25f || factor > 4.0f) factor = 1.0f;
  batteryEmptyV = prefs.getFloat("batEmpty", 3.0f);
  batteryFullV = prefs.getFloat("batFull", 4.2f);
  if (!isfinite(batteryEmptyV) || !isfinite(batteryFullV) || batteryEmptyV < 2.0f
      || batteryFullV > 6.0f || lroundf(batteryFullV*1000)-lroundf(batteryEmptyV*1000) < 100) {
    batteryEmptyV = 3.0f; batteryFullV = 4.2f;
  }

  screenTitle = prefs.getString("title", "KUPISKIS");
  alarmHour = constrain(alarmHour, 0, 23);
  alarmMinute = constrain(alarmMinute, 0, 59);
  alarmDurationSec = constrain(alarmDurationSec, 5, 120);
}

void saveSettings()
{
  prefs.putString("title", screenTitle);
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
  if (alarmRunning) return;
  alarmStarted = millis();
  alarmLength = (uint32_t)durationSec * 1000;
  alarmRunning = true;
}

void stopAlarm()
{
  noTone(AUDIO_PIN);
  digitalWrite(AUDIO_PIN, LOW);
  digitalWrite(BUZZER_STBY, LOW);
  ledsOff();
  alarmRunning = false;
}

void serviceAlarm()
{
  if (!alarmRunning) return;
  uint32_t elapsed = millis() - alarmStarted;
  if (elapsed >= alarmLength) { stopAlarm(); return; }
  bool on = elapsed % 400 < 140;
  digitalWrite(BUZZER_STBY, on ? HIGH : LOW);
  if (on) tone(AUDIO_PIN, ((elapsed / 400) % 2) ? 2400 : 1900);
  else { noTone(AUDIO_PIN); digitalWrite(AUDIO_PIN, LOW); }
  ledSet(on, false, false);
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
  const int x0 = (display.width() - 197) / 2;

  drawDigit(x0,       y, hour / 10);
  drawDigit(x0 + 47,  y, hour % 10);

  drawColon(x0 + 96, y);

  drawDigit(x0 + 108, y, minute / 10);
  drawDigit(x0 + 155, y, minute % 10);
}

// ============================================================
// SMALL ICONS / FOOTER
// ============================================================

void initBattery()
{
  pinMode(PIN_BAT_EN, OUTPUT);
  digitalWrite(PIN_BAT_EN, LOW);
  gpio_hold_dis((gpio_num_t)PIN_BAT_EN);
  analogReadResolution(12);
  for (uint8_t pin : BAT_ADC_PINS) {
    pinMode(pin, INPUT);
    analogSetPinAttenuation(pin, ADC_11db);
  }
  digitalWrite(PIN_BAT_EN, LOW);
}

void updateBatteryValue()
{
  int channel = batteryPin == 33 ? 1 : 0;
  batteryValid = batteryChannelValid[channel];
  batteryMilliVolts = (uint32_t)(batteryNominalMv[channel] * batteryCorrection[channel] + 0.5f);
  batteryPercent = batteryValid ? ClockLogic::chargePercent(batteryMilliVolts,
    (uint32_t)lroundf(batteryEmptyV*1000), (uint32_t)lroundf(batteryFullV*1000)) : -1;
}

void sampleBattery()
{
  // One averaged measurement per frame, before LED/display load is enabled.
  // RAW and calibrated mV are independent conversions, as in the v2.3 test.
  digitalWrite(PIN_BAT_EN, HIGH);
  delay(150); // Divider settling after every enable, including timer wakes.
  for (int channel = 0; channel < 2; ++channel) {
    uint8_t pin = BAT_ADC_PINS[channel];
    (void)analogRead(pin);
    uint32_t rawSum = 0, mvSum = 0;
    for (int i = 0; i < 16; ++i) {
      rawSum += analogRead(pin);
      mvSum += analogReadMilliVolts(pin);
      delayMicroseconds(150);
    }
    batteryRaw[channel] = (rawSum + 8) / 16;
    uint32_t adcMv = (mvSum + 8) / 16;
    batteryNominalMv[channel] = (uint32_t)(adcMv * BAT_DIVIDER_FACTOR + 0.5f);
    batteryChannelValid[channel] = batteryRaw[channel] > 8 && batteryRaw[channel] < 4080 && adcMv > 0;
  }
  digitalWrite(PIN_BAT_EN, LOW);
  ++batterySampleId; batterySampleAt = millis();
  updateBatteryValue();
}

void drawBattery()
{
  display.drawRect(6, 7, 22, 12, GxEPD_BLACK);
  display.fillRect(28, 10, 3, 6, GxEPD_BLACK);
  if (batteryValid) {
    int bars = ClockLogic::batteryBars(batteryPercent);
    for (int i = 0; i < bars; ++i) display.fillRect(9 + i * 4, 10, 3, 6, GxEPD_BLACK);
  } else {
    display.setFont(nullptr); display.setTextSize(1);
    display.setCursor(14, 9); display.print("?");
  }
  display.setFont(&FreeSans9pt7b);
  display.setCursor(36, 19);
  if (batteryValid) {
    char voltage[12];
    uint32_t hundredths = (batteryMilliVolts + 5) / 10;
    snprintf(voltage, sizeof(voltage), "%lu.%02lu V",
             (unsigned long)(hundredths / 100), (unsigned long)(hundredths % 100));
    display.print(voltage);
  } else {
    display.print("--.-- V");
  }
}

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

const char* weekdayName(int wday)
{
  static const char* names[] = {"Sek", "Pir", "Ant", "Tre", "Ket", "Pen", "Ses"};
  return (wday >= 0 && wday <= 6) ? names[wday] : "---";
}

void centeredText(const String& text, int left, int right, int baseline, const GFXfont* font)
{
  display.setFont(font);
  int16_t x, y; uint16_t w, h;
  display.getTextBounds(text, 0, baseline, &x, &y, &w, &h);
  display.setCursor(left + (right - left - (int)w) / 2 - x, baseline);
  display.print(text);
}

void drawHeader()
{
  drawBattery();
  // Retain older saved titles without letting them overlap the battery.
  String title = screenTitle;
  display.setFont(&FreeSansBold9pt7b);
  int16_t x, y; uint16_t w, h;
  display.getTextBounds(title, 0, 19, &x, &y, &w, &h);
  if (w > 138) {
    do {
      title.remove(title.length() - 1);
      display.getTextBounds(title + "...", 0, 19, &x, &y, &w, &h);
    } while (w > 138 && title.length());
    title += "...";
  }
  centeredText(title, 104, 242, 19, &FreeSansBold9pt7b);
  display.setFont(nullptr); display.setTextSize(1);
  display.setCursor(248, 9); display.print(webActive ? "W" : "R");
  drawWifiSimple(276, 3, WiFi.status() == WL_CONNECTED);
  display.drawLine(5, 24, 291, 24, GxEPD_BLACK);
}

// ============================================================
// DRAW CLOCK
// ============================================================

void drawClock(const struct tm& t)
{
  drawing = true;
  sampleBattery();
  ledSet(false, false, true);

  epdPowerOn();

  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);

  display.init(115200);
  display.setRotation(3);
  display.setFullWindow();
  display.setTextWrap(false);
  display.epd2.setBusyCallback([](const void*) { serviceWeb(); serviceAlarm(); delay(1); });

  display.firstPage();

  do
  {
    display.fillScreen(GxEPD_WHITE);

    display.setTextColor(GxEPD_BLACK);
    drawHeader();

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

    centeredText(dateBuf, 4, 139, 122, &FreeSans9pt7b);
    centeredText(weekdayName(t.tm_wday), 140, 183, 122, &FreeSansBold9pt7b);
    String alarmText = alarmEnabled ? "A " + alarmTimeString() : "A --:--";
    centeredText(alarmText, 185, 292, 122, &FreeSans9pt7b);
  }
  while (display.nextPage());

  display.hibernate();
  epdPowerOff();

  ledSet(false, true, false);
  delay(150);
  ledsOff();
  drawing = false;
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
    serviceWeb();
    delay(200);
  }
}

void configureNtp()
{
  sntp_set_time_sync_notification_cb([](struct timeval*) { ntpSynced = true; });
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
    serviceWeb();

    if (ntpSynced)
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

// All actions are queued; handlers never call handleClient recursively.
const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="lt"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>TickrMeter laikrodis</title><style>
:root{color-scheme:dark;font-family:system-ui,sans-serif;background:#101724;color:#e8edf7}
*{box-sizing:border-box}body{margin:0;padding:20px}main{max-width:680px;margin:auto}
h1{font-size:28px}h2{font-size:19px;margin-top:0}section{background:#1b2638;border:1px solid #33435c;border-radius:16px;padding:22px;margin:16px 0}
label{display:block;margin:14px 0 7px}input:not([type=checkbox]),select,button{font:inherit;padding:12px;border-radius:8px;border:1px solid #526581;width:100%;background:#101724;color:inherit}
input[type=checkbox]{width:22px;height:22px;vertical-align:middle;margin-right:12px}
button{background:#8ac9ff;color:#102035;border:0;cursor:pointer;margin-top:12px;font-weight:600}
.row{display:flex;gap:12px}.row>*{flex:1;min-width:0}small,p{color:#bac8dc;line-height:1.5}.time{font-size:38px;font-variant-numeric:tabular-nums}
progress{width:100%;height:18px}#message{white-space:pre-wrap}a{color:#8ac9ff}@media(max-width:440px){.row{display:block}}
</style><main><h1>TickrMeter</h1><section><div class="time" id="clock">--:--:--</div>
<p id="status">Skaitoma būsena…</p><p>Web ir OTA langas: <b id="remaining">—</b>. Langui pasibaigus laikrodis miega ir pabunda kas minutę. Norėdami vėl prisijungti, paspauskite RESET.</p></section>
<form id="settings"><section><h2>Laikrodis ir garsas</h2>
<label>Ekrano antraštė (iki 18 ASCII simbolių)</label><input name="title" maxlength="18" pattern="[ -~]{1,18}" value="%TITLE%" required>
<small>Ekrano šriftai neturi lietuviškų raidžių: rašykite, pvz., KUPISKIS. Web puslapis palaiko lietuvių kalbą.</small>
<label><input type="checkbox" name="led" %LED%>RGB efektai</label>
<label><input type="checkbox" name="minbeep" %BEEP%>Pyptelėjimas kas minutę</label>
<label><input type="checkbox" name="alarm" %ALARM%>Žadintuvas</label>
<div class="row"><div><label>Žadintuvo laikas</label><input type="time" name="alarmtime" value="%TIME%" required></div>
<div><label>Trukmė, sekundės</label><input type="number" name="alarmsec" min="5" max="120" value="%SECONDS%" required></div></div></section>
<section><h2>Namų Wi-Fi</h2><label>Tinklo pavadinimas (SSID)</label><input name="ssid" maxlength="32" value="%SSID%">
<label>Slaptažodis</label><input type="password" name="pass" maxlength="64" autocomplete="new-password" placeholder="Tuščias – išlaikyti esamą">
<label><input type="checkbox" name="openwifi">Atviras tinklas (ištrinti slaptažodį)</label>
<small>Tuščias SSID išjungia namų Wi-Fi. Išsaugojus Wi-Fi pakeitimai taikomi iškart; 5 min. langas nepratęsiamas.</small>
<button>Išsaugoti nustatymus</button></section></form>
<form id="battery"><section><h2>Baterija ir kalibravimas</h2>
<p id="battery-status">Laukiama baterijos matavimo…</p>
<label>Baterijos rodmens šaltinis</label><select name="pin" id="battery-pin"><option value="32" %PIN32%>GPIO32</option><option value="33" %PIN33%>GPIO33</option></select>
<small>Pasirinkite kanalą, prijungtą prie baterijos. GPIO33 tinklo paskirtis automatiškai nenustatoma.</small>
<div class="row"><div><label>Išmatuota be pataisos, V</label><input id="measured" readonly></div>
<div><label>Po kalibravimo, V</label><input id="calibrated" readonly></div></div>
<p id="calibration-info"></p><input type="hidden" name="sample" id="battery-sample" value="0">
<button type="button" id="battery-measure">Pamatuoti iš naujo</button>
<label>Tikra įtampa pagal multimetrą, V</label><input name="real" id="battery-real" type="number" step="0.001" min="2" max="6" placeholder="Pvz., 3.850">
<small>Įveskite vienu metu multimetru pamatuotą įtampą. Tuščias laukas išlaiko esamą kalibravimą. Pataisa saugoma atskirai kiekvienam GPIO.</small>
<div class="row"><div><label>Tuščia baterija (0 %), V</label><input name="empty" type="number" min="2" max="6" step="0.01" value="%EMPTY%" required></div>
<div><label>Pilna baterija (100 %), V</label><input name="full" type="number" min="2" max="6" step="0.01" value="%FULL%" required></div></div>
<small>4 padalos ir procentai apskaičiuojami apytiksliai, tiesiškai pagal šias ribas. Tai nėra tikslus baterijos talpos matavimas.</small>
<button name="operation" value="apply">Išsaugoti šaltinį / kalibruoti</button>
<button name="operation" value="reset" formnovalidate>Atkurti pasirinkto GPIO kalibravimą</button>
</section></form>
<section><h2>Patikra</h2><div class="row"><button type="button" data-action="led">RGB</button><button type="button" data-action="beep">Garsas</button><button type="button" data-action="alarm">Žadintuvas 3 s</button></div>
<div class="row"><button type="button" data-action="stop">Nutildyti</button><button type="button" data-action="refresh">Atnaujinti ekraną</button></div>
<small>Šio trispalvio ekrano atnaujinimas gali trukti apie 27 s.</small></section>
<section><h2>OTA programinės įrangos atnaujinimas</h2><p>Pasirinkite šiam ESP32 sukompiliuotą programos .bin failą. Naudokite skaidinių lentelę su OTA. Įkėlimo metu įrenginys nemiega.</p>
<form id="ota"><input type="file" name="update" accept=".bin" required><button>Įkelti .bin</button></form><progress id="progress" max="100" value="0"></progress></section>
<p id="message" role="status" aria-live="polite"></p></main><script>
const msg=document.querySelector('#message');let updating=false;
let batteryState=null, calibrationLocked=false, sampleWaiting=false;
function showBattery(s){batteryState=s;const channel=s.channels[document.querySelector('#battery-pin').value==='33'?1:0];
document.querySelector('#battery-status').textContent='Ekranas: GPIO'+s.pin+' · '+(s.valid?(s.mv/1000).toFixed(2)+' V · apie '+s.percent+' %':'matavimas negalioja');
if(calibrationLocked)return;
document.querySelector('#measured').value=channel.valid?(channel.nominal/1000).toFixed(3):'Nežinoma';
document.querySelector('#calibrated').value=channel.valid?(channel.corrected/1000).toFixed(3):'Nežinoma';
document.querySelector('#battery-sample').value=s.id;
document.querySelector('#calibration-info').textContent='Pataisos koeficientas: '+channel.factor.toFixed(5)+' · matavimo amžius '+Math.floor(s.age/1000)+' s.';
}
document.querySelector('#battery-pin').onchange=()=>{calibrationLocked=false;document.querySelector('#battery-real').value='';if(batteryState)showBattery(batteryState)};
document.querySelector('#battery-real').oninput=()=>{calibrationLocked=Boolean(document.querySelector('#battery-real').value)};
document.querySelector('#battery-measure').onclick=async()=>{try{calibrationLocked=false;sampleWaiting=true;await send('/action',new URLSearchParams({action:'battery-sample'}));}catch(e){msg.textContent=e.message;sampleWaiting=false}};
document.querySelector('#battery').onsubmit=async e=>{e.preventDefault();try{if(sampleWaiting)throw Error('Palaukite naujo matavimo.');const data=new URLSearchParams(new FormData(e.target));data.set('operation',e.submitter?.value||'apply');await send('/battery/save',data);calibrationLocked=false;document.querySelector('#battery-real').value='';await status();}catch(e){msg.textContent=e.message}};
async function send(url,body){const r=await fetch(url,{method:'POST',body});const t=await r.text();if(!r.ok)throw Error(t);msg.textContent=t;}
document.querySelector('#settings').onsubmit=async e=>{e.preventDefault();try{await send('/save',new URLSearchParams(new FormData(e.target)))}catch(e){msg.textContent=e.message}};
document.querySelectorAll('[data-action]').forEach(b=>b.onclick=async()=>{try{await send('/action',new URLSearchParams({action:b.dataset.action}))}catch(e){msg.textContent=e.message}});
async function status(){if(updating)return;try{const r=await fetch('/status',{cache:'no-store'});if(!r.ok)throw Error();const s=await r.json();document.querySelector('#clock').textContent=s.time;document.querySelector('#status').textContent=s.connected?'Wi-Fi prisijungęs · '+s.ip:'Wi-Fi neprisijungęs · laikas '+(s.valid?'išlaikomas RTC':'dar nesinchronizuotas');document.querySelector('#remaining').textContent=Math.floor(s.remaining/60)+':'+String(s.remaining%60).padStart(2,'0');if(sampleWaiting && !s.battery.pending){sampleWaiting=false;calibrationLocked=false;}showBattery(s.battery);}catch(e){document.querySelector('#status').textContent='Ryšys nutrūko arba įrenginys miega. Prisijungimui paspauskite RESET.'}}
setInterval(status,2000);status();
document.querySelector('#ota').onsubmit=e=>{e.preventDefault();updating=true;const b=e.target.querySelector('button');b.disabled=true;const x=new XMLHttpRequest();x.open('POST','/update');x.timeout=180000;x.upload.onprogress=e=>{if(e.lengthComputable)document.querySelector('#progress').value=e.loaded/e.total*100};x.onload=()=>{msg.textContent=x.responseText;updating=false;b.disabled=false};x.onerror=x.ontimeout=()=>{msg.textContent='Įkėlimas nutrūko. Patikrinkite ryšį ir bandykite dar kartą.';updating=false;b.disabled=false};x.send(new FormData(e.target));};
</script></html>)HTML";

String buildMainPage()
{
  String p(FPSTR(PAGE));
  p.replace("%TITLE%", htmlEscape(screenTitle));
  p.replace("%LED%", ledEnabled ? "checked" : "");
  p.replace("%BEEP%", minuteBeepEnabled ? "checked" : "");
  p.replace("%ALARM%", alarmEnabled ? "checked" : "");
  p.replace("%TIME%", alarmTimeString());
  p.replace("%SECONDS%", String(alarmDurationSec));
  p.replace("%SSID%", htmlEscape(wifiSsid));
  p.replace("%PIN32%", batteryPin == 32 ? "selected" : "");
  p.replace("%PIN33%", batteryPin == 33 ? "selected" : "");
  p.replace("%EMPTY%", String(batteryEmptyV, 2));
  p.replace("%FULL%", String(batteryFullV, 2));
  return p;
}

bool decimal(const String& s)
{
  if (!s.length()) return false;
  for (size_t i=0;i<s.length();++i) if (s[i]<'0'||s[i]>'9') return false;
  return true;
}

bool parseVoltage(String value, float& result)
{
  value.trim(); value.replace(',', '.');
  if (!value.length() || value.length() > 10) return false;
  bool dot = false;
  for (size_t i=0; i<value.length(); ++i) {
    if (value[i] == '.' && !dot) { dot = true; continue; }
    if (value[i] < '0' || value[i] > '9') return false;
  }
  char* end; result = strtof(value.c_str(), &end);
  return *end == '\0' && isfinite(result) && result >= 2.0f && result <= 6.0f;
}

String batteryJson()
{
  String json = "{\"pin\":" + String(batteryPin) + ",\"mv\":" + String(batteryMilliVolts);
  json += ",\"valid\":"; json += batteryValid ? "true" : "false";
  json += ",\"percent\":" + String(batteryPercent) + ",\"id\":" + String(batterySampleId);
  json += ",\"age\":" + String((uint32_t)(millis()-batterySampleAt));
  json += ",\"pending\":"; json += batterySampleRequested ? "true" : "false";
  json += ",\"channels\":[";
  for (int i=0; i<2; ++i) {
    if (i) json += ',';
    json += "{\"nominal\":" + String(batteryNominalMv[i]);
    json += ",\"corrected\":" + String((uint32_t)(batteryNominalMv[i]*batteryCorrection[i]+0.5f));
    json += ",\"factor\":" + String(batteryCorrection[i], 6) + ",\"valid\":";
    json += batteryChannelValid[i] ? "true" : "false";
    json += '}';
  }
  json += "]}"; return json;
}

bool wifiChanged = false;
uint8_t pendingTest = 0;
void startWebServer()
{
  server.on("/", HTTP_GET, [] { server.send(200,"text/html; charset=utf-8",buildMainPage()); });
  server.on("/status", HTTP_GET, [] {
    uint32_t elapsed=millis()-webStarted;
    String json="{\"time\":\""+currentTimeString()+"\",\"connected\":";
    json += WiFi.status()==WL_CONNECTED ? "true" : "false";
    json += ",\"valid\":"; json += time(nullptr)>VALID_TIME ? "true" : "false";
    json += ",\"ip\":\""+WiFi.localIP().toString()+"\",\"remaining\":";
    json += String(elapsed<WEB_WINDOW_MS ? (WEB_WINDOW_MS-elapsed+999)/1000 : 0);
    json += ",\"battery\":" + batteryJson() + "}";
    server.sendHeader("Cache-Control","no-store"); server.send(200,"application/json",json);
  });
  server.on("/battery/save", HTTP_POST, [] {
    if (uploadActive) {server.send(409,"text/plain","Vyksta OTA");return;}
    String pin = server.arg("pin"), operation = server.arg("operation");
    if ((pin != "32" && pin != "33") || (operation != "apply" && operation != "reset")) {
      server.send(400,"text/plain; charset=utf-8","Pasirinkite GPIO32 arba GPIO33.");return;
    }
    int channel = pin == "33" ? 1 : 0;
    float empty, full;
    if (!parseVoltage(server.arg("empty"),empty) || !parseVoltage(server.arg("full"),full)
        || lroundf(full*1000)-lroundf(empty*1000) < 100) {
      server.send(400,"text/plain; charset=utf-8","Pilnos baterijos įtampa turi viršyti tuščios bent 0.10 V (ribos 2–6 V).");return;
    }
    float correction = batteryCorrection[channel];
    String real = server.arg("real"); real.trim();
    if (operation == "reset") correction = 1.0f;
    else if (real.length()) {
      float voltage; String id = server.arg("sample");
      if (!parseVoltage(real,voltage) || !decimal(id) || id.length()>10) {
        server.send(400,"text/plain; charset=utf-8","Įveskite tikrą multimetro įtampą (2–6 V).");return;
      }
      if (batterySampleRequested || strtoul(id.c_str(),nullptr,10) != batterySampleId
          || !batteryChannelValid[channel] || millis()-batterySampleAt > 60000) {
        server.send(409,"text/plain; charset=utf-8","Matavimas pasenęs arba negalioja. Pamatuokite iš naujo ir pakartokite kalibravimą.");return;
      }
      // Always derive from the uncorrected sample; recalibration is not compounded.
      correction = voltage * 1000.0f / batteryNominalMv[channel];
      if (!isfinite(correction) || correction < 0.25f || correction > 4.0f) {
        server.send(400,"text/plain; charset=utf-8","Pataisa per didelė. Patikrinkite kanalą ir multimetro rodmenį.");return;
      }
    }
    batteryPin = channel == 0 ? 32 : 33; batteryCorrection[channel] = correction;
    batteryEmptyV = empty; batteryFullV = full; updateBatteryValue();
    bool stored = prefs.putUChar("batPin",batteryPin) == 1;
    stored = (prefs.putFloat("batCal32",batteryCorrection[0]) == 4) && stored;
    stored = (prefs.putFloat("batCal33",batteryCorrection[1]) == 4) && stored;
    stored = (prefs.putFloat("batEmpty",batteryEmptyV) == 4) && stored;
    stored = (prefs.putFloat("batFull",batteryFullV) == 4) && stored;
    refreshRequested = true;
    server.send(stored ? 200 : 500,"text/plain; charset=utf-8", stored
      ? "Baterijos nustatymai išsaugoti. Ekranas bus atnaujintas."
      : "Nustatymai taikomi, bet jų išsaugoti nepavyko.");
  });
  server.on("/save", HTTP_POST, [] {
    if(uploadActive){server.send(409,"text/plain","Vyksta OTA");return;}
    String a=server.arg("alarmtime"), dur=server.arg("alarmsec");
    String title=server.arg("title"), ssid=server.arg("ssid"), pass=server.arg("pass");
    bool good=a.length()==5 && a[2]==':' && decimal(a.substring(0,2)) && decimal(a.substring(3))
      && a.substring(0,2).toInt()<24 && a.substring(3).toInt()<60
      && dur.length()<=3 && decimal(dur) && dur.toInt()>=5 && dur.toInt()<=120
      && ssid.length()<=32 && title.length()>0 && title.length()<=18;
    for(size_t i=0;i<title.length();++i) if(title[i]<32 || title[i]>126) good=false;
    // Measure actual glyph bounds: even 18 wide letters must fit the header.
    display.setFont(&FreeSansBold9pt7b);
    int16_t bx,by; uint16_t bw,bh; display.getTextBounds(title,0,0,&bx,&by,&bw,&bh);
    if(bw>138) good=false;
    if(pass.length() && !(pass.length()>=8 && pass.length()<=63)) {
      bool hex=pass.length()==64;
      for(size_t i=0;i<pass.length();++i) if(!isxdigit((unsigned char)pass[i])) hex=false;
      if(!hex) good=false;
    }
    if(!good){server.send(400,"text/plain; charset=utf-8","Neteisingi nustatymai: patikrinkite laiką, trukmę, antraštės plotį ir Wi-Fi slaptažodį.");return;}
    String nextPass=server.hasArg("openwifi") ? "" : (pass.length() ? pass : wifiPass);
    wifiChanged=(ssid!=wifiSsid || nextPass!=wifiPass);
    wifiSsid=ssid; wifiPass=nextPass; screenTitle=title;
    ledEnabled=server.hasArg("led"); minuteBeepEnabled=server.hasArg("minbeep"); alarmEnabled=server.hasArg("alarm");
    alarmHour=a.substring(0,2).toInt();alarmMinute=a.substring(3).toInt();alarmDurationSec=dur.toInt();
    saveSettings(); refreshRequested=true;
    server.send(200,"text/plain; charset=utf-8","Nustatymai išsaugoti. Ekranas bus atnaujintas.");
  });
  server.on("/action", HTTP_POST, [] {
    if(uploadActive){server.send(409,"text/plain","Vyksta OTA");return;}
    String a=server.arg("action");
    if(a=="led") pendingTest=1;
    else if(a=="beep") pendingTest=2;
    else if(a=="alarm") runAlarm(3);
    else if(a=="stop") stopAlarm();
    else if(a=="refresh") refreshRequested=true;
    else if(a=="battery-sample") batterySampleRequested=true;
    else {server.send(400,"text/plain","Nezinomas veiksmas");return;}
    server.send(202,"text/plain; charset=utf-8","Komanda priimta.");
  });
  server.on("/update",HTTP_POST, [] {
    uploadActive=false;
    bool succeeded=uploadOK && !Update.hasError(); uploadOK=false;
    if(!succeeded) {server.send(400,"text/plain; charset=utf-8","OTA nepavyko. Patikrinkite .bin ir OTA skaidinių lentelę.");return;}
    server.send(200,"text/plain; charset=utf-8","OTA baigta. Įrenginys persikrauna.");
    restartPending=true; restartAt=millis();
  }, [] {
    HTTPUpload& u=server.upload();uploadActivity=millis();
    if(u.status==UPLOAD_FILE_START){
      uploadOK=false;stopAlarm();
      if(uploadActive) Update.abort();
      uploadActive=true;
      if(!u.filename.endsWith(".bin") || !Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.abort(); uploadActive=false;
      }
    } else if(u.status==UPLOAD_FILE_WRITE){
      if(uploadActive && Update.write(u.buf,u.currentSize)!=u.currentSize){Update.abort();uploadActive=false;}
    } else if(u.status==UPLOAD_FILE_END){
      if(uploadActive) uploadOK=u.totalSize>0 && Update.end(true);
      if(!uploadOK) Update.abort();
    } else if(u.status==UPLOAD_FILE_ABORTED){Update.abort();uploadActive=false;uploadOK=false;}
  });
  server.onNotFound([] {server.sendHeader("Location","http://192.168.4.1/",true);server.send(302,"text/plain","");});
  server.begin();
}

void serviceWeb()
{
  if (!webActive) return;
  if (!uploadActive && !restartPending && ClockLogic::windowExpired(millis(),webStarted)) {
    stopWeb(); WiFi.disconnect(true,false); WiFi.mode(WIFI_OFF); refreshRequested=true; return;
  }
  dnsServer.processNextRequest(); server.handleClient();
}

void stopWeb()
{
  if (!webActive) return;
  dnsServer.stop(); server.stop(); WiFi.softAPdisconnect(true); webActive=false;
}

void sleepToNextMinute()
{
  stopWeb(); WiFi.disconnect(true,false); WiFi.mode(WIFI_OFF);
  stopAlarm(); digitalWrite(EPD_POWER,HIGH);
  digitalWrite(PIN_BAT_EN, LOW);
  // Preserve output levels: EPD off, LEDs/audio off, battery divider OFF.
  const int held[]={EPD_POWER,LED_RED,LED_GREEN,LED_BLUE,BUZZER_STBY,AUDIO_PIN,PIN_BAT_EN};
  for(int pin:held) gpio_hold_en((gpio_num_t)pin);
  gpio_deep_sleep_hold_en();
  struct timeval tv; gettimeofday(&tv,nullptr);
  uint64_t us=ClockLogic::nextMinuteUs(tv.tv_sec,tv.tv_usec);
  esp_sleep_enable_timer_wakeup(us);
  Serial.flush(); esp_deep_sleep_start();
}

void showNoTime()
{
  drawing=true;sampleBattery();epdPowerOn();SPI.begin(EPD_SCK,-1,EPD_MOSI,EPD_CS);
  display.init(115200);display.setRotation(3);display.setFullWindow();display.setTextWrap(false);
  display.epd2.setBusyCallback([](const void*) {serviceWeb();delay(1);});
  display.firstPage();do {
    display.fillScreen(GxEPD_WHITE);display.setTextColor(GxEPD_BLACK);
    drawHeader();
    centeredText("LAIKAS NESINCHRONIZUOTAS",0,296,44,&FreeSans9pt7b);
    centeredText("--:--",0,296,75,&FreeSansBold12pt7b);
    centeredText(webActive ? "Wi-Fi: TickrMeter-CUSTOM" : "Nustatymams spauskite RESET",0,296,100,&FreeSans9pt7b);
    centeredText(webActive ? "192.168.4.1 / 5 min. po RESET" : "NTP bus bandomas vel",0,296,123,&FreeSans9pt7b);
  }while(display.nextPage());display.hibernate();epdPowerOff();drawing=false;
}

void setup()
{
  Serial.begin(115200);
  initBattery();
  bool timerWake=esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_TIMER;
  const int outputs[]={EPD_POWER,LED_RED,LED_GREEN,LED_BLUE,BUZZER_STBY,AUDIO_PIN};
  for(int pin:outputs){pinMode(pin,OUTPUT);digitalWrite(pin,(pin==BUZZER_STBY || pin==AUDIO_PIN)?LOW:HIGH);gpio_hold_dis((gpio_num_t)pin);}
  gpio_deep_sleep_hold_dis();
  loadSettings();setenv("TZ",TZ_LT,1);tzset();
  if(!timerWake){lastShownStamp=0;lastAlarmDay=-1;lastNtpSync=0;}
  if(!timerWake){
    WiFi.mode(WIFI_AP_STA);WiFi.softAPConfig(apIP,apGateway,apSubnet);
    WiFi.softAP(OTA_AP_SSID,OTA_AP_PASS);dnsServer.start(53,"*",apIP);
    webActive=true;webStarted=millis();startWebServer();
  }
  time_t now=time(nullptr);
  if(!timerWake || now<=VALID_TIME || lastNtpSync==0 || now<lastNtpSync || now-lastNtpSync>=3600){
    if(timerWake) WiFi.mode(WIFI_STA);
    connectHomeWifi();
    if(WiFi.status()==WL_CONNECTED){configureNtp();waitForTime(8000);}
    if(ntpSynced){lastNtpSync=time(nullptr);ntpSynced=false;}
    if(!webActive){WiFi.disconnect(true,false);WiFi.mode(WIFI_OFF);}
  }
  if(time(nullptr)<=VALID_TIME && !timerWake) showNoTime();
}

void loop()
{
  serviceWeb();
  if(restartPending){if(millis()-restartAt>=1000) ESP.restart();delay(1);return;}
  if(uploadActive){
    if(millis()-uploadActivity>90000){Update.abort();uploadActive=false;uploadOK=false;}
    else {delay(1);return;}
  }
  if (batterySampleRequested) {sampleBattery();batterySampleRequested=false;}
  if(webActive && ClockLogic::windowExpired(millis(),webStarted)){stopWeb();WiFi.disconnect(true,false);WiFi.mode(WIFI_OFF);refreshRequested=true;}
  if(ntpSynced){lastNtpSync=time(nullptr);ntpSynced=false;}
  if(wifiChanged){wifiChanged=false;WiFi.disconnect(false,false);if(wifiSsid.length())WiFi.begin(wifiSsid.c_str(),wifiPass.c_str());configureNtp();}
  if(webActive && WiFi.status()!=WL_CONNECTED && millis()-lastReconnectTry>60000){lastReconnectTry=millis();if(wifiSsid.length())WiFi.begin(wifiSsid.c_str(),wifiPass.c_str());}
  if(pendingTest && !drawing){uint8_t test=pendingTest;pendingTest=0;if(test==1)manualLedTest();else buzzerToneOnce(2200,100);}
  time_t now=time(nullptr);struct tm t;
  if(now>VALID_TIME && localtime_r(&now,&t)){
    int day=ClockLogic::dateStamp(t.tm_year+1900,t.tm_yday);
    if(alarmEnabled && t.tm_hour==alarmHour && t.tm_min==alarmMinute && day!=lastAlarmDay){lastAlarmDay=day;runAlarm(alarmDurationSec);}
    serviceAlarm();
    time_t stamp=now/60;
    if(!alarmRunning && (stamp!=lastShownStamp || refreshRequested)){
      bool newMinute=stamp!=lastShownStamp;
      lastShownStamp=stamp;refreshRequested=false;
      if(newMinute){minuteLedTest();shortMinuteBeep();}
      drawClock(t);
    }
  } else if(refreshRequested){refreshRequested=false;showNoTime();}
  serviceAlarm();
  if(!webActive && !alarmRunning && !restartPending) sleepToNextMinute();
  delay(5);
}

