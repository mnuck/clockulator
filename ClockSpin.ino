#include <NTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <TM1637.h>

const char *ssid     = "*";
const char *password = "*";

const int local_offset = -28800; // -8h in sec
const int utc_offset = 0;
int dial_offset = 0;

#define CLK1 5
#define DIO1 4
#define CLK2 0
#define DIO2 2

TM1637 tm1637_local(CLK1, DIO1);
TM1637 tm1637_utc(CLK2, DIO2);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

void setup_display(TM1637* display) {
  display->init();
  display->set(BRIGHT_TYPICAL);
  display->point(POINT_ON);
}

void update_display(TM1637* display, int offset) {
  timeClient.setTimeOffset(offset);
  int hours = timeClient.getHours();
  int minutes = timeClient.getMinutes();
  display->display(0, hours / 10);
  display->display(1, hours % 10);
  display->display(2, minutes / 10);
  display->display(3, minutes % 10);
}

void setup(){
  setup_display(&tm1637_local);
  setup_display(&tm1637_utc);
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  timeClient.begin();
}

void loop() {
  timeClient.update();
  update_display(&tm1637_local, local_offset + dial_offset);
  update_display(&tm1637_utc, utc_offset + dial_offset);
  delay(1000);
}
