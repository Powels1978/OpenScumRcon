# OpenScumRcon

[English](#english) | [Deutsch](#deutsch)

## English

An independent native UE4SS module providing Source RCON for SCUM dedicated servers.
The project aims to replace the discontinued Herbie RCON integration.

### Current status

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

### Build

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

### License

See [LICENSE](LICENSE) for the project's usage and redistribution terms. A
[non-binding German translation](docs/LICENSE.de.md) is available.

---

## Deutsch

Ein eigenständiges natives UE4SS-Modul mit Source RCON für SCUM-Dedicated-Server.
Das Projekt soll die eingestellte Herbie-RCON-Anbindung ersetzen.

### Aktueller Stand

Frühe Entwicklung. RCON-Listener, authentifizierte Übergabe vom Netzwerkthread zum
Spielthread und nativer GodMode-Dispatcher sind implementiert. Dieser Stand ist
noch kein vollständiger Ersatz für alle Herbie-Befehle.

Unterstützte Anfragen:

```text
SetGodMode true <SteamID>
SetGodMode false <SteamID>
!godmode_state <SteamID>
!godmode_prepare <SteamID>
```

`SetGodMode` akzeptiert auch ein führendes `#`. Benötigt wird die genaue 17-stellige
SteamID eines verbundenen Zielspielers mit aktivem Charakter. Die authentifizierte
RCON-Verbindung berechtigt zur Befehlsausführung; der Zielspieler benötigt keine
Adminrechte im Spiel. `!godmode_prepare` prüft die Objekterzeugung und zeigt die
Chatberechtigung an, ohne GodMode zu ändern. Bei der Ausführung wird der GodMode-Zustand
kontrolliert und geprüft, dass Immortality unverändert bleibt.

Der native Pfad ist für den getesteten SCUM-Build **1.3.3.1.145413** abgesichert.
Nicht unterstützte Builds werden abgewiesen. Allgemeine Befehlsausführung, Erfassung
nativer Antworttexte, `ListPlayers` und Befehle ohne verbundenen Spieler werden noch
entwickelt. Historische RPC-Diagnosen können eine Übergabebestätigung zurückgeben;
diese belegt keine tatsächliche Befehlsausführung.

Live geprüft wurden GodMode an/aus bei Zielspielern mit und ohne Adminrechte,
ungültige oder nicht verbundene Ziele sowie die Abweisung nicht authentifizierter
Anfragen. Herbie blieb während dieser Tests parallel geladen; der Betrieb ohne
Herbie wurde noch nicht gesondert bestätigt. Die aktuellen Antworten beschreiben
den geprüften Zustand und enthalten keine mitgeschnittenen SCUM-Chatantworten.

### Bauen und installieren

Voraussetzungen: Windows x64, Visual Studio 2022 Build Tools, CMake ab 3.22 und ein
separater [UE4SS-Entwicklungsstand](https://github.com/UE4SS-RE/RE-UE4SS), der zur
installierten UE4SS-Laufzeit passt. Abhängigkeiten und erzeugte Binärdateien sind
nicht enthalten.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DUE4SS_DIR="C:/dev/RE-UE4SS"
cmake --build build --config Game__Shipping__Win64 --target OpenScumRconNative OpenScumGodModeRequestTests OpenScumCommandAuthorityTests
.\build\native_module\Game__Shipping__Win64\OpenScumGodModeRequestTests.exe
.\build\native_module\Game__Shipping__Win64\OpenScumCommandAuthorityTests.exe
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
