#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <MD_MAX72xx.h>           //https://github.com/MajicDesigns/MD_MAX72XX

#define MAX_DEVICES 4
#define CLK_PIN   D5  // or SCK
#define DATA_PIN  D7  // or MOSI
#define CS_PIN    D8  // or SS
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW

MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
WiFiUDP UDP;                     // Create an instance of the WiFiUDP class to send and receive
WiFiManager wifiManager;

IPAddress timeServerIP;          // time.nist.gov NTP server address
const char* NTPServerName = "0.au.pool.ntp.org";
const int NTP_PACKET_SIZE = 48;  // NTP time stamp is in the first 48 bytes of the message
byte NTPBuffer[NTP_PACKET_SIZE]; // buffer to hold incoming and outgoing packets


void setup() {
  mx.begin();
  Serial.begin(115200);          // Start the Serial communication to send messages to the computer
  delay(10);
  Serial.println("\r\n");

  wifiManager.autoConnect();
  startUDP();

  if (!WiFi.hostByName(NTPServerName, timeServerIP)) { // Get the IP address of the NTP server
    Serial.println("DNS lookup failed. Rebooting.");
    Serial.flush();
    ESP.reset();
  }
  Serial.print("Time server IP:\t");
  Serial.println(timeServerIP);

  Serial.println("\r\nSending NTP request ...");
  sendNTPpacket(timeServerIP);
}

unsigned long intervalNTP = 60000; // Request NTP time every minute
unsigned long prevNTP = 0;
unsigned long lastNTPResponse = millis();
uint32_t timeUNIX = 0;
int lastMinute;
int lastHour;

unsigned long prevActualTime = 0;

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - prevNTP > intervalNTP) { // If a minute has passed since last NTP request
    prevNTP = currentMillis;
    Serial.println("\r\nSending NTP request ...");
    sendNTPpacket(timeServerIP);               // Send an NTP request
  }

  uint32_t time = getTime();                   // Check if an NTP response has arrived and get the (UNIX) time
  if (time) {                                  // If a new timestamp has been received
    timeUNIX = time;
    Serial.print("NTP response:\t");
    Serial.println(timeUNIX);
    lastNTPResponse = currentMillis;
  } else if ((currentMillis - lastNTPResponse) > 3600000) {
    spiral();
    Serial.println("More than 1 hour since last NTP response. Rebooting.");
    Serial.flush();
    ESP.reset();
  }

  uint32_t actualTime = timeUNIX + (currentMillis - lastNTPResponse) / 1000;
  // Change 10 below to your time offset. For example I am in AEST (QLD) which is UTC plus 10.
  uint32_t localTime = actualTime + (10 * 60 * 60);
  if (actualTime != prevActualTime && timeUNIX != 0) { // If a second has passed since last print
    prevActualTime = actualTime;
    Serial.printf("\rUTC time:\t%d:%d:%d   ", getHours(actualTime), getMinutes(actualTime), getSeconds(actualTime));
    Serial.printf("\rUTC time:\t%d:%d:%d   ", getHours(localTime), getMinutes(localTime), getSeconds(localTime));

  }
  // Convert number to string, then split the string so each char uses a single display
  // Convert to local time first
  String strhh  = String(getHours(localTime));
  String strmm  = String(getMinutes(localTime));

  // Create an animation on hour change.
  if (lastHour != getHours(localTime)) {
    spiral();
    lastHour = getHours(localTime);
  }

  // Only update if the time changes, otherwise just update ticker.
  if (lastMinute != getMinutes(localTime)) {
    mx.clear();
    for (int i = 0; i < strhh.length(); i++) {
      if (strhh.length() == 1) {
        mx.setChar((COL_SIZE * (4)) - 2, '0');
        mx.setChar((COL_SIZE * (4 - 1)) - 1, strhh[i]);

      } else {
        mx.setChar((COL_SIZE * (4 - i)) - (2 - i), strhh[i]);
      }
    }
    for (int k = 0; k < strmm.length(); k++) {
      if (strmm.length() == 1) {
        mx.setChar((COL_SIZE * (2)) - 1, '0');
        mx.setChar((COL_SIZE * (2 - 1)) - 2, strmm[k]);
      } else {
        if (k == 0) {
          mx.setChar((COL_SIZE * (2 - k)) - 3, strmm[k]);
        } else if (k == 1 ) {
          mx.setChar((COL_SIZE * (2 - k)) - 2, strmm[k]);
        }
      }
    }
    char *d = ":";
    mx.setChar((COL_SIZE * 2) , *d);
    mx.update();
    lastMinute = getMinutes(localTime);
  }
  mx.setPoint(ROW_SIZE - 1, 31, true);
  for (int j = 0; j < getSeconds(localTime); j++) {
    mx.setPoint(ROW_SIZE - 1, (30 - (j / 2)), true);
  }
  
}

