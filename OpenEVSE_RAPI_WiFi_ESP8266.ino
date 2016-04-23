/*
   Copyright (c) 2015 Chris Howell

   This file is part of Open EVSE.
   Open EVSE is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.
   Open EVSE is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   You should have received a copy of the GNU General Public License
   along with Open EVSE; see the file COPYING.  If not, write to the
   Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Added - Andrew Gregg / 21 Apr 2016:
   This version includes the following additions/changes:
   - ESP8266 OTA for Arduino IDE
   - Individual read/write functions have been broken out of loop()
   - gridServerRead() - read the current price (scale of 1-4), and requested charge percentage from OpenShift NodeJS server
   - RAPIwrite() sends the current price to the EVSE via a new RAPI command $SP
   - Functions for reading Wifi ELM327 CAN bus on Nission LEAF to get Accessory Voltage, SOC, and capacity (still several issues)

*/

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

//Added OTA includes - AG
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

//Filesystem to read files in /data
#include "FS.h"

ESP8266WebServer server(80);

//Default SSID and PASSWORD for AP Access Point Mode
const char* ssid = "OpenEVSE";
const char* password = "openevse";
String st;
String privateKey = "";
String node = "";

WiFiClient client;

//SERVER for power grid data server
const char* grid_host = "gridserv-pwrgrid.rhcloud.com";
const char* grid_url = "/?";
const int grid_port = 80;

//SERVER for ELM CANBus reader
const char* ELM_host = "192.168.0.103";
const int ELM_port = 35000;

const char ELM_commands[7][20] = {
  /*{"at z"},*/
  {"at h1"},
  {"at d0"},
  {"at sh 79b"},
  {"at fc sh 79b"},
  {"at fc sd 30 00 20"},
  {"at fc sm 1"},
  {"21 01"}
};

//SERVER strings, integers, and doubles for OpenEVSE Energy Monotoring
const char* e_host = "data.openevse.com";
const char* e_url = "/emoncms/input/post.json?node=";
const int e_port = 80;
const char* inputID_AMP   = "OpenEVSE_AMP:";
const char* inputID_VOLT   = "OpenEVSE_VOLT:";
const char* inputID_TEMP1   = "OpenEVSE_TEMP1:";
const char* inputID_TEMP2   = "OpenEVSE_TEMP2:";
const char* inputID_TEMP3   = "OpenEVSE_TEMP3:";
const char* inputID_PILOT   = "OpenEVSE_PILOT:";

//Added - Interactive
const char* inputID_PRICE   = "OpenEVSE_PRICE:";
const char* inputID_REQUEST   = "OpenEVSE_REQUEST:";
const char* inputID_BATTSOC   = "OpenEVSE_BATTSOC:";
const char* inputID_BATTAH   = "OpenEVSE_BATTAH:";
const char* inputID_ACCV   = "OpenEVSE_ACCV:";

int amp = 0;
int volt = 0;
int temp1 = 0;
int temp2 = 0;
int temp3 = 0;
int pilot = 0;
//Added - Interactive
int price = 0;
int request = -1;
float Veh_BattSOC = -1;
float Veh_BattAH = -1;
float Veh_AccV = 0;

int wifi_mode = 0;
int buttonState = 0;
int clientTimeout = 0;
int i = 0;
unsigned long Timer;

void ResetEEPROM() {
  //Serial.println("Erasing EEPROM");
  for (int i = 0; i < 512; ++i) {
    EEPROM.write(i, 0);
    //Serial.print("#");
  }
  EEPROM.commit();
}

void handleRoot() {
  String s;
  s = "<html><font size='20'><font color=006666>Open</font><b>EVSE</b></font><p><b>Open Source Hardware</b><p>Wireless Configuration<p>Networks Found:<p>";
  //s += ipStr;
  s += "<p>";
  s += st;
  s += "<p>";
  s += "<form method='get' action='a'><label><b><i>WiFi SSID:</b></i></label><input name='ssid' length=32><p><label><b><i>Password  :</b></i></label><input name='pass' length=64><p><label><b><i>Device Access Key:</b></i></label><input name='ekey' length=32><p><label><b><i>Node:</b></i></label><select name='node'><option value='0'>0 - Default</option><option value='1'>1</option><option value='2'>2</option><option value='3'>3</option><option value='4'>4</option><option value='5'>5</option><option value='6'>6</option><option value='7'>7</option><option value='8'>8</option><option value='9'>9</option></select><p><input type='submit'></form>";
  s += "</html>\r\n\r\n";
  server.send(200, "text/html", s);
}

