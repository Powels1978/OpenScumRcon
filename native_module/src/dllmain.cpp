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
                response = m_dispatch.dispatch_command(item.command_text);
            }
            catch (const std::exception& ex)
            {
                response = std::string("error: exception during dispatch: ") + ex.what();
            }
            catch (...)
            {
                response = "error: unknown exception during dispatch";
            }
            item.response.set_value(std::move(response));
        }
    }

    openscumrcon::AdminDispatch m_dispatch;
    openscumrcon::CommandQueue m_queue;
    openscumrcon::RconServer m_rcon_server;
    RC::Unreal::Hook::GlobalCallbackId m_engine_tick_callback = RC::Unreal::Hook::ERROR_ID;
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
