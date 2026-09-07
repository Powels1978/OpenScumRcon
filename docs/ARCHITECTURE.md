# Architecture / Architektur

[English](#english) | [Deutsch](#deutsch)

## English

### Request flow and native dispatch

The Source RCON listener authenticates before enqueueing a request.
`CommandAuthority::authenticated_rcon` travels through the queue to the game
thread; requests without authority are rejected. Individual futures retain response
association. Network threads never access gameplay objects.

`!commands` reads the live `AdminCommandRegistry` and command class default objects.
It reports verbs, required/declared/repeating argument counts, enabled/shipping/server
flags and native eligibility. Metadata, class identity, the shared reply method
and executable handler are checked. A native candidate is not a live-tested command.

`!exec` resolves a unique eligible entry and checks argument counts, the supported
build and response hook. It requires the exact connected SteamID, a live Prisoner
pawn and the controller's real AdminCommandExecutor interface. A normal command
instance is created with that controller as Outer. Class, Outer and vtable are
validated before invoking the native handler with a borrowed `TArray<FString>`.
The first SteamID selects context and is not appended to native arguments.

Unreal owns command instances. No class default object Outer, player admin rights
or shared chat cooldowns are changed, and no interface pointer is fabricated.
Object pointers are not cached between requests. A structured exception in the
generic native call latches further generic execution off until restart and reports
unknown execution status; no automatic retry occurs.

### GodMode, replies and player list

The direct `SetGodMode true|false <SteamID>` route independently checks GodMode and
unchanged Immortality, then appends the native reply. `!godmode_prepare` checks
construction and reports chat permissions without execution. Chat permission is
diagnostic; authenticated RCON supplies command authority.

A version- and prologue-checked hook captures the shared native reply method.
A thread-local RAII scope matches the exact currently executing command instance.
Its synchronous replies are redirected into its response buffer. Other instances
and threads pass through the original method with unchanged arguments.
The buffer allows 65,536 UTF-8 bytes and 128 messages, with explicit truncation and
failure reporting. Replies deferred until after execution returns are not captured.
Generic execution reports a false native return as an error; true without text is
explicitly identified. It does not independently verify every command's gameplay effect.

`ListPlayers` reads live controllers and network connections independently of Herbie,
returning exact SteamIDs, character names and available balances, ping and finite
pawn coordinates. Identity precedes the sanitized name for unambiguous line parsing.
Profile IDs come from checked `GetUserProfileId` reflection. IPs come from the
version-checked native connection address reader. Missing optional fields are omitted.
An empty server returns `No players online.`.

RCON responses split at UTF-8 boundaries into payloads of at most 4,086 bytes.
The 4,096-byte packet body includes the ID, type and two NUL bytes; the four-byte
length prefix is separate. Bad terminators and embedded NUL payload bytes are rejected.

Read-only `GetWeather`/`GetTimeOfDay` resolve one live controller through
`ConZWorldSettings.WeatherController2`. Reflection reads current native values on the
game thread and emits finite, locale-independent JSON. See [live queries](LIVE_QUERIES.md).

### Validation and open work

The tested native build is SCUM **1.3.3.1.145413**. Image metadata, signatures,
object identity and vtable checks constrain calls; offsets are not portable promises.
Optional observers produce bounded local diagnostics that must remain private.

Validation passed: Shipping build, 90 UE4SS imports against the installed runtime,
parser/authority/queue tests, response isolation and bounds, and UTF-8 multi-packet
framing. Live checks covered the catalogue, empty/occupied player lists, an existing
client line parser, authentication/argument rejection, non-admin GodMode on/off,
native `CheckServerTime` replies and foreign Herbie reply passthrough. GodMode was
restored, Immortality and chat permissions were unchanged, and the server stayed stable.

The ineffective reflected RPC execution fallback has been removed. Historical
observers remain diagnostics. Herbie stayed loaded during validation. Testing with
Herbie disabled, the other native candidates, a playerless server executor and full
client command compatibility remain open.

---

## Deutsch

### Anfragefluss und native Ausführung

Der Source-RCON-Listener authentifiziert vor Aufnahme einer Anfrage in die Queue.
`CommandAuthority::authenticated_rcon` gelangt zum Spielthread; Anfragen ohne
Berechtigung werden abgewiesen. Einzelne Futures erhalten die Antwortzuordnung.
Netzwerkthreads greifen niemals auf Spielobjekte zu.

`!commands` liest die aktive `AdminCommandRegistry` und die Klassenstandardobjekte
der Befehle. Angezeigt werden Namen, erforderliche/deklarierte/wiederholbare
Argumentzahlen, enabled/shipping/server-Flags und native Eignung. Metadaten,
Klassenidentität, gemeinsame Antwortmethode und ausführbarer Handler werden geprüft.
Ein nativer Kandidat ist kein live geprüfter Befehl.

`!exec` ermittelt einen eindeutigen geeigneten Eintrag und prüft Argumentzahlen,
unterstützten Build und Antwort-Hook. Benötigt werden die exakte verbundene SteamID,
eine aktive Prisoner-Pawn und das echte AdminCommandExecutor-Interface des Controllers.
Eine reguläre Befehlsinstanz wird mit diesem Controller als Outer erzeugt.
Klasse, Outer und Vtable werden vor dem nativen Aufruf mit einer geliehenen
`TArray<FString>`-Ansicht geprüft. Die erste SteamID wählt den Kontext und wird
nicht an native Argumente angehängt.

Unreal verwaltet die Befehlsinstanzen. Klassenstandardobjekt-Outer, Spielerrechte
und gemeinsame Chat-Cooldowns werden nicht verändert; kein Interfacezeiger wird
erfunden. Objektzeiger werden nicht zwischen Anfragen gespeichert. Eine strukturierte
Ausnahme im allgemeinen nativen Aufruf sperrt weitere allgemeine Ausführung bis zum
Neustart und meldet den Status als unbekannt; es gibt keine automatische Wiederholung.

### GodMode, Antworten und Spielerliste

Der direkte Pfad `SetGodMode true|false <SteamID>` prüft GodMode und unveränderte
Immortality unabhängig und ergänzt die native Antwort. `!godmode_prepare` prüft
die Erzeugung und meldet Chatrechte ohne Ausführung. Chatrechte dienen der Diagnose;
die authentifizierte RCON-Verbindung liefert die Befehlsberechtigung.

Ein versions- und signaturgeprüfter Hook erfasst die gemeinsame native Antwortmethode.
Ein threadlokaler RAII-Bereich prüft die genaue gerade ausgeführte Befehlsinstanz.
Ihre synchronen Antworten werden in ihren Antwortpuffer umgeleitet. Andere
Instanzen und Threads durchlaufen die Originalmethode mit unveränderten Argumenten.
Der Puffer erlaubt 65.536 UTF-8-Bytes und 128 Nachrichten; Kürzung und Fehler werden
ausdrücklich gemeldet. Nach Rückkehr aus der Ausführung verzögerte Antworten werden
nicht erfasst. Die allgemeine Ausführung meldet false als Fehler und true ohne Text
ausdrücklich. Sie prüft nicht unabhängig die Spielwirkung jedes einzelnen Befehls.

`ListPlayers` liest aktive Controller und Netzwerkverbindungen unabhängig von
Herbie. Zurückgegeben werden genaue SteamIDs, Charakternamen und verfügbare
Kontostände, Ping und endliche Pawn-Koordinaten. Für eindeutige Zeilenparser steht
die Identität vor dem bereinigten Namen. Profil-IDs stammen aus geprüftem
`GetUserProfileId`, IPs aus der versionsgeprüften nativen Verbindungsadressabfrage.
Fehlende optionale Felder werden weggelassen. Ein leerer Server liefert `No players online.`.

RCON-Antworten werden an UTF-8-Grenzen in Nutzdaten von höchstens 4.086 Bytes geteilt.
Der 4.096-Byte-Paketkörper enthält ID, Typ und zwei NUL-Bytes; das vier Byte lange
Längenpräfix kommt separat hinzu. Fehlerhafte Terminatoren und eingebettete
NUL-Nutzdaten werden abgewiesen.

Die lesenden Abfragen `GetWeather`/`GetTimeOfDay` ermitteln einen aktiven Controller
über `ConZWorldSettings.WeatherController2`. Reflection liest native aktuelle Werte
auf dem Spielthread und erzeugt endliches, gebietsschemaunabhängiges JSON.
Siehe [Live-Abfragen](LIVE_QUERIES.md#deutsch).

### Prüfungen und offene Arbeit

Getestet ist SCUM-Build **1.3.3.1.145413**. Image-Metadaten, Signaturen,
Objektidentität und Vtable-Prüfungen begrenzen Aufrufe; Offsets sind keine
Portabilitätszusage. Optionale Beobachter erzeugen begrenzte lokale Diagnosen,
die privat bleiben müssen.

Bestanden: Shipping-Build, 90 UE4SS-Importe gegen die installierte Laufzeit,
Parser-/Berechtigungs-/Queue-Tests, isolierte/begrenzte Antworten und mehrteilige
UTF-8-Pakete. Live geprüft wurden Katalog, leere/belegte Spielerlisten, ein bestehender
Client-Zeilenparser, abgewiesene Anmeldungen/Argumente, Nicht-Admin-GodMode an/aus,
native `CheckServerTime`-Antworten und Durchleitung fremder Herbie-Antworten.
GodMode wurde zurückgesetzt, Immortality und Chatrechte blieben unverändert,
der Server stabil.

Der wirkungslose reflektierte RPC-Ausführungspfad wurde entfernt. Historische
Beobachter bleiben Diagnosen. Herbie blieb während der Prüfung geladen.
Tests mit deaktiviertem Herbie, die übrigen nativen Kandidaten, ein serverweiter
Executor ohne Online-Spieler und vollständige Client-Befehlskompatibilität sind offen.