void handleRapi() {
  String s;
  s = "<html><font size='20'><font color=006666>Open</font><b>EVSE</b></font><p><b>Open Source Hardware</b><p>Send RAPI Command<p>Common Commands:<p>Set Current - $SC XX<p>Set Service Level - $SL 1 - $SL 2 - $SL A<p>Get Real-time Current - $GG<p>Get Temperatures - $GP<p>";
  s += "<p>";
  s += "<form method='get' action='r'><label><b><i>RAPI Command:</b></i></label><input name='rapi' length=32><p><input type='submit'></form>";
  s += "</html>\r\n\r\n";
  server.send(200, "text/html", s);
}

void handleRapiR() {
  String s;
  String rapiString;
  String rapi = server.arg("rapi");
  rapi.replace("%24", "$");
  rapi.replace("+", " ");
  Serial.flush();
  Serial.println(rapi);
  delay(100);
  while (Serial.available()) {
    rapiString = Serial.readStringUntil('\r');
  }
  s = "<html><font size='20'><font color=006666>Open</font><b>EVSE</b></font><p><b>Open Source Hardware</b><p>RAPI Command Sent<p>Common Commands:<p>Set Current - $SC XX<p>Set Service Level - $SL 1 - $SL 2 - $SL A<p>Get Real-time Current - $GG<p>Get Temperatures - $GP<p>";
  s += "<p>";
  s += "<form method='get' action='r'><label><b><i>RAPI Command:</b></i></label><input name='rapi' length=32><p><input type='submit'></form>";
  s += rapi;
  s += "<p>>";
  s += rapiString;
  s += "<p></html>\r\n\r\n";
  server.send(200, "text/html", s);
}

void handleSetup() {
  SPIFFS.begin(); // mount the fs
  File f = SPIFFS.open("/setup.html", "r");
  if (f) {
    String s = f.readString();
    s += "}</script></html>\r\n\r\n";
    server.send(200, "text/html", s);
    f.close();
  }
}

void handleSetupS() {
  String commandStrings[10];
  String responseStrings[10];

  //Delay Timer
  if (server.hasArg("ST")) {
    String start = server.arg("timerStart");
    start.replace(":", " ");
    String stop = server.arg("timerStop");
    stop.replace(":", " ");
    commandStrings[0] = "$ST " + start + " " + stop;
  }  else commandStrings[0] = "$ST 0 0 0 0"; //Cancel timer

  //Set Date/Time
  String date = server.arg("currDate");
  date = date.substring(2); //Strip first two digits of year
  date.replace("-", " ");
  String ctime = server.arg("currTime");
  ctime.replace(":", " ");
  commandStrings[1] =  "$S1 " + date + " " + ctime;

  //Backlight
  commandStrings[2] = "$S0 " + server.arg("S0");

  //Service Level
  commandStrings[3] = "$SL " + server.arg("SL");

  //Max Current
  commandStrings[4] = "$SC " + server.arg("SC");

  //Checkboxes
  commandStrings[5] = "$SD " + (server.hasArg("SD") ? server.arg("SD") : "0");
  commandStrings[6] = "$SV " + (server.hasArg("SV") ? server.arg("SV") : "0");
  commandStrings[7] = "$SG " + (server.hasArg("SG") ? server.arg("SG") : "0");
  commandStrings[8] = "$SR " + (server.hasArg("SR") ? server.arg("SR") : "0");
  commandStrings[9] = "$SS " + (server.hasArg("SS") ? server.arg("SS") : "0");

  Serial.flush();

  uint i = 0;
  for (i = 0; i < 10; i++) {
    commandStrings[i] += checkSum(commandStrings[i]); //add Checksum to each command
    Serial.println(commandStrings[i]);
    delay(100);
    while (Serial.available()) {
      responseStrings[i] += Serial.readStringUntil('\r');
    }
  }

  //display response and setup.html again
  SPIFFS.begin(); // mount the fs
  File f = SPIFFS.open("/setup.html", "r");
  if (f) {
    String s = f.readString();
    uint a = 0;
    for (a = 0; a < 10; a++) {
      s += "document.getElementById(\"";
      s += a;
      s += "\").innerHTML = \"<font color = ff0000>" + responseStrings[a] + " </font>\";";
    }
    s += "}</script></html>\r\n\r\n";
    server.send(200, "text/html", s);
    f.close();
  }
}

