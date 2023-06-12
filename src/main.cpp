
/* ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <CARguru-Buch@web.de> wrote this file. As long as you retain this
 * notice you can do whatever you want with this stuff. If we meet some day,
 * and you think this stuff is worth it, you can buy me a beer in return
 * Gustav Wostrack
 * ----------------------------------------------------------------------------
 */

#include "Arduino.h"
#include "Sweeper.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include "CARguruDefs.h"
#include "EEPROM.h"
#include "esp32-hal-ledc.h"
#include "esp_system.h"
#include <esp_now.h>
#include <esp_wifi.h>
#include <Ticker.h>
#include <ArduinoOTA.h>
#include "sound.h"
#define test

Car_Audio_Wav_Class Martinshorn_Wave; // Martinshorn theme
Car_Audio_Wav_Class Hupe_Wave;        // Martinshorn theme
                                      // Use GPIO 25, one of the 2 DAC pins and timer 0
/*
Variablen der LEDs
*/

LED LEDs[num_led_sweepers];

enum functions
{
  Abblendlicht,     // "F00" Abblendlicht
  Blinker_links,    // "F01" Blinker links
  Blinker_rechts,   // "F02" Blinker rechts
  Warnblinker,      // "F03" Warnblinker
  Fernlicht,        // "F04" Fernlicht
  Lichthupe_TASTER, // "F05" Lichthupe_TASTER
  Hupe_TASTER,      // "F06" Hupe_TASTER
  Martinshorn,      // "F07" Martinshorn
  Rundumleuchten,   // "F08" Rundumleuchten
  battery_func = Lok_Battery
};

// Forward-Declaration
void SetSpeed(uint32_t speed);
void SetFunction(functions func, uint8_t val);
void sendConfig();
void generateHash(uint8_t offset);
void sendPING();
void sendIP();
void execute_funcs(uint8_t func);

#include <OTA_include.h>

bool headlightsOn = false;
bool brakelightsOn = false;
const unsigned long updateInterval = 1000; // interval between updates
unsigned long lastUpdate;                  // last update of LEDs

// EEPROM-Adressen
// EEPROM-Speicherplätze der Local-IDs
// const uint16_t adr_setup_done = 0x00;
const uint16_t adr_decoderadr = lastAdr0 + 0x01;
const uint16_t lastAdr = adr_decoderadr + 0x01;
const uint16_t EEPROM_SIZE = lastAdr;

// Timer
Ticker brkTimer;
const float brktimerTime = 10.0;

Ticker blitzerTimer;
const float blitzertimerTime = 200.0;

// Lichthupe
Ticker lichthupeTimer;
const float lichthupetimerTime = 9.0;
const uint16_t LichthupeTime = 100;
uint16_t LichtHupeTimer;

uint8_t motorDirection = LOW;

uint16_t old_speed = 0;
uint16_t act_speed = 0;
uint16_t max_speed = 255;

// Blinker
Ticker blinkerTimer;
const float blinkertimerTime = 500.0;
bool BlinkerIsOn;
bool BlinkerIsHigh;
bool leftBlinkerIsOn;
bool rightBlinkerIsOn;

// Bremse
const uint16_t brakeTime = 100;
uint16_t brakeTimer;

// config-Daten
// Parameter-Kanäle
enum Kanals
{
  Kanal00,
  Kanal01,
  endofKanals
};

Kanals CONFIGURATION_Status_Index = Kanal00;

uint8_t decoderadr;
uint8_t uid_device[uid_num];

// Zeigen an, ob eine entsprechende Anforderung eingegangen ist
bool CONFIG_Status_Request = false;
bool SYS_CMD_Request = false;

#define VERS_HIGH 0x00 // Versionsnummer vor dem Punkt
#define VERS_LOW 0x01  // Versionsnummer nach dem Punkt

// Protokollkonstanten
#define PROT_MM MM_ACC
#define PROT_DCC DCC_TRACK

IPAddress IP;

#include "espnow.h"

void LED_ON()
{
//*  tinypico.DotStar_Show();
}

