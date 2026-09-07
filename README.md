# OpenScumRcon

[English](#english) | [Deutsch](#deutsch)

## English

An independent native UE4SS module providing Source RCON for SCUM dedicated servers.
The project aims to replace the discontinued Herbie RCON integration.

### Current status

Early development. Authenticated RCON, the game-thread queue, native GodMode,
registry-based command dispatch, synchronous SCUM replies and an independent
`ListPlayers` query are implemented. Full Herbie command compatibility remains open.

```text
ListPlayers
!commands [filter]
!exec <SteamID> <SCUM-command> [native arguments...]
SetGodMode true <SteamID>
SetGodMode false <SteamID>
!godmode_state <SteamID>
!godmode_prepare <SteamID>
```

Native execution requires the exact 17-digit SteamID of a connected player with a
live character. Their controller supplies context; RCON authentication supplies
authority. The player does not need in-game admin rights. `ListPlayers` and
`!commands` also work without online players. A leading `#` is accepted for
`ListPlayers`, `SetGodMode` and the native command name inside `!exec`.

The direct `SetGodMode` request verifies GodMode and unchanged Immortality, then
appends SCUM's captured reply. Generic `!exec` returns SCUM's synchronous text
without independently verifying every command's gameplay effect.
Native calls are guarded for SCUM build **1.3.3.1.145413**.

Live validation found 233 registered commands: 185 native candidates, 47 disabled
or client-only entries and one entry with inconsistent argument metadata.
Candidate status does not mean a command has been individually live-tested.
Tests covered GodMode on/off for a non-admin recipient, `CheckServerTime`,
empty/occupied player lists, authentication rejection and argument validation.
Herbie remained loaded; its reply path still worked. Operation with Herbie disabled
and execution without a connected player remain to be validated or implemented.
See [command usage and limitations](docs/NATIVE_COMMANDS.md).

### Build

Requirements: Windows x64, Visual Studio 2022 Build Tools, CMake 3.22+, and a separate
[UE4SS development checkout](https://github.com/UE4SS-RE/RE-UE4SS) compatible with the
installed UE4SS runtime. Dependencies and generated binaries are not included.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DUE4SS_DIR="C:/dev/RE-UE4SS"
cmake --build build --config Game__Shipping__Win64 --target OpenScumRconNative OpenScumGodModeRequestTests OpenScumCommandAuthorityTests OpenScumDispatchInfrastructureTests
.\build\native_module\Game__Shipping__Win64\OpenScumGodModeRequestTests.exe
.\build\native_module\Game__Shipping__Win64\OpenScumCommandAuthorityTests.exe
.\build\native_module\Game__Shipping__Win64\OpenScumDispatchInfrastructureTests.exe
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

### License

See [LICENSE](LICENSE) for the project's usage and redistribution terms. A
[non-binding German translation](docs/LICENSE.de.md) is available.

---

## Deutsch

Ein eigenständiges natives UE4SS-Modul mit Source RCON für SCUM-Dedicated-Server.
Das Projekt soll die eingestellte Herbie-RCON-Anbindung ersetzen.

### Aktueller Stand

Frühe Entwicklung. Authentifiziertes RCON, die Queue zum Spielthread, nativer
GodMode, Befehlsausführung über die Registry, synchrone SCUM-Antworten und eine
eigenständige `ListPlayers`-Abfrage sind implementiert. Vollständige Kompatibilität
mit Herbies Befehlen steht noch aus.

```text
ListPlayers
!commands [filter]
!exec <SteamID> <SCUM-command> [native arguments...]
SetGodMode true <SteamID>
SetGodMode false <SteamID>
!godmode_state <SteamID>
!godmode_prepare <SteamID>
```

Die native Ausführung benötigt die genaue 17-stellige SteamID eines verbundenen
Spielers mit aktivem Charakter. Sein Controller liefert den Kontext; die
RCON-Anmeldung liefert die Berechtigung. Der Spieler benötigt keine Adminrechte.
`ListPlayers` und `!commands` funktionieren auch ohne Online-Spieler. Ein führendes
`#` wird bei `ListPlayers`, `SetGodMode` und dem nativen Befehlsnamen innerhalb von
`!exec` akzeptiert.

Der direkte `SetGodMode`-Aufruf prüft GodMode sowie unveränderte Immortality und
ergänzt SCUMs erfassten Antworttext. Allgemeines `!exec` liefert SCUMs synchronen
Text ohne zusätzliche unabhängige Prüfung der Spielwirkung jedes Befehls.
Native Aufrufe sind für SCUM-Build **1.3.3.1.145413** abgesichert.

Live wurden 233 registrierte Befehle gefunden: 185 native Kandidaten, 47 deaktivierte
oder rein clientseitige Einträge und ein Eintrag mit widersprüchlichen Argumentdaten.
Der Kandidatenstatus bedeutet keine erfolgte Live-Prüfung des einzelnen Befehls.
Getestet sind GodMode an/aus bei einem Nicht-Admin-Ziel, `CheckServerTime`,
Spielerlisten bei leerem/belegtem Server, abgewiesene Anmeldungen und Argumentprüfungen.
Herbie blieb geladen; dessen Antwortpfad funktionierte weiterhin. Betrieb mit
deaktiviertem Herbie und Ausführung ohne verbundenen Spieler müssen noch geprüft
beziehungsweise implementiert werden. Siehe [Befehle und Grenzen](docs/NATIVE_COMMANDS.md#deutsch).

### Bauen und installieren

Voraussetzungen: Windows x64, Visual Studio 2022 Build Tools, CMake ab 3.22 und ein
separater [UE4SS-Entwicklungsstand](https://github.com/UE4SS-RE/RE-UE4SS), der zur
installierten UE4SS-Laufzeit passt. Abhängigkeiten und erzeugte Binärdateien sind
nicht enthalten.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DUE4SS_DIR="C:/dev/RE-UE4SS"
cmake --build build --config Game__Shipping__Win64 --target OpenScumRconNative OpenScumGodModeRequestTests OpenScumCommandAuthorityTests OpenScumDispatchInfrastructureTests
.\build\native_module\Game__Shipping__Win64\OpenScumGodModeRequestTests.exe
.\build\native_module\Game__Shipping__Win64\OpenScumCommandAuthorityTests.exe
.\build\native_module\Game__Shipping__Win64\OpenScumDispatchInfrastructureTests.exe
```

Das Modul entsprechend der UE4SS-Struktur für native Mods als
`ue4ss/Mods/OpenScumRconNative/dlls/main.dll` installieren.
[`config.example.ini`](native_module/config.example.ini) nach
`ue4ss/Mods/OpenScumRconNative/config.ini` kopieren und eigenes Passwort,
Bind-Adresse und Port festlegen. Pfade beziehen sich auf das Arbeitsverzeichnis
des Servers, normalerweise `Binaries/Win64`. Source RCON ist unverschlüsselt;
den Zugriff auf vertrauenswürdige Netze begrenzen.

Diagnosebefehle können lokale Logs im Arbeitsverzeichnis des Servers erzeugen.
Diese Logs, Konfigurationen, Mitschnitte, Spielerdaten und Server-Binärdateien
privat halten. In dieses Repository gehören ausschließlich Quellcode, synthetische
Tests und allgemeine Dokumentation. Weitere Informationen:
[Architektur](docs/ARCHITECTURE.md#deutsch),
[Änderungsprotokoll](docs/CHANGELOG.md#deutsch) und
[Referenzen](docs/REFERENCES.md#deutsch).

### Lizenz

Die Nutzungs- und Weitergabebedingungen stehen in [LICENSE](LICENSE).
Eine [unverbindliche deutsche Übersetzung](docs/LICENSE.de.md) ist verfügbar.
