/*
   da fare : controllare ora per reset NTP
             mettere isteresi

     Wemos D1 mini- - TFT 2.8" - D18B20

  // Pinout per WEMOS D1 MINI
  // Display SDO/MISO  to NodeMCU pin D6 (or leave disconnected if not reading TFT)
  // Display LED       to NodeMCU pin VIN (or 5V, see below)
  // Display SCK       to NodeMCU pin D5
  // Display SDI/MOSI  to NodeMCU pin D7
  // Display DC (RS/AO)to NodeMCU pin D3
  // Display RESET     to NodeMCU pin RST
  // Display CS        to NodeMCU pin D8 (or GND, see below)
  // Display GND       to NodeMCU pin GND (0V)
  // Display VCC       to NodeMCU 3.3V

  Linee da modificare su TFT_eSPI\User_Setups\Setup1_IL9341

  // ###### EDIT THE PIN NUMBERS IN THE LINES FOLLOWING TO SUIT YOUR ESP8266 SETUP ######

  // For NodeMCU - use pin numbers in the form PIN_Dx where Dx is the NodeMCU pin designation
  #define TFT_CS   PIN_D8  // Chip select control pin D8
  #define TFT_DC   PIN_D3  // Data Command control pin
  //#define TFT_RST  PIN_D4  // Reset pin (could connect to NodeMCU RST, see next line)
  #define TFT_RST  -1  // Set TFT_RST to -1 if the display RESET is connected to NodeMCU RST or 3.3V

*/


#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <WebSocketsClient.h> //  https://github.com/kakopappa/sinric/wiki/How-to-add-dependency-libraries
#include <ArduinoJson.h> // https://github.com/kakopappa/sinric/wiki/How-to-add-dependency-libraries
#include <StreamString.h>

#include <OneWire.h> //           Installare da Library manager
#include <DallasTemperature.h> // Installare da Library manager
#include <Wire.h>
#include "RTClib.h"  //           Installare da Library manager
#include <WiFiUdp.h>
#include <EEPROM.h>
#include <SPI.h>
#include "TFT_eSPI.h"   //        Installare da Library manager
//#define TFT_GREY 0x5AEB // New colour

TFT_eSPI tft = TFT_eSPI(); //     Use hardware SPI
#define Rele       5    // pin D1
#define puls_auto  2    // pin D4 a cui è collegato il pulsante
#define ONE_WIRE_BUS 4  // DS18B20 pin D2
#define relay_on  1     // Se il rele' si attiva con HIGH ( se si attiva con LOW mettere 0 )

ESP8266WiFiMulti WiFiMulti;
WebSocketsClient webSocket;

#define MyApiKey "2f9069a5-a024-4527-bc1b-b6dd2c29f645" // TODO: Change to your sinric API Key. Your API Key is displayed on sinric.com dashboard
#define MySSID "marconi" // TODO: Change to your Wifi network SSID
#define MyWifiPassword "quellatroiadituma" // TODO: Change to your Wifi network password

#define HEARTBEAT_INTERVAL 300000 // 5 Minutes 

uint64_t heartbeatTimestamp = 0;
bool isConnected = false;

RTC_Millis RTC;  //        Imposta un RTC Software
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);
unsigned int localPort = 2390;      // local port to listen for UDP packets
IPAddress timeServerIP(217, 147, 223, 78); //    europe.pool.ntp.org
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

WiFiUDP udp;
unsigned long tempo ;
byte isteresi  ;
byte automatico  ; // 1 = locale         0 = Alexa (auto)

byte flag_ora = 0 ;
int value_loc ;
int temp_alexa ;
bool flag_isteresi = 0 ;
bool stato_thermo  ; // 1= attivato   0= disattivato tutto
const byte eep_stato_thermo =  3 ; // locazione EEProm Locale/Alexa
const byte eep_man_aut =  2 ; // locazione EEProm Locale/Alexa
const byte eep_isteresi = 1 ; // locazione EEProm valore isteresi
const byte eep_temp_alexa = 0 ; // Locazione EEprom per temperatura

byte statopulsante_AUT = 1;         // stato corrente del pulsante:
byte last_statopulsante_AUT = 1;    // precedente stato del pulsante:

float tempC;
// String webString = "";   // String to display
unsigned long previousMillis = 0;
const long interval = 4000;

DeviceAddress insideThermometer;


