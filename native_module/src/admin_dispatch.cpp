#include "admin_dispatch.hpp"

#include <DynamicOutput/DynamicOutput.hpp>
#include <Helpers/String.hpp>
#include <Unreal/Core/Containers/FString.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/UObjectGlobals.hpp>

using namespace RC::Unreal;

namespace openscumrcon
{
    namespace
    {
        // Same "skip the CDO / skip unreachable objects" guard pattern
        // native_telemetry uses (added there after a real production crash
        // traced to touching a stale/unreachable object - see the
        // 2026-09-04 "DE-Main-Absturz" fix in the sibling private project).
        bool is_default_object_name(UObject* object)
        {
            // CDOs are named "Default__<ClassName>"; cheaper than a dedicated
            // flag check and matches the existing pattern in this codebase.
            return object->GetFullName().find(STR("Default__")) != RC::StringType::npos;
        }
    }

    bool AdminDispatch::initialize()
    {
        m_test_process_admin_command = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, STR("/Script/SCUM.MiscStatics:Test_ProcessAdminCommand"));
        m_misc_statics_cdo = UObjectGlobals::StaticFindObject<UObject*>(
                nullptr, nullptr, STR("/Script/SCUM.Default__MiscStatics"));
        m_player_controller_class = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/SCUM.ConZPlayerController"));
        m_game_instance_class = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/SCUM.ConZGameInstance"));

        if (!m_test_process_admin_command || !m_misc_statics_cdo || !m_player_controller_class)
        {
            RC::Output::send<RC::LogLevel::Error>(
                    STR("[OpenScumRcon] AdminDispatch::initialize failed - required SCUM objects not found "
                        "(function={}, statics_cdo={}, player_controller_class={})\n"),
                    static_cast<void*>(m_test_process_admin_command),
                    static_cast<void*>(m_misc_statics_cdo),
                    static_cast<void*>(m_player_controller_class));
            return false;
        }

        m_initialized = true;
        RC::Output::send<RC::LogLevel::Verbose>(STR("[OpenScumRcon] AdminDispatch initialized\n"));
        return true;
    }

    UObject* AdminDispatch::find_admin_context_object() const
    {
        // Prefer a real, currently-connected player's controller (see the
        // header comment for why: the GameInstance-only call was verified
        // NOT to work against a live server). Falls back to the
        // GameInstance if no player is online, purely so the module still
        // does *something* observable rather than silently returning
        // nothing - that fallback path is the one already known to be
        // insufficient for commands that require real authorization.
        UObject* fallback = nullptr;
        UObject* found = nullptr;

        UObjectGlobals::ForEachUObject([&](UObject* object, ...) -> RC::LoopAction {
            if (!object || object->IsUnreachable())
            {
                return RC::LoopAction::Continue;
            }
            if (m_game_instance_class && !fallback && object->IsA(m_game_instance_class)
                    && !is_default_object_name(object))
            {
                fallback = object;
            }
            if (object->IsA(m_player_controller_class) && !is_default_object_name(object))
            {
                found = object;
                return RC::LoopAction::Break;
            }
            return RC::LoopAction::Continue;
        });

        return found ? found : fallback;
    }

    std::string AdminDispatch::dispatch_command(const std::string& raw_command_text)
    {
        if (!m_initialized)
        {
            return "error: AdminDispatch not initialized";
        }

        std::string command_text = raw_command_text;
        // Trim and strip a leading '#', mirroring SourceRcon.run() in
        // local_bridge/powels_local_bridge.py so behaviour matches what
        // callers of the existing client already expect.
        const auto first = command_text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
        {
            return "error: empty command";
        }
        command_text = command_text.substr(first);
        if (!command_text.empty() && command_text.front() == '#')
        {
            command_text.erase(0, 1);
        }
        const auto last = command_text.find_last_not_of(" \t\r\n");
        command_text = command_text.substr(0, last + 1);

        UObject* context_object = find_admin_context_object();
        if (!context_object)
        {
            return "error: no valid WorldContextObject found (no player online, no GameInstance)";
        }

        struct Params
        {
            UObject* WorldContextObject{};
            FString commandText{};
        } params;
        params.WorldContextObject = context_object;
        params.commandText = FString(RC::to_wstring(command_text));

        context_object->ProcessEvent(m_test_process_admin_command, &params);

        // See the header comment and docs/ARCHITECTURE.md: the real
        // response text (what Herbie's RCON returns, e.g.
        // "God mode set to true.") is not known to come back through this
        // call at all - Herbie's own log shows he captures it via a
        // separate chat-line detour hook, not a return value. Until that is
        // solved, report only whether the call itself completed.
        return "ok: dispatched via " + std::string(context_object->IsA(m_player_controller_class)
                ? "PlayerController" : "GameInstance (fallback, known to not apply real effects)");
    }
}