void LED_OFF()
{
//*  tinypico.DotStar_Clear();
}

//*********************************************************************************************************
// das Aufblitzen der LED auf dem ESP32-Modul wird mit diesem Timer
// nach Anstoß durch die CARguru-Bridge (BlinkAlive) umgesetzt
void onLichthupeTimer()
{
  // Lichthupe
  if (LichtHupeTimer > 0)
  {
    if (LichtHupeTimer == LichthupeTime)
    {
      LEDs[frontLight].SetBrightness(veryBright);
    }
    LichtHupeTimer--;
    if (LichtHupeTimer == 0)
    {
      lichthupeTimer.detach();
      if (headlightsOn == true)
        LEDs[frontLight].SetBrightness(smallBright);
      else
        LEDs[frontLight].SetBrightness(dark);
    }
  }
}

void onBlinkerTimer()
{
  // Blinker links
  if (BlinkerIsOn == true)
  {
    if (BlinkerIsHigh)
    {
      BlinkerIsHigh = false;
      if (leftBlinkerIsOn == true)
      {
        LEDs[rightIndicator].SetBrightness(dark);
      }
      if (rightBlinkerIsOn == true)
      {
        LEDs[leftIndicator].SetBrightness(dark);
      }
    }
    else
    {
      BlinkerIsHigh = true;
      if (leftBlinkerIsOn == true)
      {
        LEDs[rightIndicator].SetBrightness(veryBright);
      }
      if (rightBlinkerIsOn == true)
      {
        LEDs[leftIndicator].SetBrightness(veryBright);
      }
    }
  }
}

void onBlitzerTimer()
{
  // blizzerTopLeft: Signallicht, oben links
  // blizzerfrontLeft: Signallicht, oben rechts
  // blizzerFrontRight: Signallicht, vorne links
  // blizzerTopRight: Signallicht, vorne rechts
  static uint8_t blitzerPhase = 0;
  // F r o n t b l i t z e r
  LEDs[blizzerTopLeft + blitzerPhase].blitzLED(100);
  blitzerPhase++;
  if (blitzerPhase > 3)
    blitzerPhase = 0;
}

void onBrakeTimer()
{
  if (brakeTimer > 0)
  {
    brakeTimer--;
    if (brakeTimer % 4)
    {
      LEDs[rearLight].SetBrightness(veryBright);
    }
    else
    {
      LEDs[rearLight].SetBrightness(dark);
    }
    if (brakeTimer == 0)
    {
      // meldet den Timer ab
      brkTimer.detach();
      if (brakelightsOn == true)
      {
        LEDs[rearLight].SetBrightness(smallBright);
      }
      else
      {
        LEDs[rearLight].SetBrightness(dark);
      }
    }
  }
}

