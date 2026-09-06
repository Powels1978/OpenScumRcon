#include "godmode_trace.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <vector>
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <polyhook2/Detour/x64Detour.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UObjectGlobals.hpp>

namespace openscumrcon::godmode_trace
{
namespace
{
    constexpr std::uintptr_t preferred_base = 0x140000000ULL;
    constexpr std::uintptr_t godmode_va = 0x1418e6530ULL;
    constexpr std::uint32_t image_timestamp = 0x6a904108;
    constexpr std::uint32_t image_size = 0x7e0c000;
    constexpr unsigned max_calls = 4;
    constexpr ULONGLONG window_ms = 120000;
    constexpr const char* log_path = "openscumrcon_godmode_trace.log";
    using ExecuteFn = bool(*)(void*, void*);
    std::unique_ptr<PLH::x64Detour> detour;
    std::uint64_t trampoline = 0;
    DWORD game_thread = 0;
    bool build_ok = false, hook_ok = false, interface_hook_ok = false;
    std::atomic<ULONGLONG> deadline{0};
    unsigned calls = 0;
    std::vector<std::string> pending;
    thread_local std::ostringstream* active = nullptr;
    thread_local unsigned interface_events = 0;

    bool read(const void* address, void* dst, std::size_t size)
    {
        SIZE_T done = 0;
        return address && ReadProcessMemory(GetCurrentProcess(), address, dst, size, &done) && done == size;
    }
    template<class T> bool read_at(const void* base, std::size_t offset, T& value)
    {
        return read(reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(base) + offset), &value, sizeof(value));
    }
    std::uintptr_t runtime(std::uintptr_t va)
    {
        return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) + va - preferred_base;
    }
    std::string utf8(const std::wstring& s)
    {
        if (s.empty()) return {};
        const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
        if (n <= 0) return "<encoding error>";
        std::string result(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), result.data(), n, nullptr, nullptr);
        return result;
    }
    bool registered(void* object)
    {
        std::int32_t index = -1;
        if (!read_at(object, 0x0c, index) || index < 0 || index >= RC::Unreal::FUObjectArray::GetNumElements()) return false;
        auto* item = RC::Unreal::FUObjectArray::IndexToObject(index);
        return item && item->GetUObject() == object && item->IsValid(false);
    }
    void describe_cpp(std::ostream& out, const char* label, void* object)
    {
        void* cls = nullptr; void* outer = nullptr; void* vtable = nullptr;
        std::uint32_t flags = 0;
        out << label << " address=" << object;
        if (!read_at(object, 0x10, cls) || !read_at(object, 0x20, outer) || !read_at(object, 8, flags))
        { out << " unreadable\n"; return; }
        read_at(object, 0, vtable);
        out << " class=" << cls << " outer=" << outer << " flags=0x" << std::hex << flags << std::dec
            << " cdo=" << ((flags & 0x10) != 0) << " vtable=" << vtable;
        const bool live = registered(object);
        out << " registered=" << live;
        if (live) out << " name=" << std::quoted(utf8(static_cast<RC::Unreal::UObject*>(object)->GetName()));
        if (registered(cls)) out << " class_name=" << std::quoted(utf8(static_cast<RC::Unreal::UObject*>(cls)->GetName()));
        out << '\n';
    }
    // Keep SEH in a leaf wrapper without C++ objects requiring unwinding.
    // Even a synthetic/non-UObject executor must not crash the observer.
    bool describe_guarded(std::ostream* out, const char* label, void* object)
    {
        __try { describe_cpp(*out, label, object); return true; }
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    void describe(std::ostream& out, const char* label, void* object)
    {
        try { if (!describe_guarded(&out, label, object)) out << label << " metadata_unavailable\n"; }
        catch (...) { out << label << " metadata_exception\n"; }
    }
    void address(std::ostream& out, const char* label, void* ptr)
    {
        HMODULE module = nullptr;
        wchar_t path[MAX_PATH]{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(ptr), &module);
        if (module) GetModuleFileNameW(module, path, MAX_PATH);
        const std::wstring full(path);
        const auto slash = full.find_last_of(L"\\/");
        out << label << " address=" << ptr << " module=" << utf8(full.substr(slash == full.npos ? 0 : slash + 1))
            << " rva=0x" << std::hex << (reinterpret_cast<std::uintptr_t>(ptr) - reinterpret_cast<std::uintptr_t>(module)) << std::dec << '\n';
    }
    void snapshot(std::ostream& out, void* command)
    {
        describe(out, "command", command);
        void* owner = nullptr; read_at(command, 0x20, owner);
        describe(out, "outer", owner);
        void* vt = nullptr;
        if (read_at(command, 0, vt))
            for (std::size_t slot = 0x278; slot <= 0x298; slot += 8)
            { void* fn = nullptr; if (read_at(vt, slot, fn)) { out << "command_vslot=0x" << std::hex << slot << std::dec << ' '; address(out,"function",fn); } }
    }
    void baseline(std::ostream& out)
    {
        auto* cdo = RC::Unreal::UObjectGlobals::StaticFindObject<RC::Unreal::UObject*>(nullptr,nullptr,STR("/Script/SCUM.Default__AdminCommand_SetGodMode"));
        out << "CDO_BASELINE\n"; snapshot(out, cdo);
    }
    struct ArrayHeader { void* data; std::int32_t num; std::int32_t max; };
    static_assert(sizeof(ArrayHeader) == 16);
    void arguments(std::ostream& out, void* args)
    {
        ArrayHeader header{};
        if (!read(args, &header, sizeof(header))) { out << "args unreadable\n"; return; }
        out << "args num=" << header.num << " max=" << header.max << '\n';
        if (header.num < 0 || header.num > 16 || header.max < header.num) return;
        for (int i=0; i<header.num; ++i)
        {
            ArrayHeader str{};
            if (!read_at(header.data, i*sizeof(ArrayHeader), str) || str.num < 0 || str.num > 512 || str.max < str.num) continue;
            std::wstring value(str.num, L'\0');
            if (str.num && !read(str.data,value.data(),str.num*sizeof(wchar_t))) continue;
            if (!value.empty() && value.back() == L'\0') value.pop_back();
            out << "arg[" << i << "]=" << std::quoted(utf8(value)) << '\n';
        }
    }
    bool capture_execute(void* command, void* args)
    {
        const auto end = deadline.load(std::memory_order_relaxed);
        const bool capture = end && GetTickCount64() < end && GetCurrentThreadId() == game_thread && !active && calls < max_calls;
        if (!capture) return reinterpret_cast<ExecuteFn>(trampoline)(command,args);
        std::ostringstream record;
        const unsigned id = ++calls;
        record << "BEGIN call=" << id << " thread=" << GetCurrentThreadId() << " tick=" << GetTickCount64() << '\n';
        try
        {
            address(record,"caller",_ReturnAddress());
            snapshot(record,command); arguments(record,args);
            void* frames[16]{}; const USHORT n=CaptureStackBackTrace(0,16,frames,nullptr);
            for(USHORT i=0;i<n;++i) { record << "frame=" << i << ' '; address(record,"stack",frames[i]); }
        }
        catch (...) { record << "snapshot_exception\n"; }
        active = &record; interface_events = 0;
        struct ResetActive { ~ResetActive() { active = nullptr; } } reset_active;
        // Exactly one call-through. No argument, object, flag or return-value overrides.
        const bool result = reinterpret_cast<ExecuteFn>(trampoline)(command,args);
        active = nullptr;
        try { record << "return=" << result << "\nAFTER\n"; snapshot(record,command); record << "END call=" << id << '\n'; pending.push_back(record.str().substr(0,65536)); }
        catch (...) { /* Observation failure must not alter the original result. */ }
        if(calls >= max_calls) deadline.store(0,std::memory_order_relaxed);
        return result;
    }
}
bool supported_build() { return build_ok; }
bool initialize()
{
    game_thread = GetCurrentThreadId();
    const auto* base = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{}; IMAGE_NT_HEADERS64 nt{};
    if (!read(base,&dos,sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 || dos.e_lfanew > 4096 ||
        !read(base+dos.e_lfanew,&nt,sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.FileHeader.TimeDateStamp != image_timestamp || nt.OptionalHeader.SizeOfImage != image_size) return false;
    constexpr unsigned char expected[] = {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x56,0x48,0x83,0xec,0x30};
    unsigned char actual[sizeof(expected)]{};
    if (!read(reinterpret_cast<void*>(runtime(godmode_va)),actual,sizeof(actual)) || std::memcmp(actual,expected,sizeof(expected))) return false;
    constexpr unsigned char interface_expected[] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20};
    unsigned char interface_actual[sizeof(interface_expected)]{};
    if (!read(reinterpret_cast<void*>(runtime(0x142d02db0ULL)),interface_actual,sizeof(interface_actual)) || std::memcmp(interface_actual,interface_expected,sizeof(interface_expected))) return false;
    build_ok = true;
    pending.reserve(max_calls + 2);
    detour = std::make_unique<PLH::x64Detour>(runtime(godmode_va), reinterpret_cast<std::uint64_t>(&capture_execute), &trampoline);
    hook_ok = detour->hook();
    return hook_ok;
}
void set_interface_hook_available(bool available) { interface_hook_ok = available; }
void observe_interface(void* owner, void* cls, void* result)
{
    if (!active || interface_events++ >= 8) return;
    try
    {
        describe(*active,"interface_owner",owner); describe(*active,"interface_class",cls);
        *active << "interface_result=" << result << " offset=0x" << std::hex
                << (reinterpret_cast<std::uintptr_t>(result)-reinterpret_cast<std::uintptr_t>(owner)) << std::dec << '\n';
        void* vt=nullptr;
        if (result && read_at(result,0,vt))
            for(std::size_t slot=0;slot<=0x30;slot+=8)
            { void* fn=nullptr; if(read_at(vt,slot,fn)) { *active << "interface_vslot=0x" << std::hex << slot << std::dec << ' '; address(*active,"function",fn); } }
    }
    catch (...) { /* pass-through observation */ }
}
void observe_effect(void* prisoner)
{
    if (!active) return;
    try { describe(*active,"NetMulticast_UpdateAdminStates",prisoner); } catch (...) {}
}
std::string start()
{
    if (!hook_ok || !interface_hook_ok) return "error: GodMode trace hooks unavailable/build mismatch";
    tick();
    if(!pending.empty()) return "error: previous trace could not be saved";
    std::ofstream file(log_path,std::ios::app);
    if(!file) return "error: trace log cannot be opened";
    std::ostringstream record;
    game_thread = GetCurrentThreadId();
    record << "ARM trace=v1 pid=" << GetCurrentProcessId() << " thread=" << game_thread << " tick=" << GetTickCount64()
           << " max_calls=" << max_calls << " timeout_ms=" << window_ms << "\n";
    baseline(record);
    file << record.str(); file.flush();
    if(!file) return "error: trace log cannot be written";
    game_thread=GetCurrentThreadId(); calls=0; deadline.store(GetTickCount64()+window_ms,std::memory_order_relaxed);
    return "ok: GodMode trace armed for 120 seconds / 4 calls";
}
void tick()
{
    if(deadline.load() && GetTickCount64() >= deadline.load()) deadline.store(0);
    if(pending.empty()) return;
    std::ofstream file(log_path,std::ios::app);
    if(file) { for(const auto& text:pending) file << text; file.flush(); if(file) pending.clear(); }
}
std::string status()
{
    std::ostringstream out;
    out << "godmode_trace=v1 build=" << build_ok << " hook=" << hook_ok << " interface_hook=" << interface_hook_ok
        << " armed=" << (deadline.load() && GetTickCount64()<deadline.load()) << " calls=" << calls << " pending=" << pending.size();
    return out.str();
}
std::string stop()
{
    deadline.store(0); tick();
    std::ofstream file(log_path,std::ios::app); if(file) { file << "STOP\n"; baseline(file); }
    return "ok: " + status();
}
void shutdown()
{
    deadline.store(0); tick();
    if(detour && hook_ok) detour->unHook();
    hook_ok=false;
}
}
