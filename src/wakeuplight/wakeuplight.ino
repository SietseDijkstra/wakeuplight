
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include <Adafruit_NeoPixel.h>

// BOARD: LOLIN(WEMOS) D1 R2 & mini

//----------------------------------------------------------------------
// User configuration

#include "localconf.h" // local wifi and ntp settings, comment if not present.

#ifndef LOCAL_CONF

#define WIFI_STA_SSID "---"
#define WIFI_STA_PSK  "---"      

#define NTP_SERVER_NAME "---" // *.pool.ntp.org

#define TIME_ZONE_OFFSET_SEC 0 // 0 = UTC

#define TIME_UP_HOURS 7
#define TIME_UP_MINUTES 0

#define TIME_PRE_UP_MINUTES 10

#define TIME_BED_HOURS 20
#define TIME_BED_MINUTES 0

#define COLOR_OFF             systemScaleColor(systemColor(  0,   0,   0), 100)

#define COLOR_WIFI_CONNECTING systemScaleColor(systemColor(255,   0,   0), 100)
#define COLOR_WIFI_CONNECTED  systemScaleColor(systemColor(  0, 255,   0), 100)

#define COLOR_NTP_REQUESTING  systemScaleColor(systemColor(255,   0, 255), 100)
#define COLOR_NTP_OBTAINED    systemScaleColor(systemColor(  0, 255,   0), 100)

#define COLOR_TIME_BED        systemScaleColor(systemColor(255, 100,   0),  10)
#define COLOR_TIME_PRE_UP     systemScaleColor(systemColor(255,   0,   0),  50)
#define COLOR_TIME_UP         systemScaleColor(systemColor(  0, 255,   0),  50)

#define COLOR_TIME_CHANGING   systemScaleColor(systemColor(255, 255, 255),  50)

#define SYSTEM_PIXEL_ORDER NEO_RGB // or NEO_GRB

#endif // LOCAL_CONF


//----------------------------------------------------------------------
// System variables/consts

enum SystemState
{
  InitialState,
  ConnectingWifiState,
  GettingTimeState,
  RunningState,
};

SystemState systemState = InitialState;

#define SYSTEM_BRIGHTNESS_PERCENTAGE 100

#define SYSTEM_PIXEL_PIN D2
#define SYSTEM_PIXEL_COUNT 1
Adafruit_NeoPixel systemPixel = Adafruit_NeoPixel(SYSTEM_PIXEL_COUNT, SYSTEM_PIXEL_PIN, SYSTEM_PIXEL_ORDER + NEO_KHZ800);