void handleCfg() {
  String s;
  String qsid = server.arg("ssid");
  String qpass = server.arg("pass");
  String qkey = server.arg("ekey");
  String qnode = server.arg("node");

  qpass.replace("%23", "#");
  qpass.replace('+', ' ');

  if (qsid != 0) {
    ResetEEPROM();
    for (int i = 0; i < qsid.length(); ++i) {
      EEPROM.write(i, qsid[i]);
    }
    //Serial.println("Writing Password to Memory:");
    for (int i = 0; i < qpass.length(); ++i) {
      EEPROM.write(32 + i, qpass[i]);
    }
    //Serial.println("Writing EMON Key to Memory:");
    for (int i = 0; i < qkey.length(); ++i) {
      EEPROM.write(96 + i, qkey[i]);
    }

    EEPROM.write(129, qnode[i]);
    EEPROM.commit();
    s = "<html><font size='20'><font color=006666>Open</font><b>EVSE</b></font><p><b>Open Source Hardware</b><p>Wireless Configuration<p>SSID and Password<p>";
    //s += req;
    s += "<p>Saved to Memory...<p>Wifi will reset to join your network</html>\r\n\r\n";
    server.send(200, "text/html", s);
    delay(2000);
    WiFi.disconnect();
    ESP.reset();
  }
  else {
    s = "<html><font size='20'><font color=006666>Open</font><b>EVSE</b></font><p><b>Open Source Hardware</b><p>Wireless Configuration<p>Networks Found:<p>";
    //s += ipStr;
    s += "<p>";
    s += st;
    s += "<p>";
    s += "<form method='get' action='a'><label><b><i>WiFi SSID:</b></i></label><input name='ssid' length=32><p><font color=FF0000><b>SSID Required<b></font><p></font><label><b><i>Password  :</b></i></label><input name='pass' length=64><p><label><b><i>Device Access Key:</b></i></label><input name='ekey' length=32><p><label><b><i>Node:</b></i></label><select name='node'><option value='0'>0</option><option value='1'>1</option><option value='2'>2</option><option value='3'>3</option><option value='4'>4</option><option value='5'>5</option><option value='6'>6</option><option value='7'>7</option><option value='8'>8</option><option value='9'>9</option></select><p><input type='submit'></form>";
    s += "</html>\r\n\r\n";
    server.send(200, "text/html", s);
  }
}
void handleRst() {
  String s;
  s = "<html><font size='20'><font color=006666>Open</font><b>EVSE</b></font><p><b>Open Source Hardware</b><p>Wireless Configuration<p>Reset to Defaults:<p>";
  s += "<p><b>Clearing the EEPROM</b><p>";
  s += "</html>\r\n\r\n";
  ResetEEPROM();
  EEPROM.commit();
  server.send(200, "text/html", s);
  WiFi.disconnect();
  delay(1000);
  ESP.reset();
}


void handleStatus() {
  String s;
  s = "<html><iframe style='width:480px; height:320px;' frameborder='0' scrolling='no' marginheight='0' marginwidth='0' src='http://data.openevse.com/emoncms/vis/rawdata?feedid=1&fill=0&colour=008080&units=W&dp=1&scale=1&embed=1'></iframe>";
  s += "</html>\r\n\r\n";

  server.send(200, "text/html", s);

}

void RAPI_read() {
  Serial.flush();
  Serial.println("$GE*B0");
  delay(100);
  while (Serial.available()) {
    String rapiString = Serial.readStringUntil('\r');
    if ( rapiString.startsWith("$OK ") ) {
      String qrapi;
      qrapi = rapiString.substring(rapiString.indexOf(' '));
      pilot = qrapi.toInt();
    }
  }

  delay(100);
  Serial.flush();
  Serial.println("$GG*B2");
  delay(100);
  while (Serial.available()) {
    String rapiString = Serial.readStringUntil('\r');
    if ( rapiString.startsWith("$OK") ) {
      String qrapi;
      qrapi = rapiString.substring(rapiString.indexOf(' '));
      amp = qrapi.toInt();
      String qrapi1;
      qrapi1 = rapiString.substring(rapiString.lastIndexOf(' '));
      volt = qrapi1.toInt();
    }
  }
  delay(100);
  Serial.flush();
  Serial.println("$GP*BB");
  delay(100);
  while (Serial.available()) {
    String rapiString = Serial.readStringUntil('\r');
    if (rapiString.startsWith("$OK") ) {
      String qrapi;
      qrapi = rapiString.substring(rapiString.indexOf(' '));
      temp1 = qrapi.toInt();
      String qrapi1;
      int firstRapiCmd = rapiString.indexOf(' ');
      qrapi1 = rapiString.substring(rapiString.indexOf(' ', firstRapiCmd + 1 ));
      temp2 = qrapi1.toInt();
      String qrapi2;
      qrapi2 = rapiString.substring(rapiString.lastIndexOf(' '));
      temp3 = qrapi2.toInt();
    }
  }
}

