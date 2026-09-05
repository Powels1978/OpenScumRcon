#include <atomic>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#define NOMINMAX
#include <windows.h>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Helpers/String.hpp>
#include <Mod/CppUserModBase.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>

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

    openscumrcon::AdminDispatch m_dispatch;
    openscumrcon::CommandQueue m_queue;
    openscumrcon::RconServer m_rcon_server;
    RC::Unreal::Hook::GlobalCallbackId m_engine_tick_callback = RC::Unreal::Hook::ERROR_ID;
    std::atomic<bool> m_capturing_calls{false};
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
