#include "command_queue.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
using namespace openscumrcon;
void require(bool pass, const char* label)
{
    if (!pass) { std::cerr << label << '\n'; std::exit(1); }
}
int main()
{
    CommandQueue queue;
    for (const auto* text : {"SetGodMode true 76561198000000001", "authenticated_rcon SetGodMode true 76561198000000001", "!godmode_prepare 76561198000000001"})
    {
        auto denied = queue.enqueue(text);
        require(denied.wait_for(std::chrono::seconds(0)) == std::future_status::ready, "untrusted request must finish immediately");
        require(denied.get() == "error: authenticated RCON connection required", "missing authority must be denied");
        require(queue.drain().empty(), "untrusted request must never reach game thread");
    }
    auto first = queue.enqueue("SetGodMode true 76561198000000001", CommandAuthority::authenticated_rcon);
    auto second = queue.enqueue("!godmode_state 76561198000000002", CommandAuthority::authenticated_rcon);
    auto items = queue.drain();
    require(items.size() == 2, "authorized requests queued");
    require(items[0].command_text == "SetGodMode true 76561198000000001", "target text preserved");
    require(is_rcon_authorized(items[0].authority) && is_rcon_authorized(items[1].authority), "authority survives queue handoff");
    items[0].response.set_value("first result");
    items[1].response.set_value("second result");
    require(first.get() == "first result" && second.get() == "second result", "responses remain associated with requests");
    require(queue.drain().empty(), "queue drained exactly once");
    std::cout << "RCON authority: untrusted requests blocked, authenticated handoff and response isolation passed\n";
}
