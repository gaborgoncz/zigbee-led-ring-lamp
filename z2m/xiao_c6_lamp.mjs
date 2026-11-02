// Zigbee2MQTT external converter for the XIAO ESP32-C6 lamp
// (firmware: ../xiao_c6_lamp/xiao_c6_lamp.ino)
//
// What it does:
//   - gives the lamp a name instead of an auto-generated definition
//   - exposes it as a colour light: on/off, brightness, colour (xy and hue/saturation),
//     colour temperature 2000-6500 K, power-on behaviour, identify
//   - sets up reporting, so a press of the button on the lamp shows up in Home Assistant
//
// Install: Zigbee2MQTT UI -> Settings -> Dev console -> External converters -> create
// "xiao_c6_lamp.mjs", paste this file, save, restart Zigbee2MQTT.

import * as m from 'zigbee-herdsman-converters/lib/modernExtend';

export default {
    zigbeeModel: ['XIAO_C6_Lamp'],
    model: 'XIAO_C6_Lamp',
    vendor: 'CustomDIY',
    description: 'XIAO ESP32-C6 lamp with a ring of 8 WS2812B LEDs',
    extend: [
        m.light({
            // Must match MIREDS_MIN and MIREDS_MAX in the firmware
            colorTemp: {range: [153, 500], startup: false},
            color: {modes: ['xy', 'hs']},
            powerOnBehavior: true,
            effect: false,
            configureReporting: true,
        }),
        m.identify(),
    ],
};
