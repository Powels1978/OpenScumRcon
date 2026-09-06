#include "godmode_dispatch.hpp"
#include "godmode_request.hpp"
#include "godmode_trace.hpp"
#include <cstdint>
#include <cstring>
#include <sstream>
#define NOMINMAX
#include <windows.h>
#include <Unreal/Core/Containers/FString.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UObjectGlobals.hpp>

using namespace RC::Unreal;

namespace openscumrcon::godmode
{
namespace
{
    // SCUM 1.3.3.1.145413 only. See the native-dispatch research note.
    constexpr std::uintptr_t execute_rva = 0x18e6530;
    constexpr std::uintptr_t can_execute_rva = 0x18c7b60;
    bool signatures_ok = false;

    std::uintptr_t address(std::uintptr_t rva)
    { return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) + rva; }

    bool read(const void* source, void* dest, std::size_t size)
    {
        SIZE_T done = 0;
        return source && ReadProcessMemory(GetCurrentProcess(), source, dest, size, &done) && done == size;
    }
    template<std::size_t N> bool signature(std::uintptr_t rva, const unsigned char (&expected)[N])
    {
        unsigned char actual[N]{};
        return read(reinterpret_cast<void*>(address(rva)), actual, N) && !std::memcmp(actual, expected, N);
    }
    std::uintptr_t slot(UObject* object, std::size_t offset)
    {
        std::uintptr_t table = 0, function = 0;
        if (read(object, &table, sizeof(table))) read(reinterpret_cast<void*>(table + offset), &function, sizeof(function));
        return function;
    }
    bool valid(UObject* object)
    {
        if (!object) return false;
        const auto index = object->GetInternalIndex();
        if (index < 0 || index >= FUObjectArray::GetNumElements()) return false;
        auto* item = FUObjectArray::IndexToObject(index);
        return item && item->GetUObject() == object && item->IsValid(false);
    }
    bool live(UObject* object)
    {
        return valid(object) && !(object->GetObjectFlags() & (RF_ClassDefaultObject | RF_ArchetypeObject));
    }
    std::string utf8(const wchar_t* value)
    {
        if (!value || !*value) return {};
        const int n = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
        if (n <= 0) return {};
        std::string result(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), n, nullptr, nullptr);
        result.pop_back();
        return result;
    }
    UObject* object_property(UObject* object, const wchar_t* name)
    {
        auto* property = CastField<FObjectPropertyBase>(object->GetPropertyByNameInChain(name));
        if (!property) return nullptr;
        auto* storage = property->ContainerPtrToValuePtr<std::uint8_t>(object);
        return storage ? property->GetObjectPropertyValue(storage) : nullptr;
    }
    struct Player
    {
        UObject* controller = nullptr;
        UObject* pawn = nullptr;
        FBoolProperty* god = nullptr;
        FBoolProperty* immortal = nullptr;
        std::string error;
    };
    Player find_player(const std::string& id)
    {
        Player result;
        auto* controller_class = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/SCUM.ConZPlayerController"));
        auto* prisoner_class = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/SCUM.Prisoner"));
        auto* get_user_id = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/SCUM.ConZPlayerController:GetUserId"));
        if (!controller_class || !prisoner_class || !get_user_id || get_user_id->GetParmsSize() != sizeof(FString))
        { result.error = "player reflection unavailable or signature mismatch"; return result; }
        unsigned matches = 0;
        UObjectGlobals::ForEachUObject([&](UObject* object, ...) -> RC::LoopAction {
            if (!valid(object) || !object->IsA(controller_class) || !live(object)) return RC::LoopAction::Continue;
            // Dedicated-server clients have a live NetConnection; discard stale controllers.
            if (!live(object_property(object, STR("NetConnection")))) return RC::LoopAction::Continue;
            struct Params { FString value{}; } params;
            object->ProcessEvent(get_user_id, &params);
            if (utf8(*params.value) == id) { result.controller = object; ++matches; }
            return RC::LoopAction::Continue;
        });
        if (matches != 1)
        { result.error = matches ? "ambiguous connected SteamID" : "target SteamID is not connected"; return result; }
        result.pawn = object_property(result.controller, STR("Pawn"));
        if (!live(result.pawn) || !result.pawn->IsA(prisoner_class))
        { result.error = "target has no live Prisoner pawn"; return result; }
        result.god = CastField<FBoolProperty>(result.pawn->GetPropertyByNameInChain(STR("_isInGodMode")));
        result.immortal = CastField<FBoolProperty>(result.pawn->GetPropertyByNameInChain(STR("_isImmortal")));
        if (!result.god || !result.immortal) result.error = "admin-state reflection unavailable";
        return result;
    }
    std::string state(const Player& player)
    {
        return std::string("godmode=") + (player.god->GetPropertyValueInContainer(player.pawn) ? "true" : "false") +
            " immortal=" + (player.immortal->GetPropertyValueInContainer(player.pawn) ? "true" : "false");
    }
    bool expected_vtable(UObject* object)
    {
        return valid(object) && slot(object, 0x280) == address(can_execute_rva) && slot(object, 0x288) == address(execute_rva);
    }
    // The handler reads this synchronous, borrowed TArray<FString> view only.
    // The FString owns its allocation; neither command nor arguments are cached.
    struct Arguments { FString* data; std::int32_t num; std::int32_t capacity; };
    static_assert(sizeof(FString) == 16 && sizeof(Arguments) == 16);
    using CanExecute = bool(*)(UObject*, UObject*, FString*);
    using Execute = bool(*)(UObject*, Arguments*);
}

