#include "dispatch_request.hpp"
#include "live_query_format.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace openscumrcon;
void check(bool pass, const char* label) { if (!pass) { std::cerr << label << '\n'; std::exit(1); } }
struct comma_decimal : std::numpunct<char> { char do_decimal_point() const override { return ','; } };
int main()
{
    check(parse_dispatch_request("#GetWeather").action == DispatchAction::weather, "weather read action");
    check(parse_dispatch_request("gettimeofday").action == DispatchAction::time_of_day, "time read action");
    for (const auto* request : {"GetWeather extra", "GetTimeOfDay 12", "GetWeather\nSetWeather 1"})
        check(parse_dispatch_request(request).action == DispatchAction::invalid, "read query rejects mutations/arguments");
    check(ingame_clock(0) == "00:00" && ingame_clock(24) == "00:00" && ingame_clock(7.75) == "07:45", "game clock");
    for (const auto invalid : {-1.0, 24.1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        bool rejected = false; try { ingame_clock(invalid); } catch (const std::runtime_error&) { rejected = true; }
        check(rejected, "invalid game clock rejected");
    }
    std::locale::global(std::locale(std::locale::classic(), new comma_decimal));
    WeatherSnapshot state{7.75, 1.25, 0.4, 0.0, 0.38};
    state.cirrostratus = 0.7; state.max_wind_speed_kph = 80;
    const auto json = weather_json(state, false);
    check(json.find("\"timeOfDay\":7.75") != std::string::npos, "locale independent JSON");
    check(json.find("\"rainIntensity\":0") != std::string::npos, "rain is not estimated from clouds");
    check(json.find("\"windSpeedKph\":null") != std::string::npos, "maximum is not reported as current wind speed");
    check(json.find("\"airTemperature\":null") != std::string::npos, "unavailable values not invented");
    check(weather_json(state, true).find("rainIntensity") == std::string::npos, "time-only response");
    state.fog_density = std::numeric_limits<double>::quiet_NaN();
    bool rejected = false; try { weather_json(state, false); } catch (const std::runtime_error&) { rejected = true; }
    check(rejected, "nonfinite weather cannot leak into JSON");
    std::cout << "Live queries: read-only parsing, clock bounds, native values and JSON formatting passed\n";
}
