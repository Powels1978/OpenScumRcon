#include <atomic>
#include <cstdio>
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
#include <Unreal/Hooks/Hooks.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include "admin_dispatch.hpp"
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
            std::ofstream file("C:\\PowelsLocalBridge\\openscumrcon_native_hook.log", std::ios::app);
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

        // config.ini lives next to this mod's own scripts/dlls folder, same
        // convention as scum_rcon's own config.ini. Path is relative to the
        // SCUM server's working directory (Binaries/Win64), matching how
        // other mods in this project already locate their own config/state
        // files (see local_bridge's C:\PowelsLocalBridge\ convention for a
        // different, absolute-path example of the same idea).
        const Config config = load_config("ue4ss/Mods/OpenScumRconNative/config.ini");

        RC::Unreal::Hook::FCallbackOptions options{};
        options.OwnerModName = ModName;
        options.HookName = STR("OpenScumRconCommandDrain");
        options.bReadonly = true;
        m_engine_tick_callback = RC::Unreal::Hook::RegisterEngineTickPreCallback(
                [this](auto&, RC::Unreal::UEngine*, float, bool) {
                    drain_and_dispatch();
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
                        log_admin_states_multicast_stack(context);
                    }

                    if (!m_capturing_calls.load(std::memory_order_relaxed))
                    {
                        return;
                    }
                    std::ofstream file("C:\\PowelsLocalBridge\\openscumrcon_capture_nested.log", std::ios::app);
                    if (file.is_open())
                    {
                        const std::string contextName = context ? narrow(context->GetFullName()) : std::string{};
                        const std::string functionName = function ? narrow(function->GetFullName()) : std::string{};
                        file << "context=[" << contextName << "] function=[" << functionName << "]\n";
                    }
                }, captureOptions);

        if (install_native_execute_hook())
        {
            RC::Output::send<RC::LogLevel::Verbose>(STR("[OpenScumRconNative] Native execute() hook installed\n"));
        }
        else
        {
            RC::Output::send<RC::LogLevel::Error>(
                    STR("[OpenScumRconNative] Native execute() hook FAILED to install - "
                        "!native_capture_start/!dump_native_hook will not work, RCON listener unaffected\n"));
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
            try
            {
                // Diagnostic sentinel (2026-09-05), not a real SCUM command -
                // triggers AdminDispatch::dump_admin_command_permission_levels()
                // instead of the normal dispatch path. See docs/research/
                // 2026-09-05-authorization-gate-analysis.md for why. Checked
                // before the '#' stripping/dispatch below on purpose.
                if (item.command_text == "!dump_admin_permissions")
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
                    std::ofstream(  "C:\\PowelsLocalBridge\\openscumrcon_capture_nested.log", std::ios::trunc);
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
                    std::ofstream("C:\\PowelsLocalBridge\\openscumrcon_native_hook.log", std::ios::trunc);
                    g_capturing_native_calls.store(true, std::memory_order_relaxed);
                    response = "ok: native capture started";
                }
                else if (item.command_text == "!native_capture_stop")
                {
                    g_capturing_native_calls.store(false, std::memory_order_relaxed);
                    response = "ok: native capture stopped";
                }
                else
                {
                    m_capturing_calls.store(true, std::memory_order_relaxed);
                    response = m_dispatch.dispatch_command(item.command_text);
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

        std::ofstream file("C:\\PowelsLocalBridge\\openscumrcon_admin_states_stack.log", std::ios::app);
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
