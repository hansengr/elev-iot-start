/*
  ELEV-IOT-MAL  (MAL: kopier hele mappen før du endrer noe)

  ESP8266 med 0.96" OLED og MPU-6050 (akselerometer og gyro) mot api.dataskyen.no.
  Boksen måler 10 ganger i sekundet, viser verdiene på skjermen,
  sender dem til serveren én gang i sekundet, og utfører kommandoer fra nettsiden din.

  NY BOKS:
  STEP 1. Kopier mappen ELEV-IOT-MAL, og gi kopien og .ino-filen samme navn, f.eks. ELEV-IOT-07
  STEP 2. Registrer boksen på https://api.dataskyen.no/register.php (klassekoden får du av læreren)
  STEP 3. Kopier secrets.example.h til secrets.h, og fyll inn WiFi og nøkkelen du fikk
  STEP 4. Sett boxName og DP_-tallene under til det du fikk på register.php
  STEP 5. Koble MPU-6050: VCC -> 3V3, GND -> GND, SCL -> D1, SDA -> D2
  STEP 6. Last opp (Board: Generic ESP8266 Module), og følg med i Serial Monitor (115200)

  KALIBRERING: Legg boksen flatt og i ro, og send kommandoen "kalibrer" med verdi 1 fra nettsiden.
  Da viser den X = 0, Y = 0 og Z = 1. Dette lagres i boksen, så det holder å gjøre én gang.

  Biblioteker (Tools > Manage Libraries): ArduinoJson og U8g2.
  MPU-6050 trenger ikke noe bibliotek, koden ligger i mpu6050.h.
  Med DHT-sensor (USE_DHT 1): "DHT sensor library" og "Adafruit Unified Sensor".
*/

#include "secrets.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include "mpu6050.h"

/*====== OPPSETT AV BOKSEN ===============================================================================================*/
const char* boxName = "ELEV-IOT-XX";             // Navn på skjermen og på nettverket, f.eks. ELEV-IOT-07

const int DP_AKS_X  = 0;                         // datapointID-ene du fikk på register.php
const int DP_AKS_Y  = 0;                         // 0 = sendes ikke
const int DP_AKS_Z  = 0;
const int DP_GYRO_X = 0;
const int DP_GYRO_Y = 0;
const int DP_GYRO_Z = 0;

const int MPU_SDA = 4;                           // GPIO4 = D2
const int MPU_SCL = 5;                           // GPIO5 = D1

#define USE_DHT 0                                // 1 = DHT-sensor for temperatur og fuktighet er koblet til
#define DHTPIN 2                                 // GPIO2 = D4
#define DHTTYPE DHT11                            // DHT11 (blå) eller DHT22 (hvit)
const int DP_TEMP = 0;                           // brukes bare når USE_DHT er 1
const int DP_FUKT = 0;

const unsigned long SENSOR_INTERVAL_MS = 100;    // mS mellom hver lesing av MPU-6050
const unsigned long SCREEN_INTERVAL_MS = 250;    // mS mellom hver oppdatering av skjermen
const unsigned long SEND_INTERVAL_MS   = 1000;   // mS mellom hver sending. IKKE lavere enn 1000,
                                                 // hele klassen deler samme server

/*====== SERVER ==========================================================================================================*/
const char* HOST       = "api.dataskyen.no";
const char* URL_INSERT = "https://api.dataskyen.no/insert.php";
const char* URL_DONE   = "https://api.dataskyen.no/command_done.php";

// Skjermen sitter fast på kortet: klokke (SCL) på GPIO12, data (SDA) på GPIO14
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, 12, 14, U8X8_PIN_NONE);

#if USE_DHT
#include "DHT.h"
DHT dht(DHTPIN, DHTTYPE);
const int LED_PIN = -1;                          // lysdioden på kortet deler pinne (D4) med DHT-en
#else
const int LED_PIN = 2;                           // den blå lysdioden på kortet, brukes av kommandoen "led"
#endif

BearSSL::WiFiClientSecure client;
HTTPClient http;

unsigned long lastSensor = 0, lastScreen = 0, lastSend = 0, lastDht = 0;
float temp = NAN, fukt = NAN;                    // fra DHT-en. NAN = ingen måling
char statusLine[19] = "Starter";                 // nederste linje på skjermen (maks 18 tegn)

