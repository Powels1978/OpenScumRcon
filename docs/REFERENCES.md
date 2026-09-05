# Referenzen

Gesammelte externe Quellen zu SCUM-RCON-Implementierungen und dem zugrundeliegenden
Protokoll, damit wir nicht bei jeder Session neu suchen müssen. Alle Links wurden am
2026-09-05 geprüft.

## Valve Source RCON Protocol

- [Offizielle Spezifikation](https://developer.valvesoftware.com/wiki/Source_RCON_Protocol) —
  das Wireprotokoll, das wir selbst implementiert haben (`native_module/src/rcon_protocol.*`).
  Öffentlich, jahrzehntealt, keine offenen Fragen hier.

## herbie96x/SCUM-RCON (das abgelöste Original)

- [GitHub-Repo](https://github.com/herbie96x/SCUM-RCON) — archiviert seit 2026-08-12,
  End-of-Life erklärt (Lizenzserver abgeschaltet, kein Quellcode geplant).
- Voll gelesene Doku (README.md, USAGE.md, PLUGIN_API.md) — Zusammenfassung im privaten
  Schwesterprojekt: `docs/herbie-rcon-replacement.md` (nicht Teil dieses Repos).
- Zentrale Erkenntnisse (siehe `docs/ARCHITECTURE.md` für Details):
  - Dispatching läuft laut eigener Doku "via compiled C++ behind Structured Exception
    Handling guards" — kompilierter nativer Code, keine interpretierten Skripte.
  - Eigenes Log (`UE4SS.log`, live beobachtet während `scum_rcon` noch lief) zeigt:
    Pattern-Scan + zwei native Hooks auf die Autorisierungs-Gates ("EngineHooks"),
    ein Chat-Detour-Hook zum Abfangen der Textantworten, ein eigener
    "Verb-Map"-Scan über alle `AdminCommand`-Subklassen, Game-Thread-Drain über einen
    `EngineTick`-Pre-Hook.
  - **Wichtig**: keine Untersuchung/Disassemblierung des eigentlichen Binaries
    (`scum_rcon\dlls\main.dll`) — alle Erkenntnisse stammen aus öffentlich lesbarer
    Doku und dem selbst erzeugten Logtext der lizenzkonform genutzten Software.

## jasonuithol/SCUM-Mods — "DeveloperMode"

- [GitHub-Repo](https://github.com/jasonuithol/SCUM-Mods) — beschreibt im README das
  generelle Konzept: AOB-Scan auf zwei Autorisierungs-Gates ("Player must be
  developer." / "Not authorized to execute command."), zwei kleine In-Memory-Hooks
  schalten sie frei. Lizenz verbietet Reverse Engineering/Codeübernahme — nur das
  öffentlich beschriebene Konzept wird hier als Ausgangspunkt genutzt, kein Code
  untersucht oder übernommen.

## ggCON (GGHost, offizieller SCUM-Hosting-Partner)

- [Docs-Repo](https://github.com/GGHostDotGames/ggCON-docs) /
  [gehostete Doku](https://ggcon.gghost.games/docs/) (Direktzugriff teils per
  Bot-Schutz blockiert — GitHub-Raw-Dateien funktionieren) — eine weitere,
  aktiv gepflegte SCUM-Admin-Lösung von GGHost, inkl. eigenem HTTP-API, RCON-Server,
  Web-Panel, Live-Map.
- **RCON** (`docs/rcon.md`): implementiert ebenfalls "the standard Source RCON packet
  format" (int32 size/request-id/type + NUL-terminierter UTF-8-Body) — bestätigt
  unabhängig, dass das Standardprotokoll der richtige Ansatz ist. RCON-Antworten
  liefern laut eigener Doku "the same JSON as the HTTP POST /command endpoint" —
  d. h. sie haben (wie wir) einen gemeinsamen internen Dispatch-Punkt hinter HTTP
  und RCON.
  Auth nur über IP-Allowlist (`AllowedIPs`/`AllowedCIDRs`) + optionales Passwort,
  keine Rollen-/Rechte-Ebenen dokumentiert.
- **Keine öffentlichen Details** zum internen Dispatch-Mechanismus (kein Hooking,
  kein UE4SS, keine Speicher-Details erwähnt) — dieselbe Zurückhaltung wie bei
  Herbie, nachvollziehbar (Wettbewerbsvorteil/Missbrauchsschutz).
- `docs/commands.md`: `isGodMode` taucht nur als **Lesefeld** in der Spielerliste
  auf (deckt sich mit unserer eigenen `native_telemetry`-Beobachtung), keine
  dokumentierte Set-Godmode-Befehlssemantik oder Berechtigungsdetails gefunden.
- Noch nicht geprüft: `docs/http-api.md` (81 KB, umfangreichste Datei),
  `docs/plugins.md`, `docs/log-watcher.md` — könnten bei Bedarf noch ergiebig sein.

## Offene Fragen, für die diese Quellen (bisher) keine Antwort liefern

- Wie genau `SetGodMode true <steamId>` (oder die interne Entsprechung) eine
  gültige Autorisierung erkennt, wenn es nicht per Reflection mit einem
  synthetischen `WorldContextObject` aufgerufen wird (unser aktueller offener
  Punkt, siehe `docs/CHANGELOG.md`, 2026-09-05).
- Die genaue Bedeutung/Wirkung von `AdminUsers.ini`-Tags wie `[godmode]` aus
  SCUM-Sicht (Nutzer-Erklärung: kennzeichnet, welcher Admin GodMode nutzen darf,
  als Anti-Cheat-Whitelist gegen automatische Kicks/Bans bei aktivem GodMode ohne
  Berechtigung — nicht unbedingt identisch mit "voller Admin-Status für
  Befehlsausführung").
