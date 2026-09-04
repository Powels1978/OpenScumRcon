# Changelog

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