/*====== Skjerm ==========================================================================================================*/
// Skriver en hendelse til nederste linje på skjermen, som printf. F.eks. setStatus("Sendt %d", antall)
void setStatus(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vsnprintf(statusLine, sizeof(statusLine), format, args);
  va_end(args);
}

// Øverst (gult felt): navnet på boksen. Under: målingene, og nederst siste hendelse.
// Uten WiFi vises MAC-adressen i stedet, den trenger IT for å slippe boksen inn på skolenettet.
void updateScreen() {
  char line[24];
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tf);
  u8g2.drawStr(0, 10, boxName);

  if (WiFi.status() != WL_CONNECTED) {
    u8g2.drawStr(0, 28, WiFi.macAddress().c_str());
    u8g2.drawStr(0, 43, "WiFi: kobler til");
  } else if (!mpu.ok) {
    u8g2.drawStr(0, 28, "Finner ikke MPU");
    u8g2.drawStr(0, 43, "SCL=D1  SDA=D2");
  } else {
    snprintf(line, sizeof(line), "X%+5.2f Y%+5.2f", mpu.ax, mpu.ay);
    u8g2.drawStr(0, 28, line);
    if (USE_DHT && !isnan(temp)) {
      snprintf(line, sizeof(line), "Z%+5.2f %2.0f\xb0" "C %2.0f%%", mpu.az, temp, fukt);
    } else {
      snprintf(line, sizeof(line), "Z%+5.2f g", mpu.az);
    }
    u8g2.drawStr(0, 43, line);
  }
  u8g2.drawStr(0, 58, statusLine);
  u8g2.sendBuffer();
}

/*====== Kommandoer ======================================================================================================*/
// LEGG TIL EGNE KOMMANDOER HER. Returner true når kommandoen er utført, false hvis den ikke kunne utføres.
bool runCommand(const String& command, const String& value) {
  if (command == "led" && LED_PIN >= 0) {
    digitalWrite(LED_PIN, value == "1" ? LOW : HIGH);    // lysdioden lyser på LOW
    return true;
  }
  if (command == "tekst") {
    setStatus("%s", value.c_str());                      // viser teksten nederst på skjermen
    return true;
  }
  if (command == "kalibrer") {                           // boksen må ligge flatt og i ro
    if (value == "0") {
      mpuClearOffset();                                  // 0 = fjern kalibreringen
      return true;
    }
    setStatus("Kalibrerer, i ro");
    updateScreen();
    bool ok = mpuCalibrate();                            // tar ett sekund, lagres i minnet på ESP-en
    Serial.printf("Kalibrering %s. Avvik: X %+.3f  Y %+.3f  Z %+.3f g\n", ok ? "OK" : "FEILET",
                  mpuOffset.ax, mpuOffset.ay, mpuOffset.az);
    return ok;
  }
  Serial.println("Ukjent kommando");
  return false;
}

// Sender JSON til serveren. Svaret havner i response. Returnerer HTTP-koden (200 = OK).
int postJson(const char* url, JsonDocument& doc, String& response) {
  String body;
  serializeJson(doc, body);

  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-MAC", WiFi.macAddress());               // bare til informasjon for læreren
  http.addHeader("X-Device-LocalIP", WiFi.localIP().toString());
  int code = http.POST(body);
  response = code > 0 ? http.getString() : http.errorToString(code);
  http.end();
  return code;
}

// Forteller serveren om kommandoen ble utført. Da viser commands.php "executed" eller "failed".
void confirmCommand(long commandID, bool ok) {
  JsonDocument doc;
  doc["key"] = deviceKey;
  doc["commandID"] = commandID;
  doc["ok"] = ok;
  String response;
  int code = postJson(URL_DONE, doc, response);
  Serial.printf("command_done: HTTP %d %s\n", code, response.c_str());
}

/*====== Målinger ========================================================================================================*/
// Legger én måling i pakken. Hopper over datapunkter som ikke er satt opp (0) og sensorfeil (NAN).
void addValue(JsonArray data, int datapointID, float value) {
  if (datapointID <= 0 || isnan(value)) {
    return;
  }
  JsonObject p = data.add<JsonObject>();
  p["datapointID"] = datapointID;
  p["value"] = round(value * 1000) / 1000.0;             // tre desimaler holder
}

