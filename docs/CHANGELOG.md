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
