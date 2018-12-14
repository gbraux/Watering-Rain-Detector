/*
  Arrosage by Guillaume BRAUX - 08/2017
*/

// ------------ INCLUDES ------------
#include <Arduino.h>
#include <math.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Time.h>
#include <ESP8266HTTPClient.h>
#include "TimeLib.h"
#include "NTPClient.h"
#include "Timezone.h"
#include "JsonStreamingParser.h"
#include "MeteoParser.h"
#include "HTTPTime.h"

extern "C" {
#include "user_interface.h"
}

ADC_MODE(ADC_VCC);

// -------------- CONFIG -------------
// -> WIFI
const char ssid[] = "GCC";       //  your network SSID (name)
const char pass[] = "sianna123"; // your network password

// -> Données d'arrosage
const int startTimeHour = 18;
const int startTimeMin = 45;
const int wateringDurationMin = 45;
const int Rainthreshold = 60;
const int rainCheckHoursForward = 24;
// USED BY METEO FRANCE API - NOT NEADED WITH WU
//const int slotCount = ceil(rainCheckHoursForward / 3);
const int slotCount = 24;
int maxRainValueDetected = -1;

// -> GPIO / Sleep
const int  standardDeepSleepTs = 3600;
#define GP_OUT 2

// -> Météo France API
#define INSEE_CODE 78171
//#define MF_API_URL "http://www.meteo-france.mobi/ws/getDetail/france/781710.json"
#define MF_API_URL "http://api.wunderground.com/api/94f6aa88e6ea50ec/hourly/lang:FR/q/France/adainville.json"

// -> NTP
const unsigned int localPort = 2390; // local port to listen for UDP packets
const char *ntpServerName = "fr.pool.ntp.org";
IPAddress timeServerIP;        // time.nist.gov NTP server address
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
WiFiUDP udp;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// -> RTC Memory Memory
#define RTCMEMORYSTART 68
#define RTCMEMORYLEN 128
typedef struct {
  uint8_t isLoopingOff; // 1 byte
  uint8_t remainingSteps; // 1 byte
  uint8_t signature; // 1 byte
  uint8_t padding; // 1 byte
} rtcStore;
rtcStore rtcMem;

typedef struct {
  uint8_t isLongSleepMode; // 1 byte
  uint8_t remainingLoops; // 1 byte
  uint8_t signature; // 1 byte
  uint8_t padding; // 1 byte
} rtcStore2;
rtcStore2 rtcMem2;

// -> TimzZone Config
TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120}; //Central European Summer Time
TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};   //Central European Standard Time
Timezone myTZ(CEST, CET);

// -> Structs for Time & Diff store
struct HMS_TIME
{
  int seconds = 0;
  int minutes;
  int hours;
};
struct HMS_TIME startTime, stopTime, currentTime, diff;

void setup()
{
  // initialize digital pin GP_OUT as an output.
  pinMode(GP_OUT, OUTPUT);

  // initialize Serial
  Serial.begin(115200); // set up Serial library at 9600 bps
}