void setPowerStateOnServer(String deviceId, String value);
void setTargetTemperatureOnServer(String deviceId, String value, String scale);

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      isConnected = false;
      Serial.printf("[WSc] Webservice disconnected from sinric.com!\n");
      break;
    case WStype_CONNECTED: {
        isConnected = true;
        Serial.printf("[WSc] Service connected to sinric.com at url: %s\n", payload);
        Serial.printf("Waiting for commands from sinric.com ...\n");
      }
      break;
    case WStype_TEXT: {
        Serial.printf("[WSc] get text: %s\n", payload);
        // Example payloads

        // For Thermostat
        // {"deviceId": xxxx, "action": "setPowerState", value: "ON"} // https://developer.amazon.com/docs/device-apis/alexa-thermostatcontroller.html
        // {"deviceId": xxxx, "action": "SetTargetTemperature", value: "targetSetpoint": { "value": 20.0, "scale": "CELSIUS"}} // https://developer.amazon.com/docs/device-apis/alexa-thermostatcontroller.html#settargettemperature
        // {"deviceId": xxxx, "action": "AdjustTargetTemperature", value: "targetSetpointDelta": { "value": 2.0, "scale": "FAHRENHEIT" }} // https://developer.amazon.com/docs/device-apis/alexa-thermostatcontroller.html#adjusttargettemperature
        // {"deviceId": xxxx, "action": "SetThermostatMode", value: "thermostatMode" : { "value": "COOL" }} // https://developer.amazon.com/docs/device-apis/alexa-thermostatcontroller.html#setthermostatmode

        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject((char*)payload);
        String deviceId = json ["deviceId"];
        String action = json ["action"];
        int value = json ["value"];

        // possiamo usare questo per regolare l'isteresi
        // imposta caldaia al 2% ( da' un errore ma funziona anche se risponde di non sapere quale problema si sia verificato )
        // il deviceId è quello del termostato
        if (deviceId == "5c166a96f7cae521a3780e58" && action == "setPowerLevel" ) // Device ID of first device
        {
          Serial.print(" Temp. isteresi = ");
          Serial.println(value);
          EEPROM.write(eep_isteresi, value ); // imposta il valore di isteresi
            EEPROM.commit();
          // Check device id if you have multiple devices.
        }

        // Alexa, accendi ( o spegni ) caldaia
        if (action == "setPowerState") { // On or Off
          String value = json ["value"];
          Serial.println("[WSc] setPowerState" + value);

          if (value == "ON") {
            stato_thermo = 1; // 1= attiv   0= disattivato tutto
            Serial.println("attivato");
          }
          else if (value == "OFF") {
            stato_thermo = 0; // 1= attiv   0= disattivato tutto
            Serial.println("disattivato");
          }
          EEPROM.write(eep_stato_thermo, stato_thermo );
          EEPROM.commit();
        }
        else if (action == "SetTargetTemperature") {  // Alexa, imposta "caldaia" s 2O gradi
          //String value = json ["value"];
          int value = json["value"]["targetSetpoint"]["value"];
          String scale = json["value"]["targetSetpoint"]["scale"];
          Serial.println("[WSc] SetTargetTemperature value: " + value);
          Serial.println("[WSc] SetTargetTemperature scale: " + scale);
          if ( flag_isteresi == 1 ) {
            EEPROM.write(eep_isteresi, value ); // imposta il valore di isteresi
            EEPROM.commit();
            flag_isteresi = 0 ;
          }
          else {
            temp_alexa = value ;
            EEPROM.write(eep_temp_alexa, temp_alexa );
            EEPROM.commit();
          }
        }
        else if (action == "AdjustTargetTemperature") {
          // NOTE:
          // Amazon have not mentioned the correct response format in the docs.
          // Alex will say device does not respond.
          // https://developer.amazon.com/docs/device-apis/alexa-thermostatcontroller.html

          // Alexa, metti più caldo ( o più freddo )
          // Alexa, aumenta temperatura ( o caldaia ) di 2 gradi
          int value = json["value"]["targetSetpointDelta"]["value"];
          String scale = json["value"]["targetSetpointDelta"]["scale"];

          Serial.println("[WSc] AdjustTargetTemperature value: " + value);
          Serial.println("[WSc] AdjustTargetTemperature scale: " + scale);
          temp_alexa = EEPROM.read (eep_temp_alexa);
          temp_alexa = temp_alexa + value ;
          EEPROM.write(eep_temp_alexa, temp_alexa );
          EEPROM.commit();
        }
        else if (action == "SetThermostatMode") {
          // Alexa, imposta "caldaia" in automatico (AUTO)
          // Alexa, imposta caldaia su freddo (COOL)
          // Alexa, imposta caldaia su riscaldamento (HEAT)
          // Alexa, imposta caldaia su spento (OFF) ( questo si riferisce solo all'automatico )
          String value = json["value"]["thermostatMode"]["value"];

          Serial.println("[WSc] SetThermostatMode value: " + value);
          if (value == ("AUTO")) {
            automatico = 0 ;             // 1 = locale     0 = Alexa
            EEPROM.write(eep_man_aut, automatico );
            EEPROM.commit();
            // Serial.println("automatico attivato");
          }
          if (value == ("OFF")) {
            automatico = 1 ;             // 1 = locale     0 = Alexa
            EEPROM.write(eep_man_aut, automatico );
            EEPROM.commit();
            // Serial.println("automatico disattivato");
          }
          // un'altro sistema per regolare l'isteresi è quello di dire
          // Alexa. metti caldaia su freddo
          // e poi dire Alexa, imposta caldaia su 2 gradi
          
          if (value == ("COOL")) {
            flag_isteresi = 1 ;
            // Serial.println("flag_isteresi attivato");
          }
        }
        else if (action == "test") {
          Serial.println("[WSc] received test command from sinric.com");
        }
      }

      break;
    case WStype_BIN:
      Serial.printf("[WSc] get binary length: %u\n", length);
      break;
  }
}


