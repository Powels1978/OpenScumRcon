# OpenScumRcon

An independent native UE4SS module providing Source RCON for SCUM dedicated servers.
The project aims to replace the discontinued Herbie RCON integration.

## Current status

Early development. The listener, authenticated worker-to-game-thread queue, and
native GodMode dispatcher are implemented. This is not yet a complete replacement
for all Herbie commands.

Supported requests:

```text
SetGodMode true <SteamID>
SetGodMode false <SteamID>
!godmode_state <SteamID>
!godmode_prepare <SteamID>
```

A leading `#` is accepted for `SetGodMode`. Supply the exact 17-digit SteamID of a
connected target with a live character. The authenticated RCON connection grants
command authority; the recipient does not need in-game admin rights.
`!godmode_prepare` checks object construction and reports chat permissions without
changing GodMode. Execution verifies GodMode and checks that Immortality is unchanged.

The native path is guarded for the tested SCUM build **1.3.3.1.145413**. Unsupported
builds are rejected. General command dispatch, native response capture, `ListPlayers`,
and commands without a connected player remain under development. Historical RPC
diagnostics may return a dispatch acknowledgement; that does not verify command execution.

Live validation covered GodMode on/off for admin and non-admin recipients, invalid
or disconnected targets, and rejected unauthenticated requests. Herbie remained
loaded alongside the module during these tests; running with Herbie removed has
not yet been validated. The current replies report verified state, not captured SCUM chat text.

## Build

Requirements: Windows x64, Visual Studio 2022 Build Tools, CMake 3.22+, and a separate
[UE4SS development checkout](https://github.com/UE4SS-RE/RE-UE4SS) compatible with the
installed UE4SS runtime. Dependencies and generated binaries are not included.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DUE4SS_DIR="C:/dev/RE-UE4SS"
cmake --build build --config Game__Shipping__Win64 --target OpenScumRconNative OpenScumGodModeRequestTests OpenScumCommandAuthorityTests
.\build\native_module\Game__Shipping__Win64\OpenScumGodModeRequestTests.exe
.\build\native_module\Game__Shipping__Win64\OpenScumCommandAuthorityTests.exe
```

Install the module using your UE4SS native-mod layout, as
`ue4ss/Mods/OpenScumRconNative/dlls/main.dll`. Copy
[`config.example.ini`](native_module/config.example.ini) to
`ue4ss/Mods/OpenScumRconNative/config.ini` and set your own password, bind address and
port. Paths are relative to the server working directory, normally `Binaries/Win64`.
Source RCON is unencrypted; restrict access to trusted networks.

Diagnostic commands can create local logs in the server working directory.
Keep those logs, configuration, captures, player records and server binaries private.
Only source code, synthetic tests and general documentation belong in this repository.
See [architecture](docs/ARCHITECTURE.md), [changes](docs/CHANGELOG.md), and
[references](docs/REFERENCES.md).

## Deutsch

OpenScumRcon soll Herbies eingestellte RCON-Schnittstelle ersetzen. Der aktuelle
Stand unterstützt eine eigene Source-RCON-Anmeldung und native GodMode-Befehle
für einen verbundenen Zielspieler. Dieser benötigt keine Adminrechte. `!godmode_state`
liest den Zustand; `!godmode_prepare` prüft den Kontext ohne Zustandsänderung.

Der native Pfad ist an den oben genannten geprüften SCUM-Build gebunden.
Weitere Adminbefehle, echte SCUM-Antworttexte, `ListPlayers` und Befehle bei leerem
Server sind noch offen. Der erfolgreiche Live-Test erfolgte mit weiterhin parallel
geladenem Herbie-Modul. Die historischen RPC-Diagnosen bestätigen keine tatsächliche
Ausführung anderer Befehle. Dieser Stand ist deshalb noch kein vollständiger Ersatz.

Serverkonfigurationen, Zugangsdaten, Spielerdaten, Binärdateien und Mitschnitte
gehören nicht in dieses Repository. Die Testdaten im Quellcode sind synthetisch.

## License

See [LICENSE](LICENSE) for the source-available project's usage and redistribution terms.