struct SystemColor
{
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

//----------------------------------------------------------------------
// Wifi variables/consts
const char* wifiSsid = WIFI_STA_SSID;
const char* wifiPassphrase = WIFI_STA_PSK;

//----------------------------------------------------------------------
// Ntp variables/consts
#define NTP_LOCAL_BASE_PORT 2390
#define NTP_PACKET_SIZE 48

uint8_t ntpPacketBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
WiFiUDP ntpUdp; // A UDP instance to let us send and receive packets over UDP

//----------------------------------------------------------------------
// Running variables/consts/declarations

enum TimeState
{
  InitialTimeState,
  BedTimeState,
  PreUpTimeState,
  UpTimeState
};

TimeState timeState = InitialTimeState;

const uint32_t timeZoneOffsetSec = TIME_ZONE_OFFSET_SEC;

uint32_t timeNtpEpoch = 0;
uint32_t timeNtpObtainedMillis = 0;

const int32_t timeNtpResyncHours = 12;
bool timeNtpResyncAllowed = false;

// todo: set using packet and store in eeprom
const int32_t timeUpHours = TIME_UP_HOURS;
const int32_t timeUpMinutes = TIME_UP_MINUTES;

const int32_t timePreUpMinutes = TIME_PRE_UP_MINUTES;

const int32_t timeBedHours = TIME_BED_HOURS;
const int32_t timeBedMinutes = TIME_BED_MINUTES;

const int32_t timeCheckIntervalMs = 1000;


//----------------------------------------------------------------------
// main code

void setup(void)
{
  Serial.begin(115200);
  Serial.println("");
  Serial.println("System start");

  testDaylightSavingTime();

  systemPixel.begin();

  // Only run wifi in STA mode. By default is also raises an AP that is unwanted.
  WiFi.mode(WIFI_STA);
}

void loop() {
  switch (systemState)
  {
    case InitialState:
    {
      Serial.println("Change state to ConnectingWifiState");
      systemState = ConnectingWifiState;
      break;
    }
    case ConnectingWifiState:
    {
      if(wifiConnectBlocking())
      {
        wifiAnimateConnected();
        
        Serial.println("Change state to GettingTimeState");
        systemState = GettingTimeState;
      }
      break;
    }
    case GettingTimeState:
    {
      timeNtpEpoch = ntpRequestTimeBlocking(true, 0);
      if (timeNtpEpoch != 0)
      {
        // initialize time  (before doing animation)
        timeNtpObtainedMillis = millis();

        // set power saving as we don't need wifi extensively anymore
        wifi_set_sleep_type(LIGHT_SLEEP_T);

        ntpAnimateObtained();

        Serial.println("Change state to RunningState");
        systemState = RunningState;
      }
      break;      
    }
    case RunningState:
    {
      const uint32_t secsSinceDayStartLocal = timeSecsSinceDayStartLocal();
      timeShow(secsSinceDayStartLocal);

      /*
      static bool test = true;
      if (test)
      {
        timeNtpResyncAllowed = true; // test
        test= false;
      }
      */
      
      timeCheckNtp(secsSinceDayStartLocal);
      
      break;          
    }
  }
}


//----------------------------------------------------------------------
// System

SystemColor systemColor(uint8_t r, uint8_t g, uint8_t b)
{
  return SystemColor {r, g, b};
}

SystemColor systemScaleColor(SystemColor c1, SystemColor c2, uint8_t scalePercentage)
{
  return SystemColor {
    c1.r + ((scalePercentage * ((int16_t)c2.r - (int16_t)c1.r)) / 100),
    c1.g + ((scalePercentage * ((int16_t)c2.g - (int16_t)c1.g)) / 100),
    c1.b + ((scalePercentage * ((int16_t)c2.b - (int16_t)c1.b)) / 100)
  };
}

SystemColor systemScaleColor(SystemColor c, uint8_t scalePercentage)
{
  return systemScaleColor(systemColor(0, 0, 0), c, scalePercentage);
}

void systemAnimateColor(SystemColor c1, SystemColor c2)
{
  Serial.println("Time: Animate color");

  const int16_t stepCount = 50;
  const int16_t stepDurationMs = 20;

  for (int16_t i = 0; i < stepCount; i++)
  {
    systemPixelSetColor(systemScaleColor(c1, c2, (100 * i) / stepCount));
    delay(stepDurationMs);
  }
  
  systemPixelSetColor(c2);
}

void systemAnimateColor(SystemColor c1, SystemColor c2, SystemColor c3)
{
  systemAnimateColor(c1, c2);
  systemAnimateColor(c2, c3);
}

void systemPixelSetColor(SystemColor c)
{
  c = systemScaleColor(c, SYSTEM_BRIGHTNESS_PERCENTAGE);
  
  for(int32_t i = 0; i < SYSTEM_PIXEL_COUNT; i++)
  {
    systemPixel.setPixelColor(i, systemPixel.Color(c.r, c.g, c.b));
    systemPixel.show();
  }  

/*
  Serial.print("System: pixel color ");
  Serial.print(c.r);
  Serial.print(" ");
  Serial.print(c.g);
  Serial.print(" ");
  Serial.println(c.b);
  */
}


//----------------------------------------------------------------------
// Time

uint32_t timeCurrentEpoch()
{
  uint32_t currentMillis = millis();
  if (timeNtpObtainedMillis <= currentMillis)
  {
    return timeNtpEpoch + ((currentMillis - timeNtpObtainedMillis) / 1000);
  }
  else
  {
    // overflow
    // handle millis rollover better?

    Serial.print("Time: overflow");

    Serial.print("Time: ntp obtained: ");
    Serial.println(timeNtpObtainedMillis);
    Serial.print("Time: current obtained: ");
    Serial.println(currentMillis);

    ESP.restart();

    return 0;
  }
}

void timePrintTime(uint32_t secsSinceDayStart)
{
  Serial.print("Time: ");
  
  Serial.print(secsSinceDayStart / 3600);
  Serial.print(':');
  if (((secsSinceDayStart % 3600) / 60) < 10) {
    Serial.print('0');
  }
  Serial.print((secsSinceDayStart  % 3600) / 60);
  Serial.print(':');
  if ((secsSinceDayStart % 60) < 10) {
    Serial.print('0');
  }
  Serial.println(secsSinceDayStart % 60); 
}

#define MARCH 2
#define OCTOBER 9
#define ONE_HOUR 3600

int dayOfWeek(int d, int m, int y) 
{ 
    static int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 }; 
    y -= m < 3; 
    return ( y + y/4 - y/100 + y/400 + t[m-1] + d) % 7; 
} 