void setup() {
  pinMode ( Rele, OUTPUT);
  digitalWrite ( Rele, !relay_on );
  pinMode(puls_auto, INPUT_PULLUP);

  EEPROM.begin(10);
  automatico = EEPROM.read(eep_man_aut); // legge su EEprom se usare temperatura Locale o Alexa
  temp_alexa = EEPROM.read(eep_temp_alexa); // legge su EEprom temperatura impostata da Alexa
  stato_thermo = EEPROM.read(eep_stato_thermo); // legge su EEprom se attivato o disattivato
  // la temperatura in manuale la legge dal potenziometro

  //EEPROM.write(eep_isteresi, 2 ); // DA FARE ---------------------------------------------------------
  //EEPROM.commit();

  Serial.begin(115200);
  delay(1000);
  RTC.begin(DateTime()); // Avvia RTC software
  DS18B20.setResolution(insideThermometer, 12); // Risoluzione sensore

  tft.init();   // initialize  chip display

  WiFiMulti.addAP(MySSID, MyWifiPassword);
  Serial.println();
  Serial.print("Connecting to Wifi: ");
  Serial.println(MySSID);

  // Waiting for Wifi connect
  while (WiFiMulti.run() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  if (WiFiMulti.run() == WL_CONNECTED) {
    Serial.println("");
    Serial.print("WiFi connected. ");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  }

  // server address, port and URL
  webSocket.begin("iot.sinric.com", 80, "/");

  // event handler
  webSocket.onEvent(webSocketEvent);
  webSocket.setAuthorization("apikey", MyApiKey);

  // try again every 5000ms if connection has failed
  webSocket.setReconnectInterval(5000);   // If you see 'class WebSocketsClient' has no member named 'setReconnectInterval' error update arduinoWebSockets

  tft.setRotation(2); // Sceglie il verso di visualizzazione 2 = 240*320 /
  tft.setTextSize(3); // scegli grandezza caratteri 3=240x320 /
  tft.fillScreen(TFT_WHITE);
  tft.setTextColor(TFT_RED);
  tft.setTextDatum(MC_DATUM);
  tft.print(" TERMOSTATO");
  tft.println();
  tft.println();
  tft.println(" Connesso a");
  tft.print(" ");
  tft.println(MySSID);
  tft.println(" IP address: ");
  tft.print(" ");
  tft.println(WiFi.localIP());

  udp.begin(localPort);
  //Serial.print("Local port: ");
  //Serial.println(udp.localPort());

  legge_ntp();
  gettemperature() ;

}

void loop() {

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    gettemperature() ;

    tft.fillScreen(TFT_BLACK);
    scrive_data();

    tft.setCursor ( 6, 70 ); // scrive temperatura sul display
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(3);
    tft.print("Temp ");
    //tft.setCursor ( 6, 70 );
    tft.print(tempC);
    tft.print(" C");
    vis_modo_auto(); // scrive sul display la modalita'
    value_loc = map (analogRead(A0), 0, 1023, 15, 31 ); //lettura_adc
    scrive_temp_impostata() ; // scrive temperatura impostata
  }

  lettura_pulsanti() ;
  ctrl_ora() ;
  ctrl_on_off() ;
  if (tempC == 85.0 || tempC == -127.0) {
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(3);
    tft.setCursor(1, 250);
    tft.print("SENSORE ROTTO");
    // while (1) ; // Blocca tutto
  }

  webSocket.loop();

  if (isConnected) {
    uint64_t now = millis();

    // Send heartbeat in order to avoid disconnections during ISP resetting IPs over night. Thanks @MacSass
    if ((now - heartbeatTimestamp) > HEARTBEAT_INTERVAL) {
      heartbeatTimestamp = now;
      webSocket.sendTXT("H");
    }
  }
}

