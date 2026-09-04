#include <Mod/CppUserModBase.hpp>

// Registrierungs-Stub: laedt als UE4SS-Mod, tut sonst noch nichts.
// Der RCON-Listener und das Admin-Befehls-Hook folgen als naechster,
// eigenstaendiger Reverse-Engineering-Schritt (siehe docs/ARCHITECTURE.md).
class ScumRconNative final : public RC::CppUserModBase
{
public:
    ScumRconNative()
    {
        ModName = STR("ScumRconNative");
        ModVersion = STR("0.0.1");
        ModDescription = STR("Open-source Source-RCON server for SCUM dedicated servers");
        ModAuthors = STR("Scum_RCON contributors");
    }

    ~ScumRconNative() override = default;
};

#define SCUM_RCON_NATIVE_API __declspec(dllexport)

extern "C"
{
    SCUM_RCON_NATIVE_API RC::CppUserModBase* start_mod()
    {
        return new ScumRconNative();
    }

    SCUM_RCON_NATIVE_API void uninstall_mod(RC::CppUserModBase* mod)
    {
        delete mod;
    }
}
