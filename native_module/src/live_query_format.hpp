#pragma once
#include <array>
#include <cmath>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace openscumrcon
{
inline std::string ingame_clock(double hours)
{
    if (!std::isfinite(hours) || hours < 0 || hours > 24)
        throw std::runtime_error("invalid live time of day");
    const auto minute = static_cast<unsigned>(hours * 60) % 1440;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setfill('0') << std::setw(2) << minute / 60 << ':' << std::setw(2) << minute % 60;
    return out.str();
}
struct WeatherSnapshot
{
    // Values are current fields of the active WeatherController2, not overrides.
    double time_of_day;
    double wind_azimuth;
    double wind_intensity;
    double rain_intensity;
    double fog_density;
    std::optional<double> time_speed, sunrise, sunset, air_temperature, water_temperature;
    std::optional<double> cirrostratus, cumulonimbus, nimbostratus, max_wind_speed_kph;
};
inline std::string weather_json(const WeatherSnapshot& s, bool time_only)
{
    const auto clock = ingame_clock(s.time_of_day);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(9)
        << "{\"ok\":true,\"source\":\"live_scum_weather_controller\",\"timeOfDay\":"
        << s.time_of_day << ",\"timeOfDayText\":\"" << clock << '"';
    auto optional = [&](const char* key, std::optional<double> value) {
        out << ",\"" << key << "\":";
        if (value && std::isfinite(*value)) out << *value; else out << "null";
    };
    optional("timeOfDaySpeed", s.time_speed);
    if (!time_only)
    {
        for (const auto value : {s.wind_azimuth, s.wind_intensity, s.rain_intensity, s.fog_density})
            if (!std::isfinite(value)) throw std::runtime_error("invalid live weather value");
        out << ",\"windAzimuth\":" << s.wind_azimuth << ",\"windIntensity\":" << s.wind_intensity
            << ",\"rainIntensity\":" << s.rain_intensity << ",\"fogDensity\":" << s.fog_density;
        optional("sunriseTime", s.sunrise); optional("sunsetTime", s.sunset);
        optional("airTemperature", s.air_temperature); optional("waterTemperature", s.water_temperature);
        optional("cirrostratusCoverage", s.cirrostratus); optional("cumulonimbusCoverage", s.cumulonimbus);
        optional("nimbostratusCoverage", s.nimbostratus); optional("maxWindSpeedKph", s.max_wind_speed_kph);
        // A maximum is configuration, not measured current speed. Do not invent a conversion.
        out << ",\"windSpeedKph\":null";
    }
    out << '}';
    return out.str();
}
}
