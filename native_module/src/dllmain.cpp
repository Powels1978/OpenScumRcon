#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#define NOMINMAX
#include <windows.h>
#include <intrin.h>

#include <polyhook2/Detour/x64Detour.hpp>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Helpers/String.hpp>
#include <Mod/CppUserModBase.hpp>
#include <Unreal/Core/Containers/FString.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include "admin_dispatch.hpp"
#include "godmode_trace.hpp"
#include "godmode_dispatch.hpp"
#include "command_queue.hpp"
#include "rcon_server.hpp"

namespace
{
    // Same config.ini convention scum_rcon itself uses (own file, next to
    // the mod's dlls/main.dll, plain "key = value" lines - see
    // docs/CHANGELOG.md, the UE4SS.log excerpt showing
    // "[SCUM-RCON] config: loaded from .../Mods/scum_rcon/config.ini").
    struct Config
    {
        std::string bind_host = "0.0.0.0";
        unsigned short port = 28016; // deliberately NOT 28015 - that's Herbie's port, avoid clashing while both run side by side during testing
        std::string password = "changeme";
    };

    // Minimal, self-contained wide->narrow conversion for the diagnostic
    // logging below - deliberately NOT using UE4SS's own Helpers::to_utf8_string
    // / ensure_str_as, which failed to compile against RC::Unreal::UObject's
    // GetFullName() return type in this translation unit for reasons not
    // worth chasing for temporary debug code.
    std::string narrow(const std::wstring& wide)
    {
        if (wide.empty()) return {};
        const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), size, nullptr, nullptr);
        return out;
    }

    Config load_config(const std::string& path)
    {
        Config config;
        std::ifstream file(path);
        if (!file.is_open())
        {
            return config;
        }
        std::string line;
        while (std::getline(file, line))
        {
            const auto comment_pos = line.find_first_of(";#");
            if (comment_pos != std::string::npos)
            {
                line = line.substr(0, comment_pos);
            }
            const auto eq_pos = line.find('=');
            if (eq_pos == std::string::npos)
            {
                continue;
            }
            auto trim = [](std::string s) {
                const auto first = s.find_first_not_of(" \t\r\n");
                if (first == std::string::npos) return std::string{};
                const auto last = s.find_last_not_of(" \t\r\n");
                return s.substr(first, last - first + 1);
            };
            const std::string key = trim(line.substr(0, eq_pos));
            const std::string value = trim(line.substr(eq_pos + 1));
            if (key == "bind_host") config.bind_host = value;
            else if (key == "port") config.port = static_cast<unsigned short>(std::stoi(value));
            else if (key == "password") config.password = value;
        }
        return config;
    }

    // Diagnostic native-code hook (2026-09-06). See docs/research/
    // 2026-09-05-authorization-gate-analysis.md, "Update 2026-09-06:
    // ProcessEvent-Capture beweist: kein reflektierter Trigger" for why this
    // step became necessary: the admin-command authorization function
    // (candidate: UAdminCommand::Execute()/CanExecute(), static VA
    // 0x1418c7b60 in the analyzed build - see tools/pe_xref_scanner) has
    // ZERO direct CALL/JMP/LEA xrefs and zero stored-pointer references
    // anywhere in the binary, so static analysis alone cannot find its
    // caller. This installs an inline x64 detour (via PolyHook_2, already
    // vendored by UE4SS - same library UE4SS's own ProcessEvent hooks use
    // internally) purely to OBSERVE: it always calls through to the
    // original function unchanged, and only logs (return address = the real
    // caller, plus the this/executor/outString arguments) while explicitly
    // enabled via the !native_capture_start/!native_capture_stop RCON
    // sentinels below - the same manual on/off pattern already used for the
    // ProcessEvent capture hook. Never alters game behavior.
    constexpr std::uint64_t kExecuteHookStaticImageBase = 0x140000000ULL;
    constexpr std::uint64_t kExecuteHookStaticVa = 0x1418c7b60ULL;

    using ExecuteHookFn = bool(*)(void* thisPtr, void* executor, void* outString);

    std::unique_ptr<PLH::x64Detour> g_execute_detour;
    std::uint64_t g_execute_trampoline = 0;
    std::atomic<bool> g_capturing_native_calls{false};

    // Signature per the earlier disassembly analysis: RCX=this (the
    // UAdminCommand_* instance), RDX=Executor, R8=output FString* for the
    // error/status message, returns bool (AL).
    bool detour_execute(void* thisPtr, void* executor, void* outString)
    {
        if (g_capturing_native_calls.load(std::memory_order_relaxed))
        {
            void* returnAddress = _ReturnAddress();
            std::ofstream file("openscumrcon_native_hook.log", std::ios::app);
            if (file.is_open())
            {
                file << "Execute called: this=" << thisPtr << " executor=" << executor
                     << " outString=" << outString << " returnAddress=" << returnAddress << "\n";
            }
        }
        return reinterpret_cast<ExecuteHookFn>(g_execute_trampoline)(thisPtr, executor, outString);
    }

    bool install_native_execute_hook()
    {
        const auto moduleBase = reinterpret_cast<std::uint64_t>(GetModuleHandleW(nullptr));
        if (!moduleBase)
        {
            return false;
        }
        const std::uint64_t targetAddress = moduleBase + (kExecuteHookStaticVa - kExecuteHookStaticImageBase);

        g_execute_detour = std::make_unique<PLH::x64Detour>(
                targetAddress, reinterpret_cast<std::uint64_t>(&detour_execute), &g_execute_trampoline);
        return g_execute_detour->hook();
    }

    // Translates a static file VA (relative to the PE's preferred image base
    // 0x140000000) to the actual runtime-loaded address, accounting for ASLR.
    std::uint64_t resolve_runtime_address(std::uint64_t staticVa)
    {
        const auto moduleBase = reinterpret_cast<std::uint64_t>(GetModuleHandleW(nullptr));
        return moduleBase + (staticVa - 0x140000000ULL);
    }

    // SCUM build 2026-08-27: this is UObject::GetInterfaceAddress, not a
    // thread-registration gate. Always preserve the original result.
    constexpr std::uint64_t kRegistrationCheckStaticVa = 0x142D02DB0ULL;
    using RegistrationCheckFn = void*(*)(void* owner, void* interfaceClass);
    std::unique_ptr<PLH::x64Detour> g_registration_check_detour;
    std::uint64_t g_registration_check_trampoline = 0;
    std::atomic<bool> g_capturing_registration_calls{false};
    std::atomic<unsigned> g_registration_log_count{0};

    void* detour_registration_check(void* owner, void* interfaceClass)
    {
        void* result = reinterpret_cast<RegistrationCheckFn>(g_registration_check_trampoline)(owner, interfaceClass);
        openscumrcon::godmode_trace::observe_interface(owner, interfaceClass, result);
        // Legacy address-only capture is bounded too. Prefer !godmode_trace_start.
        if (g_capturing_registration_calls.load(std::memory_order_relaxed) &&
            g_registration_log_count.fetch_add(1, std::memory_order_relaxed) < 256)
        {
            std::ofstream file("openscumrcon_registration_calls.log", std::ios::app);
            if (file) file << "owner=" << owner << " interfaceClass=" << interfaceClass << " result=" << result << "\n";
        }
        return result;
    }

    bool install_registration_check_hook()
    {
        const auto targetAddress = resolve_runtime_address(kRegistrationCheckStaticVa);
        g_registration_check_detour = std::make_unique<PLH::x64Detour>(
                targetAddress, reinterpret_cast<std::uint64_t>(&detour_registration_check),
                &g_registration_check_trampoline);
        return g_registration_check_detour->hook();
    }

    // Diagnostic (2026-09-06): calls the thread-local-singleton "Get()"
    // candidate (static VA 0x142685AE0 - see docs/research/
    // 2026-09-06-native-entry-point-discovery.md) directly, from the game
    // thread (same thread our own EngineTick-driven dispatch already runs
    // on), and dumps the returned object's raw memory. This is a genuine,
    // widely-used (99 call sites found statically) magic-static accessor -
    // calling it with zero arguments is exactly what all 99 other call sites
    // already do; the only new thing here is that WE are the caller instead
    // of SCUM's own code. Read-only dump, no writes to the returned object.
    std::string call_context_resolver_and_dump()
    {
        using GetContextFn = void*(*)();
        const auto func = reinterpret_cast<GetContextFn>(resolve_runtime_address(0x142685AE0ULL));

        void* result = func();

        std::ostringstream out;
        out << "ok: 0x142685AE0() returned " << result;

        std::ofstream file("openscumrcon_context_dump.log", std::ios::trunc);
        if (file.is_open())
        {
            file << "0x142685AE0() returned " << result << "\n";
            if (result)
            {
                constexpr std::size_t kDumpBytes = 256;
                const auto* bytes = static_cast<const unsigned char*>(result);
                for (std::size_t i = 0; i < kDumpBytes; ++i)
                {
                    if (i % 16 == 0)
                    {
                        if (i != 0) file << "\n";
                        file << "  +0x" << std::hex << i << std::dec << ": ";
                    }
                    file << std::hex << std::uppercase;
                    file.width(2);
                    file.fill('0');
                    file << static_cast<int>(bytes[i]) << " ";
                    file << std::dec << std::nouppercase;
                }
                file << "\n";
            }
        }
        return out.str();
    }

    // Diagnostic (2026-09-06): calls the executor-resolution helper
    // (0x1418E8A10) directly with the AdminCommand_SetGodMode instance as
    // argument - the exact same call 0x1419063d0 makes internally as its
    // very first step - and dumps the result. Isolates whether the false
    // from try_native_setgodmode() comes from executor resolution itself
    // failing (null result here) or from a later check inside 0x1419063d0
    // (non-null result here, meaning the problem is further down the
    // chain). See docs/research/2026-09-06-native-entry-point-discovery.md.
    using ExecutorResolveFn = void*(*)(void* thisPtr);

    std::string call_executor_resolver_and_dump()
    {
        RC::Unreal::UObject* commandInstance = RC::Unreal::UObjectGlobals::StaticFindObject<RC::Unreal::UObject*>(
                nullptr, nullptr, STR("/Script/SCUM.Default__AdminCommand_SetGodMode"));
        if (!commandInstance)
        {
            return "error: AdminCommand_SetGodMode instance not found";
        }

        const auto func = reinterpret_cast<ExecutorResolveFn>(resolve_runtime_address(0x1418E8A10ULL));

        void* result = nullptr;
        std::string status = "ok";
        try
        {
            result = func(commandInstance);
        }
        catch (...)
        {
            status = "exception during native call";
        }

        std::ostringstream out;
        out << status << ": 0x1418E8A10(commandInstance=" << static_cast<void*>(commandInstance)
            << ") returned " << result;

        std::ofstream file("openscumrcon_executor_dump.log", std::ios::trunc);
        if (file.is_open())
        {
            file << "0x1418E8A10(commandInstance=" << static_cast<void*>(commandInstance)
                 << ") returned " << result << "\n";
            if (result)
            {
                constexpr std::size_t kDumpBytes = 256;
                const auto* bytes = static_cast<const unsigned char*>(result);
                for (std::size_t i = 0; i < kDumpBytes; ++i)
                {
                    if (i % 16 == 0)
                    {
                        if (i != 0) file << "\n";
                        file << "  +0x" << std::hex << i << std::dec << ": ";
                    }
                    file << std::hex << std::uppercase;
                    file.width(2);
                    file.fill('0');
                    file << static_cast<int>(bytes[i]) << " ";
                    file << std::dec << std::nouppercase;
                }
                file << "\n";
            }
        }
        return out.str();
    }

    // Diagnostic (2026-09-06): the native call above returned false with no
    // crash/exception - read-only check of WHERE that false likely
    // originates. 0x1418E8A10 (the executor-resolution helper) bails
    // immediately if this+0x20 is null, before even reaching the thread-
    // singleton lookup. Reads the same bytes the game's own code already
    // dereferences at that offset - see docs/research/
    // 2026-09-06-native-entry-point-discovery.md.
    std::string dump_setgodmode_field_0x20()
    {
        RC::Unreal::UObject* commandInstance = RC::Unreal::UObjectGlobals::StaticFindObject<RC::Unreal::UObject*>(
                nullptr, nullptr, STR("/Script/SCUM.Default__AdminCommand_SetGodMode"));
        if (!commandInstance)
        {
            return "error: AdminCommand_SetGodMode instance not found";
        }
        const auto* bytes = reinterpret_cast<const unsigned char*>(commandInstance);
        void* fieldValue = nullptr;
        std::memcpy(&fieldValue, bytes + 0x20, sizeof(fieldValue));

        std::ostringstream out;
        out << "ok: this=" << static_cast<void*>(commandInstance) << " this+0x20=" << fieldValue;
        return out.str();
    }
}

