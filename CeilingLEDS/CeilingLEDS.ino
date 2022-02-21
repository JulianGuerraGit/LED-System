#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <BlynkSimpleEsp8266.h>
#include "SinricPro.h"
#include "SinricProLight.h"
#include <Kelvin2RGB.h>
#include <Adafruit_NeoPixel.h>
#define FASTLED_ALLOW_INTERRUPTS 0
#include <FastLED.h>
#include <E131.h>

/************************************************************DEFINITIONS************************************************************/

//     WIFI
#define WIFI_SSID         "*****"
#define WIFI_PASS         "*****"

//      OTA
#define OTAHOSTNAME "*****"
#define OTAPASSWORD "**********"

//     SINRIC PRO
#define APP_KEY           "*****"      // Should look like "de0bxxxx-1x3x-4x3x-ax2x-5dabxxxxxxxx"
#define APP_SECRET        "*****"   // Should look like "5f36xxxx-x3x7-4x3x-xexe-e86724a9xxxx-4c4axxxx-3x3x-x5xe-x9x3-333d65xxxxxx"
#define LIGHT_ID          "*****"    // Should look like "5dc1564130xxxxxxxxxxxxxx"

//     BLYNK
char auth[] = "*****"; // Blynk Auth

//     LED STRIP
#define NUMLEDS 180
#define LEDPIN 1

//      POWER
#define VOLTS 5
#define MILLIAMPS 8000

//     LED-FX (E131 audio effect stuff)
#define UNIVERSE 1      /* Universe to listen for */
#define CHANNEL_START 1

//     BAUDRATE
#define BAUD_RATE         9600                // Change baudrate to your need
/************************************************************END DEFINITIONS************************************************************/

E131 e131;
CRGB color(0, 0, 0);
CRGB strip[NUMLEDS];

int STATE = 1;
int LastState = 1;
int EFFECT = 0;
int boot = 0;

int Speed = 2;
int Fps = 120;
int ota = 1;

uint8_t gHue = 0;

float rainbowAddedHue = 0; // rainbow

/************************************************************WIFI STUFF************************************************************/

void setupWiFi() {
  Serial.printf("\r\n[Wifi]: Connecting");
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.printf(".");
    delay(250);
  }

  IPAddress localIP = WiFi.localIP();
  Serial.printf("connected!\r\n[WiFi]: IP-Address is %d.%d.%d.%d\r\n", localIP[0], localIP[1], localIP[2], localIP[3]);
}

/************************************************************WIFI STUFF************************************************************/

/************************************************************SINRIC STUFF************************************************************/

void setupSinricPro() {
  // get a new Light device from SinricPro
  SinricProLight &myLight = SinricPro[LIGHT_ID];

  // set callback function to device
  myLight.onPowerState(onPowerState);
  myLight.onBrightness(onBrightness);
  myLight.onAdjustBrightness(onAdjustBrightness);
  myLight.onColor(onColor);
  myLight.onColorTemperature(onColorTemperature);
  myLight.onIncreaseColorTemperature(onIncreaseColorTemperature);
  myLight.onDecreaseColorTemperature(onDecreaseColorTemperature);

  // setup SinricPro
  SinricPro.onConnected([]() {
    Serial.printf("Connected to SinricPro\r\n");
  });
  SinricPro.onDisconnected([]() {
    Serial.printf("Disconnected from SinricPro\r\n");
  });
  SinricPro.begin(APP_KEY, APP_SECRET);
}


// define array of supported color temperatures
int colorTemperatureArray[] = {2200, 2700, 4000, 5500, 7000};
int max_color_temperatures = sizeof(colorTemperatureArray) / sizeof(colorTemperatureArray[0]); // calculates how many elements are stored in colorTemperature array

// a map used to convert a given color temperature into color temperature index (used for colorTemperatureArray)
std::map<int, int> colorTemperatureIndex;

// initializes the map above with color temperatures and index values
// so that the map can be used to do a reverse search like
// int index = colorTemperateIndex[4000]; <- will result in index == 2
void setupColorTemperatureIndex() {
  Serial.printf("Setup color temperature lookup table\r\n");
  for (int i = 0; i < max_color_temperatures; i++) {
    colorTemperatureIndex[colorTemperatureArray[i]] = i;
    Serial.printf("colorTemperatureIndex[%i] = %i\r\n", colorTemperatureArray[i], colorTemperatureIndex[colorTemperatureArray[i]]);
  }
}