void setup()
{
  Serial.begin(115200);
  // Brightness is 0-255. We set it to 1/3 brightness here
//*  tinypico.DotStar_SetBrightness(smallBright);
//*  tinypico.DotStar_SetPixelColor(0xf00000); // green
  // turn the LED off by making the voltage LOW
  LED_ON(); // red
  Serial.println("\r\n\r\nCARguru - CAR");

  // der Decoder strahlt mit seiner Kennung
  // damit kennt die CARguru-Bridge (der Master) seine Decoder findet

  // startet WLAN im AP-Mode, damit meldet sich der Decoder beim Master
  Serial.println();
  Serial.println("WIFI Connect AP Mode");
  Serial.println("--------------------");

  WiFi.mode(WIFI_OFF); // https://github.com/esp8266/Arduino/issues/3100
                       // Connect to Wi-Fi
  WiFi.mode(WIFI_AP);
  String ssid0 = "CRgrSLV";
  String ssid1 = WiFi.softAPmacAddress();
  ssid0 = ssid0 + ssid1;
  char ssid[30];
  ssid0.toCharArray(ssid, 30);
  WiFi.softAP(ssid);
  WiFi.setTxPower(WIFI_POWER_MINUS_1dBm); // Set power to lowest possible value WIFI_POWER_MINUS_1dBm  WIFI_POWER_19_5dBm
  //
  // der Master (CARguru-Bridge) wird registriert
  addMaster();

  // WLAN -Verbindungen können wieder ausgeschaltet werden
  WiFi.disconnect();

  LED_OFF();

  // die EEPROM-Library wird gestartet
  if (!EEPROM.begin(EEPROM_SIZE))
  {
    Serial.println("Failed to initialise EEPROM");
  }
  uint8_t setup_todo = EEPROM.read(adr_setup_done);
  if (setup_todo != setup_done)
  {
    // alles fürs erste Mal
    //
    // wurde das Setup bereits einmal durchgeführt?
    // dann wird dieser Anteil übersprungen
    // 47, weil das EEPROM (hoffentlich) nie ursprünglich diesen Inhalt hatte

    // setzt die Boardnum anfangs auf 1
    decoderadr = 1;
    EEPROM.write(adr_decoderadr, decoderadr);
    EEPROM.commit();
    // ota auf "FALSE" setzen
    EEPROM.write(adr_ota, startWithoutOTA);
    EEPROM.commit();
    // setup_done auf "TRUE" setzen
    EEPROM.write(adr_setup_done, setup_done);
    EEPROM.commit();
  }
  else
  {
    uint8_t ota = EEPROM.readByte(adr_ota);
    if (ota == startWithoutOTA)
    {
      // nach dem ersten Mal Einlesen der gespeicherten Werte
      // Adresse
      decoderadr = readValfromEEPROM(adr_decoderadr, minadr, minadr, maxadr);
    }
    else
    {
      // ota auf "FALSE" setzen
      EEPROM.write(adr_ota, startWithoutOTA);
      EEPROM.commit();
      Connect2WiFiandOTA();
    }
  }
  // ab hier werden die Anweisungen bei jedem Start durchlaufen
  // IP-Adresse
  for (uint8_t ip = 0; ip < 4; ip++)
  {
    IP[ip] = EEPROM.read(adr_IP0 + ip);
  }

  got1CANmsg = false;
  SYS_CMD_Request = false;
  CONFIG_Status_Request = false;

  LichtHupeTimer = 0;
  brakeTimer = 0;
  BlinkerIsHigh = false;

  old_speed = 0;
  act_speed = 0;
  // die Lampen PWM werden initialisiert
  for (led_sweepers swpr_channel = frontLight; swpr_channel < LED_BUILTIN_OWN; swpr_channel = led_sweepers(swpr_channel + 1))
  {
    // Lampen-LEDs mit den PINs verbinden, initialisieren & Artikel auf dark setzen, dann einmal testen
    LEDs[swpr_channel] = LED(swpr_channel);
    LEDs[swpr_channel].blitzLED(200);
  }
  // Motor PWM initialisieren
  start_speed();

  // sound
  Martinshorn_Wave.Load_Wave("/Martinshorn.wav", false);
  Hupe_Wave.Load_Wave("/Hupe.wav", true);

  // Blinker
  BlinkerIsOn = false;
  rightBlinkerIsOn = false;
  leftBlinkerIsOn = false;
  rightBlinkerIsOn = false;
  lastUpdate = 0;
}

void SetSpeed(uint16_t speed)
{
  uint32_t tmp_speed = speed * max_speed / 1000;
  act_speed = tmp_speed;
}

void func_Abblendlicht(uint8_t val)
{
  // "F0" Scheinwerfer und Rückleuchten
  // lamp0: Scheinwerfer vorne
  // lamp1: Rücklichter
  switch (val)
  {
  case 0:
    LEDs[frontLight].SetBrightness(dark);
    LEDs[rearLight].SetBrightness(dark);
    headlightsOn = false;
    brakelightsOn = false;
    break;
  case 1:
    LEDs[frontLight].SetBrightness(smallBright);
    LEDs[rearLight].SetBrightness(smallBright);
    headlightsOn = true;
    brakelightsOn = true;
    break;
  }
}