void RAPI_write() {
  //Send current price to OpenEVSE
  String tmpStr = "$SP ";
  tmpStr += price;
  tmpStr += checkSum(tmpStr);
  Serial.println(tmpStr);
}

String checkSum(String command) {
  //Create checksum (from Lincomatic rapi_checksum)
  char buff[40];
  command.toCharArray(buff, command.length() + 1);
  char *s = buff;
  uint8 chkSum = 0;
  while (*s) {
    chkSum += *(s++);
  }
  sprintf(s, "*%02X%c", (unsigned)chkSum, 0xd);
  return s;
}

void parse_CAN_data(char * data) {
  char SOC_temp[8], AH_temp[8];
  char * sep_tok = "\r"; //Token for split
  char *lines[16] = {NULL};
  lines[0] = strtok(data, sep_tok);
  int i = 0;
  while (lines[i] != NULL) {
    i++;
    lines[i] = strtok(NULL, sep_tok);
  }
  int j = 0;
  for (j = 0; j < 8; j++)
    SOC_temp[j] = lines[5][19 + j];
  int l = 0;
  for (l = 0; l < 8; l++)
    AH_temp[l] = lines[6][10 + l];

  Veh_BattSOC = hex2int(SOC_temp, 8) / 10000;
  Veh_BattAH = hex2int(AH_temp, 8) / 10000;
  return;
}

/*  hex2int() takes a character array with spaces of given len (max 16) and
    ignoring spaces, converts to double.
*/
double hex2int(char * a, int len) {
  int k = 0;
  char no_spaces[16] = {NULL};
  int k_nosp = 0;
  double val = 0;
  no_spaces[0] = '0';
  no_spaces[1] = 'x';
  for (k = 0; k < len; k++) {
    if (a[k] != 0x20) //skip ASCII spaces
    {
      no_spaces[k_nosp + 2] = a[k];
      k_nosp++;
    }
  }
  val = strtol(no_spaces, NULL, 16);
  return val;
}

void ELM_read() {
  //Establish connection to CANBus reader and retrieve SOC (%), capacity (Ah), and AccV (V)
  // Use WiFiClient class to create TCP connection to CAN reader
  if (!client.connect(ELM_host, ELM_port)) {
    Serial.println("Connection error to CANreader\r");
    Veh_BattSOC = -1;
    Veh_BattAH = -1;
    Veh_AccV = 0;
    return;
  }
  else {
    int arraysize = sizeof (ELM_commands) / sizeof (ELM_commands[0]);
    char count = 0;
    String ELM_string;
    char ELM_buff[200];
    char temp_s[40];
    char i, x = 0;

    // ELM_wake();

    client.setTimeout(3000);

    //Read LEAF Accessory Battery Voltage
    client.print("at rv\r");
    delay(10);


    if (client.available())
      ELM_string = client.readStringUntil('>');
    i = 0;
    for (i = 0; i < ELM_string.length(); i++) {
      ELM_buff[i] = ELM_string.charAt(i);
    }
    Veh_AccV = strtod(strchr(ELM_buff, '\r'), NULL);//Skip first line, then look for the float
    //client.print("$FP 0 0 AccV= ");
    //client.println(Veh_AccV);

    for (count = 0; count < arraysize; count++) {
      sprintf(temp_s, "%s\r", ELM_commands[count]);
      client.print(temp_s);
      delay(10);

      if (client.available())
        ELM_string = client.readStringUntil('>');

      if (ELM_string.length() > 48) {
        i = 0;
        for (i = 0; i < ELM_string.length(); i++) {
          ELM_buff[i] = ELM_string.charAt(i);
        }
        parse_CAN_data(ELM_buff);
        Serial.print("$FP 0 0 SOC= ");
        Serial.println(Veh_BattSOC);
        Serial.print("$FP 0 1 AH= ");
        Serial.println(Veh_BattAH);
      }
    }

    client.flush();
    // ELM_sleep();
  }
}

