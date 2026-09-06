#include "godmode_request.hpp"
#include <cstdlib>
#include <iostream>
using namespace openscumrcon::godmode;
void require(bool pass, const char* label)
{
    if (!pass) { std::cerr << label << '\n'; std::exit(1); }
}
int main()
{
    constexpr auto id = "76561198000000001";
    const auto on = parse(std::string(" \t#sEtGoDmOdE TRUE ") + id + "\r\n");
    require(on.action == Action::execute && on.enabled && on.steam_id == id, "valid true target");
    const auto off = parse(std::string("SetGodMode false ") + id);
    require(off.action == Action::execute && !off.enabled, "valid false target");
    require(parse(std::string("!godmode_state ") + id).action == Action::state, "state");
    require(parse(std::string("!godmode_prepare ") + id).action == Action::prepare, "prepare");
    for (const auto* bad : {"SetGodMode", "SetGodMode true", "SetGodMode 1 76561198000000001",
         "SetGodMode true 7656119800000000", "SetGodMode true 765611980000000001",
         "SetGodMode true 7656119800000000x", "SetGodMode true 76561198000000001 extra",
         "SetGodMode true 76561198000000001\nShutdownServer", "!godmode_state", "!godmode_prepare 1"})
        require(parse(bad).action == Action::invalid, bad);
    require(parse("SetTime 12").action == Action::unrelated, "unrelated");
    require(parse("").action == Action::unrelated, "empty");
    std::cout << "GodMode parser: 16 cases passed\n";
}