void func_Blinker_links(uint8_t val)
{
  // "F01" Blinker links
  // lamp2: Blinker links
  switch (val)
  {
  case 0:
    BlinkerIsOn = false;
    leftBlinkerIsOn = false;
    LEDs[rightIndicator].SetBrightness(dark);
    blinkerTimer.detach();
    break;
  case 1:
    BlinkerIsOn = true;
    leftBlinkerIsOn = true;
    rightBlinkerIsOn = false;
    BlinkerIsHigh = true;
    LEDs[rightIndicator].SetBrightness(veryBright);
    blinkerTimer.attach_ms(blinkertimerTime, onBlinkerTimer);
    break;
  }
}

void func_Blinker_rechts(uint8_t val)
{
  // "F02" Blinker rechts
  // lamp3: Blinker rechts
  switch (val)
  {
  case 0:
    BlinkerIsOn = false;
    rightBlinkerIsOn = false;
    LEDs[leftIndicator].SetBrightness(dark);
    blinkerTimer.detach();
    break;
  case 1:
    BlinkerIsOn = true;
    rightBlinkerIsOn = true;
    leftBlinkerIsOn = false;
    BlinkerIsHigh = true;
    LEDs[leftIndicator].SetBrightness(veryBright);
    blinkerTimer.attach_ms(blinkertimerTime, onBlinkerTimer);
    break;
  }
}

void func_Warnblinker(uint8_t val)
{
  // "F03" Warnblinker
  // lamp2: Blinker links
  // lamp3: Blinker rechts
  switch (val)
  {
  case 0:
    BlinkerIsOn = false;
    leftBlinkerIsOn = false;
    rightBlinkerIsOn = false;
    LEDs[rightIndicator].SetBrightness(dark);
    LEDs[leftIndicator].SetBrightness(dark);
    blinkerTimer.detach();
    break;
  case 1:
    leftBlinkerIsOn = true;
    rightBlinkerIsOn = true;
    BlinkerIsOn = true;
    BlinkerIsHigh = true;
    LEDs[rightIndicator].SetBrightness(veryBright);
    LEDs[leftIndicator].SetBrightness(veryBright);
    blinkerTimer.attach_ms(blinkertimerTime, onBlinkerTimer);
    break;
  }
}

void func_Fernlicht(uint8_t val)
{
  // "F04" Lichthupe
  // frontLight: Scheinwerfer vorne
  switch (val)
  {
    // 0: switch off
  case 0:
    if (headlightsOn == true)
    {
      LEDs[frontLight].SetBrightness(smallBright);
    }
    else
    {
      LEDs[frontLight].SetBrightness(dark);
    }
    break;
    // 1: switch on
  case 1:
    LEDs[frontLight].SetBrightness(veryBright);
    break;
  }
}

void func_Lichthupe_TASTER(uint8_t val)
{
  // "F05" Fernlicht
  // lamp0: Scheinwerfer vorne
  LichtHupeTimer = LichthupeTime;
  lichthupeTimer.attach_ms(lichthupetimerTime, onLichthupeTimer);
}

void func_Rundumleuchten(uint8_t val)
{
  // "F09" Rundumleuchten
  // blizzerTopLeft: Signallicht, oben links
  // blizzerfrontLeft: Signallicht, oben rechts
  // blizzerFrontRight: Signallicht, vorne links
  // blizzerTopRight: Signallicht, vorne rechts
  switch (val)
  {
  case 0:
    LEDs[blizzerTopLeft].SetBrightness(dark);
    LEDs[blizzerfrontLeft].SetBrightness(dark);
    LEDs[blizzerFrontRight].SetBrightness(dark);
    LEDs[blizzerTopRight].SetBrightness(dark);
    blitzerTimer.detach();
    break;
  case 1:
    blitzerTimer.attach_ms(blitzertimerTime, onBlitzerTimer);
    break;
  }
}

