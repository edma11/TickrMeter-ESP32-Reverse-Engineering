/*
  Unofficial TickrMeter Rev. 1.6 hardware research example.
  Independently written test code; no original manufacturer firmware/code included.
  Verify GPIO assignments before use on another hardware revision.
*/


/*
  TickrMeter Rev 1.6 - BIG NTP CLOCK
  ESP32-WROOM-32E + 2.9" e-paper

  Every new minute:
    - very short RGB LED test: RED -> GREEN -> BLUE
    - very short beep
    - e-paper clock refresh

  Confirmed TickrMeter pinout:
    EPD_POWER / EN = GPIO19   ACTIVE LOW: LOW=ON, HIGH=OFF
    BUSY           = GPIO18   -> FPC 9
    RST / RES#     = GPIO26   -> FPC 10
    DC / D-C#      = GPIO27   -> FPC 11
    CS#            = GPIO15   -> FPC 12
    SCK / SCL      = GPIO13   -> FPC 13
    MOSI / SDA     = GPIO14   -> FPC 14

    LED RED        = GPIO23   ACTIVE LOW
    LED GREEN      = GPIO22   ACTIVE LOW
    LED BLUE       = GPIO21   ACTIVE LOW

    BUZZER STBY    = GPIO17   HIGH = ON
    AUDIO SIGNAL   = GPIO25

  OTA:
    SSID: TickrMeter-CUSTOM
    PASS: 12345678
    URL : http://192.168.4.1/update
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <SPI.h>
#include <time.h>

#include <GxEPD2_3C.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

// ============================================================
// USER SETTINGS
// ============================================================

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

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

int lastShownMinute = -1;
bool timeIsValid = false;
unsigned long lastReconnectTry = 0;

// ============================================================
// RGB LED - ACTIVE LOW
// ============================================================

void ledsOff()
{
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, HIGH);
}

void ledSet(bool redOn, bool greenOn, bool blueOn)
{
  digitalWrite(LED_RED,   redOn   ? LOW : HIGH);
  digitalWrite(LED_GREEN, greenOn ? LOW : HIGH);
  digitalWrite(LED_BLUE,  blueOn  ? LOW : HIGH);
}

// Very short RGB test each minute
void minuteLedTest()
{
  ledSet(true, false, false);
  delay(45);

  ledSet(false, true, false);
  delay(45);

  ledSet(false, false, true);
  delay(45);

  ledsOff();
}

// ============================================================
// BUZZER
// ============================================================

void shortMinuteBeep()
{
  digitalWrite(BUZZER_STBY, HIGH);
  delay(8);

  tone(AUDIO_PIN, 2200);
  delay(35);
  noTone(AUDIO_PIN);

  digitalWrite(AUDIO_PIN, LOW);
  delay(5);

  digitalWrite(BUZZER_STBY, LOW);
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
//
// Segment map:
//   -- A --
//  |       |
//  F       B
//  |-- G --|
//  E       C
//  |       |
//   -- D --
//
// Each digit: 42 x 68 px
//

const uint8_t DIGIT_SEGMENTS[10] =
{
  0b00111111, // 0: A B C D E F
  0b00000110, // 1: B C
  0b01011011, // 2: A B D E G
  0b01001111, // 3: A B C D G
  0b01100110, // 4: B C F G
  0b01101101, // 5: A C D F G
  0b01111101, // 6: A C D E F G
  0b00000111, // 7: A B C
  0b01111111, // 8
  0b01101111  // 9
};

void hSeg(int x, int y, int w, int t)
{
  display.fillRect(x + t, y, w - 2 * t, t, GxEPD_BLACK);
  display.fillTriangle(x, y + t / 2, x + t, y, x + t, y + t - 1, GxEPD_BLACK);
  display.fillTriangle(x + w - 1, y + t / 2,
                       x + w - t - 1, y,
                       x + w - t - 1, y + t - 1,
                       GxEPD_BLACK);
}

void vSeg(int x, int y, int h, int t)
{
  display.fillRect(x, y + t, t, h - 2 * t, GxEPD_BLACK);
  display.fillTriangle(x + t / 2, y, x, y + t, x + t - 1, y + t, GxEPD_BLACK);
  display.fillTriangle(x + t / 2, y + h - 1,
                       x, y + h - t - 1,
                       x + t - 1, y + h - t - 1,
                       GxEPD_BLACK);
}

void drawDigit(int x, int y, int digit)
{
  const int W = 42;
  const int H = 68;
  const int T = 7;
  const int HALF = H / 2;

  uint8_t s = DIGIT_SEGMENTS[digit];

  if (s & (1 << 0)) hSeg(x,         y,              W, T);      // A
  if (s & (1 << 1)) vSeg(x + W - T, y,              HALF, T);   // B
  if (s & (1 << 2)) vSeg(x + W - T, y + HALF,       HALF, T);   // C
  if (s & (1 << 3)) hSeg(x,         y + H - T,       W, T);      // D
  if (s & (1 << 4)) vSeg(x,         y + HALF,        HALF, T);   // E
  if (s & (1 << 5)) vSeg(x,         y,               HALF, T);   // F
  if (s & (1 << 6)) hSeg(x,         y + HALF - T / 2,W, T);      // G
}

void drawColon(int x, int y)
{
  display.fillCircle(x, y + 23, 4, GxEPD_BLACK);
  display.fillCircle(x, y + 47, 4, GxEPD_BLACK);
}

void drawBigTime(int hour, int minute)
{
  // Total width ~ 222 px, centered
  const int y = 30;
  const int x0 = 37;

  drawDigit(x0,       y, hour / 10);
  drawDigit(x0 + 47,  y, hour % 10);

  drawColon(x0 + 96, y);

  drawDigit(x0 + 108, y, minute / 10);
  drawDigit(x0 + 155, y, minute % 10);
}

// ============================================================
// SMALL ICONS
// ============================================================

// GxEPD2/Adafruit_GFX doesn't always expose drawArc.
// Use a tiny simple Wi-Fi symbol instead.
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

  // Temporary neutral fill until battery calibration is final
  display.fillRect(x + 3, y + 3, 9, 6, GxEPD_BLACK);
}

// ============================================================
// DATE / WEEKDAY
// ============================================================

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
  ledSet(false, false, true); // blue while refreshing

  epdPowerOn();

  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);

  display.init(115200);
  display.setRotation(3);
  display.setFullWindow();

  display.firstPage();

  do
  {
    display.fillScreen(GxEPD_WHITE);

    // Header
    display.setFont(&FreeSansBold12pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(7, 18);
    display.print("TickrMeter");

    drawWifiSimple(250, 3, WiFi.status() == WL_CONNECTED);

    display.drawLine(5, 24, 291, 24, GxEPD_BLACK);

    // Big clock
    drawBigTime(t.tm_hour, t.tm_min);

    // Footer separator
    display.drawLine(5, 102, 291, 102, GxEPD_BLACK);

    // Date
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

    // Weekday centered
    display.setFont(&FreeSansBold9pt7b);
    display.setCursor(133, 123);
    display.print(weekdayName(t.tm_wday));

    // Battery placeholder icon
    drawBatteryOutline(257, 110);

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

  WiFi.begin(WIFI_SSID, WIFI_PASS);

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

bool syncTime()
{
  configTzTime(
    TZ_LT,
    "pool.ntp.org",
    "time.google.com",
    "time.cloudflare.com"
  );

  unsigned long start = millis();

  while (millis() - start < 10000)
  {
    server.handleClient();

    time_t now = time(nullptr);

    if (now > 1700000000)
    {
      timeIsValid = true;
      return true;
    }

    delay(100);
  }

  return false;
}

// ============================================================
// OTA WEB
// ============================================================

const char updatePage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TickrMeter Clock</title>
</head>
<body style="font-family:Arial;text-align:center;margin-top:30px">

<h2>TickrMeter Clock</h2>

<p><a href="/beep">Test beep</a></p>
<p><a href="/led">Test RGB LED</a></p>
<p><a href="/refresh">Refresh clock</a></p>

<hr>

<h3>Firmware update</h3>

<form method="POST"
      action="/update"
      enctype="multipart/form-data">

<input type="file"
       name="update"
       accept=".bin">

<br><br>

<input type="submit"
       value="UPLOAD .BIN">

</form>

</body>
</html>
)rawliteral";

void startWebServer()
{
  server.on("/", HTTP_GET, []()
  {
    String html =
      "<html><body style='font-family:Arial'>"
      "<h2>TickrMeter Clock</h2>"
      "<p>Wi-Fi: " +
      String(
        WiFi.status() == WL_CONNECTED
        ? "CONNECTED"
        : "OFFLINE"
      ) +
      "</p>"
      "<p><a href='/beep'>Test beep</a></p>"
      "<p><a href='/led'>Test RGB LED</a></p>"
      "<p><a href='/refresh'>Refresh clock</a></p>"
      "<p><a href='/update'>Firmware update</a></p>"
      "</body></html>";

    server.send(200, "text/html", html);
  });

  server.on("/beep", HTTP_GET, []()
  {
    shortMinuteBeep();

    server.send(
      200,
      "text/html",
      "<h2>Beep OK</h2><p><a href='/'>Back</a></p>"
    );
  });

  server.on("/led", HTTP_GET, []()
  {
    minuteLedTest();

    server.send(
      200,
      "text/html",
      "<h2>LED test OK</h2><p><a href='/'>Back</a></p>"
    );
  });

  server.on("/refresh", HTTP_GET, []()
  {
    time_t now = time(nullptr);
    struct tm t;

    if (localtime_r(&now, &t))
    {
      drawClock(t);
    }

    server.send(
      200,
      "text/html",
      "<h2>Clock refreshed</h2><p><a href='/'>Back</a></p>"
    );
  });

  server.on("/update", HTTP_GET, []()
  {
    server.send_P(200, "text/html", updatePage);
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
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
          Update.printError(Serial);
      }
      else if (upload.status == UPLOAD_FILE_END)
      {
        if (!Update.end(true))
          Update.printError(Serial);
      }
    }
  );

  server.begin();
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
  Serial.begin(115200);
  delay(500);

  // RGB LED
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  ledsOff();

  // Buzzer
  pinMode(BUZZER_STBY, OUTPUT);
  digitalWrite(BUZZER_STBY, LOW);

  pinMode(AUDIO_PIN, OUTPUT);
  digitalWrite(AUDIO_PIN, LOW);

  // E-paper
  pinMode(EPD_POWER, OUTPUT);
  digitalWrite(EPD_POWER, HIGH);

  // OTA AP + home Wi-Fi
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(OTA_AP_SSID, OTA_AP_PASS);

  startWebServer();

  connectHomeWifi();
  syncTime();

  // Startup LED test + very short beep
  minuteLedTest();
  shortMinuteBeep();

  // First clock draw
  time_t now = time(nullptr);
  struct tm t;

  if (localtime_r(&now, &t))
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
  server.handleClient();

  // Reconnect Wi-Fi if home network drops
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
      // One action at each new minute
      if (t.tm_min != lastShownMinute)
      {
        lastShownMinute = t.tm_min;

        minuteLedTest();
        shortMinuteBeep();
        drawClock(t);
      }
    }
  }

  delay(50);
}
