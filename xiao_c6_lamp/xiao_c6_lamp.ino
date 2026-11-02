// XIAO ESP32-C6 Zigbee lamp with a ring of 8 SK6812 / WS2812B LEDs (Zigbee router, always on)
//   D0 -> DI pad of the ring, through a 330 ohm resistor (pads on the back: DO, GND, 5V, DI)
//
// Arduino IDE / arduino-cli settings (arduino-esp32 >= 3.3):
//   Board:            XIAO_ESP32C6
//   Zigbee mode:      Zigbee ZCZR (coordinator/router)
//   Partition scheme: Zigbee ZCZR 4MB with spiffs
//   Erase all flash:  Enabled on the first upload only
//
// Zigbee endpoint (matched by z2m/xiao_c6_lamp.mjs):
//   10  colour dimmable light: on/off, brightness, colour (xy and hue/saturation),
//       colour temperature 2000-6500 K, power-on behaviour, identify
//
// The lamp is mains (USB) powered and never sleeps, so it is a Zigbee router: it also passes
// on messages for other devices, like the range extender does.
//
// The Zigbee stack runs on its own task and changes the light through the callbacks below.
// They only store the wanted colour and brightness; loop() fades the ring towards it, so every
// change is smooth, including the steps of a transition sent from Home Assistant.
//
// BOOT button: short press switches the lamp on or off, hold 5 s to leave the network
// (the ring flashes red, the board restarts and looks for a network to join).

#ifndef ZIGBEE_MODE_ZCZR
#error "Zigbee router mode is not selected in Tools->Zigbee mode"
#endif

#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include "Zigbee.h"

/************************ User settings *****************************/
#define DEBUG_LOG            0     // 1 = print to USB serial
#define USE_EXTERNAL_ANTENNA 0     // 1 = U.FL connector, 0 = on-board ceramic antenna

#define LED_PIN        D0          // DI pad of the ring
#define LED_COUNT      8
#define MAX_CURRENT_MA 500         // the ring is dimmed as a whole to stay below this; full white needs about 480
#define CHANNEL_MA     20          // current of one colour of one LED at full brightness
#define WHITE_BALANCE_R 1.0f       // scale the colours if white looks tinted on your ring
#define WHITE_BALANCE_G 1.0f
#define WHITE_BALANCE_B 1.0f
#define GAMMA          2.2f        // brightness and colours are perceived, the LEDs are linear

#define FADE_MS        400         // time for every change, on and off included
#define FRAME_MS       10
#define MIREDS_MIN     153         // 6500 K
#define MIREDS_MAX     500         // 2000 K

// Light of a brand new lamp, until it is changed from Home Assistant (kept in flash afterwards)
#define DEFAULT_LEVEL  127
#define DEFAULT_MIREDS 370         // 2700 K, warm white

#define SAVE_DELAY_MS        5000  // save the light to flash once it has not changed for this long
#define SHORT_PRESS_MAX_MS   1000
#define FACTORY_RESET_HOLD_MS 5000
#define STARTUP_POLL_MS      1000  // how often the power-on behaviour attribute is checked for a change
/********************************************************************/

#if DEBUG_LOG
#define LOG(...) Serial.printf(__VA_ARGS__)
#else
#define LOG(...)
#endif

#define EP_LAMP 10

// Values of the ZCL StartUpOnOff attribute
#define STARTUP_OFF      0x00
#define STARTUP_ON       0x01
#define STARTUP_TOGGLE   0x02
#define STARTUP_PREVIOUS 0xFF

// The library's light has no power-on behaviour; this adds the attribute to the On/Off cluster
class LampEndpoint : public ZigbeeColorDimmableLight {
public:
  LampEndpoint(uint8_t endpoint) : ZigbeeColorDimmableLight(endpoint) {}