// we use a struct to store all states and values for our light
struct {
  bool powerState = false;
  int brightness = 100;
  int brightnessTemp = 100;
  struct {
    byte r = 255;
    byte g = 255;
    byte b = 255;
  } color;
  int colorTemperature = colorTemperatureArray[0]; // set colorTemperature to first element in colorTemperatureArray array
} device_state;

bool onPowerState(const String &deviceId, bool &state) {
  Serial.printf("Device %s power turned %s \r\n", deviceId.c_str(), state ? "on" : "off");
  device_state.powerState = state;
  if (device_state.powerState == 0) {
    STATE = 0;
    SetColor();
  }
  else if (device_state.powerState == 1) {
    STATE = LastState;
    if (STATE == 1) {
      SetColor();
    }
  }
  updateSwitch();
  return true; // request handled properly

}

bool onBrightness(const String &deviceId, int &brightness) {
  if (STATE == 0) {
    STATE = LastState;
    if (STATE == 1) {
      SetColor();
    }
  }
  updateSwitch();
  device_state.brightness = brightness;
  Serial.printf("Device %s brightness level changed to %d\r\n", deviceId.c_str(), brightness);
  Serial.print(device_state.brightness);
  //FastLED.setBrightness(2.55 * device_state.brightness);
  SetBrightness();
  updateBrightness();
  return true;
}

bool onAdjustBrightness(const String &deviceId, int brightnessDelta) {
  if (STATE == 0) {
    STATE = LastState;
    if (STATE == 1) {
      SetColor();
    }
  }
  updateSwitch();
  device_state.brightness += brightnessDelta;
  Serial.printf("Device %s brightness level changed about %i to %d\r\n", deviceId.c_str(), brightnessDelta, device_state.brightness);
  brightnessDelta = device_state.brightness;
  //FastLED.setBrightness(2.55 * device_state.brightness);
  SetBrightness();
  updateBrightness();
  return true;
}

bool onColor(const String &deviceId, byte &r, byte &g, byte &b) {
  STATE = 1;
  updateSwitch();
  device_state.color.r = r;
  device_state.color.g = g;
  device_state.color.b = b;
  SetColor();
  updateColor();
  Serial.printf("Device %s color changed to %d, %d, %d (RGB)\r\n", deviceId.c_str(), device_state.color.r, device_state.color.g, device_state.color.b);
  return true;
}

bool onColorTemperature(const String &deviceId, int &colorTemperature) {
  STATE = 1;
  updateSwitch();
  device_state.colorTemperature = colorTemperature;
  Serial.printf("Device %s color temperature changed to %d\r\n", deviceId.c_str(), device_state.colorTemperature);
  setColorTemperature(colorTemperature);
  SetColor();
  updateColor();
  return true;
}

bool onIncreaseColorTemperature(const String &deviceId, int &colorTemperature) {
  STATE = 1;
  updateSwitch();
  int index = colorTemperatureIndex[device_state.colorTemperature];              // get index of stored colorTemperature
  index++;                                                                // do the increase
  if (index < 0) index = 0;                                               // make sure that index stays within array boundaries
  if (index > max_color_temperatures - 1) index = max_color_temperatures - 1; // make sure that index stays within array boundaries
  device_state.colorTemperature = colorTemperatureArray[index];                  // get the color temperature value
  Serial.printf("Device %s increased color temperature to %d\r\n", deviceId.c_str(), device_state.colorTemperature);
  colorTemperature = device_state.colorTemperature;                              // return current color temperature value
  setColorTemperature(colorTemperature);
  SetColor();
  updateColor();
  return true;
}

bool onDecreaseColorTemperature(const String &deviceId, int &colorTemperature) {
  STATE = 1;
  updateSwitch();
  int index = colorTemperatureIndex[device_state.colorTemperature];              // get index of stored colorTemperature
  index--;                                                                // do the decrease
  if (index < 0) index = 0;                                               // make sure that index stays within array boundaries
  if (index > max_color_temperatures - 1) index = max_color_temperatures - 1; // make sure that index stays within array boundaries
  device_state.colorTemperature = colorTemperatureArray[index];                  // get the color temperature value
  Serial.printf("Device %s decreased color temperature to %d\r\n", deviceId.c_str(), device_state.colorTemperature);
  colorTemperature = device_state.colorTemperature;                              // return current color temperature value
  setColorTemperature(colorTemperature);
  SetColor();
  updateColor();
  return true;
}

