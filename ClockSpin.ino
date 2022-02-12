#include <Button2.h>
#include <ESP8266WiFi.h>
#include <ESPRotary.h>
#include <NTPClient.h>
#include <TM1637.h>
#include <WiFiUdp.h>
#include <WiFiManager.h>

#define CLK1 5
#define DIO1 4

#define CLK2 0
#define DIO2 2

#define ROTARY_PIN_CLOCK 14
#define ROTARY_PIN_DT 12
#define ROTARY_BUTTON 13
#define ROTARY_STEPS_PER_CLICK 3

#define AP_NAME "Clockulator"

int brightness = 2;        // 0 - 7
int local_offset = -28800; // -8h in sec
int dial_speed = 600;      // seconds
int auto_reset_delay = 10; // seconds

unsigned long last_touch = 0;

TM1637 tm1637_local(CLK1, DIO1);
TM1637 tm1637_utc(CLK2, DIO2);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

ESPRotary rotary(ROTARY_PIN_CLOCK, ROTARY_PIN_DT);
Button2 button;

void setup_display(TM1637* display) {
  display->init();
  display->set(brightness);
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

void reset_offset(Button2& b) {
  rotary.resetPosition();
}

void auto_reset_offset() {
  timeClient.setTimeOffset(0);
  unsigned long now = timeClient.getEpochTime();
  if (last_touch + auto_reset_delay < now) {
    rotary.resetPosition();
  }
}

void reset_last_touch(ESPRotary& r) {
  timeClient.setTimeOffset(0);
  last_touch = timeClient.getEpochTime();
}

void setup(){
  setup_display(&tm1637_local);
  setup_display(&tm1637_utc);

  rotary.setStepsPerClick(ROTARY_STEPS_PER_CLICK);
  rotary.setChangedHandler(reset_last_touch);
  rotary.setIncrement(dial_speed);

  button.begin(ROTARY_BUTTON);
  button.setTapHandler(reset_offset);

  WiFiManager wm;
  wm.autoConnect(AP_NAME);

  timeClient.begin();
}

void loop() {
  timeClient.update();
  rotary.loop();
  button.loop();
  auto_reset_offset();

  update_display(&tm1637_local, local_offset + rotary.getPosition());
  update_display(&tm1637_utc, rotary.getPosition());
}
