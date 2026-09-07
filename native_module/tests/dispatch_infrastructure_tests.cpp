#include "dispatch_request.hpp"
#include "response_buffer.hpp"
#include "rcon_protocol.hpp"
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <stdexcept>
using namespace openscumrcon;
void require(bool pass, const char* label)
{
    if (!pass) { std::cerr << label << '\n'; std::exit(1); }
}
int main()
{
    const std::string id = "76561198000000001";
    require(parse_dispatch_request(" \t#ListPlayers\r\n").action == DispatchAction::players, "players whitespace");
    require(parse_dispatch_request("ListPlayers extra").action == DispatchAction::invalid, "players rejects arguments");
    require(parse_dispatch_request("!commands GOD").filter == "god", "catalogue filter");
    const auto parsed = parse_dispatch_request("!exec " + id + " #Test \"hello world\" \"say \\\"hello\\\"\" \"\" 12");
    require(parsed.action == DispatchAction::execute && parsed.steam_id == id && parsed.verb == "Test", "explicit target");
    require(parsed.arguments == std::vector<std::string>{"hello world", "say \"hello\"", "", "12"}, "quoted argument preservation");
    for (const auto& bad : std::vector<std::string>{"", "!exec", "!exec 1 SetTime", "!exec " + id + " !commands",
         "!exec " + id + " Test \"unterminated", "!exec " + id + " Test\nShutdownServer",
         "!commands a b", "!exec " + id + " Test" + std::string("\0junk", 5)})
        require(parse_dispatch_request(bad).action == DispatchAction::invalid, "malformed command rejected");
    require(parse_dispatch_request(std::string(8193, 'a')).action == DispatchAction::invalid, "request cap");
    auto many = "!exec " + id + " Test";
    for (int i = 0; i < 65; ++i) many += " x";
    require(parse_dispatch_request(many).action == DispatchAction::invalid, "argument cap");
    require(parse_dispatch_request("SetTime 12").action == DispatchAction::unknown, "no implicit executor");

    int owner = 0, foreign = 0;
    {
        ResponseScope scope(&owner);
        require(!ResponseScope::accept(&foreign, "foreign"), "foreign reply passes through");
        require(ResponseScope::accept(&owner, "first"), "owned reply intercepted");
        require(std::async(std::launch::async, [&] { return !ResponseScope::accept(&owner, "other thread"); }).get(), "thread isolation");
        try
        {
            ResponseScope nested(&foreign);
            require(!ResponseScope::accept(&owner, "outer during nested"), "nested identity isolation");
            require(ResponseScope::accept(&foreign, "nested"), "nested reply captured");
            throw std::runtime_error("scope test");
        }
        catch (const std::runtime_error&) {}
        ResponseScope::accept(&owner, "second");
        require(scope.text() == "first\nsecond" && scope.messages() == 2, "restored scope and line joining");
    }
    require(!ResponseScope::matches(&owner), "scope cleaned");
    const std::string smile = "\xf0\x9f\x98\x80";
    {
        ResponseScope scope(&owner);
        const std::string oversized = std::string(ResponseScope::max_bytes - 1, 'a') + smile;
        ResponseScope::accept(&owner, oversized);
        require(scope.truncated() && scope.text() == std::string(ResponseScope::max_bytes - 1, 'a'), "UTF-8 bounded capture");
    }
    {
        ResponseScope scope(&owner);
        for (std::size_t i = 0; i < ResponseScope::max_messages + 1; ++i) ResponseScope::accept(&owner, {});
        require(scope.truncated() && scope.text().empty(), "message cap includes empty messages");
    }

    using namespace openscumrcon::protocol;
    for (std::size_t prefix : {MAX_RESPONSE_CHUNK - 1, MAX_RESPONSE_CHUNK - 2, MAX_RESPONSE_CHUNK - 3, MAX_RESPONSE_CHUNK})
    {
        const std::string original = std::string(prefix, 'a') + smile + std::string(9000, 'b');
        const auto packets = encode_response(42, original);
        std::string combined;
        for (const auto& bytes : packets)
        {
            int size = 0; std::memcpy(&size, bytes.data(), 4);
            require(size <= 4096 && size == static_cast<int>(bytes.size() - 4), "RCON size framing");
            Packet packet;
            require(decode_packet_body(std::vector<char>(bytes.begin() + 4, bytes.end()), packet), "packet roundtrip");
            require(packet.request_id == 42 && packet.type == SERVERDATA_RESPONSE_VALUE, "request association");
            require(packet.payload.empty() || (static_cast<unsigned char>(packet.payload.front()) & 0xc0) != 0x80, "UTF-8 packet boundary");
            combined += packet.payload;
        }
        require(combined == original, "multi-packet response complete");
    }
    require(encode_response(42, "").size() == 1, "empty response packet");
    auto encoded = encode_packet(7, SERVERDATA_EXECCOMMAND, "ListPlayers");
    std::vector<char> body(encoded.begin() + 4, encoded.end());
    Packet packet;
    body.back() = 'x';
    require(!decode_packet_body(body, packet), "missing terminator rejected");
    body.back() = '\0'; body[8] = '\0';
    require(!decode_packet_body(body, packet), "embedded NUL rejected");
    std::cout << "Dispatch infrastructure: parsing, capture isolation/limits and UTF-8 RCON framing passed\n";
}
