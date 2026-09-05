#pragma once

// Resolves and calls SCUM's admin-command entry point found during the
// reverse-engineering phase (see docs/ARCHITECTURE.md, "Gefundener
// Dispatch-Mechanismus" and its correction below it):
//
//   UMiscStatics::Test_ProcessAdminCommand(UObject* WorldContextObject, FString commandText)
//
// KNOWN, VERIFIED LIMITATION (2026-09-04 live A/B test against a real
// server): calling this with the GameInstance as WorldContextObject runs
// without error but has NO observable effect - the same command sent via
// Herbie's still-working RCON on the same server, same target, same moment,
// DID work. Working theory: the function checks the caller's admin identity
// through WorldContextObject, and a bare GameInstance carries none.
//
// This module now resolves a REAL, currently-connected ConZPlayerController
// instead and uses that as WorldContextObject - untested as of this
// writing (no live server access when this was written; verify with the
// user before trusting the result). If that still doesn't work, the
// documented fallback is memory-hooking the actual authorization gate(s),
// not a reflection-only call.

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

        bool is_initialized() const { return m_initialized; }

    private:
        RC::Unreal::UObject* find_admin_context_object() const;

        RC::Unreal::UFunction* m_test_process_admin_command = nullptr;
        RC::Unreal::UObject* m_misc_statics_cdo = nullptr;
        RC::Unreal::UClass* m_player_controller_class = nullptr;
        RC::Unreal::UClass* m_game_instance_class = nullptr;
        bool m_initialized = false;
    };
}
