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
2. **Game-Thread** leert die Queue periodisch, löst den Befehl über das noch zu findende
   SCUM-interne Autorisierungs-Gate aus, legt die Antwort in eine Per-Request-Queue, die
   der Worker-Thread über den Socket zurücksendet.

Diese Trennung vermeidet das nachweislich riskante Muster "eine Engine-interne Funktion
synchron von einem fremden Thread bzw. außerhalb ihres normalen Aufrufpfads aufrufen" —
ein verwandtes, bereits produktiv gebautes Modul in diesem Projektumfeld (nicht Teil
dieses Repos) hat mit genau diesem Muster einmal einen echten Serverabsturz ausgelöst,
als es versuchte, eine abgefangene Server-RPC-Funktion synthetisch außerhalb ihres
normalen Call-Stacks aufzurufen.

## Offene Reverse-Engineering-Frage

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
