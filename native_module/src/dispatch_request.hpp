#pragma once
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace openscumrcon
{
inline std::string lower_ascii(std::string s)
{
    for (char& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return s;
}
inline bool steam_id_valid(std::string_view id)
{
    return id.size() == 17 && std::all_of(id.begin(), id.end(), [](char c) { return c >= '0' && c <= '9'; });
}
enum class DispatchAction { unknown, invalid, players, commands, execute };
struct DispatchRequest
{
    DispatchAction action = DispatchAction::unknown;
    std::string steam_id, verb, filter, error;
    std::vector<std::string> arguments;
};
inline DispatchRequest parse_dispatch_request(std::string_view text)
{
    DispatchRequest r;
    auto fail = [&](const char* why) { r.action = DispatchAction::invalid; r.error = why; return r; };
    if (text.size() > 8192) return fail("request exceeds 8192 bytes");
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == text.npos) return fail("empty command");
    text = text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
    std::vector<std::string> tokens;
    std::string token;
    bool quoted = false, started = false;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const unsigned char c = text[i];
        if (c < 32 && c != '\t') return fail("control characters and multiple command lines are not allowed");
        if (c == 127) return fail("control characters are not allowed");
        if (c == '"') { quoted = !quoted; started = true; continue; }
        if (quoted && c == '\\' && i + 1 < text.size() && (text[i+1] == '"' || text[i+1] == '\\'))
        { token += text[++i]; started = true; }
        else if (!quoted && (c == ' ' || c == '\t'))
        {
            if (started) { tokens.push_back(std::move(token)); token.clear(); started = false; }
        }
        else { token += static_cast<char>(c); started = true; }
        if (token.size() > 4096 || tokens.size() > 67) return fail("too many or oversized arguments");
    }
    if (quoted) return fail("unterminated double quote");
    if (started) tokens.push_back(std::move(token));
    if (tokens.empty()) return fail("empty command");
    auto verb = lower_ascii(tokens[0]);
    if (!verb.empty() && verb.front() == '#') verb.erase(0, 1);
    if (verb == "listplayers")
    {
        if (tokens.size() != 1) return fail("usage: ListPlayers");
        r.action = DispatchAction::players;
    }
    else if (verb == "!commands")
    {
        if (tokens.size() > 2) return fail("usage: !commands [filter]");
        r.action = DispatchAction::commands;
        if (tokens.size() == 2) r.filter = lower_ascii(tokens[1]);
    }
    else if (verb == "!exec")
    {
        if (tokens.size() < 3 || tokens.size() > 67 || !steam_id_valid(tokens[1]))
            return fail("usage: !exec <17-digit SteamID> <command> [arguments...]");
        r.action = DispatchAction::execute; r.steam_id = tokens[1]; r.verb = tokens[2];
        if (!r.verb.empty() && r.verb.front() == '#') r.verb.erase(0, 1);
        if (r.verb.empty() || !std::all_of(r.verb.begin(), r.verb.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        })) return fail("invalid command name");
        r.arguments.assign(tokens.begin() + 3, tokens.end());
    }
    return r;
}
}