uint32_t timeEuDst(uint32_t epoch)
{
  struct tm *timeInfo = gmtime((time_t*)&epoch);

  /*
  char buf[10];
  sprintf(buf, "%d-%d-%d", timeInfo->tm_year + 1900, timeInfo->tm_mon + 1, timeInfo->tm_mday );
  Serial.print("Time: gmdate: ");
  Serial.println(buf);
  
  sprintf(buf, "%02d:%02d", timeInfo->tm_hour, timeInfo->tm_min);
  Serial.print("Time: gmtime: ");
  Serial.println(buf);
  */
 
  uint16_t year = timeInfo->tm_year + 1900;
  uint8_t month = timeInfo->tm_mon;
  uint8_t weekDay = timeInfo->tm_wday;
  uint8_t monthDay = timeInfo->tm_mday - 1;
  uint8_t hour = timeInfo->tm_hour;

  if ((month > MARCH) && (month < OCTOBER))
    return ONE_HOUR;
  if (month < MARCH)
    return 0;
  if (month > OCTOBER)
    return 0;

  uint8_t firstWeekDayOfMonth = dayOfWeek(1, month + 1, year);
  
  /*
  Serial.print("Time: gmtime: ");
  Serial.print(" firstWeekDayOfMonth: ");
  Serial.println(firstWeekDayOfMonth);
  */
  
  // determine monthDay of last Sunday
  uint8_t finalSundayMonthDay = 0;
  for (uint8_t d = 0; d < 31; d++)
  {
    if (((firstWeekDayOfMonth + d) % 7) == 0)
    {
      // sunday
      if (d > finalSundayMonthDay)
        finalSundayMonthDay = d;
    }
  }

  if (month == MARCH) 
  {
    /*
    Serial.print("Time: gmtime: March: ");
    Serial.print(" finalSundayMonthDay: ");
    Serial.print(finalSundayMonthDay);
    Serial.print(" monthDay: ");
    Serial.print(monthDay);
    Serial.print(" hour: ");
    Serial.println(hour);
    */
    
    if (monthDay < finalSundayMonthDay)
      return 0;
    if (monthDay > finalSundayMonthDay)
      return ONE_HOUR;
    if (hour < 2)
      return 0;
      
    return ONE_HOUR;
  }

  /*
  Serial.print("Time: gmtime: October: ");
  Serial.print(" finalSundayMonthDay: ");
  Serial.print(finalSundayMonthDay);
  Serial.print(" monthDay: ");
  Serial.print(monthDay);
  Serial.print(" hour: ");
  Serial.println(hour);
  */
  
  if (monthDay < finalSundayMonthDay)
    return ONE_HOUR;
  if (monthDay > finalSundayMonthDay)
    return 0;
  if (hour < 2)
    return ONE_HOUR;

  return 0;
}

uint32_t timeSecsSinceDayStartLocal()
{
   //Serial.println("Time: Show");

  // This code assumes wake up is before bed time and all times are in the same day.

  uint32_t epoch = timeCurrentEpoch();
  uint32_t dstOffsetSec = timeEuDst(epoch + timeZoneOffsetSec);

  uint32_t epochLocal = epoch + timeZoneOffsetSec + dstOffsetSec;

  /*
  Serial.print("Time: epoch: ");
  Serial.print(epoch);
  Serial.print(" time zone offset : ");
  Serial.print(timeZoneOffsetSec);
  Serial.print(" dst offset : ");
  Serial.print(dstOffsetSec);
  Serial.print(" epoch local: ");
  Serial.println(epochLocal);
  */
  
  uint32_t secsSinceDayStartLocal = epochLocal % 86400L; //(86400 equals secs per day);

  timePrintTime(secsSinceDayStartLocal);

  return secsSinceDayStartLocal;
}