// If you are going to use a push button to on/off the switch manually, use this function to update the status on the server
// so it will reflect on Alexa app.
// eg: setPowerStateOnServer("deviceid", "ON")

// Call ONLY If status changed. DO NOT CALL THIS IN loop() and overload the server.

void setPowerStateOnServer(String deviceId, String value) {
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  root["deviceId"] = deviceId;
  root["action"] = "setPowerState";
  root["value"] = value;
  StreamString databuf;
  root.printTo(databuf);

  webSocket.sendTXT(databuf);
}

//eg: setPowerStateOnServer("deviceid", "25.0", "CELSIUS")

// Call ONLY If status changed. DO NOT CALL THIS IN loop() and overload the server.

void setTargetTemperatureOnServer(String deviceId, String value, String scale) {
  DynamicJsonBuffer jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  root["action"] = "SetTargetTemperature";
  root["deviceId"] = deviceId;

  JsonObject& valueObj = root.createNestedObject("value");
  JsonObject& targetSetpoint = valueObj.createNestedObject("targetSetpoint");
  targetSetpoint["value"] = value;
  targetSetpoint["scale"] = scale;

  StreamString databuf;
  root.printTo(databuf);

  webSocket.sendTXT(databuf);
}

void gettemperature() {
  DS18B20.requestTemperatures();
  tempC = DS18B20.getTempCByIndex(0);
}

void scrive_data()  {
  DateTime now = RTC.now();
  tft.setCursor(6, 2);
  tft.setTextColor(TFT_GREEN);
  tft.setTextSize(3);
  switch (now.dayOfTheWeek() ) {   //RTClib_ADA
    case 0:
      tft.print(F("DOM"));
      break;
    case 1:
      tft.print(F("LUN"));
      break;
    case 2:
      tft.print(F("MAR"));
      break;
    case 3:
      tft.print(F("MER"));
      break;
    case 4:
      tft.print(F("GIO"));
      break;
    case 5:
      tft.print(F("VEN"));
      break;
    case 6:
      tft.print(F("SAB"));
      break;
  }
  tft.print(" ");
  if (now.day() < 10)
    tft.print("0");
  tft.print(now.day());
  tft.print("/");
  if (now.month() < 10)
    tft.print("0");
  tft.print(now.month());
  tft.print("/");
  tft.print(now.year() - 2000);
  tft.setCursor(6, 35);
  tft.print("Orario");
  tft.setCursor(130, 35);
  if (now.hour() < 10)
    tft.print(" ");
  tft.print(now.hour());
  tft.print(":");
  if (now.minute() < 10)
    tft.print("0");
  tft.print(now.minute());

}

void legge_ntp() {
  for (int i = 0; i <= 6 ; i++) {
    NTP() ;
    if ( tempo > 1111111111 ) {
      RTC.adjust(DateTime(tempo));
      break ;
    }
    delay (3000);
  }
}

void NTP()
{
  sendNTPpacket(timeServerIP); // send an NTP packet to a time server
  delay(1000);
  if (udp.parsePacket()) {
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    const unsigned long seventyYears = 2208988800UL;
    unsigned long epoch = secsSince1900 - seventyYears; // toglie 70 anni
    // print Unix time:
    //   Serial.println(epoch);
    // print the hour, minute and second:
    //    Serial.print("The UTC time is ");       // UTC is the time at Greenwich Meridian (GMT)
    //    Serial.print((epoch  % 86400L) / 3600); // print the hour (86400 equals secs per day)
    //    Serial.print(':');
    tempo = (epoch + 3600) ; // aggiunge 1 ora

    if ( epoch > 1553990400  && epoch < 1572134400 ) { // ora legale 31/03/2019 - 27/10/2019
      tempo = (tempo + 3600) ; // aggiunge 1 ora
    }
    if ( epoch > 1585440001  && epoch < 1603584001 ) { // ora legale 29/03/2020 - 25/10/2020
      tempo = (tempo + 3600) ; // aggiunge 1 ora
    }

    // if ( ((epoch % 3600) / 60) < 10 ) {
    // In the first 10 minutes of each hour, we'll want a leading '0'
    // Serial.print('0');
    //  }
    // Serial.print((epoch  % 3600) / 60); // print the minute (3600 equals secs per minute)
    // Serial.print(':');
    // if ( (epoch % 60) < 10 ) {
    // In the first 10 seconds of each minute, we'll want a leading '0'
    //    Serial.print('0');
    //  }
    //  Serial.println(epoch % 60); // print the second
  }
}

