#include "registry_dispatch.hpp"
#include "dispatch_request.hpp"
#include "godmode_trace.hpp"
#include "response_capture.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>
#define NOMINMAX
#include <windows.h>
#include <Unreal/AActor.hpp>
#include <Unreal/Core/Containers/FString.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/CoreUObject/UObject/FStrProperty.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UObjectGlobals.hpp>

using namespace RC::Unreal;

namespace openscumrcon::registry
{
namespace
{
    bool native_faulted = false;
    bool read(const void* p, void* out, std::size_t n)
    {
        SIZE_T done = 0;
        return p && ReadProcessMemory(GetCurrentProcess(), p, out, n, &done) && done == n;
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
    { return valid(object) && !(object->GetObjectFlags() & (RF_ClassDefaultObject | RF_ArchetypeObject)); }
    UClass* find_class(const wchar_t* name)
    { return UObjectGlobals::StaticFindObject<UClass*>(nullptr, nullptr, name); }
    UObject* object_property(UObject* object, const wchar_t* name)
    {
        auto* p = CastField<FObjectPropertyBase>(object->GetPropertyByNameInChain(name));
        return p ? p->GetObjectPropertyValue(p->ContainerPtrToValuePtr<void>(object)) : nullptr;
    }
    std::string utf8(const FString& value)
    {
        const auto len = value.Len();
        if (len < 0 || len > 65536) throw std::runtime_error("invalid reflected string length");
        if (!len) return {};
        const auto* data = *value;
        const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, data, len, nullptr, 0, nullptr, nullptr);
        if (!size) throw std::runtime_error("invalid reflected string encoding");
        std::string result(size, '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, data, len, result.data(), size, nullptr, nullptr);
        return result;
    }
    std::wstring wide(const std::string& value)
    {
        if (value.empty()) return {};
        const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
        if (!size) throw std::runtime_error("arguments must use valid UTF-8");
        std::wstring result(size, L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), size);
        return result;
    }
    std::string field(std::string value)
    {
        for (char& c : value) if (static_cast<unsigned char>(c) < 32 || c == 127 || c == '|') c = ' ';
        value.resize(utf8_prefix_size(value, 512));
        return value;
    }
    std::string string_call(UObject* object, UFunction* fn)
    {
        struct Params { FString value{}; } params;
        object->ProcessEvent(fn, &params);
        return utf8(params.value);
    }
    struct Player { UObject* controller; std::string id; };
    std::vector<Player> connected_players()
    {
        auto* cls = find_class(STR("/Script/SCUM.ConZPlayerController"));
        auto* id = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/SCUM.ConZPlayerController:GetUserId"));
        if (!cls || !id || id->GetParmsSize() != sizeof(FString)) throw std::runtime_error("player reflection unavailable");
        std::vector<Player> players;
        std::set<std::string> seen;
        UObjectGlobals::ForEachUObject([&](UObject* object, ...) -> RC::LoopAction {
            if (!live(object) || !object->IsA(cls) || !live(object_property(object, STR("NetConnection"))))
                return RC::LoopAction::Continue;
            const auto value = string_call(object, id);
            if (!steam_id_valid(value)) return RC::LoopAction::Continue;
            if (!seen.insert(value).second) throw std::runtime_error("ambiguous connected SteamID");
            if (players.size() >= 1024) throw std::runtime_error("connected player limit exceeded");
            players.push_back({object, value});
            return RC::LoopAction::Continue;
        });
        std::sort(players.begin(), players.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
        return players;
    }
    std::string list_players()
    {
        const auto players = connected_players();
        if (players.empty()) return "No players online.";
        auto* name = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, STR("/Script/SCUM.ConZPlayerController:GetUserProfileName"));
        auto* prisoner = find_class(STR("/Script/SCUM.Prisoner"));
        if (!name || name->GetParmsSize() != sizeof(FString) || !prisoner) throw std::runtime_error("player details reflection unavailable");
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::fixed << std::setprecision(2);
        for (const auto& player : players)
        {
            // Identity precedes the name so names containing digit sequences cannot
            // impersonate another player in existing line-based client parsers.
            out << "PLAYER steam=" << player.id;
            auto* money = CastField<FInt64Property>(player.controller->GetPropertyByNameInChain(STR("_moneyBalanceRep")));
            auto* gold = CastField<FInt64Property>(player.controller->GetPropertyByNameInChain(STR("_goldBalanceRep")));
            if (money) out << " money=" << money->GetPropertyValueInContainer(player.controller);
            if (gold) out << " gold=" << gold->GetPropertyValueInContainer(player.controller);
            if (auto* state = object_property(player.controller, STR("PlayerState")); live(state))
            {
                if (auto* ping = CastField<FByteProperty>(state->GetPropertyByNameInChain(STR("Ping"))))
                    out << " ping=" << static_cast<unsigned>(ping->GetPropertyValueInContainer(state)) * 4;
            }
            auto* pawn = object_property(player.controller, STR("Pawn"));
            if (live(pawn) && pawn->IsA(prisoner))
            {
                const auto location = static_cast<AActor*>(pawn)->K2_GetActorLocation();
                if (std::isfinite(location.X()) && std::isfinite(location.Y()) && std::isfinite(location.Z()))
                    out << " (" << location.X() << ", " << location.Y() << ", " << location.Z() << ")";
            }
            out << " | " << field(string_call(player.controller, name)) << " |\n";
        }
        return out.str();
    }
    std::uintptr_t slot(UObject* object, std::size_t offset)
    {
        std::uintptr_t table = 0, function = 0;
        if (read(object, &table, sizeof(table))) read(reinterpret_cast<void*>(table + offset), &function, sizeof(function));
        return function;
    }
    bool executable_in_game(std::uintptr_t function)
    {
        MEMORY_BASIC_INFORMATION info{};
        if (!function || !VirtualQuery(reinterpret_cast<void*>(function), &info, sizeof(info))) return false;
        const auto protect = info.Protect & 0xff;
        return info.AllocationBase == GetModuleHandleW(nullptr) && info.State == MEM_COMMIT &&
            !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
            (protect == PAGE_EXECUTE || protect == PAGE_EXECUTE_READ || protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY);
    }
    struct Command
    {
        UClass* cls;
        UObject* cdo;
        std::string verb;
        int required = 0, declared = 0, repeating = 0;
        bool enabled = false, shipping = false, server = false;
        std::string unsupported;
    };
    std::vector<Command> commands()
    {
        auto* registry_class = find_class(STR("/Script/SCUM.AdminCommandRegistry"));
        auto* command_class = find_class(STR("/Script/SCUM.AdminCommand"));
        if (!registry_class || !command_class) throw std::runtime_error("command registry reflection unavailable");
        UObject* registry = nullptr;
        unsigned matches = 0;
        UObjectGlobals::ForEachUObject([&](UObject* object, ...) -> RC::LoopAction {
            if (live(object) && object->IsA(registry_class)) { registry = object; ++matches; }
            return RC::LoopAction::Continue;
        });
        if (matches != 1) throw std::runtime_error(matches ? "ambiguous command registry" : "command registry is not ready");
        auto* array = CastField<FArrayProperty>(registry->GetPropertyByNameInChain(STR("_commands")));
        auto* inner = array ? CastField<FClassProperty>(array->GetInner()) : nullptr;
        if (!inner || inner->GetElementSize() != sizeof(void*)) throw std::runtime_error("command registry array signature mismatch");
        FScriptArrayHelper values(array, array->ContainerPtrToValuePtr<void>(registry));
        if (values.Num() <= 0 || values.Num() > 4096) throw std::runtime_error("command registry count out of bounds");
        std::vector<Command> result;
        std::set<UClass*> seen;
        for (int i = 0; i < values.Num(); ++i)
        {
            auto* cls = static_cast<UClass*>(inner->GetObjectPropertyValue(values.GetRawPtr(i)));
            if (!valid(cls) || !cls->IsChildOf(command_class) || !seen.insert(cls).second) continue;
            auto* cdo = cls->GetClassDefaultObject().Get();
            if (!valid(cdo) || !(cdo->GetObjectFlags() & RF_ClassDefaultObject)) continue;
            auto* verb = CastField<FStrProperty>(cdo->GetPropertyByNameInChain(STR("_verb")));
            if (!verb) throw std::runtime_error("command verb reflection unavailable");
            Command command{cls, cdo, utf8(verb->GetPropertyValueInContainer(cdo))};
            auto* required = CastField<FIntProperty>(cdo->GetPropertyByNameInChain(STR("_numberOfRequiredArguments")));
            auto* repeating = CastField<FIntProperty>(cdo->GetPropertyByNameInChain(STR("_numberOfRepeatingArguments")));
            auto* args = CastField<FArrayProperty>(cdo->GetPropertyByNameInChain(STR("_argumentDescriptions")));
            auto* enabled = CastField<FBoolProperty>(cdo->GetPropertyByNameInChain(STR("_isEnabled")));
            auto* shipping = CastField<FBoolProperty>(cdo->GetPropertyByNameInChain(STR("_isEnabledInShippingBuild")));
            auto* server = CastField<FBoolProperty>(cdo->GetPropertyByNameInChain(STR("_shouldExecuteOnServer")));
            if (!required || !repeating || !args || !args->GetInner() || !enabled || !shipping || !server)
            { command.unsupported = "metadata"; result.push_back(std::move(command)); continue; }
            command.required = required->GetPropertyValueInContainer(cdo);
            command.repeating = repeating->GetPropertyValueInContainer(cdo);
            command.declared = FScriptArrayHelper(args, args->ContainerPtrToValuePtr<void>(cdo)).Num();
            command.enabled = enabled->GetPropertyValueInContainer(cdo);
            command.shipping = shipping->GetPropertyValueInContainer(cdo);
            command.server = server->GetPropertyValueInContainer(cdo);
            if (command.required < 0 || command.required > command.declared || command.declared > 64 ||
                command.repeating < 0 || command.repeating > command.declared) command.unsupported = "argument_metadata";
            else if (!command.enabled || !command.shipping || !command.server) command.unsupported = "disabled_or_client_only";
            else if (slot(cdo, 0x290) != response_capture::function_address() || !executable_in_game(slot(cdo, 0x288)))
                command.unsupported = "native_vtable";
            result.push_back(std::move(command));
        }
        if (result.empty()) throw std::runtime_error("command registry contains no valid commands");
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return lower_ascii(a.verb) < lower_ascii(b.verb); });
        return result;
    }
    std::string catalogue(const std::string& filter)
    {
        const auto entries = commands();
        std::ostringstream out;
        std::size_t matched = 0;
        for (const auto& c : entries)
        {
            if (!filter.empty() && lower_ascii(c.verb).find(filter) == std::string::npos) continue;
            ++matched;
            out << field(c.verb) << " | required=" << c.required << " declared=" << c.declared << " repeating=" << c.repeating
                << " enabled=" << c.enabled << " shipping=" << c.shipping << " server=" << c.server
                << " | " << (c.unsupported.empty() ? "native_candidate" : c.unsupported) << '\n';
        }
        out << "commands=" << matched << " total=" << entries.size()
            << " capture=" << response_capture::ready() << " build=" << godmode_trace::supported_build()
            << "\nNative candidates require an explicit connected executor; individual commands are not all live-tested.";
        return out.str();
    }
    struct Arguments { FString* data; std::int32_t num, capacity; };
    static_assert(sizeof(Arguments) == 16 && sizeof(FString) == 16);
    using Execute = bool(*)(UObject*, Arguments*);
    bool invoke_guarded(Execute fn, UObject* object, Arguments* args, bool* result, DWORD* error)
    {
        __try { *result = fn(object, args); return true; }
        __except(EXCEPTION_EXECUTE_HANDLER) { *error = GetExceptionCode(); return false; }
    }
    std::string execute(const DispatchRequest& request)
    {
        if (native_faulted) return "error: native dispatch disabled after an exception; restart required";
        if (!godmode_trace::supported_build() || !response_capture::ready())
            return "error: native dispatcher disabled: unsupported build or response hook unavailable";
        const auto entries = commands();
        const auto wanted = lower_ascii(request.verb);
        const Command* command = nullptr;
        for (const auto& c : entries) if (lower_ascii(c.verb) == wanted)
        {
            if (command) return "error: ambiguous registered command";
            command = &c;
        }
        if (!command) return "error: command not found in SCUM registry; use !commands";
        if (!command->unsupported.empty()) return "error: command unavailable: " + command->unsupported;
        const int count = static_cast<int>(request.arguments.size());
        if (count < command->required || (!command->repeating && count > command->declared) ||
            (command->repeating && count > command->declared && (count - command->declared) % command->repeating))
            return "error: argument count mismatch; required=" + std::to_string(command->required) +
                " declared=" + std::to_string(command->declared) + " repeating=" + std::to_string(command->repeating);
        UObject* controller = nullptr;
        for (const auto& p : connected_players()) if (p.id == request.steam_id) controller = p.controller;
        if (!controller) return "error: target SteamID is not connected";
        auto* prisoner = find_class(STR("/Script/SCUM.Prisoner"));
        auto* pawn = object_property(controller, STR("Pawn"));
        if (!prisoner || !live(pawn) || !pawn->IsA(prisoner)) return "error: target has no live Prisoner pawn";
        auto* executor_class = find_class(STR("/Script/SCUM.AdminCommandExecutor"));
        using GetInterface = void*(*)(UObject*, UClass*);
        const auto lookup = reinterpret_cast<GetInterface>(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) + 0x2d02db0);
        if (!executor_class || !lookup(controller, executor_class)) return "error: target does not implement AdminCommandExecutor";
        std::vector<FString> strings;
        strings.reserve(request.arguments.size());
        for (const auto& arg : request.arguments) strings.emplace_back(wide(arg));
        auto* instance = UObjectGlobals::NewObject<UObject>(controller, command->cls);
        if (!live(instance) || instance->GetClassPrivate() != command->cls || instance->GetOuterPrivate() != controller ||
            slot(instance, 0x288) != slot(command->cdo, 0x288) || slot(instance, 0x290) != response_capture::function_address())
            return "error: native command instance validation failed";
        Arguments args{strings.data(), count, count};
        ResponseScope capture(instance);
        bool result = false;
        DWORD exception = 0;
        if (!invoke_guarded(reinterpret_cast<Execute>(slot(instance, 0x288)), instance, &args, &result, &exception))
        {
            native_faulted = true;
            return "error: native command raised an exception; execution status unknown; dispatch disabled until restart";
        }
        std::string reply;
        if (!result) reply = "error: native command returned false";
        if (!capture.text().empty()) reply += (reply.empty() ? "" : "\n") + capture.text();
        if (capture.has_failed()) reply += "\nerror: command completed but reply capture failed; do not retry without checking state";
        if (capture.truncated()) reply += "\n[response truncated]";
        if (reply.empty()) reply = "ok: native command returned true; no synchronous SCUM response";
        return reply;
    }
}
std::string dispatch(const std::string& text, CommandAuthority authority)
{
    if (!is_rcon_authorized(authority)) return "error: authenticated RCON connection required";
    const auto request = parse_dispatch_request(text);
    switch (request.action)
    {
    case DispatchAction::invalid: return "error: " + request.error;
    case DispatchAction::players: return list_players();
    case DispatchAction::commands: return catalogue(request.filter);
    case DispatchAction::execute: return execute(request);
    default: return "error: unsupported command; use ListPlayers, !commands, or !exec <SteamID> <command> [arguments]";
    }
}
}