void timeShow(uint32_t secsSinceDayStartLocal)
{
  //Serial.println("Time: Show");

  const int32_t upTimeSecs = (timeUpHours * 3600) + (timeUpMinutes * 60);
  const int32_t preUpTimeSecs = upTimeSecs - (timePreUpMinutes * 60);
  const int32_t bedTimeSecs = (timeBedHours * 3600) + (timeBedMinutes * 60);
  
  if (secsSinceDayStartLocal < preUpTimeSecs)
  {
    if (timeState != BedTimeState)
    {
      Serial.println("Time: Bed time");

      if (timeState == InitialTimeState)
        systemAnimateColor(COLOR_OFF, COLOR_TIME_CHANGING, COLOR_TIME_BED);
      else
        systemPixelSetColor(COLOR_TIME_BED);
        
      timeState = BedTimeState;
    }
  }
  else if ((secsSinceDayStartLocal >= preUpTimeSecs) && (secsSinceDayStartLocal < upTimeSecs))
  {
    uint8_t transitionPercentage = (100 * (secsSinceDayStartLocal - preUpTimeSecs)) / (timePreUpMinutes * 60);
    SystemColor currentColor = systemScaleColor(COLOR_TIME_BED, COLOR_TIME_PRE_UP, transitionPercentage);
    
    if (timeState != PreUpTimeState)
    {
      Serial.println("Time: Pre up time");

      if (timeState == InitialTimeState)
        systemAnimateColor(COLOR_OFF, currentColor);

      timeState = PreUpTimeState;
    }

    systemPixelSetColor(currentColor);
  }
  else if ((secsSinceDayStartLocal >= upTimeSecs) && (secsSinceDayStartLocal < bedTimeSecs))
  {
    if (timeState != UpTimeState)
    {
      Serial.println("Time: Up time");

      if (timeState == InitialTimeState)
        systemAnimateColor(COLOR_OFF, COLOR_TIME_CHANGING, COLOR_TIME_UP);
      else
        systemAnimateColor(COLOR_TIME_PRE_UP, COLOR_TIME_CHANGING, COLOR_TIME_UP);
        
      timeState = UpTimeState;
    }
  }
  else if (secsSinceDayStartLocal >= bedTimeSecs)
  {
    if (timeState != BedTimeState)
    {
      Serial.println("Time: Bed time");

      if (timeState == InitialTimeState)
        systemAnimateColor(COLOR_OFF, COLOR_TIME_CHANGING, COLOR_TIME_BED);
      else
        systemAnimateColor(COLOR_TIME_UP, COLOR_TIME_CHANGING, COLOR_TIME_BED);
        
      timeState = BedTimeState;
    }
  }
  else
  {
    Serial.println("Time: Unknown time status");
  }

  delay(timeCheckIntervalMs);
}

void timeCheckNtp(uint32_t secsSinceDayStartLocal)
{
  //Serial.println("Time: Check Ntp");

  const int32_t ntpResyncTimeSecs = timeNtpResyncHours * 3600;
    
  if ((secsSinceDayStartLocal >= ntpResyncTimeSecs) && timeNtpResyncAllowed)
  {
    Serial.println("Time: Ntp resync");
    
    // try without displaying for some time
    uint32_t ntpEpoch = ntpRequestTimeBlocking(false, 60000); 
    
    if (ntpEpoch != 0)
    {
      // ntp sync success
      timeNtpEpoch = ntpEpoch;
      timeNtpObtainedMillis = millis();
    }
    else
    {
      // fail, what to do? 
      ESP.restart();
    }

    timeNtpResyncAllowed = false;
  }

  if ((secsSinceDayStartLocal < ntpResyncTimeSecs) && !timeNtpResyncAllowed)
  {
    Serial.println("Time: Ntp resync allowed");    
    timeNtpResyncAllowed = true;
  }
}