class OpenScumRconNative final : public RC::CppUserModBase
{
public:
    OpenScumRconNative()
    {
        ModName = STR("OpenScumRconNative");
        ModVersion = STR("0.1.0");
        ModDescription = STR("Open-source Source-RCON server for SCUM dedicated servers");
        ModAuthors = STR("OpenScumRcon contributors");
    }

    ~OpenScumRconNative() override
    {
        openscumrcon::godmode_trace::shutdown();
        m_rcon_server.stop();
        if (m_engine_tick_callback != RC::Unreal::Hook::ERROR_ID)
        {
            RC::Unreal::Hook::UnregisterCallback(m_engine_tick_callback);
        }
    }

    auto on_unreal_init() -> void override
    {
        if (!m_dispatch.initialize())
        {
            RC::Output::send<RC::LogLevel::Error>(
                    STR("[OpenScumRconNative] AdminDispatch failed to initialize - RCON listener will NOT start. "
                        "See UE4SS.log above for which SCUM object could not be resolved.\n"));
            return;
        }

        // Anchor for the stack-backtrace capture below (2026-09-06): the
        // native execute()-hook at the authorization-gate address found via
        // static analysis was confirmed via a live PolyHook_2 detour to
        // NEVER fire for a real, working SetGodMode call - that whole code
        // path is apparently dead/unreachable in this build (see
        // docs/research/2026-09-05-authorization-gate-analysis.md, "Update
        // 2026-09-06"). This multicast RPC, by contrast, is CONFIRMED to
        // fire on every real admin-state change (seen in the very first
        // ProcessEvent capture). Resolving it here so the hook below can do
        // a cheap pointer comparison instead of a string compare on every
        // single ProcessEvent call in the game.
        m_admin_states_multicast_function = RC::Unreal::UObjectGlobals::StaticFindObject<RC::Unreal::UFunction*>(
                nullptr, nullptr, STR("/Script/SCUM.Prisoner:NetMulticast_UpdateAdminStates"));
        RC::Output::send<RC::LogLevel::Verbose>(
                STR("[OpenScumRconNative] NetMulticast_UpdateAdminStates resolved={}\n"),
                static_cast<void*>(m_admin_states_multicast_function));

        // Paths are relative to the server working directory (Binaries/Win64).
        // Each installation supplies its own untracked configuration.
        const Config config = load_config("ue4ss/Mods/OpenScumRconNative/config.ini");

        RC::Unreal::Hook::FCallbackOptions options{};
        options.OwnerModName = ModName;
        options.HookName = STR("OpenScumRconCommandDrain");
        options.bReadonly = true;
        m_engine_tick_callback = RC::Unreal::Hook::RegisterEngineTickPreCallback(
                [this](auto&, RC::Unreal::UEngine*, float, bool) {
                    drain_and_dispatch();
                    openscumrcon::godmode_trace::tick();
                }, options);

        // TEMPORARY diagnostic hook (2026-09-05): logs every UFunction call
        // that happens WHILE dispatch_command() is executing on the game
        // thread (m_capturing_calls guards this so it's silent the rest of
        // the time - without that guard this would log every ProcessEvent
        // call in the entire game, thousands per second). Goal: find out
        // whether Test_ProcessAdminCommand reaches any real internal SCUM
        // logic at all when called via our synthetic PlayerController
        // context, since the call currently produces no observable effect.
        // See docs/CHANGELOG.md for the full context. Remove once answered.
        RC::Unreal::Hook::FCallbackOptions captureOptions{};
        captureOptions.OwnerModName = ModName;
        captureOptions.HookName = STR("OpenScumRconCaptureNestedCalls");
        captureOptions.bReadonly = true;
        RC::Unreal::Hook::RegisterProcessEventPreCallback(
                [this](RC::Unreal::Hook::TCallbackIterationData<void>&, RC::Unreal::UObject* context, RC::Unreal::UFunction* function, void*) {
                    // Always-on, cheap anchor capture (2026-09-06): a plain
                    // pointer compare, not gated by m_capturing_calls, since
                    // this needs to catch a command triggered externally via
                    // Herbie's RCON, not just our own dispatch_command()
                    // calls. See the on_unreal_init() comment above
                    // m_admin_states_multicast_function for why this
                    // specific function was chosen as the anchor.
                    if (function && function == m_admin_states_multicast_function)
                    {
                        openscumrcon::godmode_trace::observe_effect(context);
                        log_admin_states_multicast_stack(context);
                    }

                    if (!m_capturing_calls.load(std::memory_order_relaxed))
                    {
                        return;
                    }
                    std::ofstream file("openscumrcon_capture_nested.log", std::ios::app);
                    if (file.is_open())
                    {
                        const std::string contextName = context ? narrow(context->GetFullName()) : std::string{};
                        const std::string functionName = function ? narrow(function->GetFullName()) : std::string{};
                        file << "context=[" << contextName << "] function=[" << functionName << "]\n";
                    }
                }, captureOptions);

        // Observation-only hooks (2026-09-06): testing a hypothesis the user
        // found researching how tools like this are typically built for
        // Unreal games in general - that admin commands might be dispatched
        // via the engine's OWN standard, well-documented console-command
        // pipeline (UGameViewportClient::ProcessConsoleExec /
        // UObject::CallFunctionByNameWithArguments) rather than SCUM's
        // undocumented internal native chain this project spent most of
        // today reverse-engineering. UE4SS already provides stable, proven
        // hook points for both (the same mechanism already used safely for
        // ProcessEvent above) - far lower risk than more raw PolyHook_2
        // detours on guessed addresses. Purely logs; never changes behavior
        // or a return value.
        RC::Unreal::Hook::FCallbackOptions consoleExecOptions{};
        consoleExecOptions.OwnerModName = ModName;
        consoleExecOptions.HookName = STR("OpenScumRconObserveConsoleExec");
        consoleExecOptions.bReadonly = true;
        RC::Unreal::Hook::RegisterProcessConsoleExecCallback(
                [](RC::Unreal::Hook::TCallbackIterationData<bool>&, RC::Unreal::UObject* context, const RC::CharType* cmd,
                   RC::Unreal::FOutputDevice&, RC::Unreal::UObject* executor) {
                    std::ofstream file("openscumrcon_console_exec.log", std::ios::app);
                    if (file.is_open())
                    {
                        const std::string contextName = context ? narrow(context->GetFullName()) : std::string{};
                        const std::string executorName = executor ? narrow(executor->GetFullName()) : std::string{};
                        const std::string cmdText = cmd ? narrow(std::wstring(cmd)) : std::string{};
                        file << "ProcessConsoleExec context=[" << contextName << "] executor=[" << executorName
                             << "] cmd=[" << cmdText << "]\n";
                    }
                }, consoleExecOptions);

        RC::Unreal::Hook::FCallbackOptions callByNameOptions{};
        callByNameOptions.OwnerModName = ModName;
        callByNameOptions.HookName = STR("OpenScumRconObserveCallFunctionByName");
        callByNameOptions.bReadonly = true;
        RC::Unreal::Hook::RegisterCallFunctionByNameWithArgumentsPreCallback(
                [](RC::Unreal::Hook::TCallbackIterationData<bool>&, RC::Unreal::UObject* context, const RC::CharType* str,
                   RC::Unreal::FOutputDevice&, RC::Unreal::UObject* executor, bool) {
                    std::ofstream file("openscumrcon_callfunctionbyname.log", std::ios::app);
                    if (file.is_open())
                    {
                        const std::string contextName = context ? narrow(context->GetFullName()) : std::string{};
                        const std::string executorName = executor ? narrow(executor->GetFullName()) : std::string{};
                        const std::string strText = str ? narrow(std::wstring(str)) : std::string{};
                        file << "CallFunctionByNameWithArguments context=[" << contextName << "] executor=["
                             << executorName << "] str=[" << strText << "]\n";
                    }
                }, callByNameOptions);

        const bool godmode_trace_ready = openscumrcon::godmode_trace::initialize();
        RC::Output::send<RC::LogLevel::Verbose>(STR("[OpenScumRconNative] GodMode trace v1 initialized={}\n"), godmode_trace_ready);
        const bool native_godmode_ready = openscumrcon::godmode::initialize();
        RC::Output::send<RC::LogLevel::Verbose>(STR("[OpenScumRconNative] Native GodMode dispatcher v2 (RCON authority) initialized={}\n"), native_godmode_ready);
        if (openscumrcon::godmode_trace::supported_build() && install_native_execute_hook())
        {
            RC::Output::send<RC::LogLevel::Verbose>(STR("[OpenScumRconNative] Native execute() hook installed\n"));
        }
        else
        {
            RC::Output::send<RC::LogLevel::Error>(
                    STR("[OpenScumRconNative] Native execute() hook FAILED to install - "
                        "!native_capture_start/!dump_native_hook will not work, RCON listener unaffected\n"));
        }
        if (openscumrcon::godmode_trace::supported_build() && install_registration_check_hook())
        {
            openscumrcon::godmode_trace::set_interface_hook_available(true);
            RC::Output::send<RC::LogLevel::Verbose>(STR("[OpenScumRconNative] GetInterfaceAddress observer installed\n"));
        }
        else
        {
            RC::Output::send<RC::LogLevel::Error>(
                    STR("[OpenScumRconNative] Registration-check hook FAILED to install - "
                        "!try_native_setgodmode will not work, RCON listener unaffected\n"));
        }
        if (!m_rcon_server.start(config.bind_host, config.port, config.password, m_queue))
        {
            RC::Output::send<RC::LogLevel::Error>(STR("[OpenScumRconNative] RconServer failed to start\n"));
            return;
        }

        RC::Output::send<RC::LogLevel::Verbose>(
                STR("[OpenScumRconNative] RCON listener started on {}:{}\n"),
                RC::to_wstring(config.bind_host), config.port);
    }

private:
    // Runs on the game thread (EngineTick pre-hook) - the only place it is
    // safe to call ProcessEvent / touch UObjects. See docs/ARCHITECTURE.md
    // "Modul-Struktur" for why this split exists.
    void drain_and_dispatch()
    {
        auto pending = m_queue.drain();
        for (auto& item : pending)
        {
            std::string response;
            if (!openscumrcon::is_rcon_authorized(item.authority))
            {
                item.response.set_value("error: authenticated RCON connection required");
                continue;
            }
            try
            {
                // Diagnostic sentinel (2026-09-05), not a real SCUM command -
                // triggers AdminDispatch::dump_admin_command_permission_levels()
                // instead of the normal dispatch path. See docs/research/
                // 2026-09-05-authorization-gate-analysis.md for why. Checked
                // before the '#' stripping/dispatch below on purpose.
                if (item.command_text == "!godmode_trace_start")
                {
                    response = openscumrcon::godmode_trace::start();
                }
                else if (item.command_text == "!godmode_trace_stop")
                {
                    response = openscumrcon::godmode_trace::stop();
                }
                else if (item.command_text == "!godmode_trace_status")
                {
                    response = openscumrcon::godmode_trace::status();
                }
                else if (item.command_text == "!dump_admin_permissions")
                {
                    response = m_dispatch.dump_admin_command_permission_levels();
                }
                else if (item.command_text == "!dump_func_address")
                {
                    response = m_dispatch.dump_test_process_admin_command_address();
                }
                else if (item.command_text == "!dump_rpc_channel")
                {
                    response = m_dispatch.dump_player_rpc_channel_info();
                }
                else if (item.command_text == "!dump_chat_flags")
                {
                    response = m_dispatch.dump_chat_server_function_flags();
                }
                else if (item.command_text == "!capture_start")
                {
                    // Manual on/off (2026-09-06), independent of our own
                    // dispatch_command() calls - lets us capture what SCUM's
                    // own ProcessEvent traffic looks like while a command is
                    // triggered externally (e.g. via Herbie's still-working
                    // RCON), to see which function/object boundary a real
                    // admin command crosses. We are observing SCUM's own
                    // engine calls here, not Herbie's mod code.
                    std::ofstream(  "openscumrcon_capture_nested.log", std::ios::trunc);
                    m_capturing_calls.store(true, std::memory_order_relaxed);
                    response = "ok: capture started";
                }
                else if (item.command_text == "!capture_stop")
                {
                    m_capturing_calls.store(false, std::memory_order_relaxed);
                    response = "ok: capture stopped";
                }
                else if (item.command_text == "!native_capture_start")
                {
                    std::ofstream("openscumrcon_native_hook.log", std::ios::trunc);
                    g_capturing_native_calls.store(true, std::memory_order_relaxed);
                    response = "ok: native capture started";
                }
                else if (item.command_text == "!native_capture_stop")
                {
                    g_capturing_native_calls.store(false, std::memory_order_relaxed);
                    response = "ok: native capture stopped";
                }
                else if (item.command_text == "!call_context_resolver")
                {
                    response = call_context_resolver_and_dump();
                }
                else if (item.command_text == "!capture_registration_start")
                {
                    std::ofstream("openscumrcon_registration_calls.log", std::ios::trunc);
                    g_registration_log_count.store(0);
                    g_capturing_registration_calls.store(true, std::memory_order_relaxed);
                    response = "ok: registration capture started";
                }
                else if (item.command_text == "!capture_registration_stop")
                {
                    g_capturing_registration_calls.store(false, std::memory_order_relaxed);
                    response = "ok: registration capture stopped";
                }
                else if (item.command_text == "!call_executor_resolver")
                {
                    response = call_executor_resolver_and_dump();
                }
                else if (item.command_text == "!dump_setgodmode_field")
                {
                    response = dump_setgodmode_field_0x20();
                }
                else if (item.command_text.rfind("!try_native_setgodmode ", 0) == 0)
                {
                    // Usage: !try_native_setgodmode <true|false> <steamId>
                    const std::string rest = item.command_text.substr(23);
                    const auto space = rest.find(' ');
                    if (space == std::string::npos)
                    {
                        response = "error: usage: !try_native_setgodmode <true|false> <steamId>";
                    }
                    else
                    {
                        response = "error: experimental call disabled: old address targets Immortality; use observation-only !godmode_trace_start";
                    }
                }
                else
                {
                    m_capturing_calls.store(true, std::memory_order_relaxed);
                    response = m_dispatch.dispatch_command(item.command_text, item.authority);
                    m_capturing_calls.store(false, std::memory_order_relaxed);
                }
            }
            catch (const std::exception& ex)
            {
                m_capturing_calls.store(false, std::memory_order_relaxed);
                response = std::string("error: exception during dispatch: ") + ex.what();
            }
            catch (...)
            {
                m_capturing_calls.store(false, std::memory_order_relaxed);
                response = "error: unknown exception during dispatch";
            }
            item.response.set_value(std::move(response));
        }
    }

