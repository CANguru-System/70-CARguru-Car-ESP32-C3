
/* ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <CARguru-Buch@web.de> wrote this file. As long as you retain this
 * notice you can do whatever you want with this stuff. If we meet some day,
 * and you think this stuff is worth it, you can buy me a beer in return
 * Gustav Wostrack
 * ----------------------------------------------------------------------------
 */

#pragma once

#include "Arduino.h"
#include <Ticker.h>

enum led_sweepers
{
  frontLight,        // lamp0: Scheinwerfer
  leftIndicator,     // lamp1: Blinker rechts
  rightIndicator,    // lamp2: Blinker links
  rearLight,         // lamp3: Rücklicht links
  blizzerTopLeft,    // lamp4: Signallicht, oben links
  blizzerfrontLeft,  // lamp5: Signallicht, oben rechts
  blizzerFrontRight, // lamp6: Signallicht, vorne links
  blizzerTopRight,   // lamp7: Signallicht, vorne rechts
  LED_BUILTIN_OWN
};
const uint8_t num_led_sweepers = 8;

const gpio_num_t channel2led_pins[num_led_sweepers] = {
    GPIO_NUM_2, // lamp0
    GPIO_NUM_3, // lamp1
    GPIO_NUM_4, // lamp2
    GPIO_NUM_5,  // lamp3
    GPIO_NUM_6, // lamp4
    GPIO_NUM_7 // lamp5
};

const uint8_t veryBright = 0xFF;
const uint8_t bright = veryBright / 2;
const uint8_t smallBright = bright / 2;
const uint8_t dark = 0x00;

const gpio_num_t pin_motor = GPIO_NUM_10; // motorspeed
const uint8_t channel_motor = LED_BUILTIN_OWN;  // 8 sind für die LEDs
const uint16_t resolution = 0x08;
const uint32_t freq_motor = 1000;
const uint16_t no_speed = 0;
void start_speed();
void make_speed(uint16_t speed);

const uint32_t freq_lamps = freq_motor;

class LED
{
public:
  // Voreinstellungen, LED-Nummer wird physikalisch mit
  // einer LED verbunden;
  LED()
  { };
  LED(led_sweepers nbr)
  {
    channel = nbr;
    pin = channel2led_pins[channel];
    Init_led();
  };
  // Setzt die Helligkeit
  void SetBrightness(uint8_t brightness);
  void Update();
  void blitzLED(uint16_t delay_time);
  //
private:
  void Init_led();
  uint8_t setIncrement(uint8_t p, uint8_t d)
  {
    if (p > d)
      return -1;
    else
      return 1;
  }

private:
  uint8_t channel;
  gpio_num_t pin;
  uint8_t currBrightness; // current servo position
  uint8_t destBrightness; // servo position, where to go
};