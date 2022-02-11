#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

const char *ssid     = "*";
const char *password = "*";

const int local_offset = -28800; // -8h in sec
const int utc_offset = 0;
int dial_offset = 0;

#include <TM1637.h>

//#define CLK1 13
//#define DIO1 15

#define CLK1 5
#define DIO1 4

#define CLK2 0
#define DIO2 2

TM1637 tm1637_local(CLK1,DIO1);
TM1637 tm1637_utc(CLK2,DIO2);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

void setup(){
  tm1637_local.init();
  tm1637_local.set(BRIGHT_TYPICAL);//BRIGHT_TYPICAL = 2,BRIGHT_DARKEST = 0,BRIGHTEST = 7;
  tm1637_local.point(POINT_ON);

  tm1637_utc.init();
  tm1637_utc.set(BRIGHT_TYPICAL);//BRIGHT_TYPICAL = 2,BRIGHT_DARKEST = 0,BRIGHTEST = 7;
  tm1637_utc.point(POINT_ON);
  
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  timeClient.begin();
}

void loop() {
  int hours, minutes;
  timeClient.update();

  timeClient.setTimeOffset(local_offset);
  hours = timeClient.getHours();
  minutes = timeClient.getMinutes();

  tm1637_local.display(0, hours / 10);
  tm1637_local.display(1, hours % 10);
  tm1637_local.display(2, minutes / 10);
  tm1637_local.display(3, minutes % 10);

  timeClient.setTimeOffset(utc_offset);
  hours = timeClient.getHours();
  minutes = timeClient.getMinutes();

  tm1637_utc.display(0, hours / 10);
  tm1637_utc.display(1, hours % 10);
  tm1637_utc.display(2, minutes / 10);
  tm1637_utc.display(3, minutes % 10);

  delay(1000);
}
