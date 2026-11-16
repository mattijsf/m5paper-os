// Mapping of an HTTP request to a fixture file name (docs/SIMULATOR.md "HTTP fixtures"): pure, shared by the
// simulator and the unit tests.
#pragma once

#include <string>

namespace pos {

// "GET", "https://ha.example.test:8123/api/states/sensor.x?a=1" -> "GET_ha.example.test_api_states_sensor.x_<hash>.json".
// Scheme and port are dropped, '/' in the path becomes '_', a query string adds `_` + 8 hex digits (FNV-1a of the query).
// The caller adds the directory; a `<name>.status` file next to the .json may hold a status code.
std::string fixtureFileName(const std::string& method, const std::string& url);
// The same name with `_b` + 8 hex digits (FNV-1a of the body) before ".json"; the plain name for an empty body. The simulator
// tries this one first and falls back to the plain name: two POSTs to one URL that differ only in their body (the forecast
// types of weather.get_forecasts) get their own fixtures, every other fixture keeps its name.
std::string fixtureFileName(const std::string& method, const std::string& url, const std::string& body);

}  // namespace pos
