# OpenScumRcon

*[Deutsch weiter unten](#deutsch)*

An independent, source-available replacement for [`herbie96x/SCUM-RCON`](https://github.com/herbie96x/SCUM-RCON) —
a real [Source RCON](https://developer.valvesoftware.com/wiki/Source_RCON_Protocol) server
for SCUM dedicated servers, letting you trigger SCUM's own admin commands
programmatically, without a real admin client connected in-game.

## Why this project exists

`herbie96x/SCUM-RCON` was declared end-of-life by its developer: the repository is
archived, the license server required by the public builds has been shut down, and
the source code will not be released. Every server that relied on this mod
permanently loses the ability to trigger admin commands without a connected admin
client. This project rebuilds the same functionality independently.

## Status

**Early development, builds and runs, dispatch mechanism under revision.** A
real Source RCON listener and worker-thread/game-thread command queue are
implemented and working. The originally planned dispatch path,
`Test_ProcessAdminCommand`, was confirmed via disassembly to be an empty stub
in the Shipping build (a single `ret` instruction — it does nothing at
runtime, regardless of authorization or arguments) and has been abandoned.
Current focus: `PlayerRpcChannel::Chat_Server_ProcessAdminCommand`, the actual
path real admin players already use. See
[`docs/CHANGELOG.md`](docs/CHANGELOG.md) for the full, warts-and-all
investigation history and [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for
the technical approach.

## How it's meant to work

A native [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS) C++ module, loaded as a mod
into the SCUM server process, provides a real Source RCON server on the same public
wire protocol Herbie's mod also used. Any existing RCON client (including your own
tools) can connect without any changes on their end. Details and the reasoning
behind this decision: see [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Build

Requirements: Visual Studio 2022 Build Tools (MSVC, x64), CMake, and a local copy
of the [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS) development tree (not part of
this repo — its own, large history; fetch it separately and point the CMake
variable `UE4SS_DIR` at the checkout, or place it under `vendor/RE-UE4SS/`).

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Game__Shipping__Win64
```

Copy `native_module/config.example.ini` to `config.ini` next to the built mod's
`dlls/main.dll` on the server (same convention Herbie's own mod uses) and set a
port/password before deploying. See Status above for what does and doesn't work yet.

## License

See [`LICENSE`](LICENSE). Short version: free to use, including commercially
(e.g. for game-server hosters). Redistribution only unmodified and free of
charge. No modification or derivative works without permission.

---

## Deutsch

Ein eigenständiger, quelloffen einsehbarer Ersatz für [`herbie96x/SCUM-RCON`](https://github.com/herbie96x/SCUM-RCON) —
einen echten [Source-RCON](https://developer.valvesoftware.com/wiki/Source_RCON_Protocol)-Server
für SCUM-Dedicated-Server, mit dem sich SCUMs eigene Admin-Befehle programmatisch auslösen
lassen, ohne dass ein echter Admin-Client im Spiel verbunden sein muss.

### Warum dieses Projekt existiert

`herbie96x/SCUM-RCON` wurde von seinem Entwickler End-of-Life erklärt: das Repository ist
archiviert, der von den öffentlichen Builds benötigte Lizenzserver wurde abgeschaltet, der
Quellcode wird nicht veröffentlicht. Jeder Server, der auf diesen Mod angewiesen war,
verliert damit dauerhaft die Möglichkeit, Admin-Befehle ohne verbundenen Admin-Client
auszulösen. Dieses Projekt baut dieselbe Funktionalität unabhängig nach.

### Status

**Frühe Entwicklung, baut und läuft, Dispatch-Mechanismus wird gerade neu
ausgerichtet.** Ein echter Source-RCON-Listener und die
Worker-Thread/Game-Thread-Befehls-Queue sind implementiert und funktionieren.
Der ursprünglich geplante Dispatch-Weg, `Test_ProcessAdminCommand`, wurde per
Disassemblierung als leere Stub-Funktion im Shipping-Build entlarvt (nur ein
einzelnes `ret` — sie tut zur Laufzeit nichts, unabhängig von Berechtigung
oder Argumenten) und wurde verworfen. Aktueller Fokus:
`PlayerRpcChannel::Chat_Server_ProcessAdminCommand`, der tatsächliche Pfad,
den echte Admin-Spieler bereits nutzen. Die volle, ungeschönte
Untersuchungshistorie steht in [`docs/CHANGELOG.md`](docs/CHANGELOG.md), die
technische Umsetzung in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

### Wie es funktionieren soll

Ein natives [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS)-C++-Modul, das als Mod in den
SCUM-Serverprozess geladen wird, bietet einen echten Source-RCON-Server auf demselben
öffentlichen Wireprotokoll, das Herbies Mod ebenfalls genutzt hat. Jeder bestehende
RCON-Client (inklusive selbst geschriebener Tools) kann sich damit ohne Änderung
verbinden. Details und Begründung der Architekturentscheidung: siehe
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

### Build

Voraussetzungen: Visual Studio 2022 Build Tools (MSVC, x64), CMake, sowie eine lokale
Kopie des [UE4SS](https://github.com/UE4SS-RE/RE-UE4SS)-Entwicklungsbaums (nicht Teil
dieses Repos — eigene, große Historie, separat beziehen und per CMake-Variable
`UE4SS_DIR` auf den Checkout zeigen, oder unter `vendor/RE-UE4SS/` ablegen).

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Game__Shipping__Win64
```

`native_module/config.example.ini` nach `config.ini` neben die gebaute `dlls/main.dll`
auf dem Server kopieren (dieselbe Konvention wie bei Herbies eigenem Mod) und vor
dem Deploy Port/Passwort setzen. Siehe Status oben, was schon geht und was noch nicht.

### Lizenz

Siehe [`LICENSE`](LICENSE). Kurzfassung: Nutzung frei, auch kommerziell (z. B. für
Gameserver-Hoster). Weiterverbreitung nur unverändert und kostenlos. Keine Modifikation
oder abgeleiteten Werke ohne Erlaubnis.