void setColorTemperature(int k)  {
  //2200, 2700, 4000, 5500, 7000
  Kelvin2RGB kelvin(k, 100);
  device_state.color.r = kelvin.Red;
  device_state.color.g = kelvin.Green;
  device_state.color.b = kelvin.Blue;
}
/************************************************************SINRIC STUFF************************************************************/

/************************************************************BLYNK STUFF************************************************************/

void updateColor() {
  Blynk.virtualWrite(V0, device_state.color.r, device_state.color.g, device_state.color.b);
}

void updateSwitch() {
  Blynk.virtualWrite(V1, STATE + 1);
}

void updateBrightness() {
  Blynk.virtualWrite(V3, device_state.brightness);
}

void updateOTA() {
  Blynk.virtualWrite(V5, ota);
}

BLYNK_CONNECTED() {
  if (boot == 0) {
    boot = 1;
    Blynk.syncVirtual(V0);
  }
  Blynk.syncVirtual(V1);
  Blynk.syncVirtual(V2);
  Blynk.syncVirtual(V3);
  Blynk.syncVirtual(V4);
  updateOTA();
}

BLYNK_WRITE(V0) // zeRGBa assigned to V0
{
  device_state.color.r = param[0].asInt();
  device_state.color.g = param[1].asInt();
  device_state.color.b = param[2].asInt();
  if (STATE == 1) {
    SetColor();
  }
}
BLYNK_WRITE(V1)
{
  switch (param.asInt()) {
    case 1: {
        STATE = 0;
        SetColor();
        break;
      }
    case 2: {
        STATE = 1;
        SetColor();
        break;
      }
    case 3: {
        STATE = 2;
        break;
      }
  }
}

BLYNK_WRITE(V2) {
  int value = param.asInt();
  Speed = value;

}

BLYNK_WRITE(V3) {
  int value = param.asInt();
  device_state.brightness = value;
  //FastLED.setBrightness(2.55 * device_state.brightness);
  SetBrightness();
}

BLYNK_WRITE(V4) {
  switch (param.asInt()) {
    case 1: {
        EFFECT = 0;
        break;
      }
    case 2: {
        EFFECT = 1;
        break;
      }
    case 3: {
        EFFECT = 2;
        break;
      }
    case 4: {
        EFFECT = 3;
        break;
      }
    case 5: {
        EFFECT = 4;
        break;
      }
    case 6: {
        EFFECT = 5;
        break;
      }
    case 7: {
        EFFECT = 6;
        break;
      }
    case 8: {
        EFFECT = 7;
        break;
      }
    case 9: {
        EFFECT = 8;
        break;
      }
    case 10: {
        EFFECT = 9;
        break;
      }
    case 11: {
        EFFECT = 10;
        break;
      }
    case 12: {
        EFFECT = 11;
        fill_solid( strip, NUMLEDS, CRGB(0, 0, 0));
        break;
      }
    case 13: {
        EFFECT = 12;
        break;
      }
    case 14: {
        EFFECT = 13;
        break;
      }
  }
}

BLYNK_WRITE(V5) {
  int OTA = param.asInt();
  ota = OTA;
}


void checkConnection() {
  bool result = Blynk.connected();
  if (!result) {
    Blynk.connect();
  }
}

/************************************************************BLYNK STUFF************************************************************/

/************************************************************EFFECTS STUFF************************************************************/

void effect() {

  if (EFFECT == 0) {
    rainbow();
  }
  else if (EFFECT == 1) {
    Rainbow2();
  }
  else if (EFFECT == 2) {
    fade();
  }
  else if (EFFECT == 3) {
    bellCurve();
  }
  else if (EFFECT == 4) {
    scanner();
  }
  else if (EFFECT == 5) {
    SetColor();
  }
  else if (EFFECT == 6) {
    SetColor();
  }
  else if (EFFECT == 7) {
    glitter(Speed);
  }
  else if (EFFECT == 8) {
    confetti();
  }
  else if (EFFECT == 9) {
    bpm();
  }
  else if (EFFECT == 10) {
    juggle();
  }
  else if (EFFECT == 11) {
    fill_solid(strip, NUMLEDS, CRGB(0, 0, 0));
    flame(false, 1, 61);
    flame2(true, 120, 60);
  }
  else if (EFFECT == 12) {
    AudioVisuals();
  }
  else if (EFFECT == 13) {
    meteorRain(10, 64, true);
  }
  if (EFFECT != 13){
    FastLED.show();
  }
}

