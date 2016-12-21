#include <OpenWiFi.h>
#include <ESP8266HTTPClient.h>
#include <Servo.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>

#include "SpringyValue.h"
#include "config.h"
#include "WS2812_util.h"
#include "LEDAnimation.h"

extern "C" {
#include "user_interface.h"
}

os_timer_t myTimer;
Servo myServo;
HTTPClient http;

int oldTime = 0;
int oscillationTime = 500;
String chipID;
String serverURL = SERVER_URL;
OpenWiFi hotspot;
SpringyValue spring;

LEDAnimation *currentAnimation;
SpringAnimation springAnimation;
BreatheAnimation breatheAnimation;

void printDebugMessage(String message) {
#ifdef DEBUG_MODE
  Serial.println(String(PROJECT_SHORT_NAME) + ": " + message);
#endif
}

// start of timerCallback
void inline timerCallback() {
  timer0_write(ESP.getCycleCount() + UPDATE_INTERVAL_US * 80);
  currentAnimation->update(0.01);
} // End of timerCallback


void setup()
{
  // Set up neopixels
  strip.begin();
  strip.setBrightness(255);

  // Setup timer
  noInterrupts();
  timer0_isr_init();
  timer0_attachInterrupt(timerCallback);
  timer0_write(ESP.getCycleCount() + UPDATE_INTERVAL_US * 80); // 160 when running at 160mhz
  interrupts();

  // Set up connection wait animation
  currentAnimation = &breatheAnimation;
  breatheAnimation.begin(150, 150, 255, 0.02, 0.04, 0.5);

  // Set up serial port
  Serial.begin(115200); Serial.println("");

  // Set up button
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Set up Servo
  myServo.attach(SERVO_PIN);

  // Set up deviceID
  chipID = generateChipID();
  printDebugMessage(String("Last 2 bytes of chip ID: ") + chipID);

  // Start wifi connection
  WiFiManager wifiManager;
  hotspot.begin("", "");

  //  // Reset WiFi connection when button is pressed at startup
  //  int counter = 0;
  //  while (digitalRead(BUTTON_PIN) == LOW)
  //  {
  //    counter++;
  //    delay(10);
  //
  //    if (counter > 500)
  //    {
  //      wifiManager.resetSettings();
  //      printDebugMessage("Remove all wifi settings!");
  //      setAllPixels(255, 0, 0, 1.0);
  //      fadeBrightness(255, 0, 0, 1.0);
  //      ESP.reset();
  //    }
  //  }

  // if no connection could be made, set up WiFi manager
  String configSSID = String(CONFIG_SSID) + "_" + chipID;
  setAllPixels(0, 255, 255, 255);
  wifiManager.autoConnect(configSSID.c_str());
  fadeBrightness(0, 255, 255, 1.0);

}

//This method starts an oscillation movement in both the LED and servo
void oscillate(float springConstant, float dampConstant, int c)
{
  byte red = (c >> 16) & 0xff;
  byte green = (c >> 8) & 0xff;
  byte blue = c & 0xff;

  spring.c = springConstant;
  spring.k = dampConstant / 100;
  spring.perturb(255);

  //Start oscillating
  for (int i = 0; i < oscillationTime; i++)
  {
    setAllPixels(red, green, blue, abs(spring.x));
    myServo.write(90 + spring.x / 4);
    spring.update(0.01);
    //Check for button press
    if (digitalRead(BUTTON_PIN) == LOW)
    {
      //Fade the current color out
      fadeBrightness(red, green, blue, abs(spring.x) / 255.0);
      return;
    }
    delay(10);
  }
  //fadeBrightness(red, green, blue, abs(spring.x) / 255.0);
}

void loop()
{
  //Check for button press
  if (digitalRead(BUTTON_PIN) == LOW)
  {
    sendButtonPress();
    delay(250);
  }

  //Every requestDelay, send a request to the server
  if (millis() > oldTime + REQUEST_DELAY)
  {
    requestMessage();
    oldTime = millis();
  }

  yield();  // or delay(0);
}

void sendButtonPress()
{
  printDebugMessage("Sending button press to server");

  http.begin(serverURL + "/api.php?t=sqi&d=" + chipID);
  uint16_t httpCode = http.GET();
  http.end();
}

void requestMessage()
{
  hideColor();

  String requestString = serverURL + "/api.php?t=gqi&d=" + chipID + "&v=2";

  http.begin(requestString);

  uint16_t httpCode = http.GET();

  if (httpCode == 200)
  {
    String response;
    response = http.getString();

    if (response == "-1")
    {
      printDebugMessage("There are no messages waiting in the queue");
    }
    else
    {
      //Get the indexes of some commas, will be used to split strings
      int firstComma = response.indexOf(',');
      int secondComma = response.indexOf(',', firstComma + 1);
      int thirdComma = response.indexOf(',', secondComma + 1);

      //Parse data as strings
      String hexColor = response.substring(0, 7);
      String dampConstant = response.substring(firstComma + 1, secondComma);
      String springConstant = response.substring(secondComma + 1, thirdComma);;
      String message = response.substring(thirdComma + 1, response.length());;

      printDebugMessage("Message received from server: \n");
      printDebugMessage("Hex color received: " + hexColor);
      printDebugMessage("Spring constant received: " + springConstant);
      printDebugMessage("Damp constant received: " + dampConstant);
      printDebugMessage("Message received: " + message);

      //Extract the hex color and fade the led strip
      int color = (int) strtol( &response[1], NULL, 16);
      
      //oscillate(springConstant.toFloat(), dampConstant.toFloat(), color);      
      float springValue = springConstant.toFloat() / 50.0;
      float dampValue = dampConstant.toFloat() / 100.0;

      printDebugMessage(String(springValue));
      printDebugMessage(String(dampValue));

      currentAnimation = &springAnimation;
      currentAnimation->begin(color, 255.0, 1.5, 10.0);
    }
  }
  else
  {
    ESP.reset();
  }

  http.end();
}

String generateChipID()
{
  String chipIDString = String(ESP.getChipId() & 0xffff, HEX);

  chipIDString.toUpperCase();
  while (chipIDString.length() < 4)
    chipIDString = String("0") + chipIDString;

  return chipIDString;
}
