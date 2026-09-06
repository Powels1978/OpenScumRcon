#pragma once
#include <algorithm>
#include <sstream>
#include <string>

namespace openscumrcon::godmode
{
enum class Action { unrelated, invalid, state, prepare, execute };
struct Request
{
    Action action = Action::unrelated;
    std::string steam_id;
    bool enabled = false;
};
inline std::string ascii_lower(std::string value)
{
    for (char& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}
inline Request parse(const std::string& text)
{
    std::istringstream input(text);
    std::string verb, value, extra;
    input >> verb;
    if (!verb.empty() && verb.front() == '#') verb.erase(0, 1);
    verb = ascii_lower(verb);
    Request result;
    if (verb == "setgodmode") result.action = Action::execute;
    else if (verb == "!godmode_state") result.action = Action::state;
    else if (verb == "!godmode_prepare") result.action = Action::prepare;
    else return result;
    if (result.action == Action::execute)
    {
        input >> value;
        value = ascii_lower(value);
        if (value != "true" && value != "false") result.action = Action::invalid;
        result.enabled = value == "true";
    }
    input >> result.steam_id;
    if (result.steam_id.size() != 17 ||
        !std::all_of(result.steam_id.begin(), result.steam_id.end(), [](char c) { return c >= '0' && c <= '9'; }) ||
        (input >> extra)) result.action = Action::invalid;
    return result;
}
}