bool initialize()
{
    // Run before the existing CanExecute observer patches its prologue.
    // Filled from the exact server EXE, not a guessed function ABI.
    constexpr unsigned char permission[] = {0x40,0x53,0x55,0x56,0x57,0x41,0x57,0x48,0x83,0xec,0x40,0x48,0x8b,0x05,0x66};
    signatures_ok = godmode_trace::supported_build() && signature(can_execute_rva, permission);
    return signatures_ok;
}

std::optional<std::string> dispatch(const std::string& text, CommandAuthority authority)
{
    const auto request = parse(text);
    if (request.action == Action::unrelated) return std::nullopt;
    if (!is_rcon_authorized(authority)) return "error: authenticated RCON connection required";
    if (request.action == Action::invalid)
        return "error: usage: SetGodMode <true|false> <17-digit SteamID> | !godmode_state <SteamID> | !godmode_prepare <SteamID>";
    if (!signatures_ok) return "error: native GodMode disabled: unsupported SCUM build/signature";
    auto player = find_player(request.steam_id);
    if (!player.error.empty()) return "error: " + player.error;
    const auto before = state(player);
    if (request.action == Action::state) return "ok: steam_id=" + request.steam_id + " " + before;

    auto* cls = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Game/ConZ_Files/AdminCommands/SetGodMode.SetGodMode_C"));
    auto* native = UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, STR("/Script/SCUM.AdminCommand_SetGodMode"));
    if (!valid(cls) || !native || !cls->IsChildOf(native)) return "error: SetGodMode blueprint class unavailable";
    auto* cdo = cls->GetClassDefaultObject().Get();
    if (!expected_vtable(cdo) || !(cdo->GetObjectFlags() & RF_ClassDefaultObject))
        return "error: SetGodMode CDO/vtable mismatch";
    // RCON authority was validated independently of the recipient above.
    // A non-admin recipient is valid: live Herbie traces call this GodMode
    // handler with the recipient's Outer but without the chat CanExecute gate.
    // Keep that gate only as a read-only diagnostic in !godmode_prepare.
    std::string chat_diagnostic;
    if (request.action == Action::prepare)
    {
        FString reason{};
        const bool allowed = reinterpret_cast<CanExecute>(slot(cdo, 0x280))(cdo, player.controller, &reason);
        chat_diagnostic = std::string(" chat_can_execute=") + (allowed ? "true" : "false");
        if (!allowed) chat_diagnostic += " chat_reason=" + utf8(*reason);
    }

    // Normal UObject allocation; no CDO Outer changes or fabricated interface offsets.
    // Like the game's chat call, construct and execute synchronously on the game thread.
    // GC owns the instance afterwards. Never delete or persist this raw pointer.
    auto* instance = UObjectGlobals::NewObject<UObject>(player.controller, cls);
    if (!live(instance) || instance->GetClassPrivate() != cls || instance->GetOuterPrivate() != player.controller || !expected_vtable(instance))
        return "error: constructed GodMode instance failed validation";
    if (request.action == Action::prepare)
        return "ok: prepared SetGodMode_C; authority=authenticated_rcon outer=target_controller steam_id=" + request.steam_id + " " + before + chat_diagnostic + " executed=false";

    FString value(request.enabled ? STR("true") : STR("false"));
    Arguments args{&value, 1, 1};
    const bool old_immortal = player.immortal->GetPropertyValueInContainer(player.pawn);
    // Match the observed RCON path: call the verified handler directly.
    // Do not read target-admin rights or modify shared chat cooldown timestamps.
    const bool result = reinterpret_cast<Execute>(slot(instance, 0x288))(instance, &args);
    if (!live(player.pawn) || object_property(player.controller, STR("Pawn")) != player.pawn)
        return "error: target pawn changed during native GodMode execution; verify player state";
    const auto after = state(player);
    if (!result || player.god->GetPropertyValueInContainer(player.pawn) != request.enabled ||
        player.immortal->GetPropertyValueInContainer(player.pawn) != old_immortal)
        return "error: native GodMode result/state verification failed; " + after;
    // Our own verified reply, not a claim to have captured SCUM's chat response.
    return "ok: native SetGodMode verified; steam_id=" + request.steam_id + " " + after;
}
}