void loop()
{

  Serial.println(">>>>> Hello ! I am Rain Detector <<<<<");
  Serial.println("       By Guillaume BRAUX - 2017      ");


  // Checking if we are in a long DeepSleep Mode (+1H)
  Serial.println("Checking RTC Memory Content for long DeepSleep Mode ...");
  system_rtc_mem_read(RTCMEMORYSTART+4, &rtcMem2, sizeof(rtcMem2));
  Serial.println("signature (should be \"64\") = " + (String)rtcMem2.signature);
  Serial.println("isLongSleepMode = " + (String)rtcMem2.isLongSleepMode);
  Serial.println("remainingSteps = " + (String)rtcMem2.remainingLoops);

  if (rtcMem2.isLongSleepMode == 1 && rtcMem2.signature == 64) {

    if (rtcMem2.remainingLoops > 1) {
      Serial.println("Deepsleep prolongé detecté. On redors " + (String)rtcMem2.remainingLoops + " fois.");
      rtcMem2.remainingLoops = rtcMem2.remainingLoops - 1;
      system_rtc_mem_write(RTCMEMORYSTART+4, &rtcMem2, 4); 
      ESP.deepSleep( standardDeepSleepTs * 1000000,WAKE_RF_DISABLED);
    }
    else if (rtcMem2.remainingLoops = 1) {
      Serial.println("Dernier deepsleep prolongé");
      rtcMem2.isLongSleepMode = 0;
      rtcMem2.remainingLoops = 0;
      system_rtc_mem_write(RTCMEMORYSTART+4, &rtcMem2, 4); 
      ESP.deepSleep( standardDeepSleepTs * 1000000);
    }
  }


  // Checking if we are sending OFF State (in RTC RAM) - and number of steps remaining
  Serial.println("Checking RTC Memory Content ...");
  system_rtc_mem_read(RTCMEMORYSTART, &rtcMem, sizeof(rtcMem));
  Serial.println("signature (should be \"128\") = " + (String)rtcMem.signature);
  Serial.println("isLoopingOff = " + (String)rtcMem.isLoopingOff);
  Serial.println("remainingLoops = " + (String)rtcMem.remainingSteps);

  if (rtcMem.isLoopingOff == 1 && rtcMem.signature == 128) {
    sendOFF(rtcMem.remainingSteps, true);
  }
  // ----------------------------------------------------------------------------

  // On connecte le wifi
  connectWifi();

  // On change le mode de someil (light sleep = pendant les delays, le modem wifi se coupe
  wifi_set_sleep_type(LIGHT_SLEEP_T);


  int epochVal;
  int gotTime = 0;

  int i = 0;
  while (!gotTime)
  {
    epochVal = GetHTTPTime();

    if (epochVal != 0)
    {
      gotTime = 1;
    }
    else
    {
      if (i > 20) {
        Serial.println("Erreur/lenteur NTP apres 20 tentatives. On DeepSleep et on reessaiera plus tard ...");
        ESP.deepSleep( standardDeepSleepTs * 1000000 );
      }
      
      if (!(i % 5) && (i != 0)) {
        Serial.println("Erreur de connection au NTP. On vérifie le wifi et on retente");
        connectWifi();
      }
      else {
        Serial.println("Erreur de connection au NTP. On retente 5 fois sans faire de reinit du wifi");
      }

      i = i+1;
    }
  }

  time_t epochValTZ = myTZ.toLocal(epochVal);

  /*
  String strNtpUtc = "La valeur du NTP UTC est : ";
  Serial.println(strNtpUtc + epochVal);

  

  String strNtpFr = "La valeur du NTP (TZ France) est : ";
  Serial.println(strNtpFr + epochValTZ);
  */

  Serial.println("Il est, en heure locale Francaise : ");
  printTimeFromEpoch(epochValTZ);

  startTime.hours = startTimeHour;
  startTime.minutes = startTimeMin;
 
  currentTime.hours = (epochValTZ % 86400L) / 3600;
  currentTime.minutes = (epochValTZ % 3600) / 60;

  differenceBetweenTimePeriod(startTime, currentTime, &diff);
  Serial.println("Le watering à venir est dans " + (String)diff.hours + " heures et " + (String)diff.minutes + " minutes.");


  // Si le watering est prévu d'ici 10 à 60 minutes, on redors le temps nécessaire pour se réveiller 3 min avant
  if (diff.hours == 0 && (diff.minutes > 10)) {
    Serial.println("Watering d'ici 10 à 60 minutes. On met en DeepSleep le temps d'atteindre environ -5 minutes");
    int sleepTime = (diff.minutes - 5) * 60;
    Serial.println("On se prépare à dormir pendant " + (String)(sleepTime / 60) + " minutes");
    ESP.deepSleep( sleepTime * 1000000 );
  } 

  // Watering prévu dans moins de 10 min. On calcule le temps de off, et si risque de pluie on lance la boucle.
  if ((diff.hours == 0) && ((diff.minutes <= 10) && (diff.minutes >= wateringDurationMin * -1))) {
    Serial.println("Watering dans moins de 10 minutes ou en cours. On vérifie le risque de pluie pour les prochaines 24h");
    int willRain = getRainProb();
    Serial.println("RISQUE DE PLUIE > "+(String)Rainthreshold + "% DANS LES 24 PROCHAINES HEURES : " + (String)willRain);

    // DEBUG ---
    //willRain = 1;
    // ---------
    
    if (willRain){
      int wateringTime = (wateringDurationMin + diff.minutes + 3) * 60;
      Serial.println("Temps de blocage de l'arrosage (+ 3 minutes bonus) : " + (String) (wateringDurationMin + diff.minutes + 3) + " minutes");
      SendStatusToCloud(willRain);
      sendOFF(wateringTime);
    }
    else {
      Serial.println("Il ne pleuvera pas = DODO !");
      SendStatusToCloud(willRain);
      ESP.deepSleep( standardDeepSleepTs * 1000000 );
    }
  } 

  // Si le watering est dans + d'1h, on dors 1h de plus.
  // + on configure le # d'heure à dormir dans l'epprom si > 1


  
  int remainingHours;

  if (diff.hours < 0 || diff.minutes < 0){
    remainingHours = 24 - abs(diff.hours) - 1;
  }
  else {
    remainingHours = abs(diff.hours);
  }

  Serial.println("Watering dans + d'1h (" + (String)remainingHours + "h), on met en DeepSleep");

  if (remainingHours > 1) {
    Serial.println("Deepsleep prolongé (remainingHours > 2h)");
    // Writing State to RTC Memory
    rtcMem2.isLongSleepMode = 1; // 1 byte
    //rtcMem2.remainingLoops = remainingHours - 1; // 1 byte
    rtcMem2.remainingLoops = remainingHours - 1; // 1 byte
    rtcMem2.signature = 64; // 1 byte
    system_rtc_mem_write(RTCMEMORYSTART+4, &rtcMem2, 4);

    ESP.deepSleep( standardDeepSleepTs * 1000000,WAKE_RF_DISABLED);
  }
  else {
    //Deepsleep simple (1h > remainingHours < 2h). On dort pour se reveiller à -1h
    rtcMem2.isLongSleepMode = 0; // 1 byte
    rtcMem2.remainingLoops = 0; // 1 byte
    rtcMem2.signature = 64; // 1 byte
    system_rtc_mem_write(RTCMEMORYSTART+4, &rtcMem2, 4);

    int remainingMinutes2 = diff.minutes;
    if (remainingMinutes2 <= 2) {remainingMinutes2 = 2;}

    Serial.println("Deepsleep simple (1h > remainingHours < 2h). On dort pour se reveiller à -1h ("+(String)remainingMinutes2 + " minutes)");

    ESP.deepSleep( remainingMinutes2 * 60 * 1000000 );
  }



}

