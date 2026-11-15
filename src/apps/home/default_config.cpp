// The default home.json (docs/apps/home.md "Config"): taken from the user's house dashboard source
// (read 2026-11-15). Entity ids are NOT yet verified against the live
// instance (STATUS.md, Pending device checks): the first Home refresh shows any that do not exist as "not found".
#include "apps/home/home_model.h"

namespace pos::home {

const char* defaultConfigJson() {
  return R"JSON({
  "tabs": [
    { "title": "Lights", "icon": "lightbulb", "items": [
      { "type": "scene", "name": "Evening", "calls": [
        { "service": "light.turn_on", "entity": "light.sofa_lamp", "data": { "brightness": 34, "color_temp_kelvin": 2000 } },
        { "service": "light.turn_on", "entity": "light.coffee_table", "data": { "brightness": 106 } },
        { "service": "light.turn_on", "entity": "light.entrance", "data": { "brightness": 106 } },
        { "service": "light.turn_on", "entity": "light.dining_table", "data": { "brightness": 126, "color_temp_kelvin": 2000 } },
        { "service": "light.turn_on", "entity": "light.dining_floor_lamp", "data": { "brightness": 114, "xy_color": [0.371, 0.239] } },
        { "service": "light.turn_on", "entity": "light.reading_corner", "data": { "brightness": 113 } },
        { "service": "light.turn_on", "entity": "light.kitchen_spots", "data": { "brightness": 102, "color_temp_kelvin": 2724 } }
      ] },
      { "type": "light", "entity": "light.downstairs_relative", "name": "Whole living room", "icon": "house", "group": "light.downstairs" },
      { "type": "light", "entity": "light.lounge_relative", "name": "Lounge", "icon": "sofa", "group": "light.lounge",
        "members": ["light.sofa_lamp", "light.coffee_table", "light.entrance"] },
      { "type": "light", "entity": "light.dining_area_relative", "name": "Dining area", "icon": "utensils", "group": "light.dining_area",
        "members": ["light.dining_table", "light.dining_floor_lamp"] },
      { "type": "light", "entity": "light.reading_corner", "name": "Reading corner", "icon": "blocks" },
      { "type": "light", "entity": "light.kitchen_spots_relative", "name": "Kitchen spots", "icon": "cooking-pot", "group": "light.kitchen_spots" },
      { "type": "light", "entity": "light.porch", "name": "Porch", "icon": "lamp-wall-down" },
      { "type": "light", "entity": "light.driveway", "name": "Driveway", "icon": "car" },
      { "type": "light", "entity": "light.hall", "name": "Hall", "icon": "lamp-wall-up" },
      { "type": "light", "entity": "light.bedside_lamp", "name": "Bedside lamp", "icon": "bed" }
    ] },
    { "title": "Climate", "icon": "thermometer", "items": [
      { "type": "climate", "entity": "climate.living_room", "name": "Living room" },
      { "type": "room", "name": "Outside", "temperature": "sensor.outdoor_temperature" },
      { "type": "room", "name": "Bedroom", "temperature": "sensor.bedroom_temperature", "humidity": "sensor.bedroom_humidity" },
      { "type": "room", "name": "Study", "temperature": "sensor.study_temperature", "humidity": "sensor.study_humidity" },
      { "type": "room", "name": "Bathroom", "temperature": "sensor.bathroom_temperature", "humidity": "sensor.bathroom_humidity" },
      { "type": "contact", "entity": "binary_sensor.bedroom_window", "name": "Bedroom window" }
    ] },
    { "title": "More", "icon": "settings", "items": [
      { "type": "switch", "entity": "switch.bathroom_fan", "name": "Bathroom fan", "icon": "fan" },
      { "type": "switch", "entity": "switch.air_conditioning", "name": "Air conditioning", "icon": "air-vent" },
      { "type": "fan", "entity": "fan.air_purifier", "name": "Air purifier", "icon": "wind" }
    ] }
  ]
})JSON";
}

}  // namespace pos::home