void gridServer_read() {
  // Use WiFiClient class to create TCP connection to power grid server
  if (!client.connect(grid_host, grid_port)) {
    //Serial.println("Connection error to gcloud\r");
    price = 0;
    return;
  }

  //Read pricing signal and request from server
  client.print(String("GET ") + grid_url + " HTTP/1.1\r\n" + "Host: " + grid_host + "\r\n" + "Connection: close\r\n\r\n");
  delay(100);

  String grid_line;
  while (client.connected()) {
    grid_line = client.readStringUntil('\r'); //Content is on last line, after all headers
  }
  //Serial.println(grid_line + "\r");
  String qprice;
  qprice = grid_line.substring(0, grid_line.indexOf(' '));
  price = qprice.toInt();

  String qrequest;
  qrequest = grid_line.substring(grid_line.indexOf(' '));
  request = qrequest.toInt();

  client.flush();
}

void sendToServer() {
  // Establish new connection to OpenEVSE Emoncms
  if (!client.connect(e_host, e_port)) {
    return;
  }

  // We now create a URL for OpenEVSE RAPI data upload request
  String url = e_url;
  String url_amp = inputID_AMP;
  url_amp += amp;
  url_amp += ",";
  String url_volt = inputID_VOLT;
  url_volt += volt;
  url_volt += ",";
  String url_temp1 = inputID_TEMP1;
  url_temp1 += temp1;
  url_temp1 += ",";
  String url_temp2 = inputID_TEMP2;
  url_temp2 += temp2;
  url_temp2 += ",";
  String url_temp3 = inputID_TEMP3;
  url_temp3 += temp3;
  url_temp3 += ",";

  //Added - AG
  String url_price = inputID_PRICE;
  url_price += price;
  url_price += ",";
  String url_request = inputID_REQUEST;
  url_request += request;
  url_request += ",";
  String url_battSOC = inputID_BATTSOC;
  url_battSOC += Veh_BattSOC;
  url_battSOC += ",";
  String url_battAH = inputID_BATTAH;
  url_battAH += Veh_BattAH;
  url_battAH += ",";
  String url_accV = inputID_ACCV;
  url_accV += Veh_AccV;
  url_accV += ",";
  //End Added - AG

  String url_pilot = inputID_PILOT;
  url_pilot += pilot;

  url += node;
  url += "&json={";
  url += url_amp;
  if (volt <= 0) {
    url += url_volt;
  }
  if (temp1 != 0) {
    url += url_temp1;
  }
  if (temp2 != 0) {
    url += url_temp2;
  }
  if (temp3 != 0) {
    url += url_temp3;
  }
  if (price != 0) {
    url += url_price;
  }
  if (request != -1) {
    url += url_request;
  }
  if (Veh_BattSOC != -1) {
    url += url_battSOC;
  }
  if (Veh_BattAH != -1) {
    url += url_battAH;
  }
  if (Veh_AccV != 0) {
    url += url_accV;
  }
  url += url_pilot;

  url += "}&devicekey=";
  url += privateKey.c_str();

  // This will send the request to the server
  client.print(String("GET ") + url + " HTTP/1.1\r\n" + "Host: " + e_host + "\r\n" + "Connection: close\r\n\r\n");
  delay(10);
  while (client.available()) {
    String line = client.readStringUntil('\r');
  }
  //Serial.println(e_host);
  //Serial.println(url);

  client.flush();
}

