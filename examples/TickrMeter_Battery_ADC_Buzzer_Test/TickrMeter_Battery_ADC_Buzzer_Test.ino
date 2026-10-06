/*
  Unofficial TickrMeter Rev. 1.6 hardware research example.
  Independently written test code; no original manufacturer firmware/code included.
  Verify GPIO assignments before use on another hardware revision.
*/


#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <SPI.h>

#include <GxEPD2_3C.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

// ================= PINOUT =================
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

const int adcPins[] = {32, 33, 34, 35, 36, 39};
const int ADC_COUNT = sizeof(adcPins) / sizeof(adcPins[0]);

// ================= DISPLAY =================
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

// ================= OTA =================
const char* AP_SSID = "TickrMeter-CUSTOM";
const char* AP_PASS = "12345678";

WebServer server(80);

uint16_t adcRaw[ADC_COUNT];
uint32_t adcMv[ADC_COUNT];

unsigned long lastSerial = 0;
unsigned long lastScreen = 0;

const unsigned long SERIAL_INTERVAL = 1000;
const unsigned long SCREEN_INTERVAL = 15000;

// ================= LED =================
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

// ================= E-PAPER POWER =================
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

// ================= ADC =================
void readAllAdc()
{
  for (int i = 0; i < ADC_COUNT; i++)
  {
    uint32_t rawSum = 0;
    uint32_t mvSum = 0;

    for (int n = 0; n < 8; n++)
    {
      rawSum += analogRead(adcPins[i]);
      mvSum  += analogReadMilliVolts(adcPins[i]);
      delay(2);
    }

    adcRaw[i] = rawSum / 8;
    adcMv[i]  = mvSum / 8;
  }
}

void printAdcSerial()
{
  Serial.println();
  Serial.println("===== BATTERY ADC SEARCH =====");

  for (int i = 0; i < ADC_COUNT; i++)
  {
    Serial.print("GPIO");
    Serial.print(adcPins[i]);
    Serial.print(" RAW=");
    Serial.print(adcRaw[i]);
    Serial.print(" ADC=");
    Serial.print(adcMv[i]);
    Serial.println(" mV");
  }
}

// ================= BUZZER =================
void buzzerOff()
{
  noTone(AUDIO_PIN);
  digitalWrite(BUZZER_STBY, LOW);
}

void buzzerTest()
{
  Serial.println("BUZZER TEST");

  digitalWrite(BUZZER_STBY, HIGH);
  delay(100);

  tone(AUDIO_PIN, 900, 180);
  delay(250);

  tone(AUDIO_PIN, 1400, 180);
  delay(250);

  tone(AUDIO_PIN, 2000, 250);
  delay(350);

  noTone(AUDIO_PIN);
  delay(100);

  digitalWrite(BUZZER_STBY, LOW);
}

// ================= SCREEN =================
void drawScreen()
{
  readAllAdc();

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
    display.setFont(&FreeSansBold12pt7b);
    display.setCursor(7, 18);
    display.print("BATTERY ADC TEST");

    display.drawLine(5, 25, 291, 25, GxEPD_BLACK);

    display.setFont(&FreeSansBold9pt7b);

    display.setCursor(8, 43);
    display.print("GPIO");

    display.setCursor(58, 43);
    display.print("RAW");

    display.setCursor(118, 43);
    display.print("mV");

    display.setCursor(190, 43);
    display.print("GPIO");

    display.setCursor(240, 43);
    display.print("mV");

    for (int i = 0; i < 3; i++)
    {
      int y = 62 + i * 20;

      display.setFont(&FreeSans9pt7b);

      display.setCursor(8, y);
      display.print(adcPins[i]);

      display.setCursor(48, y);
      display.print(adcRaw[i]);

      display.setCursor(110, y);
      display.print(adcMv[i]);
    }

    for (int i = 3; i < 6; i++)
    {
      int y = 62 + (i - 3) * 20;

      display.setFont(&FreeSans9pt7b);

      display.setCursor(190, y);
      display.print(adcPins[i]);

      display.setCursor(236, y);
      display.print(adcMv[i]);
    }

    display.drawLine(5, 109, 291, 109, GxEPD_BLACK);

    display.setFont(&FreeSans9pt7b);
    display.setCursor(7, 126);
    display.print("BAT ADC = stable mid voltage");

  }
  while (display.nextPage());

  display.hibernate();
  epdPowerOff();

  ledSet(false, true, false);
  delay(300);
  ledsOff();
}

// ================= WEB / OTA =================
const char updatePage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TickrMeter Test</title>
</head>
<body style="font-family:Arial;text-align:center;margin-top:35px">
<h2>TickrMeter ADC / Buzzer Test</h2>
<p><a href="/beep">BUZZER TEST</a></p>
<p><a href="/screen">REFRESH E-PAPER</a></p>
<hr>
<h3>Firmware update</h3>
<form method="POST" action="/update" enctype="multipart/form-data">
<input type="file" name="update" accept=".bin"><br><br>
<input type="submit" value="UPLOAD .BIN">
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
      "<h2>TickrMeter ADC TEST</h2>"
      "<p><a href='/beep'>Buzzer test</a></p>"
      "<p><a href='/screen'>Refresh e-paper</a></p>"
      "<p><a href='/update'>Firmware update</a></p><hr>";

    for (int i = 0; i < ADC_COUNT; i++)
    {
      html += "GPIO";
      html += String(adcPins[i]);
      html += ": ";
      html += String(adcMv[i]);
      html += " mV<br>";
    }

    html += "</body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/beep", HTTP_GET, []()
  {
    server.send(200, "text/html",
      "<h2>Buzzer test running</h2><p><a href='/'>Back</a></p>");
    buzzerTest();
  });

  server.on("/screen", HTTP_GET, []()
  {
    server.send(200, "text/html",
      "<h2>E-paper refresh running</h2><p><a href='/'>Back</a></p>");
    drawScreen();
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

// ================= SETUP =================
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

  analogReadResolution(12);

  for (int i = 0; i < ADC_COUNT; i++)
  {
    analogSetPinAttenuation(adcPins[i], ADC_11db);
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  startWebServer();

  Serial.print("OTA IP: ");
  Serial.println(WiFi.softAPIP());

  // Startup RGB test
  ledSet(true, false, false);
  delay(150);
  ledSet(false, true, false);
  delay(150);
  ledSet(false, false, true);
  delay(150);
  ledsOff();

  // Startup buzzer test
  buzzerTest();

  readAllAdc();
  printAdcSerial();

  drawScreen();

  lastSerial = millis();
  lastScreen = millis();
}

// ================= LOOP =================
void loop()
{
  server.handleClient();

  if (millis() - lastSerial >= SERIAL_INTERVAL)
  {
    lastSerial = millis();
    readAllAdc();
    printAdcSerial();
  }

  if (millis() - lastScreen >= SCREEN_INTERVAL)
  {
    lastScreen = millis();
    drawScreen();
  }

  delay(2);
}
