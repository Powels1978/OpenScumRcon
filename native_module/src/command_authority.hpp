#pragma once

namespace openscumrcon
{
// Internal metadata, never parsed from command text or a player SteamID.
// Only the RCON transport supplies authenticated_rcon after password validation.
enum class CommandAuthority { none, authenticated_rcon };
constexpr bool is_rcon_authorized(CommandAuthority authority)
{
    return authority == CommandAuthority::authenticated_rcon;
}
}
