/*
  Unofficial TickrMeter Rev. 1.6 hardware research example.
  Independently written test code; no original manufacturer firmware/code included.
  Verify GPIO assignments before use on another hardware revision.
*/


/*
  TickrMeter Rev 1.6 - ADC2 + GPIO34 battery test

  IMPORTANT:
  ESP32 classic ADC2 cannot be read reliably while Wi-Fi is active.
  Therefore ADC2 is measured FIRST at boot, before Wi-Fi starts.
  Then values are stored in RAM and shown on e-paper / web page.

  ADC2 candidates:
    GPIO0, GPIO2, GPIO4, GPIO12

  ADC1 control:
    GPIO34

  GPIO34 temporary calibration requested:
    BAT = 3.549 V when RAW = 36
    Estimated battery voltage = RAW * (3.549 / 36.0)
    This is only a TEST calibration, not final.

  Confirmed TickrMeter pinout:
    EPD_POWER = GPIO19, LOW = ON
    BUSY      = GPIO18
    RST       = GPIO26
    DC        = GPIO27
    CS        = GPIO15
    SCK       = GPIO13
    MOSI      = GPIO14

    LED RED   = GPIO23, LOW = ON
    LED GREEN = GPIO22, LOW = ON
    LED BLUE  = GPIO21, LOW = ON
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <SPI.h>

#include <GxEPD2_3C.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

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

const int adc2Pins[] = {0, 2, 4, 12};
const int ADC2_COUNT = sizeof(adc2Pins) / sizeof(adc2Pins[0]);

#define BAT_TEST_PIN 34

// ============================================================
// GPIO34 TEMPORARY CALIBRATION
// ============================================================

const float BAT_CAL_VOLTAGE = 3.549f;
const float BAT_CAL_RAW     = 36.0f;
const float BAT_RAW_FACTOR  = BAT_CAL_VOLTAGE / BAT_CAL_RAW;

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

// ============================================================
// WIFI / OTA
// ============================================================

const char* AP_SSID = "TickrMeter-CUSTOM";
const char* AP_PASS = "12345678";

WebServer server(80);

// ============================================================
// ADC STORED DATA
// ============================================================

uint16_t adc2Raw[ADC2_COUNT] = {0};
uint32_t adc2Mv[ADC2_COUNT]  = {0};

uint16_t gpio34RawBoot = 0;
uint32_t gpio34MvBoot  = 0;

uint16_t gpio34RawLive = 0;
uint32_t gpio34MvLive  = 0;

float gpio34CalcVoltage = 0.0f;

// ============================================================
// LED
// ============================================================

void ledsOff()
{
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, HIGH);
}

void ledSet(bool r, bool g, bool b)
{
  digitalWrite(LED_RED,   r ? LOW : HIGH);
  digitalWrite(LED_GREEN, g ? LOW : HIGH);
  digitalWrite(LED_BLUE,  b ? LOW : HIGH);
}

// ============================================================
// ADC HELPERS
// ============================================================

uint16_t readRawAverage(int pin, int samples = 64)
{
  uint32_t sum = 0;

  for (int i = 0; i < samples; i++)
  {
    sum += analogRead(pin);
    delay(2);
  }

  return (uint16_t)(sum / samples);
}

uint32_t readMvAverage(int pin, int samples = 32)
{
  uint32_t sum = 0;

  for (int i = 0; i < samples; i++)
  {
    sum += analogReadMilliVolts(pin);
    delay(2);
  }

  return sum / samples;
}

// ============================================================
// ADC2 BOOT TEST
// MUST RUN BEFORE WIFI
// ============================================================

void readAdc2BeforeWifi()
{
  Serial.println();
  Serial.println("===== ADC2 TEST BEFORE WIFI =====");

  analogReadResolution(12);

  for (int i = 0; i < ADC2_COUNT; i++)
  {
    analogSetPinAttenuation(adc2Pins[i], ADC_11db);

    adc2Raw[i] = readRawAverage(adc2Pins[i], 64);
    adc2Mv[i]  = readMvAverage(adc2Pins[i], 32);

    Serial.print("GPIO");
    Serial.print(adc2Pins[i]);

    Serial.print(" RAW=");
    Serial.print(adc2Raw[i]);

    Serial.print(" mV=");
    Serial.println(adc2Mv[i]);
  }

  analogSetPinAttenuation(BAT_TEST_PIN, ADC_11db);

  gpio34RawBoot = readRawAverage(BAT_TEST_PIN, 64);
  gpio34MvBoot  = readMvAverage(BAT_TEST_PIN, 32);

  gpio34CalcVoltage =
    ((float)gpio34RawBoot) * BAT_RAW_FACTOR;

  Serial.println();
  Serial.println("GPIO34 BOOT:");

  Serial.print("RAW=");
  Serial.println(gpio34RawBoot);

  Serial.print("ADC mV=");
  Serial.println(gpio34MvBoot);

  Serial.print("CALC BAT=");
  Serial.print(gpio34CalcVoltage, 3);
  Serial.println(" V");
}

// ============================================================
// GPIO34 LIVE TEST
// ADC1 WORKS WITH WIFI
// ============================================================

void readGpio34Live()
{
  gpio34RawLive = readRawAverage(BAT_TEST_PIN, 32);
  gpio34MvLive  = readMvAverage(BAT_TEST_PIN, 16);

  gpio34CalcVoltage =
    ((float)gpio34RawLive) * BAT_RAW_FACTOR;
}

// ============================================================
// E-PAPER
// ============================================================

void epdPowerOn()
{
  digitalWrite(EPD_POWER, LOW);
  delay(700);
}

void epdPowerOff()
{
  delay(100);
  digitalWrite(EPD_POWER, HIGH);
}

void drawScreen()
{
  readGpio34Live();

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

    display.setTextColor(GxEPD_BLACK);

    // HEADER
    display.setFont(&FreeSansBold12pt7b);
    display.setCursor(7, 18);
    display.print("ADC2 + BAT TEST");

    display.drawLine(5, 25, 291, 25, GxEPD_BLACK);

    // COLUMN HEADERS
    display.setFont(&FreeSansBold9pt7b);

    display.setCursor(7, 43);
    display.print("GPIO");

    display.setCursor(53, 43);
    display.print("RAW");

    display.setCursor(111, 43);
    display.print("mV");

    display.setCursor(174, 43);
    display.print("GPIO34");

    display.setCursor(238, 43);
    display.print("RAW");

    // ADC2 rows
    for (int i = 0; i < ADC2_COUNT; i++)
    {
      int y = 61 + i * 16;

      display.setFont(&FreeSans9pt7b);

      display.setCursor(8, y);
      display.print(adc2Pins[i]);

      display.setCursor(48, y);
      display.print(adc2Raw[i]);

      display.setCursor(104, y);
      display.print(adc2Mv[i]);
    }

    // GPIO34 right side
    display.setFont(&FreeSans9pt7b);

    display.setCursor(176, 61);
    display.print("BOOT");

    display.setCursor(238, 61);
    display.print(gpio34RawBoot);

    display.setCursor(176, 78);
    display.print("LIVE");

    display.setCursor(238, 78);
    display.print(gpio34RawLive);

    display.setCursor(176, 95);
    display.print("mV");

    display.setCursor(238, 95);
    display.print(gpio34MvLive);

    // Bottom line
    display.drawLine(5, 111, 291, 111, GxEPD_BLACK);

    display.setFont(&FreeSansBold9pt7b);
    display.setCursor(7, 127);
    display.print("BAT34 CALC ");

    display.print(gpio34CalcVoltage, 3);
    display.print("V");

  }
  while (display.nextPage());

  display.hibernate();
  epdPowerOff();

  ledSet(false, true, false);
  delay(300);
  ledsOff();
}

// ============================================================
// WEB / OTA
// ============================================================

const char updatePage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TickrMeter ADC Test</title>
</head>
<body style="font-family:Arial;text-align:center;margin-top:30px">

<h2>ADC2 + GPIO34 Test</h2>

<p>ADC2 was measured BEFORE Wi-Fi started.</p>
<p>Reboot device to take a new ADC2 measurement.</p>

<p><a href="/screen">Refresh e-paper</a></p>

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
    readGpio34Live();

    String html =
      "<html><body style='font-family:Arial'>"
      "<h2>ADC2 + GPIO34 TEST</h2>"
      "<b>ADC2 BOOT values:</b><br>";

    for (int i = 0; i < ADC2_COUNT; i++)
    {
      html += "GPIO";
      html += String(adc2Pins[i]);
      html += " RAW=";
      html += String(adc2Raw[i]);
      html += " mV=";
      html += String(adc2Mv[i]);
      html += "<br>";
    }

    html += "<hr>";
    html += "GPIO34 BOOT RAW=";
    html += String(gpio34RawBoot);

    html += "<br>GPIO34 LIVE RAW=";
    html += String(gpio34RawLive);

    html += "<br>GPIO34 LIVE mV=";
    html += String(gpio34MvLive);

    html += "<br><b>BAT CALC=";
    html += String(gpio34CalcVoltage, 3);
    html += " V</b>";

    html += "<hr>";
    html += "<p>Calibration: RAW 36 = 3.549 V</p>";
    html += "<p><a href='/screen'>Refresh e-paper</a></p>";
    html += "<p><a href='/update'>Firmware update</a></p>";
    html += "</body></html>";

    server.send(200, "text/html", html);
  });

  server.on("/screen", HTTP_GET, []()
  {
    server.send(
      200,
      "text/html",
      "<h2>Screen refresh started.</h2>"
      "<p><a href='/'>Back</a></p>"
    );

    drawScreen();
  });

  server.on("/update", HTTP_GET, []()
  {
    server.send_P(
      200,
      "text/html",
      updatePage
    );
  });

  server.on(
    "/update",
    HTTP_POST,

    []()
    {
      if (Update.hasError())
      {
        server.send(
          500,
          "text/plain",
          "UPDATE FAILED"
        );
      }
      else
      {
        server.send(
          200,
          "text/plain",
          "UPDATE OK - REBOOT"
        );

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

  server.begin();
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
  Serial.begin(115200);
  delay(700);

  // LEDs
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  ledsOff();

  // E-paper OFF
  pinMode(EPD_POWER, OUTPUT);
  digitalWrite(EPD_POWER, HIGH);

  // --------------------------------------------------------
  // CRITICAL:
  // READ ADC2 BEFORE STARTING WIFI
  // --------------------------------------------------------
  readAdc2BeforeWifi();

  // --------------------------------------------------------
  // NOW WIFI CAN START
  // --------------------------------------------------------
  WiFi.mode(WIFI_AP);

  WiFi.softAP(
    AP_SSID,
    AP_PASS
  );

  startWebServer();

  Serial.print("OTA IP: ");
  Serial.println(WiFi.softAPIP());

  drawScreen();
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
  server.handleClient();

  static unsigned long lastPrint = 0;
  static unsigned long lastScreen = 0;

  // GPIO34 can be tested continuously because it is ADC1.
  if (millis() - lastPrint >= 1000)
  {
    lastPrint = millis();

    readGpio34Live();

    Serial.print("GPIO34 LIVE RAW=");
    Serial.print(gpio34RawLive);

    Serial.print(" mV=");
    Serial.print(gpio34MvLive);

    Serial.print(" BAT_CALC=");
    Serial.print(gpio34CalcVoltage, 3);

    Serial.println("V");
  }

  // E-paper every 20 s
  if (millis() - lastScreen >= 20000)
  {
    lastScreen = millis();
    drawScreen();
  }

  delay(2);
}
