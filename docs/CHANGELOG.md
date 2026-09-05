# Changelog

## 2026-09-05 (Folgesession) — Autorisierungs-Gate-Funktionen disassembliert

`PeXrefScanner` um `dumpFunctionAt()` erweitert (lineare Disassemblierung ab einer
bekannten virtuellen Adresse, statt nur Xref-Suche nach Strings) und damit
`0x141A45AA0` und `0x141A4D050` direkt untersucht — die beiden Funktionen, deren
Aufrufstellen in der vorherigen Session gefunden, aber nicht selbst analysiert
wurden.

Kernfunde (Details: [`docs/research/2026-09-05-authorization-gate-analysis.md`](research/2026-09-05-authorization-gate-analysis.md#update-2026-09-05-folgesession-0x141a45aa0-und-0x141a4d050-disassembliert)):

- `0x141A4D050` ist trivial: `return Executor + 0x690;` — kein komplexer
  Auflösungsschritt, nur ein Feldzugriff.
- `0x141A45AA0` ist ein **Berechtigungsstufen-Dispatcher** (Switch über ein Byte
  auf der Kommando-Instanz, Werte 0–4 mit Cascading-Fallback zur jeweils
  nächsten Stufe). **Stufe 0 liefert unconditional `true` — ganz ohne jede
  Identitätsprüfung.** Alle anderen Stufen benötigen einen gültigen Treffer in
  einer von mehreren Registry-Strukturen (Gruppen-Set, Einzel-Executor-Set,
  Rollen-Flags), abhängig vom `Executor+0x690`-Objekt.
- Nächster Schritt: per Reflection auslesen, welche Berechtigungsstufe
  `SetGodMode` (und andere Zielkommandos) tatsächlich hat — Kommandos mit Stufe
  0 sollten über unseren synthetischen `ProcessEvent`-Aufruf ohne weitere
  Änderungen funktionieren.

Rohdaten: [`docs/research/pexref_funcdump_report_2026-09-05.txt`](research/pexref_funcdump_report_2026-09-05.txt).

## 2026-09-04 — Projekt-Setup und Architekturentscheidung

Anlass: `herbie96x/SCUM-RCON` wurde vom Entwickler End-of-Life erklärt (Repository
archiviert, Lizenzserver abgeschaltet, kein Quellcode geplant). Ausgangsrecherche und
vollständiger Abhängigkeits-Audit des abhängigen Projekts wurden zuvor bereits an anderer
Stelle dokumentiert (privates Projekt, nicht Teil dieses Repos).

Marktsichtung: keine fertige quelloffene Drop-in-Alternative gefunden. Einziger technisch
relevanter Fund war `jasonuithol/SCUM-Mods` ("DeveloperMode") — beschreibt im README das
generelle Konzept (AOB-Scan auf ein Autorisierungs-Gate, dort hooken), Lizenz verbietet
aber Codeübernahme/Reverse Engineering; nur das Konzept wird als Ausgangspunkt genutzt.

Entscheidungen:

- Eigenes, separates Repository statt Unterordner eines bestehenden privaten Projekts —
  von Anfang an quell-einsehbar.
- Zielarchitektur: eigenständiges UE4SS-C++-Modul, das denselben Source-RCON-Wireprotokoll-
  Standard implementiert wie das abgelöste Original (siehe `docs/ARCHITECTURE.md`), mit
  Worker-Thread für den TCP-Listener und Game-Thread für die eigentliche Befehlsausführung.
- Lizenz: eigener, kurzer Lizenztext statt Standard-Open-Source-Lizenz — Nutzung frei
  (auch kommerziell, z. B. für Gameserver-Hoster), Weiterverbreitung nur unverändert und
  kostenlos, keine Modifikation ohne Erlaubnis (siehe `LICENSE`).

Geänderte Bereiche:

- Projekt-Grundgerüst angelegt: `README.md`, `LICENSE`, `.gitignore`,
  `docs/ARCHITECTURE.md`, dieses Changelog.
- `native_module/`: minimaler CMake-Build (`CMakeLists.txt`, `cxx_std_23`, linkt gegen
  `UE4SS`) und ein reiner Registrierungs-Stub (`src/dllmain.cpp`) — lädt als UE4SS-Mod,
  implementiert noch keinen Hook und keinen RCON-Listener.

Ausdrücklich offen (nächster Schritt):

- Reverse Engineering von SCUMs internem Admin-Befehls-Autorisierungs-Gate.
- Implementierung des RCON-Listeners und der eigentlichen Befehlsausführung.
- Jeglicher Test gegen einen echten SCUM-Server.

## 2026-09-04 — Erste Reverse-Engineering-Ergebnisse: Admin-Befehle sind UE-Reflection-Klassen

Anlass: Start der eigentlichen Reverse-Engineering-Phase. Erster Schritt bewusst
**rein lesend und risikofrei**: ein Textstring-Scan direkt gegen die auf einem
Testserver liegende `SCUMServer.exe`, ohne die Datei zu übertragen, den laufenden
Prozess anzufassen oder irgendetwas zu deployen.

Vorgehen: `SCUMServer.exe` (123 MB) serverseitig eingelesen, als ASCII und als
UTF-16 (Unicode) dekodiert, nach bekannten Autorisierungs-Strings aus der
`DeveloperMode`-Doku gesucht (siehe `docs/ARCHITECTURE.md`), danach per Regex
alle `U?AdminCommand[A-Za-z0-9_]*`-Vorkommen als eindeutige Namen gesammelt.

Fund (deutlich mehr und deutlich konkreter als erwartet):

- **247 eindeutige `UAdminCommand_<Name>`-Klassen** gefunden — praktisch die
  komplette Liste von SCUMs ~230 nativen Admin-Befehlen, jeder als eigene
  UE-Reflection-Klasse (`ListPlayers`, `Kick`/`BanPlayer`/`UnbanPlayer`,
  `Teleport`, `SpawnItem`/`SpawnVehicle`/`SpawnAnimal`, `SetCurrencyBalance`,
  `SetGodMode`, `SetTime`/`SetWeather`, `ExecuteConsoleCommand`, u.v.m. —
  Namen decken sich exakt mit den Befehlen, die `local_bridge` heute per RCON
  nutzt).
- Drei zentrale Infrastruktur-Klassen identifiziert, sehr wahrscheinlich der
  gesuchte Dispatch-Mechanismus:
  - `UAdminCommandExecutor` — vermutlich das Kontext-Objekt, das
    repräsentiert "wer führt den Befehl aus" (echter Spieler, Konsole, o.ä.).
  - `UAdminCommandRegistry` — vermutlich die Tabelle, die Befehlsnamen
    (z. B. "AddGardenPlantPest") auf die passende `UAdminCommand_*`-Klasse
    abbildet.
  - `UAdminCommandsStatics` — vermutlich eine Blueprint-Function-Library mit
    aufrufbaren statischen Helfern (u. a. ein `ExecuteCommand`-String
    gefunden, ~7,5 Mio. Zeichen entfernt von den Klassennamen — passt zur
    Vermutung, ist aber noch nicht verifiziert).
  - Dazu ein komplettes Argument-Typsystem (`UAdminCommandArgumentDataType_
    {Bool,Location,Numeric,String,Transform,TransformOrLocation}`) und
    Tab-Vervollständigung (`UAdminCommandCompletionManager`,
    `UAdminCommandArgumentCompletion_*`).
- Die Autorisierungs-String-Kette liegt tatsächlich in genau dieser
  Reihenfolge im Speicher, wie von `DeveloperMode` beschrieben: "Command is
  disabled" → "Command is disabled in shipping build" → **"Player must be
  developer"** → "Command is on cooldown. Try again later." → **"Not
  authorized to execute command"** — bestätigt, dass es (mindestens) zwei
  getrennte Prüfungen vor der eigentlichen Ausführung gibt, wie in der
  Architekturplanung angenommen.

Technische Einordnung: reine String-Namen aus der Kompilat-Metadatentabelle
(FName-Pool) — keine C++-Symbolnamen, keine Methodennamen mit „::"
gefunden (in einem Shipping-Build erwartungsgemäß entfernt). Um die
tatsächlichen `UFunction`-Signaturen dieser drei Klassen zu bekommen (Namen,
Parameter, welche Funktion man per `ProcessEvent` aufrufen müsste), reicht
reines String-Scanning nicht mehr aus — das braucht einen echten
Live-Reflection-Dump (UE4SS' eingebautes `DumpAllObjects()`, wofür bereits
ein fertiger, ungenutzter Mod im privaten Schwesterprojekt existiert,
`PowelsScumSdkDump`) oder eine gezielte `StaticFindObject`-Abfrage aus einem
neuen, geladenen Modul heraus. Beides erfordert einen Serverneustart auf dem
Testserver — **bewusst noch nicht ausgeführt**, das ist der nächste
Entscheidungspunkt.

Nebenbei entdeckter, kleiner Infrastruktur-Bug (behoben, nur lokal, betrifft
nicht dieses Repo): das private Recherche-Tooling zum Ausführen von
PowerShell-Skripten auf dem Testserver liest die SSH-Kanalausgabe in falscher
Reihenfolge (`recv_exit_status()` vor `stdout.read()`) — klassischer
Paramiko-Deadlock bei größerer Ausgabe. Für diese Recherche wurde
stattdessen eine korrigierte Variante mit richtiger Lesereihenfolge genutzt.

Ausdrücklich offen (nächster Schritt):

- Entscheidung, ob/wann ein kurzer Testserver-Neustart für einen
  Live-Reflection-Dump akzeptabel ist (kickt aktuell verbundene Spieler,
  kurze Downtime).
- Danach: tatsächliche `UFunction`-Signaturen von `UAdminCommandExecutor`/
  `UAdminCommandRegistry`/`UAdminCommandsStatics` bestimmen und einen validen
  `UAdminCommandExecutor` synthetisch erzeugen bzw. finden.
- Weiterhin offen: RCON-Listener und native Hook-Implementierung selbst.

## 2026-09-04, Abend — Durchbruch: Dispatch-Funktion gefunden, kein Memory-Hook nötig

Anlass: Nutzerfreigabe für einen kurzen Testserver-Neustart, um den geplanten
Live-Reflection-Dump zu ziehen (0 Spieler online zum Zeitpunkt des Neustarts,
vorher per `/players.json` geprüft).

Vorgehen:

1. `PowelsScumSdkDump` (bereits vorhandener, bisher ungenutzter UE4SS-Lua-Mod im
   privaten Schwesterprojekt, nutzt UE4SS' eingebautes `DumpAllObjects()`) in
   `mods.txt` des Testservers aktiviert (`mods.txt` vorher als Zeitstempel-Backup
   gesichert).
2. Testserver über die etablierten Scheduled Tasks "IOTD SCUM Manual Stop"/
   "...Manual Start" neu gestartet (derselbe Mechanismus, den auch die
   Standard-Restart-Tools des privaten Projekts nutzen).
3. Dump lief automatisch 90 Sekunden nach Mod-Start, fertig nach 8 Sekunden
   (`UE4SS_ObjectDump.txt`, 280,54 MB).
4. Dump **nicht** komplett heruntergeladen — stattdessen serverseitig mit
   `Select-String` gezielt nach den relevanten Klassen/Funktionen gefiltert, um
   nur kleine, thematisch passende Ausschnitte zurückzubekommen.

Fund:

- `UAdminCommand_*`-Klassen (alle 247), `AdminCommandRegistry`,
  `AdminCommandExecutor` und `AdminCommandsStatics` haben **keine** einzige
  reflektierte (`UFUNCTION`) Methode — reines natives C++, wie befürchtet nicht
  per `ProcessEvent` aufrufbar.
- `AdminCommandRegistry` ist aber eine echte, aktuell laufende Singleton-Instanz
  — erreichbar über die `ObjectProperty _adminCommandRegistry` auf SCUMs
  Kernklasse `ConZGameInstance`, mit einem `_commands`-Array (`ArrayProperty`
  aus `ClassProperty`-Einträgen) als eigentlicher Befehlstabelle.
- **Durchbruch**: `UMiscStatics::Test_ProcessAdminCommand(UObject*
  WorldContextObject, FString commandText)` — eine ganz normale, öffentlich
  reflektierte `BlueprintCallable`-Funktion auf einer Statics-Klasse. Genau
  zwei Parameter, kein Spieler-Kontext nötig. Zum Vergleich auch gefunden:
  `UPlayerRpcChannel::Chat_Server_ProcessAdminCommand(FString commandText)` —
  die echte Produktions-RPC, die beim Tippen von `#Befehl` im Spielchat läuft,
  aber eine echte verbundene `PlayerRpcChannel`-Instanz braucht (der schwerere
  Weg, den ein programmatischer Ersatz gerade vermeiden will).

Bedeutung für die Architektur: **kein In-Memory-Hook, kein AOB-Scan, kein
Autorisierungs-Gate-Bypass mehr nötig** (siehe überarbeiteter Abschnitt
"Gefundener Dispatch-Mechanismus" in `docs/ARCHITECTURE.md`). Das neue Modul
muss nur noch `Test_ProcessAdminCommand` per `StaticFindObject`+`ProcessEvent`
aufrufen — exakt dieselbe, bereits bewährte Technik, die
`native_telemetry`s `call_object_function`-Infrastruktur im privaten
Schwesterprojekt heute schon nutzt.

Aufräumen: `mods.txt`-Änderung **bewusst nicht rückgängig gemacht** (kein
zweiter Neustart) — `PowelsScumSdkDump` ist idempotent (überspringt beim
nächsten Start dank Marker-Datei), entspricht damit demselben Verhalten wie
beim ursprünglichen (privaten) Deploy-Tooling für diesen Mod. Backup der
vorherigen `mods.txt` liegt auf dem Testserver unter
`mods.txt.before-openscumrcon-sdk-dump-20260904-184510`.

Nebenbefund (privates Tooling, nicht Teil dieses Repos): das bestehende
Skript zum Ausführen von PowerShell auf der Test-VM
(`tools/run_test_vm_remote_ps.py`) hat einen Paramiko-Deadlock bei größerer
Kommandoausgabe (`recv_exit_status()` vor `stdout.read()` aufgerufen) — für
diese Recherche wurde eine lokal korrigierte Variante mit richtiger
Lesereihenfolge genutzt, der Fix selbst wurde nicht ins private Repo
übernommen (nicht Teil dieses Auftrags).

Ausdrücklich offen (nächster Schritt):

- `Test_ProcessAdminCommand` tatsächlich aufrufen (bisher nur per
  Reflection-Dump *gefunden*, noch nicht *aufgerufen*) und Rückgabeverhalten
  verifizieren.
- Prüfen, ob die `Test_`-Variante denselben Autorisierungsweg wie ein echter
  Admin nimmt.
- Danach: RCON-Listener (Worker-Thread, Source-RCON-Wireprotokoll) und die
  Verdrahtung der beiden Teile zueinander implementieren.

## 2026-09-04, Nacht — Erster echter Aufruf verifiziert: GodMode per Test_ProcessAdminCommand deaktiviert

Anlass: Nutzer war mit echtem Charakter auf dem Testserver online (GodMode zuvor per
Herbies RCON auf `true` gesetzt, siehe unten) — passender Moment, um den in der
Architekturplanung gefundenen Aufrufweg erstmals tatsächlich auszuprobieren, nicht nur
zu dokumentieren.

Vorgehen:

1. Referenzwert über Herbies noch laufendes RCON gesetzt: `SetGodMode true <steamId>`
   → Antwort "God mode set to true.", per `native_telemetry` bestätigt (`isGodMode: true`).
2. Minimaler, einmaliger UE4SS-**Lua**-Diagnose-Mod geschrieben (kein C++/Build nötig,
   schneller Iterationszyklus) — eigener, temporärer Mod-Ordner `OpenScumRconProbe`,
   nach demselben Muster wie `PowelsScumSdkDump`: `ExecuteWithDelay(20000, ...)`, dann
   `StaticFindObject("/Script/SCUM.Default__MiscStatics")` +
   `FindFirstOf("ConZGameInstance")` (als `WorldContextObject`) auflösen und
   `MiscStatics:Test_ProcessAdminCommand(GameInstance, "SetGodMode false <steamId>")`
   aufrufen — via UE4SS' Lua-`__index`-Metamethode, die reflektierte `UFunction`s als
   normale Methodenaufrufe auf einem `UObject` verfügbar macht (kein `CallFunction`
   nötig).
3. `mods.txt` um `OpenScumRconProbe : 1` ergänzt (Backup vorher), Testserver über
   dieselben Scheduled Tasks wie beim vorherigen Neustart neu gestartet.
4. Nach Neustart: Aufruf lief **fehlerfrei durch** — beide Objekte gefunden, Funktion
   aufgerufen, Rückgabewert `nil` (kein direkter Return-String über den Lua-Weg).
5. **Endgültige Bestätigung**, nachdem der Nutzer sich (nach einem zwischenzeitlichen,
   unabhängigen Client-seitigen Unreal-Crash auf seinem eigenen Notebook — nicht
   server- oder testbezogen) wieder verbunden hatte: `native_telemetry` meldet
   `isGodMode: false` — **der Aufruf hat tatsächlich gewirkt**, unabhängig von Herbies
   Mod und vom Probe selbst gemessen.

Nebenbefund während der Diagnose: ein dritter Testserver-Neustart (separat von den
beiden selbst ausgelösten) fiel zeitlich mit dem Nutzer-Crash zusammen — durch Prüfung
des rotierten `SCUM.log` als sauberer, geplanter Shutdown identifiziert (ordentliches
Modul-für-Modul-Herunterfahren, kein Exception-/Crash-Log, nicht über die
"Manual Stop/Start"-Tasks ausgelöst wie unsere eigenen Neustarts) — passt zum
bekannten festen Restart-Rhythmus des Servers, komplett unabhängig von unserem Test.

Aufräumen: `OpenScumRconProbe` wieder aus `mods.txt` entfernt (Backup vorher), damit es
nicht bei jedem künftigen Neustart erneut denselben Befehl feuert. Kein zusätzlicher
Neustart dafür ausgelöst — Änderung greift erst beim nächsten ohnehin fälligen Restart.

Bedeutung: **Der in der Architekturplanung gefundene Mechanismus ist nicht mehr nur eine
Hypothese aus einem Reflection-Dump, sondern nachweislich funktionsfähig**, end-to-end
gegen einen echten Server mit echtem Spieler getestet. Damit ist der riskanteste Teil
des Projekts (funktioniert der gefundene Hook überhaupt?) geklärt — übrig bleibt
"nur" noch die Verpackung in ein natives C++-Modul mit echtem RCON-Listener.

Ausdrücklich offen (nächster Schritt):

- Wie genau die Textantwort erzeugt wird (Rückgabewert war `nil`) — vermutlich ein
  Print-/Log-Seitenkanal, muss für eine echte RCON-Antwort abgefangen werden.
- Verhalten bei ungültigen/verweigerten Befehlen (Fehlertexte, Berechtigungsprüfung)
  noch nicht getestet.
- Portierung von der Lua-Diagnose auf die eigentliche native C++-Implementierung
  (`ProcessEvent` statt Lua-Metamethode) sowie der RCON-Listener selbst.

## 2026-09-04, Abend (Teil 2) — Herbies eigenes Log als zusätzliche, legitime Bestätigungsquelle

Anlass: Herbies `scum_rcon`-Mod läuft auf demselben Testserver noch aktiv (Lizenzserver
laut letztem Stand noch nicht final abgeschaltet). Idee: statt seinen Code zu untersuchen
(lizenzrechtlich nicht erlaubt), einfach sein **eigenes, freiwillig geschriebenes
UE4SS-Log** lesen (das jeder Betreiber seines Mods ohnehin sieht) und sein RCON ganz
normal als Endnutzer benutzen — beides vollständig legitime, nicht-invasive Beobachtung,
kein Reverse Engineering seines Binaries.

Fund aus dem UE4SS-Log (Mod-Start, `[SCUM-RCON] init - v0.4.6`):

- Fünf Pattern-Scan-Signaturen (`sig_a`/`sig_b`/`sig_c`/`sig_lv`/`sig_x`), je mit eigenem
  Zweck: `sig_a`+`sig_b` = "EngineHooks" (2 Hooks — passt zu den zwei
  Autorisierungs-Gates, die auch `DeveloperMode` beschreibt); `sig_c` = "chat-line detour"
  (fängt Text-Ausgaben von Befehlen ab); `sig_lv` = "vehlist RPC-send detour" (Grund für
  die Multi-Paket-Sonderbehandlung von `ListSpawnedVehicles`, die unsere eigene
  `SourceRcon`-Klasse ebenfalls kennt); `sig_x` = "command executor ready".
- **Baut selbst eine "Verb-Map"** durch Scannen aller geladenen `UClass`-Objekte nach
  `AdminCommand`-Subklassen — exakt dieselbe Technik wie unser eigener Fund. Erster
  Versuch (kurz nach Start, Welt noch nicht voll geladen): 0 von 2908 Objekten. Automatischer
  Retry beim nächsten Befehl erfolgreich: **"verb map built - 233 command(s) discovered
  (scanned 8059 class objects, 233 AdminCommand subclasses)"** — sehr nah an unseren
  eigenen 247 (Differenz sind Hilfs-/Completion-Klassen wie
  `AdminCommandArgumentCompletion_*`, keine echten Befehle).
- **Game-Thread-Drain über einen `EngineTick`-Pre-Hook** ("added prehook ... RCON command
  game-thread drain") — bestätigt unsere eigene geplante Architektur (Worker-Thread nimmt
  Netzwerk entgegen, Game-Thread verarbeitet den Befehl) als richtigen, bereits bewährten
  Ansatz.
- Live-Test bestätigt zwei unterschiedliche Ausführungspfade: einfache/häufige Befehle wie
  `ListPlayers` laufen ohne sichtbare "dispatch"-Zeile (vermutlich fest verdrahteter
  Sonderfall), während generische Befehle über den Verb-Map-Pfad laufen und dabei eine
  eigene Log-Zeile erzeugen: `dispatch: 'listspawnedarmednpcs' executed (1 line(s)
  captured)` — der Teil "line(s) captured" bestätigt, dass die Ausgabe über den
  Chat-Detour-Hook (`sig_c`) **als Text abgefangen** wird, nicht über einen sauberen
  Rückgabewert der aufgerufenen Funktion.
- Läuft auf `10.77.0.2:28015` (interne WireGuard-Tunnel-Adresse der Test-VM).

Einordnung für die eigene Architektur: Herbies Mod nutzt offenbar den **härteren, nativen**
Weg über die tatsächliche `AdminCommandRegistry`/`AdminCommandExecutor`-Maschinerie (nicht
den von uns gefundenen `MiscStatics::Test_ProcessAdminCommand`-Shortcut) — vermutlich um
exakt denselben Ausführungs-/Berechtigungsweg wie ein echter Admin zu bekommen. Das ist ein
nützlicher Rückfallplan, falls sich `Test_ProcessAdminCommand` als unzuverlässig oder in
Zukunft entfernt herausstellt: die generische Verb-Map-Technik ist unabhängig bestätigt
funktionsfähig. Außerdem wichtiger Hinweis für die eigene Ausgabe-Behandlung: falls
`Test_ProcessAdminCommand` seine Ausgabe ebenfalls nur druckt statt zurückzugeben, braucht
auch unser Modul einen Weg, diese Text-Ausgabe abzufangen (z. B. einen Log-/Chat-Hook,
analog zu Herbies `sig_c`) statt sich auf einen Rückgabewert zu verlassen.

Nicht getan (bewusst außerhalb des Erlaubten): keine Untersuchung/Disassemblierung von
`scum_rcon\dlls\main.dll` selbst — alle Erkenntnisse stammen ausschließlich aus dem von
der laufenden, lizenzkonform genutzten Software selbst erzeugten Logtext.

## 2026-09-04, Nacht (Teil 2) — Korrektur: Test_ProcessAdminCommand wirkt NICHT zuverlässig, sauberer A/B-Test durchgeführt

Anlass: Nutzer wollte GodMode explizit über "unser neues RCON-Tool" aktivieren. Dabei
fiel auf: ein eigenständiges, wiederverwendbares Tool existierte noch gar nicht — nur
der einmalige Lua-Testaufruf von zuvor. Statt das wieder als Einmal-Skript zu bauen,
wurde ein kleiner, wiederverwendbarer Lua-Diagnose-Mod gebaut (derselbe Mod-Ordner
`OpenScumRconProbe`, neu geschrieben): pollt alle 1s eine Befehlsdatei
(`C:\PowelsLocalBridge\openscumrcon_pending_command.txt`) und führt jeden hineingeschriebenen
Befehlsstring per `Test_ProcessAdminCommand` aus — kein Neustart mehr pro Testbefehl nötig.

Stolperstein dabei: die erste Version nutzte `LoopInGameThreadWithDelay` (laut UE4SS-Doku
der empfohlene Nachfolger von `ExecuteWithDelay`/`LoopAsync`) — dieser Aufruf lief aber
ins Leere, ohne Fehlermeldung im Log (vermutlich in dieser UE4SS-Version nicht verfügbar
oder anders benannt). Auf das bereits zweimal bewährte, rekursiv sich selbst
neu-planende `ExecuteWithDelay`-Muster zurückgewechselt — danach lief das Polling
zuverlässig.

**Der eigentliche Befund:** Bei einem sauberen A/B-Vergleich — derselbe Befehl
(`SetGodMode true <steamId>`) einmal über unseren `Test_ProcessAdminCommand`-Aufruf,
einmal direkt danach über Herbies weiterhin funktionierendes RCON, jeweils während
der Nutzer nachweislich mit echtem Charakter online war — zeigte sich:

- Unser Aufruf: lief fehlerfrei durch (Objekte gefunden, Funktion aufgerufen,
  Rückgabewert `nil`), **aber keine messbare Wirkung** (per `native_telemetry`
  bestätigt: `isGodMode` blieb `false`).
- Herbies identischer Befehl direkt danach: **wirkte sofort** (`isGodMode: true`,
  vom Nutzer auch im Spiel selbst bestätigt).

Der zuvor als "verifiziert" gemeldete erste Erfolg (Eintrag "2026-09-04, Nacht") wird
hiermit **korrigiert** — vermutlich Fehlinterpretation einer zufälligen zeitlichen
Abfolge (GodMode war zu dem Zeitpunkt bereits durch einen Herbie-Aufruf gesetzt; ein
zwischenzeitlicher Spieler-Reconnect setzt GodMode ohnehin automatisch zurück, wie der
Nutzer selbst bestätigte — "Rejoin ist GodMode immer weg").

Naheliegendste Erklärung: `Test_ProcessAdminCommand` prüft vermutlich dieselbe
Autorisierung wie ein echter Admin-Aufruf. Unser `WorldContextObject`
(die `ConZGameInstance` — kein echter Spieler-/Admin-Kontext) erfüllt diese Prüfung
nicht, der Aufruf wird intern still verworfen. Passt zur bereits gefundenen
Autorisierungs-String-Kette ("Player must be developer" / "Not authorized to execute
command") und dazu, dass Herbie selbst den aufwendigeren Weg über echte
Autorisierungs-Gate-Hooks geht statt eines einfachen Funktionsaufrufs.

Nebenbefund (kein Bug, unabhängig bestätigt vom Nutzer): ein zwischenzeitlicher
Verdacht auf einen weiteren Serverabsturz stellte sich als (a) ein Client-seitiger
Unreal-Crash auf dem Notebook des Nutzers heraus (unabhängig vom Server) und (b) als
verzögert bemerkter, von uns selbst ausgelöster Neustart — der Testserver-Prozess lief
beide Male nachweislich durchgehend weiter (per Prozess-Startzeit-Vergleich bestätigt).

Aufräumen: `OpenScumRconProbe` erneut aus `mods.txt` entfernt (kein weiterer Neustart
dafür ausgelöst).

Bedeutung für die Architektur: siehe überarbeiteter Abschnitt "Gefundener
Dispatch-Mechanismus" in `docs/ARCHITECTURE.md` — der einfache `Test_ProcessAdminCommand`-
Shortcut reicht vermutlich allein nicht aus. Der härtere, native Weg über echte
Autorisierungs-Gate-Hooks (wie bei Herbie beobachtet) ist wohl doch notwendig.

Ausdrücklich offen (nächster Schritt):

- Testen, ob ein *echtes* Spieler-/PlayerController-Objekt als `WorldContextObject`
  (statt der `GameInstance`) die Autorisierungsprüfung erfüllt — bevor auf
  Memory-Hooking zurückgegriffen wird.
- Falls das nicht reicht: Array-of-Bytes-Scan auf die beiden Autorisierungs-Gates
  (eigenständig implementiert, kein Code von Herbie/DeveloperMode übernommen).

## 2026-09-05 — Reflection-Grenze erreicht: kein einziger nachgelagerter Funktionsaufruf beobachtbar

Anlass: Live-Deploy der ersten echten nativen C++-Implementierung (RCON-Server,
Command-Queue, `AdminDispatch` mit `ConZPlayerController`-Kontext statt
`GameInstance`) auf den Testserver, end-to-end getestet. Zusätzlich: Nutzer-Hinweis,
dass `AdminUsers.ini`-Einträge wie `76561198023499707[godmode]` ein reguläres,
selbst konfigurierbares SCUM-Feature sind — der Tag legt fest, dass dieser Admin
GodMode nutzen (aktivieren/deaktivieren) darf; kein Hinweis auf einen fehlerhaften
Dateieintrag, wie zuvor fälschlich vermutet (siehe Korrektur unten).

**Deploy-Verifikation**: eigener Winsock2-RCON-Server (Port 28016, parallel zu
Herbies 28015) läuft stabil, kompletter Roundtrip funktioniert
(TCP-Connect → Auth → Command → Worker-Thread-Queue →
`EngineTick`-Game-Thread-Drain → `ProcessEvent`-Aufruf → Antwort-Paket zurück an
den Client). Verifiziert per eigenem PowerShell-RCON-Testclient direkt auf dem
Server (localhost, um Netzwerk-/Firewall-Fragen auszuschließen — ein initialer
Windows-Firewall-Prompt für den neu lauschenden Port musste einmalig vom Nutzer
bestätigt werden).

**Versehentlicher Fehlgriff (sofort korrigiert)**: Da der erste `SetGodMode`-Test
über unseren eigenen `PlayerController`-Kontext wieder keine Wirkung zeigte, wurde
`AdminUsers.ini` fälschlich als "falsch formatiert" vermutet und probeweise auf
eine SteamID ohne `[godmode]`-Tag geändert. Nutzer korrigierte: das Tag ist
korrektes, beabsichtigtes SCUM-Verhalten (Anti-Cheat-Whitelist — ohne den Tag würde
ein Spieler mit aktivem GodMode beim ersten Erkennen gekickt und gebannt). Eintrag
sofort auf den Originalzustand zurückgesetzt.

**Der eigentliche Durchbruch in der Diagnose**: ein temporärer, global registrierter
`ProcessEvent`-Pre-Hook (aktiv nur während unseres eigenen `dispatch_command()`-
Aufrufs, über ein Atomic-Flag gesteuert) sollte zeigen, welche internen
Funktionsaufrufe `Test_ProcessAdminCommand` auslöst. Ergebnis: **53 aufgezeichnete
Zeilen, davon 52 komplett unabhängiges Hintergrundrauschen** (der bereits
produktiv laufende, separate `native_telemetry`-Mod fragt im selben Zeitfenster
routinemäßig Zombie-`GetHealth()`/`IsAlive()` ab — zufällige zeitliche
Überlappung, keine Kausalität). Die **einzige tatsächlich durch unseren Aufruf
verursachte Zeile ist der Aufruf selbst** (`Test_ProcessAdminCommand`) — **keine
einzige nachgelagerte, per Reflection sichtbare Funktion wurde ausgelöst.**

**Schlussfolgerung**: `Test_ProcessAdminCommand` führt seine gesamte interne Logik
(Befehl parsen, `AdminCommandRegistry` nachschlagen, `UAdminCommand_SetGodMode`
ausführen, Autorisierung prüfen) in reinem, nicht-reflektiertem C++ aus, das nie
wieder durch `ProcessEvent` geht. Das erklärt rückwirkend auch, warum der frühere
`CallFunctionByNameWithArguments`-Hook-Versuch nichts fing — es gibt auf dieser
Ebene schlicht nichts zu beobachten. Arbeitshypothese für das Ausbleiben der
Wirkung: die Autorisierungsprüfung liest vermutlich nicht live aus
`AdminUsers.ini`, sondern prüft ein In-Memory-Flag auf dem `PlayerController`
(z. B. `bIsAdmin`), das nur beim echten Admin-Login/-Spawn gesetzt wird — unser
synthetischer `ProcessEvent`-Aufruf umgeht diesen Session-Zustand nicht, egal wie
die Datei aussieht.

**Bedeutung für die Architektur**: Reflection-basierte Beobachtung (UE4SS-Hooks auf
Standard-Engine-Funktionen) hat ihre Grenze erreicht. Der nächste Schritt ist
zwingend echtes natives Hooking — Array-of-Bytes-Scan auf die tatsächliche
Autorisierungs-/Dispatch-Funktion im Serverbinary selbst, analog zum öffentlich
beschriebenen (aber nicht kopierten) Konzept aus Herbies und DeveloperModes eigener
Dokumentation. Das ist ein eigenständiges, größeres Arbeitspaket, keine
Erweiterung der bisherigen reflection-basierten Implementierung.

Nebenbei erledigt (auf Nutzerwunsch): `docs/REFERENCES.md` neu angelegt (Valve-RCON-
Spec, Herbie, DeveloperMode, ggCON/GGHost gesammelt) — ggCON nutzt ebenfalls
Standard-Source-RCON, verrät aber wie Herbie keine internen Dispatch-Details.

Ausdrücklich offen (nächster Schritt):

- Natives AOB-Scanning + Hooking der tatsächlichen Autorisierungs-/Dispatch-
  Funktion(en) im SCUM-Serverbinary — eigener Arbeitsblock, nicht mehr
  reflection-basiert.
- Alternative, noch nicht ausprobierte Idee: prüfen, ob es einen Weg gibt, das
  `bIsAdmin`-artige In-Memory-Flag (falls vorhanden) auf dem `PlayerController`
  direkt zu setzen/zu lesen, um die Hypothese zu verifizieren, bevor der
  aufwendigere AOB-Scan-Weg begonnen wird.

## 2026-09-05 (Abend) — Durchbruch: eigenes Disassembly-Tool findet die echte Autorisierungskette

Anlass: Nutzer-Vorschlag, ein eigenes Werkzeug zu bauen, das zeigt, wie SCUM (bzw.
wie Herbie es nutzt) die Autorisierung tatsächlich prüft — nachdem reine
Reflection-Beobachtung (siehe voriger Eintrag) an ihre Grenze gestoßen war.

**Neues Werkzeug**: [`tools/pe_xref_scanner`](../tools/pe_xref_scanner) (`PeXrefScanner`) —
ein eigenständiges, von `native_module` komplett unabhängiges Kommandozeilen-Tool
(nur gegen `Zydis`, den in UE4SS' eigenem Build bereits vorhandenen x86/x64-
Disassembler, gelinkt — kein Unreal/UE4SS-Code nötig). Arbeitsweise: **rein
statische Analyse einer PE-Datei auf der Platte, kein Eingriff in einen laufenden
Prozess** — parst die PE-Sektionstabelle selbst (Handrolled-Parser, keine
Fremdbibliothek), findet bekannte String-Literale, rechnet ihre Datei-Offsets in
virtuelle Adressen um, disassembliert die komplette Code-Sektion und meldet jede
Instruktion, die per RIP-relativer Adressierung oder relativem Call/Jump auf eine
dieser Adressen zeigt (Xref-Suche).

**Deploy & Lauf**: Tool (477 KB) wie gewohnt per Base64 durch die schon bewährte
PowerShell-Direct-Pipeline auf den Testserver gebracht, dort gegen die *laufende*
`SCUMServer.exe`-Datei ausgeführt (Windows erlaubt gemeinsames Lesen einer
laufenden .exe) — **kein Neustart, keinerlei Risiko für den Live-Server**, da nur
Dateibytes gelesen wurden. Ergebnis: 22.132.011 Instruktionen aus 85 MB Code
disassembliert, **20 Xrefs** zu den vier bekannten Status-Strings gefunden.

**Kernfund**: Alle 20 Treffer liegen in einem einzigen ~860-Byte-Codeblock
(`0x1418c7b60`–`0x1418c7fd0`) — eine zentrale Funktion (vermutlich
`UAdminCommand::Execute()`/`::CanExecute()`), die alle vier Meldungen gemeinsam
behandelt. Der Kontrollfluss ließ sich vollständig nachvollziehen:

1. Erste, "stille" Prüfung über einen virtuellen Funktionsaufruf
   (`vtable+0x278` auf der `UAdminCommand`-Instanz) — bei `false` bricht die
   Funktion sofort ab, ganz ohne Meldung.
2. Ein Cooldown-Zeitstempel-Vergleich (führt zu "Command is on cooldown...").
3. **Der entscheidende Fund**: ein Aufruf von `0x141A45AA0(this=RBP, arg=RAX
   [aus `0x141A4D050`], flagByte=[this_cmd+0x52], &this_cmd[0x28])` — bei
   `false` wird `"Not authorized to execute command"` gesetzt. **Das ist mit
   hoher Wahrscheinlichkeit die eigentliche Berechtigungsprüfung**, die
   entscheidet, ob ein Aufruf durchgeht.
4. Eine zweite, vorgelagerte Funktion (`0x1418c7e10`) prüft zwei benachbarte
   Byte-Flags auf der Command-Instanz (`+0x50`/`+0x51`, vermutlich
   `bEnabled`/`bDisabledInShippingBuild`) für die "Command is disabled..."-
   Meldungen.

Volle Analyse mit Adressen, Register-Bedeutungen und Tabelle der vermuteten
Feld-Offsets: [`docs/research/2026-09-05-authorization-gate-analysis.md`](research/2026-09-05-authorization-gate-analysis.md).
Rohes Disassembly aller 20 Fundstellen: [`docs/research/pexref_report_2026-09-05.txt`](research/pexref_report_2026-09-05.txt).

**Rechtliche Einordnung**: Dies ist eine vollständig eigenständige statische
Analyse der SCUM-Server-Binärdatei, die wir als Serverbetreiber legitim
ausführen dürfen — kein Code oder Binary von `herbie96x/SCUM-RCON` oder
`jasonuithol/SCUM-Mods` wurde untersucht, dekompiliert oder übernommen. Wir
haben den Autorisierungsmechanismus komplett eigenständig gefunden.

Ausdrücklich offen (nächster Schritt):

- `0x141A45AA0` und `0x141A4D050` selbst disassemblieren (bisher nur die
  Aufrufstellen gefunden, nicht die Zielfunktionen).
- Klären, was das zweite Argument der Haupt-Funktion (`RDX`/`R15`) tatsächlich
  ist — Arbeitshypothese: der "Executor" (vgl. `UAdminCommandExecutor`).
- Danach: entweder einen validen Executor synthetisch erzeugen, der die Prüfung
  besteht, oder `0x141A45AA0` selbst hooken.

## 2026-09-04, Nacht (Teil 3) — Versuch, Herbies Aufrufweg per Engine-Hook mitzuschneiden (ergebnislos, aber lehrreich)

Anlass: Nutzer-Idee — statt nur zu vermuten, WIE Herbies Dispatch funktioniert, den
eigenen Serverprozess während eines echten, per Herbie ausgelösten `SetGodMode`-Aufrufs
beobachten. Rein passives Mitschneiden von Engine-Events im eigenen Prozess (UE4SS'
öffentliche Hook-API), kein Anfassen von Herbies Binary.

Vorgehen: neuer, separater Diagnose-Mod `OpenScumRconCapture` registriert
`RegisterCallFunctionByNameWithArgumentsPreHook` (UE4SS-Lua-API, hookt
`UObject::CallFunctionByNameWithArguments` — der naheliegendste Verdacht für den
Dispatch-Mechanismus hinter `#Befehl`-artigen Aufrufen) und protokolliert jeden Aufruf,
dessen Befehlsstring `"godmode"` enthält (Context-Objekt, String, Executor-Objekt).
Testserver neu gestartet, Hook erfolgreich registriert (`hook_registered_ok`), danach
`SetGodMode true` per Herbies RCON ausgelöst, während der Nutzer nachweislich mit
echtem Charakter online war (`isGodMode` vorher `false`, danach `true` — Wirkung wie
gewohnt bestätigt).

**Ergebnis: keine einzige Zeile aufgezeichnet.** Zwei mögliche Erklärungen, nicht
unterschieden:

1. Der Hook feuerte nie, weil Herbies Dispatch für `AdminCommand`-Befehle tatsächlich
   nicht über `CallFunctionByNameWithArguments` läuft (passt zu Herbies eigenem Log:
   seine `sig_a`/`sig_b`-Hooks sind native In-Memory-Patches auf eigene interne
   Funktionen, nicht auf einen der UE4SS-Standard-Hookpunkte).
2. Der Hook feuerte, aber der Textvergleich griff nie, weil `Str` in UE4SS-Lua
   vermutlich kein einfacher Lua-String ist, sondern ein `FString`-Objekt — `tostring()`
   darauf liefert wahrscheinlich keinen brauchbaren Text, wodurch das
   `string.find(..., "godmode")`-Filter nie hätte matchen können, egal wie oft die
   Funktion aufgerufen wurde.

Nicht weiter verfolgt (bewusste Priorisierung): eine sichere Unterscheidung der beiden
Fälle bräuchte entweder ungefiltertes Loggen aller Aufrufe (riskant wegen Log-Flut/
Performance auf einem Live-Server) oder die korrekte `FString`-Konvertierung in Lua zu
klären. Beides bringt uns nicht näher an eine funktionierende Lösung heran — die
eigentliche Erkenntnis bleibt bestehen: der Dispatch-Mechanismus ist mit den
Lua-verfügbaren UE4SS-Hookpunkten nicht ohne Weiteres beobachtbar, was zusätzlich dafür
spricht, dass echtes natives Hooking (C++, nicht Lua) für den nächsten Schritt nötig ist.

Aufräumen: `OpenScumRconCapture` wieder aus `mods.txt` entfernt (kein weiterer Neustart
dafür ausgelöst). Serverzustand nach allen Tests dieser Session final geprüft: stabil,
normale Speicherwerte (Prozess lief zwischenzeitlich nach einem Neustart länger als
gewohnt hoch — Herbies eigene "server still starting (initialising)"-Meldung bestätigte,
dass das ein normaler, wenn auch langsamerer Boot-Vorgang war, kein Hänger).

Stand am Ende dieser (sehr langen) Recherche-Session: Der Dispatch-Mechanismus ist als
Konzept identifiziert (`AdminCommandRegistry`/`Executor`, 233-247 Befehlsklassen,
Autorisierungs-Gates), ein einfacher Reflection-Shortcut wurde geprüft und als nicht
ausreichend verworfen, ein Versuch, Herbies echten Weg passiv zu beobachten, blieb
ergebnislos. Der nächste sinnvolle Schritt ist vermutlich ein Wechsel von
Lua-Experimenten zu echtem nativen C++ (Debugger/Disassembler gegen den eigenen
Serverprozess, oder gezielte AOB-Scans wie ursprünglich geplant) — das sprengt den
Rahmen weiterer Lua-Restarts und sollte in einer eigenen, dafür vorbereiteten Sitzung
angegangen werden.

## 2026-09-05 (nachts, unbeaufsichtigt) — Erste echte native C++-Implementierung, baut sauber

Anlass: Nutzer bat darum, während er schläft schon mal zu programmieren, Review am
nächsten Tag. Bewusste Einschränkung für diese Session: **kein Deploy, kein
Neustart des Testservers** — nur Code schreiben und lokal kompilieren, da ein
Restart-Test ohne den Nutzer als Ansprechpartner nicht verantwortbar ist, falls
etwas schiefgeht.

Umgesetzt (`native_module/src/`, alles neu außer `dllmain.cpp`, das umgeschrieben
wurde):

- **`rcon_protocol.hpp/.cpp`** — Server-seitige Implementierung des Source-RCON-
  Wireprotokolls (Paket-Framing, Multi-Paket-Split bei langen Antworten), exaktes
  Gegenstück zur bereits bewährten Client-Implementierung in
  `local_bridge/powels_local_bridge.py` (`SourceRcon`-Klasse) — dieselbe
  Paketstruktur, dieselbe 4096-Byte-Chunking-Konvention.
- **`command_queue.hpp`** — Thread-sichere Übergabe Worker-Thread → Game-Thread
  per `std::promise`/`std::future` (Worker-Thread blockiert auf die Antwort, statt
  zu pollen) — die "echte" Umsetzung der Architektur, die die Lua-Diagnose-Mods der
  letzten Session nur simuliert haben (dateibasiertes Polling alle 1s).
- **`rcon_server.hpp/.cpp`** — Winsock2-TCP-Server auf einem eigenen Worker-Thread
  (v1 bewusst eine Verbindung gleichzeitig, siehe Kommentar im Header für die
  Begründung und den geplanten Ausbau). Kein UE4SS-/Unreal-Include in dieser Datei
  — sauber getrennt vom Rest, nur über `CommandQueue` verbunden.
- **`admin_dispatch.hpp/.cpp`** — löst `UMiscStatics::Test_ProcessAdminCommand`
  und die nötigen `UClass`-Zeiger per `StaticFindObject` auf (exakt dieselbe
  Technik, die `native_telemetry`s `on_unreal_init()` bereits produktiv nutzt),
  ruft die Funktion per `ProcessEvent` auf.
- **`dllmain.cpp`** — verdrahtet alles: `config.ini`-Reader (dieselbe Konvention
  wie Herbies eigener Mod), `EngineTick`-Pre-Hook (bestätigt derselbe Mechanismus,
  den auch Herbies Log als "game-thread drain" zeigt) leert die Queue und ruft
  `AdminDispatch::dispatch_command()` auf dem Game-Thread auf.

**Wichtige, bewusste Korrektur gegenüber der letzten Session**: `admin_dispatch.cpp`
nutzt NICHT mehr die `GameInstance` als `WorldContextObject` (das war der Aufruf, der
im A/B-Test nachweislich nichts bewirkt hat) — stattdessen sucht
`find_admin_context_object()` per `ForEachUObject` zuerst nach einer echten, aktuell
verbundenen `ConZPlayerController`-Instanz und nutzt die. Das ist die erste konkrete
Umsetzung des offenen Punkts aus `docs/ARCHITECTURE.md` ("Ob sich die
Autorisierungsprüfung mit einem echten Spieler-Objekt umgehen lässt") — **noch nicht
live getestet**, da das einen Serverneustart gebraucht hätte.

Build-Verifikation (lokal, kein Server involviert): `cmake --build` gegen den
vorhandenen UE4SS-Entwicklungsbaum durchlaufen lassen. Ein Zwischenfehler
(`C1060: Kein verfügbarer Speicher mehr im Heap`) beim ersten Versuch war ein reiner
MSBuild-Parallelitäts-Effekt (UE4SS' eigene ~vollständige Neukompilierung mit hoher
Parallelität), kein Bug — mit `/m:1` (seriell) behoben, UE4SS selbst kompilierte
danach vollständig durch. Zwei echte, kleine Fehler im eigenen Code gefunden und
behoben: fehlende `RC::`-Namespace-Qualifizierung bei `LoopAction` und `StringType`
(beide leben in `RC`, nicht `RC::Unreal`, `using namespace RC::Unreal` allein reicht
nicht). Danach: **sauberer Build, `OpenScumRconNative.dll` erfolgreich erzeugt**
(162 KB). Build-Ordner wieder gelöscht (nicht Teil des Commits, wie gehabt).

Ausdrücklich offen (nächster Schritt, braucht den Nutzer):

- Deploy auf den Testserver und echter Verbindungstest über einen Source-RCON-Client
  (Port `28016`, siehe `config.example.ini` — bewusst nicht Herbies `28015`, damit
  beide parallel laufen können).
- Verifizieren, ob der PlayerController-basierte Aufruf tatsächlich wirkt (die
  eigentliche offene Frage aus der letzten Session).
- Antwortformat/-mechanismus weiterhin ungelöst — `dispatch_command()` gibt aktuell
  nur einen technischen Status zurück ("ok: dispatched via ..."), nicht SCUMs
  echten Antworttext.
