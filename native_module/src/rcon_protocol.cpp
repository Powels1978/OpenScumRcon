#include "rcon_protocol.hpp"

#include <cstring>

namespace openscumrcon::protocol
{
    std::vector<char> encode_packet(std::int32_t request_id, std::int32_t type, const std::string& payload)
    {
        // body = request_id(4) + type(4) + payload + NUL + NUL
        const std::size_t body_size = 8 + payload.size() + 2;
        std::vector<char> out(4 + body_size);

        std::int32_t size = static_cast<std::int32_t>(body_size);
        std::memcpy(out.data(), &size, 4);
        std::memcpy(out.data() + 4, &request_id, 4);
        std::memcpy(out.data() + 8, &type, 4);
        if (!payload.empty())
        {
            std::memcpy(out.data() + 12, payload.data(), payload.size());
        }
        out[out.size() - 2] = '\0';
        out[out.size() - 1] = '\0';
        return out;
    }

    std::vector<std::vector<char>> encode_response(std::int32_t request_id, const std::string& text)
    {
        std::vector<std::vector<char>> packets;
        if (text.empty())
        {
            packets.push_back(encode_packet(request_id, SERVERDATA_RESPONSE_VALUE, ""));
            return packets;
        }
        std::size_t offset = 0;
        while (offset < text.size())
        {
            const std::size_t chunk_len = std::min(MAX_RESPONSE_CHUNK, text.size() - offset);
            packets.push_back(encode_packet(request_id, SERVERDATA_RESPONSE_VALUE, text.substr(offset, chunk_len)));
            offset += chunk_len;
        }
        return packets;
    }

    bool decode_packet_body(const std::vector<char>& body, Packet& out)
    {
        if (body.size() < 10)
        {
            return false;
        }
        std::int32_t request_id = 0;
        std::int32_t type = 0;
        std::memcpy(&request_id, body.data(), 4);
        std::memcpy(&type, body.data() + 4, 4);
        // Body is request_id(4) + type(4) + payload + NUL + NUL.
        out.request_id = request_id;
        out.type = type;
        out.payload.assign(body.begin() + 8, body.end() - 2);
        return true;
    }
}