/*
Response
Bestimmt, ob CAN Meldung eine Anforderung oder Antwort oder einer
vorhergehende Anforderung ist. Grundsätzlich wird eine Anforderung
ohne ein gesetztes Response Bit angestoßen. Sobald ein Kommando
ausgeführt wurde, wird es mit gesetztem Response Bit, sowie dem
ursprünglichen Meldungsinhalt oder den angefragten Werten, bestätigt.
Jeder Teilnehmer am Bus, welche die Meldung ausgeführt hat, bestätigt ein
Kommando.
*/
void sendCanFrame()
{
  // to Server
  for (uint8_t i = CAN_FRAME_SIZE - 1; i < 8 - opFrame[4]; i--)
    opFrame[i] = 0x00;
  opFrame[1]++;
  opFrame[2] = hasharr[0];
  opFrame[3] = hasharr[1];
  sendTheData();
}

/*
CAN Grundformat
Das CAN Protokoll schreibt vor, dass Meldungen mit einer 29 Bit Meldungskennung,
4 Bit Meldungslänge sowie bis zu 8 Datenbyte bestehen.
Die Meldungskennung wird aufgeteilt in die Unterbereiche Priorit�t (Prio),
Kommando (Command), Response und Hash.
Die Kommunikation basiert auf folgendem Datenformat:

Meldungskennung
Prio	2+2 Bit Message Prio			28 .. 25
Command	8 Bit	Kommando Kennzeichnung	24 .. 17
Resp.	1 Bit	CMD / Resp.				16
Hash	16 Bit	Kollisionsaufl�sung		15 .. 0
DLC
DLC		4 Bit	Anz. Datenbytes
Byte 0	D-Byte 0	8 Bit Daten
Byte 1	D-Byte 1	8 Bit Daten
Byte 2	D-Byte 2	8 Bit Daten
Byte 3	D-Byte 3	8 Bit Daten
Byte 4	D-Byte 4	8 Bit Daten
Byte 5	D-Byte 5	8 Bit Daten
Byte 6	D-Byte 6	8 Bit Daten
Byte 7	D-Byte 7	8 Bit Daten
*/

// Mit testMinMax wird festgestellt, ob ein Wert innerhalb der
// Grenzen von min und max liegt
bool testMinMax(uint8_t oldval, uint8_t val, uint8_t min, uint8_t max)
{
  return (oldval != val) && (val >= min) && (val <= max);
}

// receiveKanalData dient der Parameterübertragung zwischen Decoder und CARguru-Server
// es erhält die evtuelle auf dem Server geänderten Werte zurück
void receiveKanalData()
{
  uint8_t oldval;
  switch (opFrame[10])
  {
  // Kanalnummer #1 - Decoderadresse
  case 1:
  {
    oldval = decoderadr;
    decoderadr = (opFrame[11] << 8) + opFrame[12];
    if (testMinMax(oldval, decoderadr, minadr, maxadr))
    {
      // speichert die neue Adresse
      EEPROM.write(adr_decoderadr, decoderadr);
      EEPROM.commit();
      // neue Adressen
    }
    else
    {
      decoderadr = oldval;
    }
  }
  // Kanalnummer #2 - leer
  case 2:

    break;
  // Kanalnummer #3 - leer
  case 3:
  {
  }
  break;
  // Kanalnummer #4 - leer
  case 4:
  {
  }
  break;
  }
  //
  opFrame[11] = 0x01;
  opFrame[4] = 0x07;
  sendCanFrame();
}

// sendPING ist die Antwort der Decoder auf eine PING-Anfrage
void sendPING()
{
  opFrame[1] = PING;
  opFrame[4] = 0x08;
  for (uint8_t i = 0; i < uid_num; i++)
  {
    opFrame[i + 5] = uid_device[i];
  }
  opFrame[9] = VERS_HIGH;
  opFrame[10] = VERS_LOW;
  opFrame[11] = DEVTYPE_CAR_CAR >> 8;
  opFrame[12] = DEVTYPE_CAR_CAR;
  sendCanFrame();
}