void setup() {
  delay(1000);
  Serial.begin(115200);
  EEPROM.begin(512);
  pinMode(0, INPUT);
  char tmpStr[40];
  String esid;
  String epass = "";

  for (int i = 0; i < 32; ++i) {
    esid += char(EEPROM.read(i));
  }
  for (int i = 32; i < 96; ++i) {
    epass += char(EEPROM.read(i));
  }
  for (int i = 96; i < 128; ++i) {
    privateKey += char(EEPROM.read(i));
  }
  node += char(EEPROM.read(129));

  if ( esid != 0 ) {
    //Serial.println(" ");
    //Serial.print("Connecting as Wifi Client to: ");
    //Serial.println(esid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.begin(esid.c_str(), epass.c_str());
    delay(50);
    int t = 0;
    int attempt = 0;
    while (WiFi.status() != WL_CONNECTED) {
      // test esid
      //Serial.print("#");
      delay(500);
      t++;
      if (t >= 20) {
        //Serial.println(" ");
        //Serial.println("Trying Again...");
        delay(2000);
        WiFi.disconnect();
        WiFi.begin(esid.c_str(), epass.c_str());
        t = 0;
        attempt++;
        if (attempt >= 5) {
          //Serial.println();
          //Serial.print("Configuring access point...");
          WiFi.mode(WIFI_STA);
          WiFi.disconnect();
          delay(100);
          int n = WiFi.scanNetworks();
          //Serial.print(n);
          //Serial.println(" networks found");
          st = "<ul>";
          for (int i = 0; i < n; ++i) {
            st += "<li>";
            st += WiFi.SSID(i);
            st += "</li>";
          }
          st += "</ul>";
          delay(100);
          WiFi.softAP(ssid, password);
          IPAddress myIP = WiFi.softAPIP();
          //Serial.print("AP IP address: ");
          //Serial.println(myIP);
          Serial.println("$FP 0 0 SSID...OpenEVSE.");
          delay(100);
          Serial.println("$FP 0 1 PASS...openevse.");
          delay(5000);
          Serial.println("$FP 0 0 IP_Address......");
          delay(100);
          sprintf(tmpStr, "$FP 0 1 %d.%d.%d.%d", myIP[0], myIP[1], myIP[2], myIP[3]);
          Serial.println(tmpStr);
          wifi_mode = 1;
          break;
        }
      }
    }
  }
  else {
    //Serial.println();
    //Serial.print("Configuring access point...");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    int n = WiFi.scanNetworks();
    st = "<ul>";
    for (int i = 0; i < n; ++i) {
      st += "<li>";
      st += WiFi.SSID(i);
      st += "</li>";
    }
    st += "</ul>";
    delay(100);
    WiFi.softAP(ssid, password);
    IPAddress myIP = WiFi.softAPIP();
    //Serial.print("AP IP address: ");
    //Serial.println(myIP);
    Serial.println("$FP 0 0 SSID...OpenEVSE.");
    delay(100);
    Serial.println("$FP 0 1 PASS...openevse.");
    delay(5000);
    Serial.println("$FP 0 0 IP_Address......");
    delay(100);
    sprintf(tmpStr, "$FP 0 1 %d.%d.%d.%d", myIP[0], myIP[1], myIP[2], myIP[3]);
    Serial.println(tmpStr);


    wifi_mode = 2; //AP mode with no SSID in EEPROM
  }

  if (wifi_mode == 0) {
    //Serial.println(" ");
    //Serial.println("Connected as a Client");
    IPAddress myAddress = WiFi.localIP();
    //Serial.println(myAddress);
    //Serial.println("$FP 0 0 Client-IP.......");
    //delay(100);
    //sprintf(tmpStr, "$FP 0 1 %d.%d.%d.%d", myAddress[0], myAddress[1], myAddress[2], myAddress[3]);
    //Serial.println(tmpStr);
  }

  //Added OTA lines below - AG
  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

  server.on("/", handleRoot);
  server.on("/a", handleCfg);
  server.on("/r", handleRapiR);
  server.on("/s", handleSetupS);
  server.on("/reset", handleRst);
  server.on("/status", handleStatus);
  server.on("/rapi", handleRapi);
  server.on("/setup", handleSetup);
  server.begin();
  //Serial.println("HTTP server started");
  delay(100);
  Timer = millis();
}



void loop() {

  //Handle OTA reprogram requests
  ArduinoOTA.handle();

  //Handle web server
  server.handleClient();

  //Handle GPIO 0 to wipe AP and API key data from EEPROM
  int erase = 0;
  buttonState = digitalRead(0);
  while (buttonState == LOW) {
    buttonState = digitalRead(0);
    erase++;
    if (erase >= 5000) {
      ResetEEPROM();
      int erase = 0;
      WiFi.disconnect();
      Serial.print("Finished...");
      delay(2000);
      ESP.reset();
    }
  }
  // Remain in AP mode for 5 Minutes before resetting
  if (wifi_mode == 1) {
    if ((millis() - Timer) >= 300000) {
      ESP.reset();
    }
  }

  //If connected as a Wifi client with a privateKey, perform the following every 10 sec
  if (wifi_mode == 0 && privateKey != 0) {
    if ((millis() - Timer) >= 10000) {
      Timer = millis();

      //Read values from EVSE via RAPI
      RAPI_read();

      //Read CANbus data
      //ELM_read();

      //Read price and request from power grid server
      gridServer_read();

      //Write values to EVSE via RAPI
      RAPI_write();

      //Send data to OpenEVSE Emoncms
      sendToServer();
    }
  }
}