void rainbow()
{
  fill_rainbow( strip, NUMLEDS, gHue);
  //FastLED.delay(1000/Speed);
  if (Speed == 1) {
    Fps = 15;
  }
  else if (Speed == 2) {
    Fps = 100;
  }
  else if (Speed == 3) {
    Fps = 500;
  }
  delay(500 / Fps);
  EVERY_N_MILLISECONDS( 20 ) {
    gHue++;
  }
}

void fade() {
  fill_solid( strip, NUMLEDS, CHSV(gHue, 255, 255));
  if (Speed == 1) {
    Fps = 2;
  }
  else if (Speed == 2) {
    Fps = 10;
  }
  else if (Speed == 3) {
    Fps = 20;
  }
  FastLED.delay(1000 / Fps);
  EVERY_N_MILLISECONDS( 20 ) {
    gHue++;
  }
}

void scanner() {
  fadeToBlackBy(strip, NUMLEDS, 20);
  if (Speed == 1) {
    Fps = 350;
  }
  else if (Speed == 2) {
    Fps = 250;
  }
  else if (Speed == 3) {
    Fps = 100;
  }
  int pos = beatsin16( 1000 / Fps, 0, NUMLEDS - 4 );
  fill_solid( &(strip[pos]), 4, CRGB(device_state.color.r, device_state.color.g, device_state.color.b));
}

void glitter( fract8 chanceOfGlitter)
{
  if (chanceOfGlitter == 1) {
    Fps = 5;
  }
  else if (chanceOfGlitter == 2) {
    Fps = 15;
  }
  else if (chanceOfGlitter == 3) {
    Fps = 50;
  }
  fadeToBlackBy( strip, NUMLEDS, 10);
  if ( random8() < Fps) {
    strip[ random16(NUMLEDS) ] = CRGB::White;
  }
}

void confetti()
{
  // random colored speckles that blink in and fade smoothly
  fadeToBlackBy( strip, NUMLEDS, 10);
  int pos = random16(NUMLEDS);
  strip[pos] += CHSV( gHue + random8(64), 200, 255);
  EVERY_N_MILLISECONDS( 20 ) {
    gHue++;
  }
}

void bpm()
{
  // colored stripes pulsing at a defined Beats-Per-Minute (BPM)
  CRGBPalette16 palette = PartyColors_p;
  if (Speed == 1) {
    Fps = 35;
  }
  else if (Speed == 2) {
    Fps = 50;
  }
  else if (Speed == 3) {
    Fps = 90;
  }
  uint8_t beat = beatsin8( Fps, 64, 255);
  for ( int i = 0; i < NUMLEDS; i++) { //9948
    strip[i] = ColorFromPalette(palette, gHue + (i * 2), beat - gHue + (i * 10));
  }
  EVERY_N_MILLISECONDS( 20 ) {
    gHue++;
  }
}

void juggle() {
  // eight colored dots, weaving in and out of sync with each other
  fadeToBlackBy( strip, NUMLEDS, 20);
  byte dothue = 0;
  for ( int i = 0; i < 8; i++) {
    strip[beatsin16( i + 7, 0, NUMLEDS - 1 )] |= CHSV(dothue, 200, 255);
    dothue += 32;
  }
}

// COOLING: How much does the air cool as it rises?
// Less cooling = taller flames.  More cooling = shorter flames.
// Default 50, suggested range 20-100
#define COOLING  65

// SPARKING: What chance (out of 255) is there that a new spark will be lit?
// Higher chance = more roaring fire.  Lower chance = more flickery fire.
// Default 120, suggested range 50-200.
#define SPARKING 65

void flame(bool gReverseDirection,  int ledStart, int ledNum )
{

  // Array of temperature readings at each simulation cell
  int ledEnd = ledNum + ledStart;
  static byte heat[60];

  //bool gReverseDirection = true;
  // Step 1.  Cool down every cell a little
  for ( int i = 0; i < ledNum; i++) {
    heat[i] = qsub8( heat[i],  random8(0, ((COOLING * 10) / ledNum) + 2));
  }

  // Step 2.  Heat from each cell drifts 'up' and diffuses a little
  for ( int k = ledNum - 1; k >= 2; k--) {
    heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2] ) / 3;
  }

  // Step 3.  Randomly ignite new 'sparks' of heat near the bottom
  if ( random8() < SPARKING ) {
    int y = random8(7);
    heat[y] = qadd8( heat[y], random8(160, 255) );
  }

  // Step 4.  Map from heat cells to LED colors
  for ( int j = 0; j < ledNum; j++) {
    CRGB color = HeatColor( heat[j]);
    int pixelnumber;
    if ( gReverseDirection ) {
      pixelnumber = (ledEnd - 1) - j;
    } else {
      pixelnumber = ledStart + j;
    }
    strip[pixelnumber] = color;
  }
  delay(1000 / Fps);
}

