#pragma once

// Resolves and calls SCUM's admin-command entry point.
//
// HISTORY: originally targeted UMiscStatics::Test_ProcessAdminCommand
// (UObject* WorldContextObject, FString commandText) - a Blueprint-callable
// static helper found during the initial reflection scan. Confirmed via
// disassembly on 2026-09-06 to be an empty stub (a single `ret` instruction)
// in the Shipping build - it does nothing at all, regardless of arguments or
// authorization. See docs/research/2026-09-05-authorization-gate-analysis.md,
// "Update 2026-09-06", and docs/ARCHITECTURE.md.
//
// CURRENT APPROACH (2026-09-06): call the real production RPC instead -
//
//   UPlayerRpcChannel::Chat_Server_ProcessAdminCommand(FString commandText)
//
// - the same entry point a connected admin's own "#command" chat message
// triggers. PlayerRpcChannel is a UActorComponent attached to
// ConZPlayerController (default subobject named "PlayerRpcChannel"); the
// live instance is fetched via the connected player's PlayerController
// (find_admin_context_object()) rather than a fresh UObject scan, since a
// scan can't distinguish which of several connected players' channels to
// use. Server RPCs called via ProcessEvent from code that already has
// server authority execute their _Implementation directly (no actual
// network round-trip) - this module runs inside the dedicated server
// process, so that should apply here.

#include <string>

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

        // Must only be called from the game thread (e.g. from the EngineTick
        // pre-hook that drains CommandQueue). Strips a leading '#' the same
        // way local_bridge's SourceRcon.run() does before dispatch.
        //
        // Return value is currently a best-effort placeholder, NOT SCUM's
        // real response text - see the "Rueckgabeformat" open item in
        // docs/ARCHITECTURE.md. The underlying UFunction returns no value
        // over the Lua calling convention we probed; whether it is reachable
        // as a real return parameter from C++ is one of the first things to
        // check once this can be tested live.
        std::string dispatch_command(const std::string& raw_command_text);

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
