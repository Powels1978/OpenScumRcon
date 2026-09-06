# Changelog / Änderungsprotokoll

[English](#english) | [Deutsch](#deutsch)

## English

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