void startUDP() {
  Serial.println("Starting UDP");
  UDP.begin(123);                          // Start listening for UDP messages on port 123
  Serial.print("Local port:\t");
  Serial.println(UDP.localPort());
  Serial.println();
}

uint32_t getTime() {
  if (UDP.parsePacket() == 0) { // If there's no response (yet)
    return 0;
  }
  UDP.read(NTPBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
  // Combine the 4 timestamp bytes into one 32-bit number
  uint32_t NTPTime = (NTPBuffer[40] << 24) | (NTPBuffer[41] << 16) | (NTPBuffer[42] << 8) | NTPBuffer[43];
  // Convert NTP time to a UNIX timestamp:
  // Unix time starts on Jan 1 1970. That's 2208988800 seconds in NTP time:
  const uint32_t seventyYears = 2208988800UL;
  // subtract seventy years:
  uint32_t UNIXTime = NTPTime - seventyYears;
  return UNIXTime;
}

void sendNTPpacket(IPAddress& address) {
  memset(NTPBuffer, 0, NTP_PACKET_SIZE);  // set all bytes in the buffer to 0
  // Initialize values needed to form NTP request
  NTPBuffer[0] = 0b11100011;   // LI, Version, Mode

  // send a packet requesting a timestamp:
  UDP.beginPacket(address, 123); // NTP requests are to port 123
  UDP.write(NTPBuffer, NTP_PACKET_SIZE);
  UDP.endPacket();
}

inline int getSeconds(uint32_t UNIXTime) {
  return UNIXTime % 60;
}

inline int getMinutes(uint32_t UNIXTime) {
  return UNIXTime / 60 % 60;
}

inline int getHours(uint32_t UNIXTime) {
  return UNIXTime / 3600 % 24;
}
void spiral()
// setPoint() used to draw a spiral across the whole display
{
  int  rmin = 0, rmax = ROW_SIZE - 1;
  int  cmin = 0, cmax = (COL_SIZE * MAX_DEVICES) - 1;

  mx.clear();
  while ((rmax > rmin) && (cmax > cmin))
  {
    // do row
    for (int i = cmin; i <= cmax; i++)
    {
      mx.setPoint(rmin, i, true);
      delay(100 / MAX_DEVICES);
    }
    rmin++;

    // do column
    for (uint8_t i = rmin; i <= rmax; i++)
    {
      mx.setPoint(i, cmax, true);
      delay(100 / MAX_DEVICES);
    }
    cmax--;

    // do row
    for (int i = cmax; i >= cmin; i--)
    {
      mx.setPoint(rmax, i, true);
      delay(100 / MAX_DEVICES);
    }
    rmax--;

    // do column
    for (uint8_t i = rmax; i >= rmin; i--)
    {
      mx.setPoint(i, cmin, true);
      delay(100 / MAX_DEVICES);
    }
    cmin++;
  }
}
void cross()
// Combination of setRow() and setColumn() with user controlled
// display updates to ensure concurrent changes.
{
  mx.clear();
  mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);

  // diagonally down the display R to L
  for (uint8_t i = 0; i < ROW_SIZE; i++)
  {
    for (uint8_t j = 0; j < MAX_DEVICES; j++)
    {
      mx.setColumn(j, i, 0xff);
      mx.setRow(j, i, 0xff);
    }
    mx.update();
    delay(100);
    for (uint8_t j = 0; j < MAX_DEVICES; j++)
    {
      mx.setColumn(j, i, 0x00);
      mx.setRow(j, i, 0x00);
    }
  }

  // moving up the display on the R
  for (int8_t i = ROW_SIZE - 1; i >= 0; i--)
  {
    for (uint8_t j = 0; j < MAX_DEVICES; j++)
    {
      mx.setColumn(j, i, 0xff);
      mx.setRow(j, ROW_SIZE - 1, 0xff);
    }
    mx.update();
    delay(100);
    for (uint8_t j = 0; j < MAX_DEVICES; j++)
    {
      mx.setColumn(j, i, 0x00);
      mx.setRow(j, ROW_SIZE - 1, 0x00);
    }
  }

  // diagonally up the display L to R
  for (uint8_t i = 0; i < ROW_SIZE; i++)
  {
    for (uint8_t j = 0; j < MAX_DEVICES; j++)
    {
      mx.setColumn(j, i, 0xff);
      mx.setRow(j, ROW_SIZE - 1 - i, 0xff);
    }
    mx.update();
    delay(100);
    for (uint8_t j = 0; j < MAX_DEVICES; j++)
    {
      mx.setColumn(j, i, 0x00);
      mx.setRow(j, ROW_SIZE - 1 - i, 0x00);
    }
  }
  mx.control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
}
