#include "response_capture.hpp"
#include "godmode_trace.hpp"
#include <cstring>
#include <memory>
#include <string>
#define NOMINMAX
#include <windows.h>
#include <polyhook2/Detour/x64Detour.hpp>

namespace openscumrcon::response_capture
{
namespace
{
    std::unique_ptr<PLH::x64Detour> detour;
    std::uint64_t trampoline = 0;
    bool hooked = false;
    // Shared AdminCommand response method, verified for the supported SCUM build.
    constexpr std::uintptr_t response_rva = 0x18f4e00;
    struct StringHeader { const wchar_t* data; std::int32_t num, capacity; };
    using Send = void(*)(void*, void*, bool, bool);
    bool read(const void* source, void* target, std::size_t bytes)
    {
        SIZE_T done = 0;
        return source && ReadProcessMemory(GetCurrentProcess(), source, target, bytes, &done) && done == bytes;
    }
    void receive(void* command, void* message, bool flag1, bool flag2)
    {
        // Foreign calls always pass through, with every argument unchanged.
        if (!ResponseScope::matches(command))
        { reinterpret_cast<Send>(trampoline)(command, message, flag1, flag2); return; }
        try
        {
            StringHeader header{};
            if (!read(message, &header, sizeof(header)) || header.num < 0 || header.num > 1048576 ||
                header.capacity < header.num || (header.num && !header.data))
            { ResponseScope::failure(); return; }
            if (!header.num) { ResponseScope::accept(command, {}); return; }
            // Read a bounded prefix; FString's count includes the trailing NUL.
            const auto n = std::min<std::int32_t>(header.num, 65538);
            std::wstring wide(n, L'\0');
            if (!read(header.data, wide.data(), n * sizeof(wchar_t))) { ResponseScope::failure(); return; }
            if (n == header.num && !wide.empty() && wide.back() == L'\0') wide.pop_back();
            if (n < header.num && !wide.empty() && wide.back() >= 0xd800 && wide.back() <= 0xdbff) wide.pop_back();
            for (auto& c : wide) if (c == L'\0' || (c < 32 && c != L'\n' && c != L'\r' && c != L'\t')) c = L' ';
            const int bytes = wide.empty() ? 0 : WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(),
                static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
            if (!wide.empty() && !bytes) { ResponseScope::failure(); return; }
            std::string text(bytes, '\0');
            if (bytes) WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()),
                text.data(), bytes, nullptr, nullptr);
            ResponseScope::accept(command, text);
        }
        catch (...) { ResponseScope::failure(); }
        // Only synchronous replies belonging to our own command are redirected.
    }
}
std::uintptr_t function_address()
{ return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) + response_rva; }
bool ready() { return hooked; }
bool initialize()
{
    if (hooked) return true;
    if (!godmode_trace::supported_build()) return false;
    constexpr unsigned char expected[] = {
        0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,
        0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x59,0x20
    };
    unsigned char actual[sizeof(expected)]{};
    if (!read(reinterpret_cast<void*>(function_address()), actual, sizeof(actual)) ||
        std::memcmp(actual, expected, sizeof(actual))) return false;
    detour = std::make_unique<PLH::x64Detour>(function_address(), reinterpret_cast<std::uint64_t>(&receive), &trampoline);
    hooked = detour->hook();
    return hooked;
}
void shutdown()
{
    if (hooked && detour) detour->unHook();
    hooked = false;
}
}
