#pragma once
#include <optional>
#include <string>
#include "command_authority.hpp"

namespace openscumrcon::godmode
{
// Call initialize before diagnostic detours; dispatch only on the game thread.
bool initialize();
// Unrelated commands return nullopt; malformed GodMode requests never fall back.
std::optional<std::string> dispatch(const std::string& command, CommandAuthority authority = CommandAuthority::none);
}
