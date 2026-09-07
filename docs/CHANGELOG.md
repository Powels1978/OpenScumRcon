# Changelog / Änderungsprotokoll

[English](#english) | [Deutsch](#deutsch)

## English

### 2026-09-07 — Player profile/IP and live InGame weather

Added guarded profile IDs and actual connection IPs to `ListPlayers`.
Added authenticated `GetWeather` and `GetTimeOfDay` JSON queries using the active
SCUM weather controller without an online executor. Read native rain, fog, wind,
clouds and time; do not substitute database snapshots or override values.
Current km/h is explicitly unavailable until its conversion is verified.

Validation: Shipping build, 96 runtime-compatible UE4SS imports, all four standalone
test programs, empty/occupied live queries, Herbie profile comparison and IP comparison
against SCUM's login record. Herbie remained enabled. No public player or server data
is included. General commands still require an online executor; full parity remains open.

### 2026-09-07 — Registry dispatch, native replies and player list

Added `!commands [filter]`, `!exec <SteamID> <command> [arguments...]` and independent
`ListPlayers`. Native execution uses checked registry metadata and an explicit
connected player context. Synchronous replies are captured for the exact command;
foreign replies pass through. Direct GodMode now appends SCUM's text to verified
state. Removed the ineffective RPC fallback. Added quoted argument parsing,
metadata/build/vtable guards and a generic native exception latch. RCON response
payloads are limited to 4,086 bytes at UTF-8 boundaries; malformed terminators and
embedded NUL payload bytes are rejected.

Validation: Shipping build, 90 compatible UE4SS imports, parser/authority/queue and
new infrastructure tests. Live tests passed for the 233-entry catalogue
(185 candidates), empty/occupied player lists, client parsing, rejected
authentication/arguments, non-admin GodMode on/off, native server-time replies and
foreign Herbie replies. GodMode was restored, Immortality and chat rights were
unchanged, and the server remained stable. Herbie stayed loaded. Other candidates,
a playerless executor and complete client compatibility remain unvalidated or open.

### 2026-09-06 — English and German publication standard

Expanded the README, architecture, changelog, references and configuration example
with matching English and German explanations. Added publication instructions and a
non-binding German license translation; the original license remains unchanged.
Future public documentation, commit messages, release notes and pull-request text
will be provided in both languages, English first. This update changes documentation
and configuration comments only; configuration values and runtime source are unchanged.

Validation: Markdown language links, document completeness, unchanged configuration
values, unchanged runtime source, and publication checks for private data.

### 2026-09-06 — Native GodMode and authenticated RCON authority

- Added native `SetGodMode true|false <SteamID>` for a connected target with a live pawn.
- Added `!godmode_state` and construction-only `!godmode_prepare` diagnostics.
- Carried authenticated RCON authority through the queue; rejected unauthenticated
  requests before game-thread execution. Target chat permissions remain independent.
- Added strict target parsing, validated object construction and vtable checks,
  native build guards, and verification of GodMode with unchanged Immortality.
- Added a bounded observer for development diagnostics.
- Preserved existing observer tools; disabled the obsolete interface-bypass experiment.
- Made diagnostic log paths relative to the server working directory and expanded
  exclusions for deployment configuration and runtime data.
- Replaced environment-specific research documents with general public documentation.
  Raw research, captures and the local development history remain private.

Validation: Windows x64 Shipping build, 16 parser checks and authority/queue tests.
Live tests confirmed on/off for admin and non-admin recipients, invalid/disconnected
request rejection and authentication failure without a native call. Herbie remained
loaded; a test without it is still pending. Portable log paths were rebuilt after
these live tests and have not been redeployed as part of publication.

### Earlier development

Implemented the Source RCON listener, game-thread command queue, object-discovery
diagnostics and PE cross-reference tooling. Reflected command paths were found
ineffective. Early native experiments failed; the current GodMode path uses normal
object construction and the correctly identified native handler. Raw investigation
logs and environment-specific notes are excluded from the public tree.

---

## Deutsch

### 2026-09-07 — Spielerprofil/IP und aktuelles InGame-Wetter

Geprüfte Profil-IDs und tatsächliche Verbindungs-IPs in `ListPlayers` ergänzt.
Authentifizierte JSON-Abfragen `GetWeather` und `GetTimeOfDay` verwenden den aktiven
SCUM-Wettercontroller ohne Online-Executor. Native Werte für Regen, Nebel, Wind,
Wolken und Zeit auslesen; keine Datenbankstände oder Overrides als Ersatz verwenden.
Aktuelle km/h bleiben bis zur geprüften Umrechnung ausdrücklich nicht verfügbar.

Prüfungen: Shipping-Build, 96 zur Laufzeit passende UE4SS-Importe, alle vier
Testprogramme, leere/belegte Live-Abfragen, Herbie-Profilvergleich und IP-Abgleich
mit SCUMs Login-Eintrag. Herbie blieb aktiv. Keine echten Spieler-/Serverdaten
veröffentlicht. Allgemeine Befehle benötigen weiter einen Online-Executor;
vollständige Funktionsgleichheit bleibt offen.

### 2026-09-07 — Registry-Ausführung, native Antworten und Spielerliste

`!commands [filter]`, `!exec <SteamID> <command> [arguments...]` und eigenständiges
`ListPlayers` ergänzt. Native Ausführung verwendet geprüfte Registry-Metadaten und
einen expliziten verbundenen Spielerkontext. Synchrone Antworten werden für den
genauen Befehl erfasst; fremde Antworten durchgelassen. Direktes GodMode ergänzt
den geprüften Zustand um SCUMs Text. Wirkungslosen RPC-Pfad entfernt.
Zitierte Argumente, Metadaten-/Build-/Vtable-Prüfungen und eine Sperre nach allgemeiner
nativer Ausnahme ergänzt. RCON-Antwortnutzdaten sind an UTF-8-Grenzen auf 4.086 Bytes
begrenzt; fehlerhafte Terminatoren und eingebettete NUL-Nutzdaten werden abgewiesen.

Prüfungen: Shipping-Build, 90 kompatible UE4SS-Importe, Parser-/Berechtigungs-/Queue-
und neue Infrastrukturtests. Live bestanden: Katalog mit 233 Einträgen
(185 Kandidaten), leere/belegte Spielerlisten, Client-Parser, abgewiesene
Anmeldungen/Argumente, Nicht-Admin-GodMode an/aus, native Serverzeit-Antworten und
fremde Herbie-Antworten. GodMode wurde zurückgesetzt, Immortality und Chatrechte
blieben unverändert, der Server stabil. Herbie blieb geladen. Andere Kandidaten,
Executor ohne Online-Spieler und vollständige Client-Kompatibilität sind ungeprüft
beziehungsweise offen.

### 2026-09-06 — Veröffentlichungen auf Englisch und Deutsch

README, Architektur, Änderungsprotokoll, Referenzen und Beispielkonfiguration um
inhaltlich entsprechende englische und deutsche Erläuterungen ergänzt.
Veröffentlichungsregeln und eine unverbindliche deutsche Lizenzübersetzung
hinzugefügt; die ursprüngliche Lizenz bleibt unverändert. Künftige öffentliche
Dokumentation, Commit-Nachrichten, Versionshinweise und Pull-Request-Texte werden
in beiden Sprachen verfasst, Englisch zuerst. Diese Änderung betrifft nur Texte
und Konfigurationskommentare; Konfigurationswerte und Laufzeitquellcode sind unverändert.

Prüfungen: Markdown-Sprachverweise, Vollständigkeit der Dokumente, unveränderte
Konfigurationswerte, unveränderter Laufzeitquellcode und Prüfung auf private Daten
vor der Veröffentlichung.

### 2026-09-06 — Nativer GodMode und authentifizierte RCON-Berechtigung

- Nativen Befehl `SetGodMode true|false <SteamID>` für einen verbundenen Zielspieler
  mit gültiger Pawn hinzugefügt.
- `!godmode_state` und `!godmode_prepare` ergänzt; letzterer prüft nur die Vorbereitung.
- Authentifizierte RCON-Berechtigung durch die Queue weitergereicht und nicht
  authentifizierte Anfragen vor der Ausführung auf dem Spielthread abgewiesen.
  Die Chatrechte des Zielspielers bleiben davon unabhängig.
- Strenge Zielprüfung, geprüfte Objekterzeugung und Vtable-Prüfung,
  Absicherung des nativen Builds sowie Kontrolle von GodMode bei unveränderter
  Immortality hinzugefügt.
- Einen begrenzten Beobachter für Entwicklungsdiagnosen ergänzt.
- Bestehende Beobachtungswerkzeuge erhalten und den veralteten Versuch zur
  Umgehung der Interfaceprüfung deaktiviert.
- Diagnose-Logpfade auf das Arbeitsverzeichnis des Servers bezogen und Ausschlüsse
  für Bereitstellungskonfigurationen und Laufzeitdaten erweitert.
- Umgebungsspezifische Forschungsdokumente durch allgemeine öffentliche
  Dokumentation ersetzt. Rohdaten, Mitschnitte und der lokale Entwicklungsverlauf
  werden privat gehalten.

Prüfungen: Windows-x64-Shipping-Build, 16 Parserfälle und Berechtigungs-/Queue-Tests.
Live-Tests bestätigten an/aus für Zielspieler mit und ohne Adminrechte, die Abweisung
ungültiger oder nicht verbundener Ziele sowie fehlgeschlagene Anmeldung ohne nativen
Aufruf. Herbie blieb geladen; ein Test ohne Herbie steht aus. Die portablen Logpfade
wurden nach diesen Live-Tests neu gebaut, im Rahmen der Veröffentlichung jedoch
nicht erneut bereitgestellt.

### Frühere Entwicklung

Source-RCON-Listener, Queue für den Spielthread, Diagnosewerkzeuge zur Objektsuche
und PE-Querverweisanalyse implementiert. Die reflektierten Befehlspfade erwiesen sich
als wirkungslos. Frühe native Experimente scheiterten; der aktuelle GodMode-Pfad
verwendet reguläre Objekterzeugung und den korrekt ermittelten nativen Handler.
Untersuchungslogs und umgebungsspezifische Notizen sind aus dem öffentlichen
Dateibaum ausgeschlossen.
