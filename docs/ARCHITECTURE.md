# Architecture / Architektur

[English](#english) | [Deutsch](#deutsch)

## English

### Request and authority flow

The native UE4SS module starts a Source RCON TCP listener. A client must authenticate
before a command is enqueued. `CommandAuthority::authenticated_rcon` travels with
the request through the queue to the game-thread dispatcher. Requests without that
authority are rejected before any native command call. Response futures remain
associated with their individual requests.

Unreal object discovery, construction and execution happen synchronously on the
game thread. Network threads do not access gameplay objects.

### Native GodMode

The target is resolved by its exact SteamID using live `ConZPlayerController`
instances with a valid connection and Prisoner pawn. No fallback to another player
is allowed. The dispatcher resolves the `SetGodMode_C` class and creates a regular
command instance with the target controller as Outer.

The implementation calls the version-checked native GodMode handler with a borrowed
`TArray<FString>` view containing one boolean argument. The SteamID is used to select
the recipient and is not forwarded as an extra native argument. Unreal owns the
command instance; the module neither modifies the class default object's Outer nor
fabricates an executor interface pointer.

RCON authority is separate from the target's chat permissions. The preparation
request reports SCUM's chat permission check for diagnosis; it is not a permission
gate for an authenticated RCON command. The dispatcher does not grant the recipient
admin rights or change shared chat cooldown timestamps.

Execution verifies the resulting GodMode flag and checks that Immortality was
preserved. Replies describe that verified state. Native SCUM response capture has
not yet been implemented.

### Version and observation guards

The tested native path validates the executable image metadata, function signatures,
object identity and vtable slots. An unsupported signature disables execution.
Offsets are implementation details of the tested build, not portable promises.

The optional GodMode observer is bounded to four calls and a 120-second window. It
records local diagnostic data and calls the original function without altering its
arguments or result. Other historical diagnostic commands are development tools;
their local output must not be committed or published.

### Validation and open work

Standalone tests cover request parsing, rejecting missing authority, authenticated
queue handoff and response association. Live tests verified GodMode on/off for
admin and non-admin targets, unchanged Immortality, disconnected-target rejection,
and rejection before native execution when authentication was missing or incorrect.
Herbie remained loaded during the tests; removal still needs a separate validation.

Two reflected paths did not perform the requested gameplay change:
`MiscStatics::Test_ProcessAdminCommand` is a Shipping stub, and the attempted
`PlayerRpcChannel::Chat_Server_ProcessAdminCommand` invocation had no effect.
A historical RPC acknowledgement must not be interpreted as successful execution.

Next work is the registry-based dispatcher, native response capture and an independent
`ListPlayers` query. Commands needing a server-wide executor without any connected
player require additional investigation. No blanket compatibility claim is made.

---

## Deutsch

### Anfrage und Berechtigung

Das native UE4SS-Modul startet einen TCP-Listener für Source RCON. Ein Client muss
sich anmelden, bevor ein Befehl in die Warteschlange gelangt.
`CommandAuthority::authenticated_rcon` wird mit der Anfrage über die Queue zum
Dispatcher auf dem Spielthread weitergegeben. Anfragen ohne diese Berechtigung
werden vor jedem nativen Befehlsaufruf abgewiesen. Antwort-Futures bleiben ihrer
jeweiligen Anfrage zugeordnet.

Unreal-Objekte werden synchron auf dem Spielthread gesucht, erzeugt und verwendet.
Netzwerkthreads greifen nicht auf Spielobjekte zu.

### Nativer GodMode

Das Ziel wird anhand seiner exakten SteamID unter gültigen `ConZPlayerController`-
Instanzen mit aktiver Verbindung und Prisoner-Pawn ermittelt. Ein Ausweichen auf
einen anderen Spieler ist ausgeschlossen. Der Dispatcher löst `SetGodMode_C` auf
und erzeugt eine reguläre Befehlsinstanz mit dem Zielcontroller als Outer.

Die Implementierung ruft den versionsgeprüften nativen GodMode-Handler mit einer
geliehenen `TArray<FString>`-Ansicht auf, die genau ein boolesches Argument enthält.
Die SteamID dient der Zielauswahl und wird nicht als zusätzliches natives Argument
übergeben. Unreal verwaltet die Befehlsinstanz; das Modul verändert weder den Outer
des Klassenstandardobjekts noch erfindet es einen Executor-Interfacezeiger.

Die RCON-Berechtigung ist von den Chatrechten des Zielspielers getrennt. Der
Vorbereitungsbefehl zeigt SCUMs Chatberechtigungsprüfung zur Diagnose an; sie ist
keine Zugangsvoraussetzung für einen authentifizierten RCON-Befehl. Der Dispatcher
vergibt keine Adminrechte an den Zielspieler und verändert keine gemeinsam genutzten
Zeitstempel für Chat-Cooldowns.

Die Ausführung prüft den resultierenden GodMode-Zustand und unveränderte Immortality.
Antworten beschreiben diesen kontrollierten Zustand. Die Erfassung nativer
SCUM-Antworttexte ist noch nicht implementiert.

### Versionsprüfung und Beobachtung

Der getestete native Pfad prüft Metadaten der ausführbaren Datei,
Funktionssignaturen, Objektidentität und Vtable-Einträge. Eine unbekannte Signatur
deaktiviert die Ausführung. Offsets sind Implementierungsdetails des getesteten
Builds und keine Zusage zur Kompatibilität mit anderen Versionen.

Der optionale GodMode-Beobachter ist auf vier Aufrufe und ein Zeitfenster von
120 Sekunden begrenzt. Er schreibt lokale Diagnosedaten und ruft die Originalfunktion
mit unveränderten Argumenten und unverändertem Ergebnis auf. Weitere historische
Diagnosebefehle sind Entwicklungswerkzeuge; ihre lokalen Ausgaben dürfen nicht
versioniert oder veröffentlicht werden.

### Prüfungen und offene Arbeiten

Eigenständige Tests prüfen das Einlesen der Befehle, die Abweisung fehlender
Berechtigung, die authentifizierte Queue-Übergabe und die Zuordnung der Antworten.
Live-Tests bestätigten GodMode an/aus bei Zielspielern mit und ohne Adminrechte,
unveränderte Immortality, die Abweisung nicht verbundener Ziele und die Abweisung
fehlender oder falscher Anmeldung vor der nativen Ausführung. Herbie blieb während
der Tests geladen; sein Entfernen erfordert noch eine gesonderte Prüfung.

Zwei Reflection-Pfade bewirkten die angeforderte Spieländerung nicht:
`MiscStatics::Test_ProcessAdminCommand` ist im Shipping-Build eine leere
Stub-Funktion; der versuchte Aufruf von
`PlayerRpcChannel::Chat_Server_ProcessAdminCommand` blieb wirkungslos.
Eine historische RPC-Bestätigung darf nicht als erfolgreiche Ausführung gelten.

Als Nächstes folgen ein Dispatcher auf Basis der Befehlsregistrierung, die Erfassung
nativer Antworten und eine unabhängige `ListPlayers`-Abfrage. Befehle mit einem
serverweiten Executor ohne verbundenen Spieler müssen weiter untersucht werden.
Eine allgemeine Kompatibilität mit allen Befehlen wird nicht zugesagt.
