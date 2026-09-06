# Architektur

Stand: 2026-09-04. Ergebnis der Architektur-Planungsphase, bevor das eigentliche
Reverse Engineering beginnt.

## Ausgangslage

`herbie96x/SCUM-RCON` implementierte einen echten Source-RCON-Server als natives UE4SS-Modul
innerhalb von `SCUMServer.exe`. Damit ließen sich SCUMs rund 230 eingebaute, native
Admin-Befehle programmatisch auslösen, ohne dass ein echter Admin-Client verbunden sein
musste. Das Projekt wurde archiviert (EOL, Lizenzserver abgeschaltet, Quellcode wird nicht
veröffentlicht).

Marktsichtung vor Projektstart ergab: keine fertige, quelloffene Drop-in-Alternative.
Der einzige technisch relevante Fund war [`jasonuithol/SCUM-Mods`](https://github.com/jasonuithol/SCUM-Mods)
("DeveloperMode"), dessen README die generelle Technik beschreibt: ein AOB-Scan (Array-of-Bytes-Scan)
findet zwei Autorisierungs-Gates im Speicher (anhand der Strings "Player must be developer."
/ "Not authorized to execute command."), zwei kleine In-Memory-Hooks schalten sie frei —
danach läuft "the game's own dispatcher" für freigeschaltete Nutzer-Tiers. Dessen Lizenz
verbietet Reverse Engineering und Codeübernahme; dieses Projekt reimplementiert nur das
öffentlich beschriebene *Konzept* eigenständig, ohne Code oder Binary von SCUM-RCON oder
DeveloperMode zu untersuchen oder zu übernehmen.

## Klarstellung: "Memory Injection" vs. legitimes UE4SS-Modul

Herbie soll (Nutzer-Erinnerung, Quelle nicht mehr auffindbar) einmal geschrieben haben,
SCUM untersage "Memory Injection", weshalb er es "anders gemacht" habe. Das steht auf den
ersten Blick im Widerspruch zu seinem eigenen Log (`[SCUM-RCON] PatternScan: ...`,
`EngineHooks: hook 'sig_a' installed`) — tatsächlich dürfte hier eine wichtige, aber
enge Definition gemeint sein:

- **"Memory Injection"** im Anti-Cheat-relevanten, meist verbotenen Sinn: Code von
  **außen** in einen bereits laufenden, fremden Prozess einschleusen (klassisches
  `CreateRemoteThread`/`WriteProcessMemory`-Cheat-Tool-Muster) — das erkennen und
  ahnden Anti-Cheat-Systeme gezielt.
- **Was UE4SS-Mods (Herbies wie auch unsere eigenen) tatsächlich tun**: eine DLL wird
  ganz regulär beim **Start** von `SCUMServer.exe` über die normale Windows-
  DLL-Ladereihenfolge geladen (`dwmapi.dll`-Shim) — keine externe Einschleusung in
  einen bereits laufenden Prozess, sondern ein von Anfang an legitim geladenes Modul.
  Pattern-Scans/Hooks passieren danach **innerhalb des eigenen, bereits geladenen
  Moduls** auf den eigenen Prozessspeicher — technisch/rechtlich etwas anderes als
  "Injektion" in einen fremden Prozess.

**Für dieses Projekt**: Wir folgen ohnehin demselben, bereits etablierten Muster (eigenes
UE4SS-Modul, ganz normal per `mods.txt` geladen, kein externes Einschleusen in einen
fremden oder bereits laufenden Prozess) — dieselbe Konvention, die auch
`native_telemetry` und `scum_rcon` selbst nutzen. Die Klarstellung ändert nichts an der
geplanten Architektur, bestätigt aber, dass der eingeschlagene Weg (eigenes, normal
geladenes Modul statt externer Prozessmanipulation) der richtige und auch von Herbie
selbst (implizit) für zulässig gehaltene ist.

## Protokollwahl: Source RCON beibehalten

Das neue Modul implementiert **denselben Source-RCON-Wireprotokoll-Standard**
(Valve, öffentlich dokumentiert, jahrzehntealt) statt etwas Eigenes zu erfinden:

- Jeder bestehende RCON-Client kann sich unverändert verbinden — keine neue,
  proprietäre Schnittstelle nötig.
- Während der Entwicklung mit jedem beliebigen Standard-RCON-Client testbar.
- Das Protokoll ist bereits vollständig verstanden (Auth-Paket, Command-Paket,
  Multi-Paket-Antworten für lange Listenausgaben).

## Modul-Struktur

Ein eigenständiges UE4SS-C++-Modul (eigene DLL, eigener `Mods/<Name>/dlls/main.dll`-Ordner
— **nicht** Teil eines bereits produktiv laufenden Telemetrie-Moduls, um dessen Stabilität
nicht zu gefährden):

1. **Worker-Thread** (nicht der Game-Thread) hört auf dem konfigurierten TCP-Port, spricht
   das Source-RCON-Wireprotokoll (Auth, Command, Multi-Paket-Antworten), legt eingehende
   Befehle in eine thread-sichere Queue.
2. **Game-Thread** leert die Queue periodisch und ruft
   `UMiscStatics::Test_ProcessAdminCommand(WorldContextObject, commandText)` per
   `ProcessEvent` auf (siehe "Gefundener Dispatch-Mechanismus" unten) — legt die Antwort
   in eine Per-Request-Queue, die der Worker-Thread über den Socket zurücksendet.

Diese Trennung vermeidet das nachweislich riskante Muster "eine Engine-interne Funktion
synchron von einem fremden Thread bzw. außerhalb ihres normalen Aufrufpfads aufrufen" —
ein verwandtes, bereits produktiv gebautes Modul in diesem Projektumfeld (nicht Teil
dieses Repos) hat mit genau diesem Muster einmal einen echten Serverabsturz ausgelöst,
als es versuchte, eine abgefangene Server-RPC-Funktion synthetisch außerhalb ihres
normalen Call-Stacks aufzurufen.

## Gefundener Dispatch-Mechanismus (Stand 2026-09-04, Abend) — ENDGÜLTIG VERWORFEN, siehe Update 2026-09-06

**Update 2026-09-06**: Disassemblierung der nativen Implementierung hinter
`Test_ProcessAdminCommand` (Details:
[`docs/research/2026-09-05-authorization-gate-analysis.md`](research/2026-09-05-authorization-gate-analysis.md#update-2026-09-06-test_processadmincommand-ist-im-shipping-build-eine-leere-stub-funktion))
hat die Frage aus "Noch offen" unten ("Ob `Test_`-Funktionen in einem
Shipping-Build zuverlässig erhalten bleiben") endgültig beantwortet: **die
Funktion besteht in der aktuell installierten Shipping-Build-Version bereits
JETZT nur aus einem einzelnen `ret`-Byte — sie tut nichts.** Kein
Autorisierungsproblem, kein Executor-Auflösungsproblem, einfach eine leere
Funktion. Dieser gesamte Abschnitt (der ursprünglich als Lösung galt) ist damit
als Sackgasse einzustufen und wird nur aus Transparenzgründen nicht gelöscht.
Der neue Fokus ist `PlayerRpcChannel::Chat_Server_ProcessAdminCommand`.

Ein Live-Reflection-Dump (UE4SS' `DumpAllObjects()`, ausgeführt auf einem Testserver mit
0 Spielern online, nach expliziter Freigabe — siehe `docs/CHANGELOG.md`) hat den
tatsächlichen Aufrufpunkt gefunden:

```
UMiscStatics::Test_ProcessAdminCommand(UObject* WorldContextObject, FString commandText)
```

Eine ganz normale `UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"))`
auf einer Blueprint-Function-Library-Klasse (`MiscStatics`) — **kein** Speicher-Hook,
**kein** Autorisierungs-Gate-Bypass nötig. Zum Vergleich existiert auch die tatsächliche
Produktions-RPC `UPlayerRpcChannel::Chat_Server_ProcessAdminCommand(FString commandText)`,
die im Spiel läuft, wenn ein echter Spieler `#Befehl` in den Chat tippt — die braucht
aber eine echte `PlayerRpcChannel`-Instanz, also einen verbundenen Client. Die
`Test_`-Variante auf `MiscStatics` braucht nur irgendein gültiges `UObject*` mit Zugriff
auf die Spielwelt (z. B. die `ConZGameInstance`, die unser Modul ohnehin schon kennt) und
den rohen Befehlsstring — exakt das, was ein programmatischer RCON-Ersatz ohne verbundenen
Admin braucht.

Damit reduziert sich der Kern des neuen Moduls auf:

1. `UFunction*` für `Test_ProcessAdminCommand` einmalig per
   `UObjectGlobals::StaticFindObject<UClass*>(..., STR("/Script/SCUM.MiscStatics"))` +
   `->FindFunction(STR("Test_ProcessAdminCommand"))` auflösen (dieselbe Technik, die
   `native_telemetry`s `call_object_function`-Infrastruktur bereits produktiv nutzt).
2. Pro eingehendem RCON-Befehl: Parameter-Struct befüllen (`WorldContextObject` +
   `commandText`-`FString`), `Object->ProcessEvent(function, &params)` aufrufen.
3. Rückgabewert/Ausgabe der Funktion ermitteln und als RCON-Antwort zurückgeben (ob die
   Funktion einen Rückgabewert hat oder die Ausgabe stattdessen z. B. über Log/Broadcast
   läuft, ist noch zu verifizieren — nächster Schritt).

**Warum das die ursprüngliche AOB-Scan-/Hook-Architektur überflüssig macht:** die
Befürchtung war, dass SCUM den Dispatcher rein nativ (nicht reflektiert) implementiert
und ein In-Memory-Hook auf ein Autorisierungs-Gate nötig wäre (siehe `DeveloperMode`s
Ansatz). Die Reflection-Suche hat zwar bestätigt, dass `UAdminCommand_*`,
`AdminCommandRegistry`, `AdminCommandExecutor` und `AdminCommandsStatics` selbst **keine**
reflektierten Funktionen haben (reines natives C++) — aber `MiscStatics` bietet einen
bereits vorhandenen, offiziell reflektierten Eintrittspunkt, der diese ganze interne
Maschinerie kapselt. Kein Fallback auf AOB-Scanning/Vtable-Patching nötig, solange dieser
eine Eintrittspunkt zuverlässig funktioniert.

**Teilweise verifiziert, dann korrigiert (2026-09-04, Nacht):** Ein erster Test
(`MiscStatics:Test_ProcessAdminCommand(GameInstance, "SetGodMode false <steamId>")`)
zeigte tatsächlich eine Wirkung (GodMode ging von `true` auf `false`) — allerdings
stellte sich bei einem sauberen A/B-Test (derselbe Befehlsstring einmal über unseren
Aufrufweg, einmal über Herbies noch funktionierendes RCON, jeweils während derselbe
Spieler nachweislich online war) heraus: **Herbies Aufruf wirkt zuverlässig, unser
eigener Aufruf über `Test_ProcessAdminCommand` nicht** — läuft fehlerfrei durch
(kein Lua-Fehler, `WorldContextObject`/Funktion beide gefunden), verändert aber den
per `native_telemetry` gemessenen Spielzustand nicht. Der erste scheinbare Erfolg war
vermutlich Zufall/Fehlinterpretation (z. B. GodMode war zu dem Zeitpunkt bereits durch
einen vorherigen Herbie-Aufruf gesetzt und der spätere Reconnect des Spielers hat es
ohnehin zurückgesetzt — siehe `docs/CHANGELOG.md` für den vollen Testverlauf).

**Wahrscheinlichste Erklärung:** `Test_ProcessAdminCommand` prüft vermutlich dieselbe
Autorisierung wie ein echter Admin-Aufruf (siehe die gefundene String-Kette "Player must
be developer" / "Not authorized to execute command"), und unser synthetisches
`WorldContextObject` (die `ConZGameInstance`, kein echter Spieler-/Admin-Kontext) erfüllt
diese Prüfung nicht — der Aufruf wird intern still verworfen, ohne dass das über den
Lua-Rückgabewert sichtbar wird (die Antworttext-Ausgabe läuft laut Herbies eigenem Log
ohnehin nur über einen Chat-/Konsolen-Kanal, nicht über einen Rückgabewert oder das
Standard-Logfile — dort war entsprechend auch nichts zu finden).

**Bedeutung**: Der ursprünglich gefundene "einfache Shortcut" reicht damit *allein*
vermutlich nicht aus. Herbies eigener, aufwendigerer Ansatz (zwei native
Autorisierungs-Gate-Hooks plus eigener `AdminCommandRegistry`/`Executor`-Dispatch, siehe
oben) ist wohl kein optionaler Fallback, sondern der eigentlich notwendige Weg. Die im
Abschnitt "Modul-Struktur" oben skizzierte Architektur (Worker-Thread + Game-Thread-Drain)
bleibt gültig — nur der Baustein "wie genau der Befehl ausgeführt wird" muss wahrscheinlich
doch über die härtere Route laufen, nicht über `Test_ProcessAdminCommand` allein.

**Noch offen:**

- Ob sich die Autorisierungsprüfung mit einem *echten* Spieler-/PlayerController-Objekt
  als `WorldContextObject` (statt der `GameInstance`) umgehen lässt, oder ob tatsächlich
  ein Autorisierungs-Gate-Hook (analog zu Herbies `sig_a`/`sig_b`) nötig ist.

- Ob `Test_ProcessAdminCommand` denselben Autorisierungs-/Antwortweg nutzt wie ein echter
  Admin (inkl. der von SCUM selbst gemeldeten Fehlertexte) oder eine vereinfachte
  Testvariante ist, die z. B. Berechtigungsprüfungen anders handhabt.
- Rückgabeformat/-mechanismus der Funktion — bestätigt `nil`/kein direkter Rückgabewert
  über den Lua-Aufrufweg; wie die Textantwort (z. B. "God mode set to true.", die Herbies
  RCON zurückgibt) tatsächlich erzeugt/übertragen wird, ist noch offen (vermutlich ein
  Print-/Log-Seitenkanal, analog zu Herbies eigenem Chat-Capture-Hook, siehe
  `docs/CHANGELOG.md`).
- Ob `Test_`-Funktionen in einem Shipping-Build zuverlässig über Spiel-Updates hinweg
  erhalten bleiben (Name klingt nach Entwickler-/Testwerkzeug — könnte in einer
  zukünftigen SCUM-Version entfernt werden; deshalb bleibt Namensauflösung per
  `StaticFindObject` statt hartkodierter Adresse wichtig, plus ein Fallback-Plan für
  den Fall, dass die Funktion irgendwann verschwindet).

## Offene Reverse-Engineering-Frage (historisch, vor obigem Fund)

Wie genau SCUMs interner Admin-Befehls-Dispatcher aufgerufen wird, war zu Beginn dieser
Phase noch unbekannt. Ein erster, rein lesender String-Scan direkt gegen die
`SCUMServer.exe` (kein Deploy, kein Prozesseingriff — siehe `docs/CHANGELOG.md`,
Eintrag "Erste Reverse-Engineering-Ergebnisse") hat das Bild deutlich geschärft:

**Bestätigt per Reflection-Metadaten im Binary:**

- SCUMs Admin-Befehle sind **keine Freiform-Funktionen**, sondern 247 einzelne
  UE-Reflection-Klassen nach dem Muster `UAdminCommand_<Name>` (z. B.
  `UAdminCommand_ListPlayers`, `UAdminCommand_SetGodMode`,
  `UAdminCommand_ExecuteConsoleCommand`) — vermutlich ein Command-Pattern, bei dem
  jede Klasse ihre eigene Ausführungslogik kapselt.
- Drei Infrastruktur-Klassen sind die wahrscheinlichsten Kandidaten für den
  eigentlichen Dispatch-Mechanismus:
  - `UAdminCommandRegistry` — bildet vermutlich Befehlsnamen (Text nach `#`) auf die
    passende `UAdminCommand_*`-Klasse ab.
  - `UAdminCommandExecutor` — vermutlich das Kontext-/Berechtigungsobjekt, das beim
    Ausführen mitgegeben wird (repräsentiert den Aufrufer: Spieler, Konsole, o. ä.).
  - `UAdminCommandsStatics` — vermutlich eine Blueprint-Function-Library mit
    aufrufbaren statischen Helfern.
- Die Autorisierungs-Strings liegen exakt in der von `DeveloperMode`s README
  beschriebenen Reihenfolge im Speicher: "Command is disabled" → "...in shipping
  build" → **"Player must be developer"** → "...on cooldown..." → **"Not authorized
  to execute command"** — bestätigt mindestens zwei getrennte Prüfungen vor der
  eigentlichen Ausführung.

**Was reines String-Scanning nicht mehr liefert:** die tatsächlichen `UFunction`-
Signaturen dieser drei Klassen (Namen, Parameter, welche Funktion man per
`ProcessEvent` aufrufen müsste) — C++-Methodennamen sind in einem Shipping-Build
nicht als Strings vorhanden, nur Klassen-/Property-Namen (FName-Metadaten).

**Nächste Schritte, in Reihenfolge:**

1. Live-Reflection-Dump der drei Klassen (`UAdminCommandExecutor`,
   `UAdminCommandRegistry`, `UAdminCommandsStatics`) — entweder über UE4SS' eingebautes
   `DumpAllObjects()` (dafür existiert im privaten Schwesterprojekt bereits ein
   fertiger, bisher ungenutzter Mod `PowelsScumSdkDump`) oder eine gezielte, neue
   `StaticFindObject<UClass*>`-Abfrage. **Beides braucht einen Serverneustart auf
   einem Testserver** — noch nicht ausgeführt, wartet auf explizite Freigabe.
2. Sobald die Funktionssignaturen bekannt sind: prüfen, ob sich ein valider
   `UAdminCommandExecutor` synthetisch erzeugen oder aus einem bestehenden Kontext
   (z. B. dem Server selbst, analog zu "kein Admin-Client nötig") ableiten lässt.
3. Falls die Reflection-Ebene allein nicht reicht (z. B. weil die Autorisierungsprüfung
   nicht rein über Parameter, sondern über zusätzliche interne Zustandsprüfungen läuft):
   Array-of-Bytes-Scan auf die beiden Autorisierungs-Gates als Fallback, analog zum
   öffentlich beschriebenen Konzept aus `DeveloperMode`s README — eigenständig gegen
   den SCUM-Serverprozess selbst neu implementiert, kein Code von dort übernommen.
4. UE4SS' `ProcessConsoleExec`/`ULocalPlayerExec`-Hooks bleiben ein alternativer
   Ansatzpunkt, falls sich herausstellt, dass Konsolenbefehle über einen anderen Pfad
   laufen als der direkte `UAdminCommandRegistry`-Lookup.

## Nicht Teil dieser Phase

Reverse Engineering am Testserver, Implementierung des RCON-Listeners/Hooks, jeglicher
Test gegen einen echten SCUM-Server. Diese Architekturplanung ist bewusst der erste,
eigenständige Schritt vor dem eigentlichen Reverse Engineering.