unsigned long sendNTPpacket(IPAddress & address)
{
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

void vis_modo_auto() {
  tft.setTextSize(3);
  tft.setCursor(4, 210 );
  if ( digitalRead(Rele) == HIGH ) {
    tft.setTextColor(TFT_YELLOW);
    tft.print("Acceso ");
  }
  else {
    tft.setTextColor(TFT_RED);
    tft.print("Spento ");
  }
  tft.setCursor(120, 210 );
  tft.print("      ");
  tft.setCursor(120, 210 );

  if (  automatico == 1 ) {
    tft.print("Locale");
  }
  else {
    tft.print("Alexa");
  }
  tft.setCursor(4, 175);
  tft.setTextColor(TFT_BLUE );
  tft.print("Isteresi ");
  tft.print ( EEPROM.read(eep_isteresi) );
  tft.print(" C");
  tft.setTextColor(TFT_WHITE );
  if (stato_thermo == 0 ) { // visualizza sistema disattivato
    tft.setCursor(58, 250 );
    tft.print("SISTEMA");
    tft.setCursor(21, 285 );
    tft.print("DISATTIVATO");
  }
  //  else {
  //        tft.setCursor(58, 250 );
  //  tft.print("SISTEMA");
  //        tft.setCursor(50, 285 );
  //  tft.print("ATTIVATO");
  //  }

}

//********************** se si vuole impostare degli orari
void ctrl_ora() {
  DateTime now = RTC.now();
  if (( now.hour() * 60) +  now.minute()  < 360 ) { // tieni spento fino alle 6
    flag_ora = 0 ;
  }
  else {
    flag_ora = 1 ;
  }
  if (( now.hour() * 60) + now.minute()  > 1350 )  { // ore 22.30
    flag_ora = 0 ;
  }
}

void ctrl_on_off() { // Accende-Spegne Relè
  if (stato_thermo == 1) {
    if ( automatico == 0 )  // Automatico abilitato
    {
      if ( flag_ora == 1 ) {
        if ( tempC < temp_alexa - EEPROM.read(eep_isteresi) )  {
          digitalWrite(Rele, relay_on);   // Acceso
        }
        if ( tempC > temp_alexa )  {
          digitalWrite(Rele, !relay_on);   // Spento
        }
      }
      else {
        digitalWrite(Rele, !relay_on);   // orario notturno
      }
    }
    if ( automatico == 1 ) // Automatico disabilitato
    {
      if ( tempC < value_loc - EEPROM.read(eep_isteresi) )  {
        digitalWrite(Rele, relay_on);   // Acceso
      }
      if ( tempC > value_loc )  {
        digitalWrite(Rele, !relay_on);   // Spento
      }
    }
  }
  else {
    digitalWrite(Rele, !relay_on);   // Spento

  }
}

void scrive_temp_impostata() {
  tft.setTextSize(3);
  if ( automatico == 1 ) {
    tft.setTextColor(TFT_YELLOW);
  }
  else {
    tft.setTextColor(TFT_RED);
  }

  tft.setCursor(4, 105);
  tft.print(F("Temp.Imp "));
  //  tft.setTextSize(3);
  //  tft.setCursor(100, 105-3);
  tft.print(value_loc);
  tft.print(" C");

  if ( automatico == 1 ) {
    tft.setTextColor(TFT_RED);
  }
  else {
    tft.setTextColor(TFT_YELLOW);
  }
  tft.setTextSize(3);
  tft.setCursor(4, 140);
  tft.print(F("T.Alexa  "));
  //  tft.setTextSize(3);
  //  tft.setCursor(100, 156);
  tft.print(temp_alexa);
  tft.print(" C");

}

void lettura_pulsanti() {
  statopulsante_AUT = digitalRead(puls_auto);              // Leggi il Pin del pulsante:
  if (statopulsante_AUT != last_statopulsante_AUT ) {
    if (statopulsante_AUT == LOW) {
      automatico =  !automatico ;
      EEPROM.write(eep_man_aut, automatico);
      delay (50 ) ;
      EEPROM.commit();
      //  prepara_txtbox();
      vis_modo_auto(); // scrive sul display la modalita'
    }
  }
  last_statopulsante_AUT = statopulsante_AUT ;
}
