
/* ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <gustavwostrack@web.de> wrote this file. As long as you retain this
 * notice you can do whatever you want with this stuff. If we meet some day,
 * and you think this stuff is worth it, you can buy me a beer in return
 * Gustav Wostrack
 * ----------------------------------------------------------------------------
 */

#include <Sweeper.h>
#include <Ticker.h>
#include <CARguruDefs.h>

// SOUND ////////////////////////////////////////////////////////////

// MOTOR ////////////////////////////////////////////////////////////
void start_speed()
{
  analogWrite(pin_motor, no_speed);
}

void make_speed(uint16_t speed)
{
  analogWrite(pin_motor, speed);
}

// LED ////////////////////////////////////////////////////////////
// Voreinstellungen, LED-Nummer wird physikalisch mit
// einer LED verbunden
void LED::Init_led()
{
    currBrightness = dark;
    analogWrite(pin, currBrightness);
    SetBrightness(currBrightness);
}

// Setzt die Zielposition
void LED::SetBrightness(uint8_t brightness)
{
  destBrightness = brightness;
}

// Überprüft periodisch, ob die Zielposition (Helligkeit) erreicht ist
void LED::Update()
{
  // Überprüfung, ob die Zielposition bereits erreicht wurde
  if (currBrightness != destBrightness)
  {
    // time to update
    currBrightness += setIncrement(currBrightness, destBrightness);
    analogWrite(pin, currBrightness);
  }
}

void LED::blitzLED(uint16_t delay_time)
{
    analogWrite(pin, smallBright);
    delay(delay_time);
    analogWrite(pin, dark);
}