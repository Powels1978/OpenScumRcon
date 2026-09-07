# Native commands / Native Befehle

[English](#english) | [Deutsch](#deutsch)

## English

All requests require Source RCON authentication. Replace `<SteamID>` with the exact
17-digit ID returned by `ListPlayers`; angle brackets are placeholders.

```text
ListPlayers
!commands
!commands time
!exec <SteamID> CheckServerTime
!exec <SteamID> SetGodMode true
!exec <SteamID> SetGodMode false
!godmode_state <SteamID>
!godmode_prepare <SteamID>
SetGodMode true <SteamID>
SetGodMode false <SteamID>
```

`ListPlayers` and `!commands [filter]` work without online players. The filter is a
case-insensitive substring. The catalogue reports argument counts, flags, eligibility
and overall `capture`/`build` status. On the tested build, 233 entries include
185 candidates, 47 disabled/client-only entries and `SetBodyType` rejected for
inconsistent argument metadata. Candidate status is not a successful live test.

Player records use this format, with optional balances, ping and coordinates:

```text
PLAYER steam=<SteamID> money=<balance> gold=<balance> ping=<milliseconds> (<x>, <y>, <z>) | <character name> |
```

No profile ID is returned. Identity and metadata precede the bounded name, which
has control characters and `|` removed. This was checked against an existing RCON
client line parser. Empty servers return `No players online.`.

`!exec` requires a connected controller with a live Prisoner pawn; the player needs
no chat admin rights. This selects execution context, not necessarily the affected
object: native commands may affect the whole server or other objects.
The first SteamID is consumed by OpenScumRcon. Remaining arguments follow SCUM's
native syntax; do not automatically append a Herbie-style target SteamID.

Native verbs are case-insensitive and may start with `#`. Double quotes group one
argument containing spaces; inside quotes, `\"` and `\\` represent a quote and a
backslash. Empty quoted arguments are preserved. Input must use UTF-8.
Limits are 8,192 request bytes, 64 native arguments and 4,096 bytes per token.
Embedded controls except tabs and multiple command lines are rejected. Argument
counts, eligibility and exact target connection are checked before execution.

`!godmode_prepare` checks construction without execution. `chat_can_execute=false`
is diagnostic and does not deny authenticated RCON. Direct `SetGodMode` additionally
verifies the requested state and unchanged Immortality. Generic `!exec` returns
native text without this additional state check.

Synchronous replies are bounded to 65,536 UTF-8 bytes and 128 messages. Long replies
span multiple RCON packets; clients must collect packets for the request ID.
Unrelated commands retain their reply path. Deferred replies are not captured.
A false native return is an error. True without text is explicitly reported and
does not prove gameplay state. Capture failure may occur after execution; check
state before retrying. A native exception disables further generic execution until
restart and reports unknown status.

Only GodMode and `CheckServerTime` have been live-validated in this native execution
work. Other candidate commands, direct compatibility aliases beyond GodMode,
playerless execution and validation with Herbie disabled remain open.
See [architecture](ARCHITECTURE.md) and [changes](CHANGELOG.md).

---

## Deutsch

Alle Anfragen benötigen Source-RCON-Anmeldung. `<SteamID>` durch die genaue
17-stellige ID aus `ListPlayers` ersetzen; Winkelklammern kennzeichnen Platzhalter.

```text
ListPlayers
!commands
!commands time
!exec <SteamID> CheckServerTime
!exec <SteamID> SetGodMode true
!exec <SteamID> SetGodMode false
!godmode_state <SteamID>
!godmode_prepare <SteamID>
SetGodMode true <SteamID>
SetGodMode false <SteamID>
```

`ListPlayers` und `!commands [filter]` funktionieren ohne Online-Spieler.
Der Filter sucht einen Teilstring ohne Beachtung der Groß-/Kleinschreibung.
Der Katalog meldet Argumentzahlen, Flags, Eignung und den Gesamtstatus von
`capture`/`build`. Im getesteten Build enthalten 233 Einträge 185 Kandidaten,
47 deaktivierte/clientseitige Einträge und `SetBodyType`, das wegen widersprüchlicher
Argumentdaten abgewiesen wird. Kandidatenstatus bedeutet keinen bestandenen Live-Test.

Spielerdatensätze verwenden dieses Format mit optionalen Kontoständen, Ping und Koordinaten:

```text
PLAYER steam=<SteamID> money=<balance> gold=<balance> ping=<milliseconds> (<x>, <y>, <z>) | <character name> |
```

Profil-IDs fehlen. Identität und Metadaten stehen vor dem begrenzten Namen, aus dem
Steuerzeichen und `|` entfernt werden. Dies wurde gegen einen vorhandenen
RCON-Client-Zeilenparser geprüft. Leere Server liefern `No players online.`.

`!exec` benötigt einen verbundenen Controller mit aktiver Prisoner-Pawn; der Spieler
braucht keine Chat-Adminrechte. Dies wählt den Ausführungskontext, nicht zwingend das
betroffene Objekt: Native Befehle können den ganzen Server oder andere Objekte
betreffen. Die erste SteamID wird von OpenScumRcon verarbeitet. Restliche Argumente
folgen SCUMs nativer Syntax; keine Ziel-SteamID nach Herbie-Schema automatisch anhängen.

Native Befehlsnamen unterscheiden nicht zwischen Groß-/Kleinschreibung und dürfen
mit `#` beginnen. Doppelte Anführungszeichen fassen ein Argument mit Leerzeichen
zusammen; darin stehen `\"` und `\\` für Anführungszeichen und Backslash. Leere zitierte
Argumente bleiben erhalten. Eingaben müssen UTF-8 verwenden.
Grenzen: 8.192 Anfrage-Bytes, 64 native Argumente und 4.096 Bytes pro Token.
Eingebettete Steuerzeichen außer Tabulatoren und mehrere Befehlszeilen werden
abgewiesen. Argumentzahlen, Eignung und genaue Zielverbindung werden vorab geprüft.

`!godmode_prepare` prüft die Erzeugung ohne Ausführung. `chat_can_execute=false` dient
der Diagnose und verweigert kein authentifiziertes RCON. Direktes `SetGodMode` prüft
zusätzlich den angeforderten Zustand und unveränderte Immortality. Allgemeines
`!exec` liefert nativen Text ohne diese zusätzliche Zustandskontrolle.

Synchrone Antworten sind auf 65.536 UTF-8-Bytes und 128 Nachrichten begrenzt.
Lange Antworten umfassen mehrere RCON-Pakete; Clients müssen Pakete zur Anfrage-ID
einsammeln. Andere Befehle behalten ihren Antwortpfad. Verzögerte Antworten fehlen.
Ein nativer Rückgabewert false ist ein Fehler. True ohne Text wird ausdrücklich
gemeldet und beweist keinen Spielzustand. Erfassungsfehler können nach der Ausführung
auftreten; vor Wiederholung den Zustand prüfen. Eine native Ausnahme sperrt die
weitere allgemeine Ausführung bis zum Neustart und meldet einen unbekannten Status.

Im Rahmen dieser nativen Ausführung wurden nur GodMode und `CheckServerTime` live
bestätigt. Andere Kandidaten, direkte Kompatibilitätsaliase über GodMode hinaus,
Ausführung ohne Online-Spieler und Prüfung mit deaktiviertem Herbie bleiben offen.
Siehe [Architektur](ARCHITECTURE.md#deutsch) und [Änderungen](CHANGELOG.md#deutsch).