// sendIP ist die Antwort der Decoder auf eine Abfrage der IP-Adresse
void sendIP()
{
  opFrame[1] = SEND_IP;
  opFrame[4] = 0x08;
  // IP-Adresse eintragen
  for (uint8_t ip = 0; ip < 4; ip++)
    opFrame[5 + ip] = IP[ip];
  opFrame[0x09] = decoderadr >> 8;
  opFrame[0x0A] = decoderadr;
  sendCanFrame();
}

// auf Anforderung des CARguru-Servers sendet der Decoder
// mit dieser Prozedur sendConfig seine Parameterwerte
void sendConfig()
{
  const uint8_t Kanalwidth = 8;
  const uint8_t numberofKanals = endofKanals - 1;
  const uint8_t NumLinesKanal00 = 4 * Kanalwidth;
  uint8_t arrKanal00[NumLinesKanal00] = {
      /*1*/ Kanal00, numberofKanals, (uint8_t)0, (uint8_t)0, (uint8_t)0, (uint8_t)0, (uint8_t)0, decoderadr,
      /*2.1*/ (uint8_t)highbyte2char(hex2dec(uid_device[0])), (uint8_t)lowbyte2char(hex2dec(uid_device[0])),
      /*2.2*/ (uint8_t)highbyte2char(hex2dec(uid_device[1])), (uint8_t)lowbyte2char(hex2dec(uid_device[1])),
      /*2.3*/ (uint8_t)highbyte2char(hex2dec(uid_device[2])), (uint8_t)lowbyte2char(hex2dec(uid_device[2])),
      /*2.4*/ (uint8_t)highbyte2char(hex2dec(uid_device[3])), (uint8_t)lowbyte2char(hex2dec(uid_device[3])),
      /*3*/ 'C', 'A', 'R', 'g', 'u', 'r', 'u', ' ',
      /*4*/ 'C', 'A', 'R', 0, 0, 0, 0, 0};
  const uint8_t NumLinesKanal01 = 4 * Kanalwidth;
  uint8_t arrKanal01[NumLinesKanal01] = {
      // #2 - WORD immer Big Endian, wie Uhrzeit
      /*1*/ Kanal01, 2, 0, minadr, 0, maxadr, 0, decoderadr,
      /*2*/ 'M', 'o', 'd', 'u', 'l', 'a', 'd', 'r',
      /*3*/ 'e', 's', 's', 'e', 0, '1', 0, (maxadr / 100) + '0',
      /*4*/ (maxadr - (uint8_t)(maxadr / 100) * 100) / 10 + '0', (maxadr - (uint8_t)(maxadr / 10) * 10) + '0', 0, 'A', 'd', 'r', 0, 0};
  uint8_t NumKanalLines[numberofKanals + 1] = {
      NumLinesKanal00, NumLinesKanal01};

  uint8_t paket = 0;
  uint8_t outzeichen = 0;
  CONFIG_Status_Request = false;
  for (uint8_t inzeichen = 0; inzeichen < NumKanalLines[CONFIGURATION_Status_Index]; inzeichen++)
  {
    opFrame[1] = CONFIG_Status + 1;
    switch (CONFIGURATION_Status_Index)
    {
    case Kanal00:
    {
      opFrame[outzeichen + 5] = arrKanal00[inzeichen];
    }
    break;
    case Kanal01:
    {
      opFrame[outzeichen + 5] = arrKanal01[inzeichen];
    }
    break;
    case endofKanals:
    {
      // der Vollständigkeit geschuldet
    }
    break;
    }
    outzeichen++;
    if (outzeichen == 8)
    {
      opFrame[4] = 8;
      outzeichen = 0;
      paket++;
      opFrame[2] = 0x00;
      opFrame[3] = paket;
      sendTheData();
      delay(wait_time_small);
    }
  }
  //
  memset(opFrame, 0, sizeof(opFrame));
  opFrame[1] = CONFIG_Status + 1;
  opFrame[2] = hasharr[0];
  opFrame[3] = hasharr[1];
  opFrame[4] = 0x06;
  for (uint8_t i = 0; i < 4; i++)
  {
    opFrame[i + 5] = uid_device[i];
  }
  opFrame[9] = CONFIGURATION_Status_Index;
  opFrame[10] = paket;
  sendTheData();
  delay(wait_time_small);
}