void testDaylightSavingTime()
{
  // Date and time (GMT): Saturday, March 28, 2020 7:00:00 AM
  // 1585378800
  if (timeEuDst(1585378800) != 0)
    Serial.println("Time: test 1 failed");    

  // Date and time (GMT): Sunday, March 29, 2020 7:00:00 AM
  // 1585465200
  if (timeEuDst(1585465200) != ONE_HOUR)
    Serial.println("Time: test 2 failed");    

  // Date and time (GMT): Monday, March 30, 2020 7:00:00 AM
  // Epoch timestamp: 1585551600
  if (timeEuDst(1585551600) != ONE_HOUR)
    Serial.println("Time: test 3 failed");    

  // Date and time (GMT): Saturday, October 24, 2020 7:00:00 AM
  // 1603522800
  if (timeEuDst(1603522800) != ONE_HOUR)
    Serial.println("Time: test 4 failed");    
  
  // Date and time (GMT): Sunday, October 25, 2020 7:00:00 AM
  // 1603609200
  if (timeEuDst(1603609200) != 0)
    Serial.println("Time: test 5 failed");    


  // 2021

  // Epoch timestamp: 1616828400
  // Date and time (GMT): Saturday, March 27, 2021 7:00:00 AM
  if (timeEuDst(1616828400) != 0)
    Serial.println("Time: test 6 failed");    

  // Epoch timestamp: 1616914800
  // Date and time (GMT): Sunday, March 28, 2021 7:00:00 AM
  if (timeEuDst(1616914800) != ONE_HOUR)
    Serial.println("Time: test 7 failed");    

  // Epoch timestamp: 1617001200
  // Date and time (GMT): Monday, March 29, 2021 7:00:00 AM
  if (timeEuDst(1617001200) != ONE_HOUR)
    Serial.println("Time: test 8 failed");    

  // Epoch timestamp: 1635577200
  // Date and time (GMT): Saturday, October 30, 2021 7:00:00 AM
  if (timeEuDst(1635577200) != ONE_HOUR)
    Serial.println("Time: test 9 failed");    

  // Epoch timestamp: 1635663600
  // Date and time (GMT): Sunday, October 31, 2021 7:00:00 AM
  if (timeEuDst(1635663600) != 0)
    Serial.println("Time: test 10 failed");    


  // 2022

  // Epoch timestamp: 1648278000
  // Date and time (GMT): Saturday, March 26, 2022 7:00:00 AM
  if (timeEuDst(1616828400) != 0)
    Serial.println("Time: test 11 failed");    

  // Epoch timestamp: 1648364400
  // Date and time (GMT): Sunday, March 27, 2022 7:00:00 AM
  if (timeEuDst(1616914800) != ONE_HOUR)
    Serial.println("Time: test 12 failed");    

  // Epoch timestamp: 1648450800
  // Date and time (GMT): Monday, March 28, 2022 7:00:00 AM
  if (timeEuDst(1617001200) != ONE_HOUR)
    Serial.println("Time: test 13 failed");    

  // Epoch timestamp: 1667026800
  // Date and time (GMT): Saturday, October 29, 2022 7:00:00 AMAM
  if (timeEuDst(1635577200) != ONE_HOUR)
    Serial.println("Time: test 14 failed");    

  // Epoch timestamp: 1667113200
  // Date and time (GMT): Sunday, October 30, 2022 7:00:00 AM
  if (timeEuDst(1635663600) != 0)
    Serial.println("Time: test 15 failed");    


    
}

//----------------------------------------------------------------------
// Wifi

void wifiAnimateConnecting()
{
  Serial.println("Wifi: Animate connecting");

  systemAnimateColor(COLOR_OFF, COLOR_WIFI_CONNECTING, COLOR_OFF);
}

void wifiAnimateConnected()
{
  Serial.println("Wifi: Animate connected");

  systemAnimateColor(COLOR_OFF, COLOR_WIFI_CONNECTED, COLOR_OFF);
}

bool wifiConnectBlocking()
{
  Serial.println("Wifi: Connecting");
  
  WiFi.begin(wifiSsid, wifiPassphrase);

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    wifiAnimateConnecting();
  }

  Serial.print("Wifi: Connected to ");
  Serial.println(wifiSsid);
  Serial.print("Wifi: IP address: ");
  Serial.println(WiFi.localIP());  

  return true;
}

//----------------------------------------------------------------------
// NTP