void sendON(int duration)
{
  Serial.println("Start Send On");
  for (int i = 0; i < duration / 30; i++)
  {
    Serial.println("Loop ON Start #" + i);
    digitalWrite(GP_OUT, LOW);
    digitalWrite(GP_OUT, HIGH);
    delay(100);
    digitalWrite(GP_OUT, LOW);
    delay(29000);
    Serial.println("Loop ON End");
  }
}

void sendOFF(int remainingSteps, bool isAfterSleep)
{
  if (!isAfterSleep) {
    Serial.println("Start Send Off");
    for (int j = 0; j <= 10; j++)
    {
      digitalWrite(GP_OUT, HIGH); // turn the LED on (HIGH is the voltage level)
      delay(100);                 // wait for a second
      digitalWrite(GP_OUT, LOW);  // turn the LED off by making the voltage LOW
      delay(100);                 // wait for a second
    }
  }
  
  //for (int i = 0; i < remainingSteps; i++)
  if (remainingSteps > 0)
  {
    Serial.println("Loop Keep OFF");
    digitalWrite(GP_OUT, LOW);
    digitalWrite(GP_OUT, HIGH);
    delay(100);
    digitalWrite(GP_OUT, LOW);

    // Writing State to RTC Memory
    rtcMem.isLoopingOff = 1;
    rtcMem.remainingSteps = remainingSteps - 1;
    rtcMem.signature = 128;
    system_rtc_mem_write(RTCMEMORYSTART, &rtcMem, 4);

    Serial.println("Before Delay 29000");
    
    // Demain DeepSleep29
    ESP.deepSleep( 29 * 1000000,WAKE_RF_DISABLED);
  }
  Serial.println("Before Delay 35000 (LAST)");
  Serial.println("Loop Keep OFF Last");
  digitalWrite(GP_OUT, LOW);
  digitalWrite(GP_OUT, HIGH);
  delay(100);
  digitalWrite(GP_OUT, LOW);

  rtcMem.isLoopingOff = 0;
  system_rtc_mem_write(RTCMEMORYSTART, &rtcMem, 4);

  ESP.deepSleep(  35 * 1000000,WAKE_RF_DEFAULT);
}