void flame2(bool gReverseDirection,  int ledStart, int ledNum )
{

  // Array of temperature readings at each simulation cell
  int ledEnd = ledNum + ledStart;
  static byte heat2[60];

  //bool gReverseDirection = true;
  // Step 1.  Cool down every cell a little
  for ( int i = 0; i < ledNum; i++) {
    heat2[i] = qsub8( heat2[i],  random8(0, ((COOLING * 10) / ledNum) + 2));
  }

  // Step 2.  Heat from each cell drifts 'up' and diffuses a little
  for ( int k = ledNum - 1; k >= 2; k--) {
    heat2[k] = (heat2[k - 1] + heat2[k - 2] + heat2[k - 2] ) / 3;
  }

  // Step 3.  Randomly ignite new 'sparks' of heat2 near the bottom
  if ( random8() < SPARKING ) {
    int y = random8(7);
    heat2[y] = qadd8( heat2[y], random8(160, 255) );
  }

  // Step 4.  Map from heat2 cells to LED colors
  for ( int j = 0; j < ledNum; j++) {
    CRGB color = HeatColor( heat2[j]);
    int pixelnumber;
    if ( gReverseDirection ) {
      pixelnumber = (ledEnd - 1) - j;
    } else {
      pixelnumber = ledStart + j;
    }
    strip[pixelnumber] = color;
  }
  delay(900 / Fps);
}

void AudioVisuals() {
  if (e131.parsePacket()) {
    if (e131.universe == 1) {
      for (int i = 0; i < NUMLEDS; i++) {
        int j = i * 3 + (CHANNEL_START - 1);
        strip[i].setRGB(e131.data[j], e131.data[j + 1], e131.data[j + 2]);
      }
    }
  }
  FastLED.delay(15);
}

void Rainbow2() {
  int startHue = 0;
  if (Speed == 1) {
    Fps = 60;
  }
  else if (Speed == 2) {
    Fps = 20;
  }
  else if (Speed == 3) {
    Fps = 1;
  }
  int speed = 30;

  rainbowAddedHue = rainbowAddedHue + 1;
  delay(Fps);
  if (rainbowAddedHue > 254) {
    rainbowAddedHue = 0;
  }

  // Constrain the variables before using
  startHue = constrain(startHue, 0, 255);
  speed = speed > 0 ? speed : 0;

  // Update the hue by 1 every 360th of the allocated time
  if (speed > 0) {
    float rainbowDeltaHue = (255 / ((float)speed * 1000)) * 50;
    startHue += (int)rainbowAddedHue;
  }

  // Calculate the rainbow so it lines up
  float deltaHue = (float)255 / (float)NUMLEDS;
  float currentHue = startHue;
  for (int i = 0; i < NUMLEDS; i++) {
    currentHue = startHue + (float)(deltaHue * i);
    currentHue = (currentHue < 255) ? currentHue : currentHue - 255;
    strip[i] = CHSV( currentHue, 255, 255);
  }
}

void bellCurve() {

  for (int i = 0; i < NUMLEDS; i++) {
    int ledNrightness = cubicwave8( ( 255 / (float)NUMLEDS  ) * i );
    strip[i] = CRGB(device_state.color.r, device_state.color.g, device_state.color.b);
    strip[i] %= ledNrightness;
  }
}

void meteorRain(byte meteorSize, byte meteorTrailDecay, boolean meteorRandomDecay) {
  fill_solid(strip, NUMLEDS, CRGB(0, 0, 0));
  if (Speed == 1) {
    Fps = 20;
  }
  else if (Speed == 2) {
    Fps = 10;
  }
  else if (Speed == 3) {
    Fps = 7;
  }
  for (int i = 0; i < NUMLEDS + 40; i++) {


    // fade brightness all LEDs one step
    for (int j = 0; j < NUMLEDS; j++) {
      if ( (!meteorRandomDecay) || (random(10) > 5) ) {
        strip[j].fadeToBlackBy( meteorTrailDecay );
      }
    }
    // draw meteor
    for (int j = 0; j < meteorSize; j++) {
      if ( ( i - j < NUMLEDS) && (i - j >= 0) ) {
        strip[i - j] = CRGB(device_state.color.r, device_state.color.g, device_state.color.b);
      }
    }
    FastLED.show();
    FastLED.delay(Fps);
  }
}