void ntpSendPacket(IPAddress& ntpServerIp) 
{
  Serial.println("Ntp: Send packet");

  memset(ntpPacketBuffer, 0, NTP_PACKET_SIZE);

  // prepare request
  ntpPacketBuffer[0] = 0b11100011;  // LI, version, mode
  ntpPacketBuffer[1] = 0;           // Stratum, or type of clock
  ntpPacketBuffer[2] = 6;           // Polling interval
  ntpPacketBuffer[3] = 0xEC;        // Peer clock precision
  
  ntpPacketBuffer[12] = 49;
  ntpPacketBuffer[13] = 0x4E;
  ntpPacketBuffer[14] = 49;
  ntpPacketBuffer[15] = 52;

  // send packet
  ntpUdp.beginPacket(ntpServerIp, 123);
  ntpUdp.write(ntpPacketBuffer, NTP_PACKET_SIZE);
  ntpUdp.endPacket();
}

void ntpAnimateRequesting(int32_t timeMs)
{
  Serial.print("Ntp: Animate requesting ");
  Serial.println(timeMs);

  systemAnimateColor(COLOR_OFF, COLOR_NTP_REQUESTING, COLOR_OFF);
}

void ntpAnimateObtained()
{
  Serial.println("Ntp: Animate obtained ");

  systemAnimateColor(COLOR_OFF, COLOR_NTP_OBTAINED, COLOR_OFF);
}

void ntpPrintTime(uint32_t epoch)
{
  // convert epoch in day time:
  Serial.print("Ntp: Epoch ");
  Serial.println(epoch);
  
  Serial.print("Ntp: Time ");
  Serial.print((epoch  % 86400L) / 3600);
  Serial.print(':');
  if (((epoch % 3600) / 60) < 10) {
    Serial.print('0');
  }
  Serial.print((epoch  % 3600) / 60);
  Serial.print(':');
  if ((epoch % 60) < 10) {
    Serial.print('0');
  }
  Serial.println(epoch % 60);
}

uint32_t ntpRequestTimeBlocking(const bool animate, const uint32_t maxTimeMs)
{
  const uint16_t port = NTP_LOCAL_BASE_PORT + (millis() % 100); // somewhat random port in case multiple devices are used behind nat
  ntpUdp.begin(port);
  Serial.print("Ntp: Local port: ");
  Serial.println(ntpUdp.localPort());
  
  Serial.println("Ntp: Request time");

  uint32_t waitTimeMs = 0;
  uint32_t elapsedMs = 0;
  bool done = false;
  
  while (!done)
  {
    IPAddress ntpServerIp;
    WiFi.hostByName(NTP_SERVER_NAME, ntpServerIp);
    ntpSendPacket(ntpServerIp);

    waitTimeMs = 1000;
    if (animate)
      ntpAnimateRequesting(waitTimeMs);
    else
      delay(waitTimeMs);
    elapsedMs += waitTimeMs;

    int32_t cb = ntpUdp.parsePacket();
    if (cb) 
    {
      Serial.print("Ntp: Packet received, length=");
      Serial.println(cb);

      ntpUdp.read(ntpPacketBuffer, NTP_PACKET_SIZE);
  
      // The timestamp starts at byte 40 of the received packet and is four bytes, or two words, long. 
      // Extract the two words:


      // timestamp starts at byte 20, get two words and combine into 32b
      uint16_t highWord = word(ntpPacketBuffer[40], ntpPacketBuffer[41]);
      uint16_t lowWord = word(ntpPacketBuffer[42], ntpPacketBuffer[43]);
      uint32_t secsSince1900 = highWord << 16 | lowWord;

      // now convert ntp time (seconds since Jan 1 1900) into everyday time:
      // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
      const uint32_t seventyYears = 2208988800UL;
      uint32_t epoch = secsSince1900 - seventyYears;
      
      ntpPrintTime(epoch);

      return epoch;
    }
    else
    {
      waitTimeMs = 5000;
      if (animate)
        ntpAnimateRequesting(waitTimeMs);
      else
        delay(waitTimeMs);
      elapsedMs += waitTimeMs;
    }

    done = (maxTimeMs > 0) && (elapsedMs > maxTimeMs);
  }

  return 0;
}