void sendOFF(int duration)
{
  sendOFF(ceil((duration / 30) - 1), false);
}

void connectWifi() {

  int j = 0;
  int connected = 0;

  while(!connected) {

  // We start by connecting to a WiFi network
    WiFi.disconnect();
    delay(500);

    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);

    int i = 0;
    int brk = 0;
    while (WiFi.status() != WL_CONNECTED || brk)
    {
      delay(1000);
      Serial.print(".");
      if (i > 20) {
        Serial.println("Erreur/lenteur de connexion au wifi. On reessaie");
        brk = 1;
      }
      i = i+1;
    }

    if (!brk) {
      Serial.println("");
      Serial.println("WiFi connected");
      Serial.println("IP address: ");
      Serial.println(WiFi.localIP());
      //delay(1000);
      connected = 1;
    }
    else if (j > 5) {
      Serial.println("Décidement, ca va pas pour le wifi apres 5 tentatives de déco/reco. Dodo 1h.");
      ESP.deepSleep( standardDeepSleepTs * 1000000 );
    }
    j = j + 1;
  }
}

// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress &address)
{
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011; // LI, Version, Mode
  packetBuffer[1] = 0;          // Stratum, or type of clock
  packetBuffer[2] = 6;          // Polling Interval
  packetBuffer[3] = 0xEC;       // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

void printTimeFromEpoch(int epoch)
{
  // print the hour, minute and second:
  Serial.print("The local time (FRANCE) is "); // UTC is the time at Greenwich Meridian (GMT)
  Serial.print((epoch % 86400L) / 3600);       // print the hour (86400 equals secs per day)
  Serial.print(':');
  if (((epoch % 3600) / 60) < 10)
  {
    // In the first 10 minutes of each hour, we'll want a leading '0'
    Serial.print('0');
  }
  Serial.print((epoch % 3600) / 60); // print the minute (3600 equals secs per minute)
  Serial.print(':');
  if ((epoch % 60) < 10)
  {
    // In the first 10 seconds of each minute, we'll want a leading '0'
    Serial.print('0');
  }
  Serial.println(epoch % 60); // print the second
}

void differenceBetweenTimePeriod(struct HMS_TIME start, struct HMS_TIME stop, struct HMS_TIME *diff)
{
  if (stop.seconds > start.seconds)
  {
    --start.minutes;
    start.seconds += 60;
  }

  diff->seconds = start.seconds - stop.seconds;
  if (stop.minutes > start.minutes)
  {
    --start.hours;
    start.minutes += 60;
  }

  diff->minutes = start.minutes - stop.minutes;
  
  diff->hours = start.hours - stop.hours;

  if (diff->hours < 0) {
    diff->hours = diff->hours + 1;
    diff->minutes = (60 - diff->minutes) * (-1);
  }
}

time_t getNtpTime()
{
  // On prépare UDP pour la requette
  Serial.println("Starting UDP");
  udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());

  while (udp.parsePacket() > 0)
    ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  // get a random server from the pool
  WiFi.hostByName(ntpServerName, timeServerIP);
  Serial.print(timeServerIP);
  Serial.print(": ");
  Serial.println(timeServerIP);
  sendNTPpacket(timeServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 700)
  {
    int size = udp.parsePacket();
    if (size >= NTP_PACKET_SIZE)
    {
      Serial.println("Receive NTP Response");
      udp.read(packetBuffer, NTP_PACKET_SIZE); // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 = (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

int getRainProb()
{
  int currentProbaPluie[24] = {0};

  int sendOK = 0;
  int nbError = 0;
  while (!sendOK)
  {
    //connectWifi();
    Serial.println("Start sending HTTP GET request to Meteo France");
    HTTPClient http;
    http.begin(MF_API_URL); //HTTP

    // start connection and send HTTP header
    int httpCode = http.GET();

    // httpCode will be negative on error
    if (httpCode > 0)
    {
      // HTTP header has been send and Server response header has been handled
      Serial.println("[HTTP] GET got an answer. HTTP code bellow : ");
      Serial.println(httpCode);
      Serial.println("Answer Size");
      Serial.println(http.getSize());

      // file found at server
      if (httpCode == HTTP_CODE_OK)
      {
        // get lenght of document (is -1 when Server sends no Content-Length header)
        int len = http.getSize();

        // create buffer for read
        uint8_t buff[128] = {0};

        // get tcp stream
        WiFiClient *stream = http.getStreamPtr();

        JsonStreamingParser parser;
        MeteoListener listener;
        parser.setListener(&listener);

        // read all data from server
        while (http.connected() && (len > 0 || len == -1))
        {
          // get available data size
          size_t size = stream->available();

          if (size)
          {
            // read up to 128 byte
            int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
            
            for (int i = 0; i < c; i++) {
              parser.parse((char)buff[i]);
            }  

            if (len > 0)
            {
              len -= c;
            }
          }
        }
          memcpy(currentProbaPluie,listener.probaPluies,24*sizeof(int));
          delay(1);

          http.end();

          Serial.println(system_get_free_heap_size());

          Serial.println("Progabilité de pluie : ");
          for (int i = 0; i<24;i++)
          {
            int y = i+1;
            Serial.println("--> +" + (String)y + "h : " + (String)currentProbaPluie[i] + "%");
          }
//          Serial.println("--> +3h : " + (String)currentProbaPluie[0] + "%");
//          Serial.println("--> +6h : " + (String)currentProbaPluie[1] + "%");
//          Serial.println("--> +9h : " + (String)currentProbaPluie[2] + "%");
//          Serial.println("--> +12h : " + (String)currentProbaPluie[3] + "%");
//          Serial.println("--> +15h : " + (String)currentProbaPluie[4] + "%");
//          Serial.println("--> +18h : " + (String)currentProbaPluie[5] + "%");
//          Serial.println("--> +21h : " + (String)currentProbaPluie[6] + "%");
//          Serial.println("--> +24h : " + (String)currentProbaPluie[7] + "%");

          int yesRain = 0;
          maxRainValueDetected = 0;
          for (int i = 0; i < slotCount; i++) {
            if (currentProbaPluie[i] >= Rainthreshold) {
              yesRain = 1;
            }
            if (currentProbaPluie[i] > maxRainValueDetected) {
              maxRainValueDetected = currentProbaPluie[i];
            }
          }
        
        return yesRain;
      
        }
        else {
          Serial.println("HTTP HEADER NOT 200 AS EXPECTED ...");
          http.end();
          Serial.println("Erreur/lenteur a l'acces au site Meteo France. On reessaie 10 fois");
          nbError = nbError + 1;
        }
      }
    else
    {
      Serial.println("[HTTP] GET... failed. Error Message bellow : ");
      Serial.println(http.errorToString(httpCode).c_str());
      http.end();

      //Serial.println("Erreur/lenteur a l'acces au site Meteo France. On DeepSleep et on reessaiera plus tard ...");
      Serial.println("Erreur/lenteur a l'acces au site Meteo France. On reessaie 10 fois");
      //ESP.deepSleep( standardDeepSleepTs * 1000000 );
      
      nbError = nbError + 1;
    }

    if (nbError > 10) {
      Serial.println("Trop d'erreurs, on deepsleep");
      ESP.deepSleep( standardDeepSleepTs * 1000000 );
    }
  }
}

void SendStatusToCloud(int willRain) {
      
  
  float vcc = (float)ESP.getVcc()/1024.0;
  HTTPClient http;

  int sendOK = 0;
  int i = 0;
  while (!sendOK)
  {
    //connectWifi();
    http.begin("http://api.thingspeak.com/update?api_key=M7RUFRWZCHQVKJJX&field2=" + (String)vcc + "&field1="+ (String)willRain + "&field3=" + (String)maxRainValueDetected);
    //Serial.println("http://api.thingspeak.com/update?api_key=M7RUFRWZCHQVKJJX&field2=" + (String)vcc + "&field1="+ (String)willRain);
    int httpCode = http.GET();
  
    if(httpCode > 0) {
      // We got a header
      Serial.printf("[HTTP] GET... code: %d\n", httpCode);
  
      // We got a 200!
      if(httpCode == HTTP_CODE_OK) {
          //String payload = http.getString();
          //Serial.println(payload);
          sendOK = 1;
          http.end();
          Serial.println("Datas envoyés dans les nuages ;-) - VCC = "+(String)vcc);
      }
    } else {
      http.end();
      Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }

    if (i>10) {
      Serial.println("+ de 10 tentatives pour partner à ThingSpeak ... On abandonne");
      return;
    }
    i = i+1;
  }
}


int GetHTTPTime() {

  String dateTime;
  HTTPClient http;
  http.begin("http://www.google.com");
  
  const char* headerNames[] = { "date" };
  http.collectHeaders(headerNames, sizeof(headerNames)/sizeof(headerNames[0]));
  
  int rc = http.GET();
  if (rc >0) {
    dateTime = http.header("date");
  }
  else {
    Serial.println("Erreur HTTP Date");
    http.end();
    return 0;
  };

  http.end();

  Serial.println("Date obtenu de google.com : "+dateTime);

  String http_hr = extractHourFromDateTimeString(dateTime);
  String http_min = extractMinuteFromDateTimeString(dateTime);
  String http_sec = extractSecondFromDateTimeString(dateTime);
  String http_day = extractDayFromDateTimeString(dateTime);
  String http_month = translateMonth(extractMonthFromDateTimeString(dateTime));
  String http_year = extractYearFromDateTimeString(dateTime);

  setTime(http_hr.toInt(),http_min.toInt(),http_sec.toInt(),http_day.toInt(),http_month.toInt(),http_year.toInt());
  int currentT = now();
  Serial.println("Timestamp GMT Google.com : "+(String)currentT);

  return currentT;


/*
  t.tm_year = 2011-1900;
  t.tm_mon = 7;           // Month, 0 - jan
  t.tm_mday = 8;          // Day of the month
  t.tm_hour = 16;
  t.tm_min = 11;
  t.tm_sec = 42;
  t.tm_isdst = -1;        // Is DST on? 1 = yes, 0 = no, -1 = unknown
  t_of_day = mktime(&t);
*/
}


