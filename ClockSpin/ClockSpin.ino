/*
 * Clockulator
 * Matthew (mnuck) Nuckolls <matthew.nuckolls@gmail.com>
 * 
 * Setup:
 *  1. Plug into USB for power.
 *  2. Grab any device with wifi and a browser.
 *  3. Connect to wifi access point named Clockulator.
 *  4. Go to any website, get redirected.
 *  5. Add your Wifi connection to this device.
 *  
 *  If you need to re-run the config workflow, unplug the device, 
 *  then press the rotary knob while plugging it back in.
 *  
 * Theory of Operation:
 *    This device connects to pool.ntp.org every 60 seconds
 *    to synchronize its internal clock. Local time is displayed
 *    on one 4-digit display. UTC time is displayed on the other.
 *    The rotary knob offsets both displays to show time in the
 *    past or future. Press the rotary knob button to return to
 *    present time. If the knob is not touched for 10 seconds,
 *    offset automatically returns to present time.
 *    
 * But Why?
 *    This device answers four common questions.
 *    1. What time is right now?
 *    2. What time is it UTC right now?
 *    3. When it is timeX here, what time is it UTC?
 *    4. When it is timeX UTC, what time is it here?
 *    
 * ESP-12E microcontroller board (ESP8266 module)
 * Two TM1637 4-digit clock displays
 * One rotary encoder with button
 * 
 * Libraries Used:
 *  Wifi Manager
 *  NTPClient
 *  Time
 *  Timezone
 *  ESPRotary
 *  Button2
 *  Grove 4-Digit Display  
 */

#include <Button2.h>
#include <ESP8266WiFi.h>
#include <ESPRotary.h>
#include <NTPClient.h>
#include <Timezone.h>
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
#define ROTARY_STEPS_PER_CLICK 4

#define AP_NAME "Clockulator"

const TimeChangeRule PDT = {"PDT", Second, Sun, Mar, 2, -7 * 60};
const TimeChangeRule PST = {"PST", Second, Sun, Nov, 2, -8 * 60};
Timezone Pacific(PDT, PST);

const int brightness = 4;          // 0 - 7
const int auto_reset_delay = 10;   // seconds
const int rotary_increment = -600; // seconds

ESPRotary rotary(ROTARY_PIN_CLOCK, ROTARY_PIN_DT);
Button2 button;

TM1637 tm1637_local(CLK1, DIO1);
TM1637 tm1637_utc(CLK2, DIO2);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

int dial_offset = 0;
unsigned long last_touch = 0;


void setup_display(TM1637* display) {
  display->init();
  display->set(brightness);
  display->point(POINT_ON);
}

void update_display(TM1637* display, const time_t moment) {
  const int h = hour(moment);
  const int m = minute(moment);
  display->display(0, h / 10);
  display->display(1, h % 10);
  display->display(2, m / 10);
  display->display(3, m % 10);
}

void reset_offset(Button2& b) {
  dial_offset = 0;
}

void auto_reset_offset() {
  unsigned long now = timeClient.getEpochTime();
  if (last_touch + auto_reset_delay < now) {
    dial_offset = 0;
  }
}

void decrement_offset(ESPRotary& r) {
  dial_offset -= rotary_increment;
  last_touch = timeClient.getEpochTime();
}

void increment_offset(ESPRotary& r) {
  dial_offset += rotary_increment;
  last_touch = timeClient.getEpochTime();
}

void setup(){
  setup_display(&tm1637_local);
  setup_display(&tm1637_utc);

  rotary.setLeftRotationHandler(decrement_offset);
  rotary.setRightRotationHandler(increment_offset);

  WiFiManager wm;
  pinMode(ROTARY_BUTTON, INPUT_PULLUP);
  if (digitalRead(ROTARY_BUTTON) == LOW) {
    wm.startConfigPortal(AP_NAME);
  } else {
    wm.autoConnect(AP_NAME);
  }

  button.begin(ROTARY_BUTTON);
  button.setTapHandler(reset_offset);

  timeClient.begin();
}

void loop() {
  timeClient.update();
  rotary.loop();
  button.loop();
  auto_reset_offset();

  time_t utc = dial_offset + timeClient.getEpochTime();
  time_t local = Pacific.toLocal(utc);
    
  update_display(&tm1637_utc, utc);
  update_display(&tm1637_local, local);
}
