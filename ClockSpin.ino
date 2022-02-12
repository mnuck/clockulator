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

#define AP_NAME "Clockulator"

int brightness = 2;        // 0 - 7
int local_offset = -28800; // -8h in sec

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
  dial_offset += 600;
}

void incrementOffset(ESPRotary& r) {
  dial_offset -= 600;
}

void resetOffset(Button2& b) {
  dial_offset = 0;
}

void setup(){
  setup_display(&tm1637_local);
  setup_display(&tm1637_utc);

  rotary.setLeftRotationHandler(decrementOffset);
  rotary.setRightRotationHandler(incrementOffset);

  button.begin(ROTARY_BUTTON);
  button.setTapHandler(reset_offset);

  WiFiManager wm;
  wm.autoConnect(AP_NAME);

  timeClient.begin();
}

void loop() {
  timeClient.update();
  update_display(&tm1637_local, local_offset + dial_offset);
  update_display(&tm1637_utc, utc_offset + dial_offset);
  rotary.loop();
  button.loop();
}
