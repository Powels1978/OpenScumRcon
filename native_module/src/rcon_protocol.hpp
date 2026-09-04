#pragma once

// Valve Source RCON wire protocol - server side.
// Mirrors the client-side implementation already proven in the sibling
// private project's local_bridge (SourceRcon class): same packet framing,
// same auth/command semantics, same 4096-byte-oriented multi-packet split
// for long responses. See docs/ARCHITECTURE.md "Protokollwahl".

#include <cstdint>
#include <string>
#include <vector>

namespace openscumrcon::protocol
{
    constexpr std::int32_t SERVERDATA_RESPONSE_VALUE = 0;
    constexpr std::int32_t SERVERDATA_EXECCOMMAND = 2;
    constexpr std::int32_t SERVERDATA_AUTH_RESPONSE = 2;
    constexpr std::int32_t SERVERDATA_AUTH = 3;

    // Practical payload chunk size for splitting long responses across
    // multiple SERVERDATA_RESPONSE_VALUE packets. Matches the 4096-byte
    // convention already handled by the existing client (see
    // local_bridge/powels_local_bridge.py, recv_packet()/SourceRcon.run()).
    constexpr std::size_t MAX_RESPONSE_CHUNK = 4096;

    struct Packet
    {
        std::int32_t request_id = 0;
        std::int32_t type = 0;
        std::string payload;
    };

    // Serializes a packet exactly like local_bridge's own `packet()` helper:
    // <size:int32><request_id:int32><type:int32><payload><NUL><NUL>
    std::vector<char> encode_packet(std::int32_t request_id, std::int32_t type, const std::string& payload);

    // Splits `text` into one or more SERVERDATA_RESPONSE_VALUE packets sized
    // so the encoded payload never exceeds MAX_RESPONSE_CHUNK bytes.
    std::vector<std::vector<char>> encode_response(std::int32_t request_id, const std::string& text);

    // Parses a packet body (everything after the 4-byte size prefix, which
    // the caller must already have read separately via a length-prefixed
    // recv loop - see RconServer::recv_exact). Returns false if the body is
    // too short to be a valid packet (matches recv_packet()'s `len(body) <
    // 10` guard in the Python client).
    bool decode_packet_body(const std::vector<char>& body, Packet& out);
}
