#pragma once
#include "command_authority.hpp"
#include <string>

namespace openscumrcon::registry
{
// Game thread only. No gameplay objects are cached across requests.
std::string dispatch(const std::string& text, CommandAuthority authority);
}