    // Logs a Windows stack backtrace (return addresses only - no symbol
    // resolution needed) plus which module each frame belongs to, whenever
    // the confirmed-live NetMulticast_UpdateAdminStates anchor fires. Frames
    // inside SCUMServer.exe itself (as opposed to UE4SS.dll/ntdll/etc., each
    // loaded at a very different base address) are the ones worth
    // translating back to a static file VA afterward - see
    // docs/research/2026-09-05-authorization-gate-analysis.md.
    void log_admin_states_multicast_stack(RC::Unreal::UObject* context)
    {
        constexpr USHORT kMaxFrames = 32;
        void* frames[kMaxFrames]{};
        const USHORT captured = CaptureStackBackTrace(0, kMaxFrames, frames, nullptr);

        std::ofstream file("openscumrcon_admin_states_stack.log", std::ios::app);
        if (!file.is_open())
        {
            return;
        }
        file << "=== NetMulticast_UpdateAdminStates, context=["
             << (context ? narrow(context->GetFullName()) : std::string{}) << "] ===\n";
        for (USHORT i = 0; i < captured; ++i)
        {
            HMODULE module = nullptr;
            GetModuleHandleExW(
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(frames[i]), &module);
            wchar_t moduleName[MAX_PATH]{};
            if (module)
            {
                GetModuleFileNameW(module, moduleName, MAX_PATH);
            }
            file << "  [" << i << "] " << frames[i]
                 << " moduleBase=" << static_cast<void*>(module)
                 << " module=" << narrow(moduleName) << "\n";
        }
    }

    openscumrcon::AdminDispatch m_dispatch;
    openscumrcon::CommandQueue m_queue;
    openscumrcon::RconServer m_rcon_server;
    RC::Unreal::Hook::GlobalCallbackId m_engine_tick_callback = RC::Unreal::Hook::ERROR_ID;
    std::atomic<bool> m_capturing_calls{false};
    RC::Unreal::UFunction* m_admin_states_multicast_function = nullptr;
};

#define OPEN_SCUM_RCON_NATIVE_API __declspec(dllexport)

extern "C"
{
    OPEN_SCUM_RCON_NATIVE_API RC::CppUserModBase* start_mod()
    {
        return new OpenScumRconNative();
    }

    OPEN_SCUM_RCON_NATIVE_API void uninstall_mod(RC::CppUserModBase* mod)
    {
        delete mod;
    }
}