  bool addStartUpOnOff(uint8_t value) {
    esp_zb_attribute_list_t *onOff = esp_zb_cluster_list_get_cluster(_cluster_list, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    return onOff != nullptr && esp_zb_on_off_cluster_add_attr(onOff, ESP_ZB_ZCL_ATTR_ON_OFF_START_UP_ON_OFF, &value) == ESP_OK;
  }
};

LampEndpoint zbLamp = LampEndpoint(EP_LAMP);
Adafruit_NeoPixel ring(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);
Preferences prefs;

// The light as Zigbee sees it. Colour is kept in the mode it was last set in, so it can be
// handed back to the stack the same way after a restart.
struct Light {
  bool on;
  uint8_t level;                              // 1-254
  uint8_t mode;                               // ZIGBEE_COLOR_MODE_
  uint8_t r, g, b;                            // xy mode: colour at full brightness
  uint8_t hue, sat;                           // hue/saturation mode: 0-254 each
  uint16_t mireds;                            // colour temperature mode
};
Light light;
uint8_t startUpOnOff = STARTUP_PREVIOUS;

// light is written by the Zigbee task (callbacks) and read by loop()
portMUX_TYPE lightMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool lightChanged = false;
volatile uint32_t identifyUntil = 0;          // millis() at which identify ends, 0 = off

// Fade, in perceived brightness 0-1 per colour (before gamma)
float shown[3] = {0, 0, 0};
float fadeFrom[3] = {0, 0, 0};
float fadeTo[3] = {0, 0, 0};
uint32_t fadeStartMs = 0;
bool fading = false;

/************************ Colour *****************************/
// Colour temperature to RGB, after Tanner Helland's fit of the black body curve
void miredsToRgb(uint16_t mireds, float rgb[3]) {
  float t = 10000.0f / constrain(mireds, MIREDS_MIN, MIREDS_MAX);  // kelvin / 100
  float r, g, b;
  if (t <= 66) {
    r = 255;
    g = 99.4708025861f * logf(t) - 161.1195681661f;
    b = t <= 19 ? 0 : 138.5177312231f * logf(t - 10) - 305.0447927307f;
  } else {
    r = 329.698727446f * powf(t - 60, -0.1332047592f);
    g = 288.1221695283f * powf(t - 60, -0.0755148492f);
    b = 255;
  }
  rgb[0] = constrain(r, 0.0f, 255.0f) / 255.0f;
  rgb[1] = constrain(g, 0.0f, 255.0f) / 255.0f;
  rgb[2] = constrain(b, 0.0f, 255.0f) / 255.0f;
}

// Zigbee hue and saturation (0-254) to RGB at full brightness
void hueSatToRgb(uint8_t hue, uint8_t sat, float rgb[3]) {
  float h = hue * 6.0f / 254.0f;
  float s = sat / 254.0f;
  int sector = min((int)h, 5);
  float f = h - sector;
  float p = 1 - s, q = 1 - s * f, t = 1 - s * (1 - f);
  const float table[6][3] = {{1, t, p}, {q, 1, p}, {p, 1, t}, {p, q, 1}, {t, p, 1}, {1, p, q}};
  for (int i = 0; i < 3; i++) {
    rgb[i] = table[sector][i];
  }
}

// What the ring should show for a light: perceived brightness per colour, 0-1
void lightToTarget(const Light &l, float target[3]) {
  float rgb[3];
  switch (l.mode) {
    case ZIGBEE_COLOR_MODE_TEMPERATURE:    miredsToRgb(l.mireds, rgb); break;
    case ZIGBEE_COLOR_MODE_HUE_SATURATION: hueSatToRgb(l.hue, l.sat, rgb); break;
    default:                               rgb[0] = l.r / 255.0f; rgb[1] = l.g / 255.0f; rgb[2] = l.b / 255.0f; break;
  }
  // Scale the brightest colour to full, so brightness comes from the level alone
  float top = max(rgb[0], max(rgb[1], rgb[2]));
  float scale = (l.on && top > 0) ? (l.level / 254.0f) / top : 0;
  for (int i = 0; i < 3; i++) {
    target[i] = rgb[i] * scale;
  }
}

/************************ Zigbee callbacks *****************************/
// These run on the Zigbee task. They only update light; loop() does the rest.
void updateLight(const Light &l) {
  portENTER_CRITICAL(&lightMux);
  light = l;
  portEXIT_CRITICAL(&lightMux);
  lightChanged = true;
}

Light copyLight() {
  portENTER_CRITICAL(&lightMux);
  Light l = light;
  portEXIT_CRITICAL(&lightMux);
  return l;
}

void onLightRgb(bool state, uint8_t red, uint8_t green, uint8_t blue, uint8_t level) {
  Light l = copyLight();
  l.on = state;
  l.level = level;
  l.mode = ZIGBEE_COLOR_MODE_CURRENT_X_Y;
  l.r = red;
  l.g = green;
  l.b = blue;
  updateLight(l);
  LOG("Light xy: %d level %u rgb %u %u %u\n", state, level, red, green, blue);
}

void onLightHsv(bool state, uint8_t hue, uint8_t saturation, uint8_t value) {
  Light l = copyLight();
  l.on = state;
  l.level = value;
  l.mode = ZIGBEE_COLOR_MODE_HUE_SATURATION;
  l.hue = hue;
  l.sat = saturation;
  updateLight(l);
  LOG("Light hs: %d level %u hue %u sat %u\n", state, value, hue, saturation);
}

void onLightTemp(bool state, uint8_t level, uint16_t mireds) {
  Light l = copyLight();
  l.on = state;
  l.level = level;
  l.mode = ZIGBEE_COLOR_MODE_TEMPERATURE;
  l.mireds = mireds;
  updateLight(l);
  LOG("Light ct: %d level %u mireds %u\n", state, level, mireds);
}

// Called with the identify time in seconds, and with 0 when it is over
void onIdentify(uint16_t seconds) {
  identifyUntil = seconds ? millis() + seconds * 1000UL : 0;
}

/************************ Flash *****************************/
void loadLight() {
  prefs.begin("lamp", true);
  light.on = prefs.getBool("on", true);
  light.level = prefs.getUChar("level", DEFAULT_LEVEL);
  light.mode = prefs.getUChar("mode", ZIGBEE_COLOR_MODE_TEMPERATURE);
  light.r = prefs.getUChar("r", 255);
  light.g = prefs.getUChar("g", 255);
  light.b = prefs.getUChar("b", 255);
  light.hue = prefs.getUChar("hue", 0);
  light.sat = prefs.getUChar("sat", 0);
  light.mireds = prefs.getUShort("mireds", DEFAULT_MIREDS);
  startUpOnOff = prefs.getUChar("startup", STARTUP_PREVIOUS);
  prefs.end();
}

// Preferences writes only the values that differ from what is stored
void saveLight(const Light &l) {
  prefs.begin("lamp", false);
  prefs.putBool("on", l.on);
  prefs.putUChar("level", l.level);
  prefs.putUChar("mode", l.mode);
  prefs.putUChar("r", l.r);
  prefs.putUChar("g", l.g);
  prefs.putUChar("b", l.b);
  prefs.putUChar("hue", l.hue);
  prefs.putUChar("sat", l.sat);
  prefs.putUShort("mireds", l.mireds);
  prefs.end();
  LOG("Light saved\n");
}

void saveStartUp(uint8_t value) {
  prefs.begin("lamp", false);
  prefs.putUChar("startup", value);
  prefs.end();
  LOG("Power-on behaviour saved: 0x%02x\n", value);
}

/************************ Zigbee state *****************************/
// Hand the light restored from flash to the stack, so Zigbee2MQTT reads the real state
void restoreZigbeeLight(const Light &l) {
  switch (l.mode) {
    case ZIGBEE_COLOR_MODE_TEMPERATURE:
      zbLamp.setLightColorTemperature(l.mireds);
      zbLamp.setLightLevel(l.level);
      zbLamp.setLightState(l.on);
      break;
    case ZIGBEE_COLOR_MODE_HUE_SATURATION:
      zbLamp.setLightColor(espHsvColor_t{l.hue, l.sat, l.level});
      zbLamp.setLightState(l.on);
      break;
    default:
      zbLamp.setLight(l.on, l.level, l.r, l.g, l.b);
      break;
  }
}

// Zigbee2MQTT writes the power-on behaviour straight into the attribute, without a callback,
// so it is read back from time to time and saved when it changes
void checkStartUpOnOff() {
  static uint32_t lastPollMs = 0;
  if (!Zigbee.started() || millis() - lastPollMs < STARTUP_POLL_MS) {
    return;
  }
  lastPollMs = millis();
  uint8_t value = startUpOnOff;
  esp_zb_lock_acquire(portMAX_DELAY);
  esp_zb_zcl_attr_t *attr = esp_zb_zcl_get_attribute(EP_LAMP, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_ON_OFF_START_UP_ON_OFF);
  if (attr != nullptr && attr->data_p != nullptr) {
    value = *(uint8_t *)attr->data_p;
  }
  esp_zb_lock_release();
  if (value != startUpOnOff) {
    startUpOnOff = value;
    saveStartUp(value);
  }
}

/************************ Ring *****************************/
// Sets the ring to perceived brightness values (0-1 per colour), with gamma and current limit
void showColour(const float perceived[3]) {
  const float balance[3] = {WHITE_BALANCE_R, WHITE_BALANCE_G, WHITE_BALANCE_B};
  float out[3];
  float top = 0;
  for (int i = 0; i < 3; i++) {
    out[i] = 255.0f * powf(constrain(perceived[i], 0.0f, 1.0f), GAMMA) * balance[i];
    top = max(top, out[i]);
  }
  float currentMa = (out[0] + out[1] + out[2]) / 255.0f * CHANNEL_MA * LED_COUNT;
  float limit = currentMa > MAX_CURRENT_MA ? MAX_CURRENT_MA / currentMa : 1.0f;
  uint8_t c[3];
  for (int i = 0; i < 3; i++) {
    c[i] = (uint8_t)lroundf(out[i] * limit);
  }
  // The lowest levels would round to black: keep the strongest colour at least at 1
  if (top > 0 && c[0] == 0 && c[1] == 0 && c[2] == 0) {
    int strongest = out[0] >= out[1] ? (out[0] >= out[2] ? 0 : 2) : (out[1] >= out[2] ? 1 : 2);
    c[strongest] = 1;
  }
  ring.fill(ring.Color(c[0], c[1], c[2]));
}

void startFade(const float target[3]) {
  for (int i = 0; i < 3; i++) {
    fadeFrom[i] = shown[i];
    fadeTo[i] = target[i];
  }
  fadeStartMs = millis();
  fading = true;
}

// One frame of the ring: identify blink, fade, and the pairing hint
void renderFrame() {
  uint32_t now = millis();
  uint32_t until = identifyUntil;
  if (until != 0 && (int32_t)(until - now) > 0) {
    ring.fill((now / 250) % 2 ? ring.Color(120, 120, 120) : 0);
    ring.show();
    return;
  }
  if (until != 0) {
    identifyUntil = 0;
    fading = true;  // repaint the light
  }

  if (fading) {
    float t = min(1.0f, (now - fadeStartMs) / (float)FADE_MS);
    t = t * t * (3 - 2 * t);  // ease in and out
    for (int i = 0; i < 3; i++) {
      shown[i] = fadeFrom[i] + (fadeTo[i] - fadeFrom[i]) * t;
    }
    fading = t < 1.0f;
  }
  showColour(shown);

  // Not on a network yet and dark: one LED breathes blue, so pairing mode can be seen
  bool dark = shown[0] == 0 && shown[1] == 0 && shown[2] == 0;
  if (!Zigbee.connected() && dark) {
    float breath = (1 - cosf(now * 2 * PI / 3000.0f)) / 2;
    ring.setPixelColor(0, ring.Color(0, 0, 2 + (uint8_t)(breath * 30)));
  }
  ring.show();
}

/************************ Button *****************************/
void flashRed(int times) {
  for (int i = 0; i < times; i++) {
    ring.fill(ring.Color(80, 0, 0));
    ring.show();
    delay(150);
    ring.clear();
    ring.show();
    delay(150);
  }
}

// Short press toggles the lamp, a long hold leaves the network. Never blocks.
void checkButton() {
  static uint32_t pressedMs = 0;
  static bool pressed = false;
  bool down = digitalRead(BOOT_PIN) == LOW;
  uint32_t now = millis();

  if (down && !pressed) {
    pressed = true;
    pressedMs = now;
  } else if (down && now - pressedMs >= FACTORY_RESET_HOLD_MS) {
    LOG("Factory reset\n");
    flashRed(3);
    Zigbee.factoryReset(true);
  } else if (!down && pressed) {
    pressed = false;
    uint32_t held = now - pressedMs;
    if (held >= 30 && held < SHORT_PRESS_MAX_MS) {
      // Goes through the stack, so the new state is reported to Zigbee2MQTT as well
      zbLamp.setLightState(!zbLamp.getLightState());
    }
  }
}

/************************ Antenna *****************************/
// The XIAO ESP32-C6 has an RF switch between the radio and its two antennas:
//   WIFI_ENABLE (GPIO3)      LOW  = switch powered
//   WIFI_ANT_CONFIG (GPIO14) HIGH = external U.FL antenna, LOW = on-board antenna
void antennaOn() {
  pinMode(WIFI_ENABLE, OUTPUT);
  digitalWrite(WIFI_ENABLE, LOW);
  pinMode(WIFI_ANT_CONFIG, OUTPUT);
  digitalWrite(WIFI_ANT_CONFIG, USE_EXTERNAL_ANTENNA ? HIGH : LOW);
}

/************************ Arduino *****************************/
void setup() {
#if DEBUG_LOG
  Serial.begin(115200);
  delay(1500);
  LOG("Lamp starting, restart reason %d\n", (int)esp_reset_reason());
#endif

  pinMode(BOOT_PIN, INPUT_PULLUP);
  ring.begin();
  ring.clear();
  ring.show();

  // The light comes on straight away, before the network is there
  loadLight();
  switch (startUpOnOff) {
    case STARTUP_OFF:    light.on = false; break;
    case STARTUP_ON:     light.on = true; break;
    case STARTUP_TOGGLE: light.on = !light.on; break;
    default:             break;  // previous
  }
  Light restored = light;
  float target[3];
  lightToTarget(restored, target);
  startFade(target);
  LOG("Restored: on %d level %u mode %u, power-on behaviour 0x%02x\n", restored.on, restored.level, restored.mode, startUpOnOff);

  antennaOn();

  zbLamp.setManufacturerAndModel("CustomDIY", "XIAO_C6_Lamp");
  zbLamp.setPowerSource(ZB_POWER_SOURCE_MAINS);
  zbLamp.setLightColorCapabilities(ZIGBEE_COLOR_CAPABILITY_X_Y | ZIGBEE_COLOR_CAPABILITY_HUE_SATURATION | ZIGBEE_COLOR_CAPABILITY_COLOR_TEMP);
  zbLamp.setLightColorTemperatureRange(MIREDS_MIN, MIREDS_MAX);
  zbLamp.addStartUpOnOff(startUpOnOff);
  zbLamp.onLightChangeRgb(onLightRgb);
  zbLamp.onLightChangeHsv(onLightHsv);
  zbLamp.onLightChangeTemp(onLightTemp);
  zbLamp.onIdentify(onIdentify);
  Zigbee.addEndpoint(&zbLamp);

  // A router starts at once and keeps looking for a network in the background, so this does
  // not wait for the coordinator
  esp_zb_cfg_t zigbeeConfig = ZIGBEE_DEFAULT_ROUTER_CONFIG();
  if (!Zigbee.begin(&zigbeeConfig)) {
    LOG("Zigbee failed to start, restarting\n");
    delay(1000);
    ESP.restart();
  }

  restoreZigbeeLight(restored);
  // The library callbacks above have just rebuilt light from the stack; take the restored one
  updateLight(restored);
  lightChanged = false;
  saveLight(restored);  // a toggle at power-on is the new previous state

  enableLoopWDT();
}

void loop() {
  static uint32_t lastFrameMs = 0;
  static uint32_t lastChangeMs = 0;
  static bool unsaved = false;
  uint32_t now = millis();

  checkButton();
  checkStartUpOnOff();

  if (lightChanged) {
    lightChanged = false;
    float target[3];
    lightToTarget(copyLight(), target);
    startFade(target);
    lastChangeMs = now;
    unsaved = true;
  }

  if (unsaved && now - lastChangeMs >= SAVE_DELAY_MS) {
    unsaved = false;
    saveLight(copyLight());
  }

  if (now - lastFrameMs >= FRAME_MS) {
    lastFrameMs = now;
    renderFrame();
  }
  delay(1);
}