// Sender de siste målingene i én pakke. Svaret fra serveren inneholder en kommando hvis en venter.
void sendData() {
  JsonDocument doc;
  doc["key"] = deviceKey;
  JsonArray data = doc["data"].to<JsonArray>();
  addValue(data, DP_AKS_X, mpu.ax);
  addValue(data, DP_AKS_Y, mpu.ay);
  addValue(data, DP_AKS_Z, mpu.az);
  addValue(data, DP_GYRO_X, mpu.gx);
  addValue(data, DP_GYRO_Y, mpu.gy);
  addValue(data, DP_GYRO_Z, mpu.gz);
  addValue(data, DP_TEMP, temp);
  addValue(data, DP_FUKT, fukt);

  if (data.size() == 0) {
    setStatus("Ingen datapunkter");                      // DP_-tallene er 0, eller sensoren mangler
    return;
  }

  String response;
  int code = postJson(URL_INSERT, doc, response);

  JsonDocument answer;
  deserializeJson(answer, response);

  if (code != 200) {
    Serial.printf("insert: HTTP %d %s\n", code, response.c_str());
    setStatus(code > 0 ? "Feil HTTP %d" : "Ingen kontakt", code);
    return;
  }
  if (strncmp(statusLine, "Kmd", 3) != 0) {              // lar siste kommando stå på skjermen
    setStatus("Sender OK");
  }

  if (!answer["command"].isNull()) {
    long commandID = answer["command"]["commandID"];
    String command = answer["command"]["command"].as<String>();
    String value = answer["command"]["value"].as<String>();
    Serial.printf("Kommando %ld: %s = %s\n", commandID, command.c_str(), value.c_str());

    bool ok = runCommand(command, value);
    if (command != "tekst") {
      setStatus(ok ? "Kmd OK: %s" : "Kmd feil: %s", command.c_str());
    }
    confirmCommand(commandID, ok);
  }
}

/*====== Oppstart og hovedløkke ==========================================================================================*/
void setup() {
  Serial.begin(115200);
  if (LED_PIN >= 0) {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);                         // av
  }
  u8g2.begin();
  updateScreen();

  if (!mpuBegin(MPU_SDA, MPU_SCL)) {
    Serial.println("Finner ikke MPU-6050. Sjekk ledningene: VCC-3V3, GND-GND, SCL-D1, SDA-D2");
  }
#if USE_DHT
  dht.begin();
#endif

  Serial.printf("\n%s  MAC %s\nKobler til WiFi", boxName, WiFi.macAddress().c_str());
  WiFi.mode(WIFI_STA);
  WiFi.hostname(boxName);                                // slik vises boksen hos IT
  WiFi.begin(ssid, password);                            // kobler også til igjen selv hvis nettet faller ut
  while (WiFi.status() != WL_CONNECTED && millis() < 30000) {
    delay(500);
    Serial.print(".");
    updateScreen();
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? "\nTilkoblet, IP " + WiFi.localIP().toString() : "\nIngen WiFi ennå");

  // https uten sjekk av sertifikatet: trafikken er kryptert, men boksen sjekker ikke hvem den snakker med.
  // Greit i et øvingssystem. I et ordentlig system sjekkes sertifikatet.
  client.setInsecure();
  if (WiFi.status() == WL_CONNECTED && client.probeMaxFragmentLength(HOST, 443, 1024)) {
    client.setBufferSizes(1024, 1024);                   // mindre minnebruk når serveren støtter det
  }
  http.setReuse(true);                                   // gjenbruker forbindelsen, ellers tar hver sending over ett sekund
  http.setTimeout(5000);
}

void loop() {
  unsigned long now = millis();

  if (now - lastSensor >= SENSOR_INTERVAL_MS) {
    lastSensor = now;
    mpuRead();                                           // oppdaterer mpu.ax, mpu.ay, mpu.az, mpu.gx, mpu.gy, mpu.gz
  }

#if USE_DHT
  if (now - lastDht >= 5000) {                           // DHT-en er treg, hvert 5. sekund holder
    lastDht = now;
    temp = dht.readTemperature();                        // NAN ved lesefeil
    fukt = dht.readHumidity();
  }
#endif

  if (now - lastScreen >= SCREEN_INTERVAL_MS) {
    lastScreen = now;
    updateScreen();
  }

  if (WiFi.status() == WL_CONNECTED && now - lastSend >= SEND_INTERVAL_MS) {
    lastSend = now;
    sendData();
  }
}