void execute_funcs(uint8_t func)
{
  float voltage;
  switch (func)
  {
  // spezielle Aufrufe / Kommendos
  case Lok_Speed:
  {
    // <Clnt< 0x00(08)0300   [06] 00 00 00 C8 01 35 00 00 .....5..
    // Lng: 06
    // Loc-ID: 00 00 00 C8
    // Speed: 01 35
    uint16_t ownAddress = PROT_DCC + decoderadr;
    uint16_t rcvdAddress = (opFrame[data2] << 8) + opFrame[data3];
    if (ownAddress == rcvdAddress)
    {
      uint16_t speed = (opFrame[data4] << 8) + opFrame[data5];
      SetSpeed(speed);
    }
  }
  break;
  case Lok_Function:
  {
    uint16_t ownAddress = PROT_DCC + decoderadr;
    uint16_t rcvdAddress = (opFrame[data2] << 8) + opFrame[data3];
    if (ownAddress == rcvdAddress)
    {
      functions func = (functions)opFrame[data4];
      uint8_t val = opFrame[data5];
      switch (func)
      {
      case Abblendlicht: // "F0" Abblendlicht
        func_Abblendlicht(val);
        break;
      case Blinker_links: // "F01" Blinker links
        func_Blinker_links(val);
        break;
      case Blinker_rechts: // "F02" Blinker rechts
        func_Blinker_rechts(val);
        break;
      case Warnblinker: // "F03" Warnblinker
        func_Warnblinker(val);
        break;
      case Fernlicht: // "F4" Lichthupe
        func_Fernlicht(val);
        break;
      case Lichthupe_TASTER: // "F05" Fernlicht
        func_Lichthupe_TASTER(val);
        break;
      case Hupe_TASTER: // "F06" Hupe
        Hupe_Wave.PlayWav();
        break;
      case Martinshorn: // "F7" Martinshorn
        // "F07" Martinshorn
        switch (val)
        {
        case 0:
          Martinshorn_Wave.StopPlaying();
          break;
        case 1:
          Martinshorn_Wave.PlayWav();
          break;
        }
        break;
      case Rundumleuchten: // "F09" Rundumleuchten
        func_Rundumleuchten(val);
        break;
      case battery_func:
//*        voltage = tinypico.GetBatteryVoltage();    // 3.14
        opFrame[data5] = voltage;                  // 3
        voltage = (voltage - opFrame[data5]) * 10; // 1.4
        opFrame[data6] = voltage;                  // 1
        voltage = (voltage - opFrame[data6]) * 10; // 4
        opFrame[data7] = voltage;
        opFrame[dataLength] = 0x07;
        sendCanFrame();
        break;
      }
    }
  }
  break;

  default:
    Serial.print("Unbehandelte Function: ");
    Serial.println(func, HEX);
    break;
  }
}

// In dieser Schleife verbringt der Decoder die meiste Zeit
void loop()
{
  float voltage = 0;
  if (old_speed != act_speed)
  {
    if (old_speed > act_speed)
    {
      // meldet den Timer an
      brkTimer.attach_ms(brktimerTime, onBrakeTimer);
      brakeTimer = brakeTime;
      LEDs[rearLight].SetBrightness(veryBright);
    }
    old_speed = act_speed;
    // actual speed to the motor
    make_speed(act_speed);
    // actual speed set is done; report to the bridge
    sendCanFrame();
  }
  if ((micros() - lastUpdate) > updateInterval)
  { // time to update
    for (led_sweepers l = frontLight; l < LED_BUILTIN_OWN; l = led_sweepers(l + 1))
      LEDs[l].Update();
    lastUpdate = micros();
  }
  if (got1CANmsg)
  {
    got1CANmsg = false;
    // Parameterwerte vom CARguru-Server erhalten
    if (SYS_CMD_Request)
      receiveKanalData();
    // Parameterwerte an den CARguru-Server liefern
    if (CONFIG_Status_Request)
      sendConfig();
  }
}
