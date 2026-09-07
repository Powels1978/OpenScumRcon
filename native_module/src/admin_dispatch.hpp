#pragma once

// Authenticated game-thread routing for verified GodMode, ListPlayers,
// the live command catalogue, and explicit-context native commands.

#include <string>
#include "command_authority.hpp"

namespace RC::Unreal
{
    class UClass;
    class UFunction;
    class UObject;
}

namespace openscumrcon
{
    class AdminDispatch
    {
    public:
        // Resolves UClass/UFunction pointers. Call once from on_unreal_init(),
        // same lifecycle point native_telemetry resolves its own pointers.
        // Returns false (and logs why) if a required pointer could not be
        // resolved - the caller should refuse to start the RCON listener in
        // that case rather than silently no-op every command.
        bool initialize();

        // Game thread only. Unknown commands are rejected; no ineffective RPC fallback.
        // !exec uses a registered command with an explicit connected player context.
        std::string dispatch_command(const std::string& raw_command_text, CommandAuthority authority = CommandAuthority::none);

        // Diagnostic (2026-09-05): reads the raw permission-level byte
        // (offset +0x52 on every UAdminCommand_* instance/CDO - see
        // docs/research/2026-09-05-authorization-gate-analysis.md) so we can
        // find out whether SetGodMode's permission level is 0 (the one level
        // 0x141A45AA0 grants unconditionally, no Executor identity needed).
        // Read-only: these are the exact same bytes UAdminCommand::Execute()
        // itself dereferences on every admin command call, so reading them
        // here carries the same safety profile as the game's own code path.
        // Triggered via a sentinel RCON command (see dllmain.cpp), not a
        // real SCUM command - safe to call with nobody online.
        std::string dump_admin_command_permission_levels() const;

        // Diagnostic (2026-09-06): returns the native function pointer behind
        // Test_ProcessAdminCommand (UFunction::GetFuncPtr()) as a hex string,
        // so it can be handed to tools/pe_xref_scanner for static
        // disassembly - the next step in figuring out how it resolves an
        // "Executor" from WorldContextObject (see docs/research/
        // 2026-09-05-authorization-gate-analysis.md, "Update 2. Folgesession").
        // Cheap reflection call, not a UObject scan - no game-thread stall
        // risk like dump_admin_command_permission_levels() had.
        std::string dump_test_process_admin_command_address() const;

        // Diagnostic (2026-09-06): Test_ProcessAdminCommand was confirmed to
        // be an empty stub in the Shipping build (see docs/research/
        // 2026-09-05-authorization-gate-analysis.md, "Update 2026-09-06") -
        // new focus is UPlayerRpcChannel::Chat_Server_ProcessAdminCommand,
        // the real path a connected admin's chat "#command" already uses.
        // This finds live PlayerRpcChannel instances (IsA-filtered, cheap -
        // no repeat of the earlier full-UObject GetFullName() stall) and
        // dumps their reflected properties, to figure out how such an
        // instance relates to a specific connected player/PlayerController
        // before attempting to call the RPC on it.
        std::string dump_player_rpc_channel_info() const;

        // Diagnostic (2026-09-06): reports EFunctionFlags bits relevant to
        // RPC dispatch (FUNC_Net/NetServer/NetReliable/NetValidate) on
        // Chat_Server_ProcessAdminCommand. A first call attempt completed
        // without error but had no visible effect - this checks whether the
        // function is actually a Server RPC at all (which would mean
        // ProcessEvent's net-dispatch logic, not our call itself, decides
        // whether the implementation runs).
        std::string dump_chat_server_function_flags() const;

        bool is_initialized() const { return m_initialized; }

    private:
        RC::Unreal::UObject* find_admin_context_object() const;

        RC::Unreal::UFunction* m_test_process_admin_command = nullptr;
        RC::Unreal::UClass* m_player_controller_class = nullptr;
        RC::Unreal::UClass* m_game_instance_class = nullptr;
        RC::Unreal::UClass* m_player_rpc_channel_class = nullptr;
        RC::Unreal::UFunction* m_chat_server_process_admin_command = nullptr;
        bool m_initialized = false;
    };
}
