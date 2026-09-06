#include "admin_dispatch.hpp"

#include <cstdint>
#include <fstream>
#include <sstream>

#define NOMINMAX
#include <windows.h>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Helpers/String.hpp>
#include <Unreal/Core/Containers/FString.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
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

        // Same standalone wide->narrow conversion as dllmain.cpp (duplicated
        // rather than shared - see the comment there for why RC's own
        // ensure_str_as/to_utf8_string failed to compile against
        // UObject::GetFullName()'s return type in that translation unit;
        // kept identical here rather than risking the same issue).
        std::string narrow(const std::wstring& wide)
        {
            if (wide.empty()) return {};
            const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
            std::string out(static_cast<std::size_t>(size), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), size, nullptr, nullptr);
            return out;
        }
    }

    bool AdminDispatch::initialize()
    {
        // Kept resolved only for the historical dump_test_process_admin_command_address()
        // diagnostic (see docs/research/2026-09-05-authorization-gate-analysis.md,
        // "Update 2026-09-06") - Test_ProcessAdminCommand itself is confirmed
        // dead code and no longer gates initialization success.
        m_test_process_admin_command = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, STR("/Script/SCUM.MiscStatics:Test_ProcessAdminCommand"));
        m_player_controller_class = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/SCUM.ConZPlayerController"));
        m_game_instance_class = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/SCUM.ConZGameInstance"));
        m_player_rpc_channel_class = UObjectGlobals::StaticFindObject<UClass*>(
                nullptr, nullptr, STR("/Script/SCUM.PlayerRpcChannel"));
        m_chat_server_process_admin_command = UObjectGlobals::StaticFindObject<UFunction*>(
                nullptr, nullptr, STR("/Script/SCUM.PlayerRpcChannel:Chat_Server_ProcessAdminCommand"));

        if (!m_player_controller_class || !m_player_rpc_channel_class || !m_chat_server_process_admin_command)
        {
            RC::Output::send<RC::LogLevel::Error>(
                    STR("[OpenScumRcon] AdminDispatch::initialize failed - required SCUM objects not found "
                        "(player_controller_class={}, player_rpc_channel_class={}, chat_server_process_admin_command={})\n"),
                    static_cast<void*>(m_player_controller_class),
                    static_cast<void*>(m_player_rpc_channel_class),
                    static_cast<void*>(m_chat_server_process_admin_command));
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

        if (!m_chat_server_process_admin_command)
        {
            return "error: Chat_Server_ProcessAdminCommand not resolved (see UE4SS.log at startup)";
        }

        UObject* context_object = find_admin_context_object();
        if (!context_object || !context_object->IsA(m_player_controller_class))
        {
            // Test_ProcessAdminCommand's GameInstance fallback is gone along
            // with that dead code path - Chat_Server_ProcessAdminCommand only
            // exists on a connected player's own PlayerRpcChannel component,
            // so without a real PlayerController there is nothing to call.
            return "error: no connected player found (Chat_Server_ProcessAdminCommand needs a real PlayerController)";
        }

        // PlayerRpcChannel is a default subobject (component), not exposed
        // through a plain named UPROPERTY reference on ConZPlayerController
        // (confirmed 2026-09-06: GetPropertyByName(STR("PlayerRpcChannel"))
        // returns nullptr) - find the live instance by matching its Outer to
        // our chosen PlayerController instead. Cheap: IsA() first (pointer-
        // chain walk), pointer comparison second, no string work per object -
        // same safe pattern as dump_player_rpc_channel_info().
        UObject* rpc_channel = nullptr;
        if (m_player_rpc_channel_class)
        {
            UObjectGlobals::ForEachUObject([&](UObject* object, ...) -> RC::LoopAction {
                if (!object || object->IsUnreachable())
                {
                    return RC::LoopAction::Continue;
                }
                if (object->IsA(m_player_rpc_channel_class) && object->GetOuterPrivate() == context_object)
                {
                    rpc_channel = object;
                    return RC::LoopAction::Break;
                }
                return RC::LoopAction::Continue;
            });
        }
        if (!rpc_channel)
        {
            return "error: no PlayerRpcChannel component found on this PlayerController";
        }

        // Chat_Server_ProcessAdminCommand is the handler for a real player's
        // raw chat message - it almost certainly expects the same text the
        // in-game chat box would send, leading '#' included (unlike the dead
        // Test_ProcessAdminCommand path, which took a pre-stripped command).
        // First attempt (2026-09-06) sent the stripped text and completed
        // without error but with no visible effect - untested hypothesis:
        // the handler silently no-ops on anything not starting with '#'.
        struct Params
        {
            FString commandText{};
        } params;
        params.commandText = FString(RC::to_wstring("#" + command_text));

        rpc_channel->ProcessEvent(m_chat_server_process_admin_command, &params);

        // See docs/ARCHITECTURE.md: the real response text (what Herbie's
        // RCON returns, e.g. "God mode set to true.") is not known to come
        // back through this call at all - Herbie's own log shows he captures
        // it via a separate chat-line detour hook, not a return value. Until
        // that is solved, report only whether the call itself completed.
        return "ok: dispatched via PlayerRpcChannel::Chat_Server_ProcessAdminCommand";
    }

    std::string AdminDispatch::dump_admin_command_permission_levels() const
    {
        // Diagnostic (2026-09-05): instead of hardcoding the +0x50/+0x51/+0x52
        // byte offsets found via static disassembly (see
        // docs/research/2026-09-05-authorization-gate-analysis.md), this
        // walks each AdminCommand_* UClass's REFLECTED properties via
        // UStruct's own TFieldRange<FProperty> and reads values through
        // FProperty::ContainerPtrToValuePtr(). That resolves whatever the
        // property's actual current offset is from the class metadata itself
        // - portable across SCUM builds/servers, not tied to one binary's
        // layout the way a hardcoded offset would be. If these fields turn
        // out not to be UPROPERTY-reflected at all, this will simply find no
        // matching property (logged below) rather than silently misreading
        // memory - a raw-offset read was deliberately avoided here.
        std::size_t objects_scanned = 0;
        std::ostringstream log;

        UObjectGlobals::ForEachUObject([&](UObject* object, ...) -> RC::LoopAction {
            if (!object || object->IsUnreachable())
            {
                return RC::LoopAction::Continue;
            }
            // Class name convention confirmed during reflection scan
            // (2026-09-04): "UAdminCommand_<Name>" per SCUM admin command,
            // reflected name drops the "U" prefix -> "AdminCommand_<Name>".
            if (object->GetFullName().find(STR("AdminCommand_")) == RC::StringType::npos)
            {
                return RC::LoopAction::Continue;
            }
            ++objects_scanned;

            UClass* object_class = object->GetClassPrivate();
            log << narrow(object->GetFullName()) << "\n";
            if (!object_class)
            {
                log << "  <no UClass>\n";
                return RC::LoopAction::Continue;
            }

            for (FProperty* property : TFieldRange<FProperty>(object_class))
            {
                if (!property)
                {
                    continue;
                }
                log << "  " << narrow(property->GetName())
                    << " type=" << narrow(property->GetClass().GetName())
                    << " offset=" << property->GetOffset_ForInternal()
                    << " size=" << property->GetElementSize();

                // Small integral-ish properties (bool/byte/enum, <=4 bytes) are
                // the plausible candidates for the enabled/shipping/permission
                // flags found in the disassembly - read and print their raw
                // value through the property's own accessor, not a fixed offset.
                if (property->GetElementSize() > 0 && property->GetElementSize() <= 4)
                {
                    const auto* value_ptr = property->ContainerPtrToValuePtr<std::uint8_t>(object);
                    if (value_ptr)
                    {
                        log << " value=" << static_cast<int>(*value_ptr);
                    }
                }
                log << "\n";
            }
            return RC::LoopAction::Continue;
        });

        std::ofstream file("C:\\PowelsLocalBridge\\openscumrcon_permission_levels.log", std::ios::trunc);
        if (file.is_open())
        {
            file << log.str();
        }

        std::ostringstream summary;
        summary << "ok: scanned " << objects_scanned
                << " AdminCommand_* object(s), full property dump written to "
                   "C:\\PowelsLocalBridge\\openscumrcon_permission_levels.log";
        return summary.str();
    }

    std::string AdminDispatch::dump_test_process_admin_command_address() const
    {
        if (!m_test_process_admin_command)
        {
            return "error: Test_ProcessAdminCommand UFunction not resolved";
        }
        const auto func_ptr = m_test_process_admin_command->GetFuncPtr();
        std::ostringstream out;
        out << "ok: Test_ProcessAdminCommand native func ptr = 0x" << std::hex
            << reinterpret_cast<std::uintptr_t>(func_ptr);
        return out.str();
    }

    std::string AdminDispatch::dump_player_rpc_channel_info() const
    {
        if (!m_player_rpc_channel_class)
        {
            return "error: PlayerRpcChannel UClass not resolved (see UE4SS.log at startup)";
        }

        std::size_t found = 0;
        std::ostringstream log;

        UObjectGlobals::ForEachUObject([&](UObject* object, ...) -> RC::LoopAction {
            if (!object || object->IsUnreachable())
            {
                return RC::LoopAction::Continue;
            }
            // Cheap pointer-chain check FIRST, before any string work - the
            // 2026-09-05 permission-level scan stalled the game thread by
            // calling GetFullName() on every single UObject before filtering.
            // IsA() only walks the class hierarchy, no string ops, so this is
            // safe to run over the entire UObject universe.
            if (!object->IsA(m_player_rpc_channel_class))
            {
                return RC::LoopAction::Continue;
            }
            ++found;
            log << narrow(object->GetFullName()) << "\n";

            UClass* object_class = object->GetClassPrivate();
            if (!object_class)
            {
                log << "  <no UClass>\n";
                return RC::LoopAction::Continue;
            }
            for (FProperty* property : TFieldRange<FProperty>(object_class))
            {
                if (!property)
                {
                    continue;
                }
                log << "  " << narrow(property->GetName())
                    << " type=" << narrow(property->GetClass().GetName())
                    << " offset=" << property->GetOffset_ForInternal()
                    << " size=" << property->GetElementSize() << "\n";
            }
            return RC::LoopAction::Continue;
        });

        std::ofstream file("C:\\PowelsLocalBridge\\openscumrcon_rpc_channel_info.log", std::ios::trunc);
        if (file.is_open())
        {
            file << log.str();
        }

        std::ostringstream summary;
        summary << "ok: found " << found << " PlayerRpcChannel instance(s), "
                << "Chat_Server_ProcessAdminCommand=" << (m_chat_server_process_admin_command ? "resolved" : "NOT FOUND")
                << ", details written to C:\\PowelsLocalBridge\\openscumrcon_rpc_channel_info.log";
        return summary.str();
    }

    std::string AdminDispatch::dump_chat_server_function_flags() const
    {
        if (!m_chat_server_process_admin_command)
        {
            return "error: Chat_Server_ProcessAdminCommand not resolved";
        }
        const auto flags = m_chat_server_process_admin_command->GetFunctionFlags();
        std::ostringstream out;
        out << "ok: flags=0x" << std::hex << flags << std::dec
            << " Net=" << ((flags & FUNC_Net) != 0)
            << " NetServer=" << ((flags & FUNC_NetServer) != 0)
            << " NetReliable=" << ((flags & FUNC_NetReliable) != 0)
            << " NetValidate=" << ((flags & FUNC_NetValidate) != 0)
            << " Native=" << ((flags & FUNC_Native) != 0)
            << " BlueprintCallable=" << ((flags & FUNC_BlueprintCallable) != 0);
        return out.str();
    }
}