/************************************************************EFFECTS STUFF************************************************************/

/************************************************************COLOR STUFF************************************************************/

void SetColor() {
  if (STATE == 0) color = CRGB(0, 0, 0);
  else if (STATE == 1) color = CRGB( dim8_video(device_state.color.r), dim8_video(device_state.color.g), dim8_video(device_state.color.b));
  for (int i = 0; i < 40; i++) {
    fadeTowardColor( strip, NUMLEDS, color, 15);
    FastLED.delay(10);
  }
  FastLED.show();
}

void nblendU8TowardU8( uint8_t& cur, const uint8_t target, uint8_t amount)
{
  if ( cur == target) return;

  if ( cur < target ) {
    uint8_t delta = target - cur;
    delta = scale8_video( delta, amount);
    cur += delta;
  } else {
    uint8_t delta = cur - target;
    delta = scale8_video( delta, amount);
    cur -= delta;
  }
}

// Blend one CRGB color toward another CRGB color by a given amount.
// Blending is linear, and done in the RGB color space.
// This function modifies 'cur' in place.
CRGB fadeTowardColor( CRGB& cur, const CRGB& target, uint8_t amount)
{
  nblendU8TowardU8( cur.red,   target.red,   amount);
  nblendU8TowardU8( cur.green, target.green, amount);
  nblendU8TowardU8( cur.blue,  target.blue,  amount);
  return cur;
}

// Fade an entire array of CRGBs toward a given background color by a given amount
// This function modifies the pixel array in place.
void fadeTowardColor( CRGB* L, uint16_t N, const CRGB& bgColor, uint8_t fadeAmount)
{
  if (bgColor.red == 255) {
    for ( uint16_t i = 0; i < N; i++) {
      fadeTowardColor( L[i], bgColor, fadeAmount + 10);
    }
  }
  else if (bgColor.red == 0 && bgColor.green == 0 && bgColor.blue == 0) {
    for ( uint16_t i = 0; i < N; i++) {
      fadeTowardColor( L[i], bgColor, fadeAmount + 20);
    }
  }
  else {
    for ( uint16_t i = 0; i < N; i++) {
      fadeTowardColor( L[i], bgColor, fadeAmount);
    }
  }
}

void SetBrightness() {
  int c = 2.55 * device_state.brightnessTemp;
  int t = 2.55 * device_state.brightness;
  if (c == t) return;
  if (c > t) {
    for (int i = 0; i < (c - t) + 1; i++) {
      FastLED.setBrightness(c - i);
      FastLED.delay(5);
      if (c == t) break;
    }
  }
  if (c < t) {
    for (int i = 0; i < (t - c) + 1; i++) {
      FastLED.setBrightness(c + i);
      FastLED.delay(5);
      if (c == t) break;
    }
  }
  device_state.brightnessTemp = device_state.brightness;
}

/************************************************************COLOR STUFF************************************************************/

/************************************************************OTA STUFF************************************************************/

void SetupArduinoOTA() {

  ArduinoOTA.setHostname("UpperLEDs");

  ArduinoOTA.setPassword("Ilikesopa2@");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });

}

/************************************************************OTA STUFF************************************************************/


void setup() {
  //Serial.begin(BAUD_RATE); Serial.printf("\r\n\r\n");

  pinMode(LEDPIN, OUTPUT);

  SetupArduinoOTA();
  setupColorTemperatureIndex(); // setup our helper map
  setupWiFi();
  setupSinricPro();
  Blynk.begin(auth, WIFI_SSID, WIFI_PASS);
  ArduinoOTA.begin();

  e131.begin(WIFI_SSID, WIFI_PASS);

  FastLED.addLeds<NEOPIXEL, LEDPIN>(strip, NUMLEDS);

  FastLED.setMaxPowerInVoltsAndMilliamps(VOLTS, MILLIAMPS);
}

void loop() {
  Blynk.run();
  SinricPro.handle();
  checkConnection();
  if (STATE == 0) {
    //fadeToBlackBy(strip, NUMLEDS, 15);
  }
  else if (STATE == 1) {
    LastState = STATE;
  }
  else if (STATE == 2) {
    effect();
    LastState = STATE;
  }



  if (ota == 1) {
    ArduinoOTA.handle();
  }
}
