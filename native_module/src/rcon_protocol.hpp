#pragma once

// Valve Source RCON wire protocol: authenticated requests and UTF-8 responses.

#include <cstdint>
#include <string>
#include <vector>

namespace openscumrcon::protocol
{
    constexpr std::int32_t SERVERDATA_RESPONSE_VALUE = 0;
    constexpr std::int32_t SERVERDATA_EXECCOMMAND = 2;
    constexpr std::int32_t SERVERDATA_AUTH_RESPONSE = 2;
    constexpr std::int32_t SERVERDATA_AUTH = 3;

    // A 4096-byte body includes 8 header bytes and two NUL terminators.
    // The 4-byte length prefix is separate; payloads split at UTF-8 boundaries.
    constexpr std::size_t MAX_RESPONSE_CHUNK = 4086;

    struct Packet
    {
        std::int32_t request_id = 0;
        std::int32_t type = 0;
        std::string payload;
    };

    // Serializes a packet:
    // <size:int32><request_id:int32><type:int32><payload><NUL><NUL>
    std::vector<char> encode_packet(std::int32_t request_id, std::int32_t type, const std::string& payload);

    // Splits `text` into one or more SERVERDATA_RESPONSE_VALUE packets sized
    // so the encoded payload never exceeds MAX_RESPONSE_CHUNK bytes.
    std::vector<std::vector<char>> encode_response(std::int32_t request_id, const std::string& text);

    // Parses the body after the separately read 4-byte size prefix.
    // Rejects short bodies, missing terminators and embedded NUL payload bytes.
    bool decode_packet_body(const std::vector<char>& body, Packet& out);
}
