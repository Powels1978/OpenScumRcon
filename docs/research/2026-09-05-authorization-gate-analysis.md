# Analyse der Autorisierungs-Gate-Funktion(en) — 2026-09-05

Ergebnis der ersten Session mit `tools/pe_xref_scanner` (`PeXrefScanner`) gegen die
laufende `SCUMServer.exe` auf dem Testserver. Rein statische Analyse (Datei auf der
Platte gelesen, kein Eingriff in den laufenden Prozess). Rohdaten:
[`pexref_report_2026-09-05.txt`](pexref_report_2026-09-05.txt) (5485 Zeilen, alle 20
gefundenen Xrefs mit vollem Disassembly-Kontext).

## Vorgehen

`PeXrefScanner` sucht die vier bereits bekannten Autorisierungs-/Status-Strings
("Player must be developer", "Not authorized to execute command", "Command is on
cooldown. Try again later.", "Command is disabled in shipping build") im Binary,
wandelt ihre Datei-Offsets in virtuelle Adressen um (per PE-Sektionstabelle) und
disassembliert die komplette `.text`-Sektion (85 MB, ~22,1 Millionen Instruktionen)
mit Zydis, um jede Instruktion zu finden, die per RIP-relativer Adressierung oder
relativem Call/Jump auf eine dieser Adressen verweist.

## Kernfund: alle 20 Treffer liegen in einem einzigen ~860-Byte-Codeblock

Alle vier Strings werden ausschließlich aus dem Adressbereich `0x1418c7b60` –
`0x1418c7fd0` referenziert (je 5×) — das ist mit hoher Sicherheit **eine einzige
zentrale Funktion** (oder ein Paar eng verwandter Funktionen), die alle vier
Status-Meldungen gemeinsam behandelt. Aus dem MSVC-Funktions-Prolog (`push rbx;
push rbp; push rsi; push rdi; push r15; sub rsp, 0x40` + Stack-Cookie) und dem
Aufbau lässt sich der Ablauf gut rekonstruieren.

## Rekonstruierter Kontrollfluss (zwei zusammenhängende Funktionen gefunden)

### Funktion A, Start `0x1418c7b60` — vermutlich `UAdminCommand::Execute()` oder `::CanExecute()`

Signatur (x64-Calling-Convention): `func(RCX=this /* UAdminCommand-Instanz? */,
RDX=arg2 /* "Executor"? */, R8=arg3 /* Ausgabe-FString? */)`

1. `RDX` (→ `R15`) auf `null` geprüft — bei `null` sofort zum gemeinsamen
   Rückgabepunkt `0x1418c7cf7` (liefert `false`, keine Meldung gesetzt).
2. `[RDX]`-Vtable-Aufruf `[+0x160]` → Ergebnis `RAX`; `RBP = [RAX+0x118]`,
   auf `null` geprüft.
3. Aufruf `0x1426F00C0(...)`, danach ein **Index-/Bounds-Check** gegen
   `[RBP+0x10]` (Array-Länge bei `+0x38`, Datenzeiger bei `+0x30`) —
   liest wie eine **Registrierungs-/Mitgliedschaftsprüfung** ("ist `RBP` in
   diesem Array enthalten").
4. **Wichtigster Gate-Aufruf**: `[RSI-Vtable][+0x278](this=RSI, RDX=R15,
   R8=RDI)` → `bool` in `AL`. **Bei `false`: sofortiger Abbruch ohne jede
   Meldung** (gemeinsamer Rückgabepunkt, `AL` bleibt `0`). Das ist die
   allererste, "stille" Prüfung — vermutlich eine virtuelle,
   pro-Unterklasse überschreibbare Methode auf der `UAdminCommand_*`-Instanz
   selbst (z. B. `IsAllowedForExecutor(Executor)`).
5. Danach ein **Cooldown-Timer-Vergleich** (`comisd`, Fließkomma-Zeitstempel
   bei `[RSI+0x58]` vs. einem über `[RAX+0x78]` gelesenen Wert) — bei "noch
   im Cooldown" wird `"Command is on cooldown. Try again later."` in den
   `R8`-Ausgabepuffer geschrieben (klassisches inline-`FString::operator=`:
   Kapazitätsprüfung, ggf. Realloc-Aufruf `0x140919310`/`0x140918E20`, dann
   SSE-Kopie der UTF-16-Literal-Daten) und zum gemeinsamen Rückgabepunkt
   gesprungen.
6. Nach dem Cooldown-Check: Aufruf `0x141A4D050(RCX=R15)` → `RAX`.
7. **Zweiter, entscheidender Gate-Aufruf**: `0x141A45AA0(RCX=RBP, RDX=RAX
   [von Schritt 6], R8B=[RSI+0x52] /* Byte-Flag auf der Command-Instanz */,
   R9=&RSI[0x28])` → `bool` in `AL`.
   - **`true`** → Erfolg, `AL=1`, direkt zum Rückgabepunkt (Meldung bleibt
     leer).
   - **`false`** → `"Not authorized to execute command"` wird in den
     Ausgabepuffer geschrieben (gleiches `FString`-Zuweisungsmuster wie
     oben), `AL=0`.

**`0x141A45AA0` ist damit der wahrscheinlichste Haupt-Autorisierungs-Check** — genau
die Funktion, die (vermutlich) prüft, ob der übergebene "Executor" die nötige
Berechtigungsstufe für diesen konkreten Befehl hat.

### Funktion B, Start `0x1418c7e10` — Flag-Prüfung vor Funktion A (Reihenfolge: erst B, dann vermutlich A)

Signatur: `func(RCX=this /* dieselbe UAdminCommand-Instanz? */, R8=Ausgabe-FString)`

1. `cmp byte ptr [RCX+0x50], 0` — Flag "ist Befehl aktiviert?". Bei `false`
   (`0`): `"Command is disabled"` (Kurzform, Adresse `0x145955060`, **nicht**
   identisch mit den vier gesuchten Strings — eine fünfte, verwandte Meldung)
   in den Ausgabepuffer, `AL=0`, return.
2. Bei `true`: weiter zu `cmp byte ptr [RCX+0x51], 0` (im Report bei Zeile
   4206 abgeschnitten, noch nicht vollständig verfolgt) — vermutlich die
   "in Shipping Build deaktiviert"-Prüfung, die zu
   `"Command is disabled in shipping build"` führt.

**Wahrscheinliche Bedeutung der Byte-Flags auf der `UAdminCommand_*`-Instanz**
(nebeneinanderliegend, `this+0x50`/`+0x51`/`+0x52`):

| Offset | Vermutete Bedeutung |
|---|---|
| `+0x50` | `bEnabled` — Befehl grundsätzlich aktiv |
| `+0x51` | `bDisabledInShippingBuild` o. ä. |
| `+0x52` | Berechtigungsstufen-Byte, an `0x141A45AA0` als `R8B` übergeben |
| `+0x58` | Fließkomma-Zeitstempel für Cooldown |
| `+0x28` | Datenblock, per Zeiger an `0x141A45AA0` übergeben (`R9`) |

## Update 2026-09-05 (Folgesession): `0x141A45AA0` und `0x141A4D050` disassembliert

Mit dem neuen `dumpFunctionAt()`-Feature von `PeXrefScanner` (lineare Disassemblierung
ab einer bekannten VA statt nur Xref-Suche) wurden beide Zielfunktionen direkt
disassembliert. Rohdaten:
[`pexref_funcdump_report_2026-09-05.txt`](pexref_funcdump_report_2026-09-05.txt).

### `0x141A4D050` — trivialer Feld-Zugriff, keine komplexe Auflösung

Die gesamte Funktion besteht aus zwei Instruktionen:

```
lea rax, [rcx+0x690]
ret
```

Das heißt: `0x141A4D050(RCX=R15)` liefert einfach `R15 + 0x690` zurück — einen
**eingebetteten Unterobjekt-Zeiger innerhalb des "Executor"-Arguments**, keine
Objektauflösung, keine Lookup-Logik. Die als `rbp` bezeichnete Variable in
Funktion A (Schritt 6/7 der ursprünglichen Analyse) ist also schlicht
`Executor + 0x690`.

### `0x141A45AA0` — Berechtigungsstufen-Dispatcher mit Cascading-Fallback

Signatur bestätigt: `func(RCX=this /* Registry/Manager-Objekt, aus
[EAX+0x118] in Funktion A */, RDX=rbp /* Executor+0x690 */, R8B=Berechtigungsstufe
[cmd+0x52], R9=&cmd[0x28] /* Kommando-Identität */) -> bool`.

Ablauf (vollständig nachvollzogen bis `ret`):

1. Sperrt eine kritische Sektion bei `this+0x698` (`call [0x145139770]` = Lock,
   `call [0x145139778]` = Unlock am Ende — feste Thunk-Adressen, keine virtuellen
   Aufrufe).
2. **Switch über die Berechtigungsstufe** (`R8B`, Werte 0–4, alles ≥5 oder negativ
   → sofort `false`):
   - **Stufe 0 → immer `true`**, ganz ohne jede Identitätsprüfung. Das ist der
     wichtigste Einzelfund dieser Session (siehe "Praktische Konsequenz" unten).
   - **Stufe 1**: Lookup von `rbp` als Key in einem Set/einer Sparse-Array bei
     `this+0x6C0` (Elementgröße `0x78` Bytes). Gefunden → `true`. Nicht gefunden
     → **fällt durch zu Stufe 2** (kein `return false`, sondern Fallthrough!).
   - **Stufe 2**: identischer Lookup in `this+0x6C0` mit demselben Key `rbp`
     (offenbar dieselbe Struktur, evtl. weil Stufe 1/2 zwei Sub-Fälle derselben
     Rechte-Gruppen-Prüfung sind). Bei Treffer wird zusätzlich im gefundenen
     Eintrag bei `Eintrag+0x20` ein **verschachteltes Set konkreter erlaubter
     Kommando-IDs** durchsucht, Key = `R9` (`&cmd[0x28]`, die Kommando-Identität
     selbst). Nur wenn *sowohl* die Gruppe gefunden wird *als auch* dieses
     spezielle Kommando in der Gruppen-Erlaubnisliste steht → `true`. Sonst
     Fallthrough zu Stufe 3.
   - **Stufe 3**: liest `this+0xE8`/`this+0xEC` (zwei Rollen-/Flag-Felder auf dem
     Registry-Objekt selbst, nicht auf `rbp`!) und vergleicht sie über
     `0x142AF9AE0(role, konstante)` gegen `0` bzw. `0x11A` (282) — vermutlich ein
     globaler "aktueller Ausführungskontext" (z. B. Server-Konsole vs.
     RPC-Aufruf). Bei Erfolg zusätzlich ein Callback `0x143ECC150(this)`. Sonst:
     Lookup von `rbp` in einem **weiteren, eigenständigen Set** bei `this+0x8A0`
     (`0x141A2C770`) — das sieht nach einer direkten Einzel-Executor-Whitelist aus
     (unabhängig von Gruppen — passt konzeptionell zu "einzelne SteamID
     freigeschaltet", auch wenn strukturell nichts mit `AdminUsers.ini` selbst zu
     tun hat, wie der Nutzer bereits klargestellt hatte). Treffer → `true`, sonst
     Fallthrough zu Stufe 4.
   - **Stufe 4**: ein einziger Aufruf `0x141F24E20(rbp)` → `bool`. Vermutlich ein
     globaler "ist das ein Server-/Entwickler-Executor"-Check. `true` → erlaubt,
     `false` → endgültige Ablehnung (`"Not authorized to execute command"`).
3. Entsperrt die kritische Sektion, gibt das Ergebnis zurück.

### Praktische Konsequenz

**Wenn ein Kommando sein Berechtigungsstufen-Byte (`this+0x52` auf der
`UAdminCommand_*`-Instanz) auf `0` stehen hat, prüft `0x141A45AA0` überhaupt
keine Identität — es liefert unconditional `true`.** Das ist der einzige Pfad,
der komplett ohne gültigen `Executor`-Inhalt auskommt (alle anderen Stufen
brauchen einen `rbp`, der in mindestens einer der Registry-Strukturen als
gültiger Schlüssel auftaucht — bei unserem synthetischen `ProcessEvent`-Aufruf,
der kein echtes Spieler-`Executor`-Objekt mitbringt, ist das mit hoher
Wahrscheinlichkeit nie der Fall).

Nächster sinnvoller Schritt: **herausfinden, welches Berechtigungsstufen-Byte
`SetGodMode` (bzw. allgemein: welche der ~230 `UAdminCommand_*`-Klassen welche
Stufe) tatsächlich hat.** Das lässt sich vermutlich per Reflection zur Laufzeit
auslesen (Feld `+0x52` relativ zum `UAdminCommand_*`-Instanzbeginn, sofern die
UE4SS-Objektinspektion Zugriff auf rohe Instanzbytes erlaubt) — deutlich
risikoärmer als ein In-Memory-Hook auf `0x141A45AA0` selbst.

## Update 2026-09-05 (2. Folgesession): `_requiredExecutorLevel` ist ein reflektiertes Property — und bei praktisch allen Kommandos = 4

Statt die per Disassemblierung gefundenen Byte-Offsets (`+0x50`/`+0x51`/`+0x52`) hart
im eigenen Code zu verankern (was bei jedem SCUM-Update erneut brechen würde), wurde
geprüft, ob diese Felder echte, reflektierte `UPROPERTY`s sind — das native RCON-Modul
läuft jetzt einmalig über `UStruct::TFieldRange<FProperty>` (UE4SS-eigene
Reflection-API) statt über rohe Pointer-Arithmetik. Ergebnis: **ja, es sind ganz
normale reflektierte Felder**, mit sprechenden Namen:

| Property | Offset | Typ | Entspricht |
|---|---|---|---|
| `_isEnabled` | 80 (0x50) | BoolProperty | vermutete `bEnabled` |
| `_isEnabledInShippingBuild` | 81 (0x51) | BoolProperty | vermutete Shipping-Sperre |
| `_requiredExecutorLevel` | 82 (0x52) | **EnumProperty** | die Berechtigungsstufe aus `0x141A45AA0` |
| `_shouldExecuteOnServer`/`_shouldExecuteOnClient` | 83/84 | BoolProperty | — |
| `_hasCooldown`/`_cooldown` | 86/88 | Bool/Float | Cooldown-Timer |

Rohdaten (alle ~230 `AdminCommand_*`-Objekte, per Reflection ausgelesen):
[`admin_command_permission_levels_2026-09-05.txt`](admin_command_permission_levels_2026-09-05.txt).

**Kernfund**: `_requiredExecutorLevel` steht bei **jedem einzelnen** untersuchten
Admin-Kommando (inkl. `SetGodMode`) auf dem Wert `4` — keine Streuung über die
Stichprobe. Das bedeutet praktisch: von den vier Stufen in `0x141A45AA0` kommt für
Admin-Kommandos nur Stufe 4 überhaupt zum Einsatz; Stufen 0–3 (Gruppen-Sets,
Rollen-Flags, Einzel-Whitelist bei `+0x8A0`) sind für dieses konkrete Subsystem
totes Gewicht — vermutlich eine generische, projektweit wiederverwendete
Berechtigungs-Utility-Funktion, die auch anderswo in SCUM verwendet wird.

Das vereinfacht die eigentliche Zielfrage erheblich: **es muss nur noch verstanden
werden, was Stufe 4 (`0x141F24E20(Executor+0x690)`) prüft.**

### `0x141F24E20` disassembliert: Lookup in einem global gecachten Admin-Set

Rohdaten: [`pexref_funcdump_report2_2026-09-05.txt`](pexref_funcdump_report2_2026-09-05.txt).

Ablauf (vollständig nachvollzogen bis `ret`):

1. Baut eine temporäre `FString` auf dem Stack.
2. Liest `[rbx+0]`/`[rbx+8]` (Datenzeiger/Anzahl eines Arrays auf dem
   `Executor+0x690`-Objekt, `rbx` = das übergebene Argument) — bei einem leeren
   Array wird stattdessen ein statischer Leerstring-Literal verwendet, sonst das
   **erste Element** dieses Arrays.
3. Weist dieses Element der temporären FString zu und berechnet einen 96-Bit-Hash
   davon (drei Werte: `[rsp+0xF0]`/`[rsp+0xF8]`/`[rsp+0x100]`).
4. Holt einen **einmalig lazy-initialisierten globalen Set-Zeiger** (klassisches
   MSVC-"magic static"-Muster: Thread-Local-Storage-Guard bei `gs:[0x58]`,
   atomarer Init-Zähler bei `0x14742A418`) — d. h. eine Datenstruktur, die genau
   **einmal beim ersten Aufruf** aus irgendeiner Quelle aufgebaut und danach nur
   noch gelesen wird.
5. Durchsucht dieses Set linear nach einem Eintrag, dessen drei gespeicherte
   Werte exakt dem berechneten Hash entsprechen. Treffer → `true`, sonst `false`.

**Interpretation**: Das sieht exakt nach "ist der (Steam-)Identifier des Executors
in einer einmalig geladenen, global gecachten Admin-Liste enthalten" aus — mit
sehr hoher Wahrscheinlichkeit die zur Laufzeit aus `AdminUsers.ini` aufgebaute
Admin-Menge. Das erste Element des Arrays bei `Executor+0x690` wäre dann
vermutlich die SteamID64 (oder ein äquivalenter eindeutiger Identifier) als
String.

### Praktische Konsequenz: der Fehler liegt vermutlich VOR diesem Check, nicht in ihm

Wenn diese Interpretation stimmt, MÜSSTE unser synthetischer Aufruf eigentlich
funktionieren, sofern: (a) der von uns gewählte `WorldContextObject` (ein echter,
verbundener `ConZPlayerController`) korrekt zu einem `Executor`-Objekt mit
gültiger SteamID aufgelöst wird, UND (b) diese SteamID tatsächlich in
`AdminUsers.ini` steht (beim Testaccount der Fall). Der naheliegendste
verbleibende Verdächtige ist daher **nicht mehr Stufe 4 selbst**, sondern:

- die **stille Null-Prüfung ganz am Anfang von Funktion A** (`0x1418c7b60`):
  „`RDX`/`R15` auf null geprüft — bei null sofort... liefert `false`, keine
  Meldung“ — falls `Test_ProcessAdminCommand` beim Aufruf über eine rohe
  `ProcessEvent`-Reflection (statt über den echten
  `PlayerRpcChannel::Chat_Server_ProcessAdminCommand`-RPC-Pfad) gar kein
  gültiges `Executor`-Objekt aus unserem `WorldContextObject` konstruieren
  kann — dann bliebe `R15` null, und die gesamte Kette (inklusive des hier
  analysierten, korrekten Admin-Checks) würde nie erreicht.
- oder die stille Vtable-Prüfung `[RSI-Vtable][+0x278]`, ebenfalls vor jeder
  Meldungsausgabe.

Nächster sinnvoller Schritt: die native Implementierung von
`MiscStatics::Test_ProcessAdminCommand` selbst disassemblieren (Funktionszeiger
über die bereits aufgelöste `UFunction*` beziehbar), um zu sehen, wie/ob sie
intern überhaupt einen `Executor` aus dem `WorldContextObject` konstruiert.

## Update 2026-09-06: `Test_ProcessAdminCommand` ist im Shipping-Build eine leere Stub-Funktion

Um den vermuteten Blocker "VOR" der Berechtigungsprüfung zu finden (siehe oben),
wurde die native Implementierung hinter `Test_ProcessAdminCommand` direkt
untersucht:

1. Die Adresse wurde zur Laufzeit per Reflection ausgelesen
   (`UFunction::GetFuncPtr()`, neue Diagnosefunktion
   `AdminDispatch::dump_test_process_admin_command_address()`, Trigger über
   den Sentinel-RCON-Befehl `!dump_func_address`). Ergebnis (ASLR-verschoben):
   `0x7ff7ad768cd0`.
2. Umgerechnet auf die statische Datei-Adresse über die tatsächliche
   Prozess-Ladeadresse (`0x7ff7aafc0000`, per `Get-Process ...
   MainModule.BaseAddress`): Offset `0x27a8cd0`, statische VA
   `0x1427a8cd0`.
3. `PeXrefScanner`s `dumpFunctionAt()` wurde um ein `:<bytes>`-Suffix für
   individuelle Dump-Längen erweitert und gegen diese Adresse laufen lassen
   (rein statische Dateianalyse, kein weiterer Server-Neustart nötig). Rohdaten:
   [`pexref_funcdump_report3_2026-09-06.txt`](pexref_funcdump_report3_2026-09-06.txt).

**Befund**: `0x1427a8cd0` ist exakt der von UnrealHeaderTool generierte
`execTest_ProcessAdminCommand`-Wrapper — die Disassemblierung zeigt zweifelsfrei
das bekannte Muster (`P_GET_OBJECT`/`P_GET_PROPERTY`: `FFrame::Code`
bedingt inkrementieren, zwei Parameter aus dem `FFrame` lesen, dann der
eigentliche native Aufruf, dann `FString`-Destruktor-Aufruf für den
`commandText`-Parameter). Der eigentliche Aufruf der "echten" Implementierung
sitzt bei `0x1427a8d7e call 0x14090A820` — mit exakt den beiden erwarteten
Argumenten (`RCX=WorldContextObject`, `RDX=&commandText`).

Diese Zieladresse wurde ebenfalls disassembliert (Rohdaten:
[`pexref_funcdump_report4_2026-09-06.txt`](pexref_funcdump_report4_2026-09-06.txt)):

```
0x14090a820  ret 0x00
0x14090a823  int3
... (Padding bis zur naechsten, unabhaengigen Funktion bei 0x14090a830)
```

**`UMiscStatics::Test_ProcessAdminCommand` besteht im Shipping-Build aus genau
einem Byte (`C3`, `ret`) — die Funktion tut buchstäblich nichts.** Das erklärt
vollständig und abschließend, warum jeder bisherige Versuch, sie per
`ProcessEvent`-Reflection aufzurufen, ergebnislos blieb: nicht wegen fehlender
Executor-Identität, nicht wegen der Berechtigungsprüfung (die, wie oben gezeigt,
für einen echten Admin-Account eigentlich durchlaufen sollte) — die Funktion
ist in dieser Build-Konfiguration schlicht leer. Vermutlich ist
`Test_ProcessAdminCommand` ein reiner Editor-/Entwicklungs-Helfer (Namenskonvention
"Test_"-Präfix passt dazu), dessen Körper hinter einem
`#if !UE_BUILD_SHIPPING`-o.ä. Compile-Schalter steht und in Shipping-Builds
komplett wegoptimiert wird.

### Konsequenz für die Projektrichtung

`Test_ProcessAdminCommand` ist als Einstiegspunkt für diesen Nachbau
**endgültig ungeeignet** — unabhängig davon, wie gut Executor/Berechtigung
aufgelöst würden, hätte ein Aufruf nie eine Wirkung gehabt. Der bereits in
`docs/ARCHITECTURE.md` als "der echte Produktionspfad" notierte
`PlayerRpcChannel::Chat_Server_ProcessAdminCommand` (der tatsächliche
Chat-Befehls-Pfad, den jeder eingeloggte Admin-Spieler heute schon
regulär nutzt) muss also der neue Fokus werden — dieser darf, da er real
im laufenden Spielbetrieb verwendet wird, nicht ebenfalls eine leere
Shipping-Stub sein.

## Update 2026-09-06 (Folgesession): Chat_Server_ProcessAdminCommand ausprobiert — ohne Wirkung; ProcessEvent-Capture während Herbies Aufruf zeigt: kein reflektierter Trigger

Nachdem `Test_ProcessAdminCommand` als toter Code entlarvt wurde (siehe oben), wurde
`UPlayerRpcChannel::Chat_Server_ProcessAdminCommand(FString commandText)` als neuer
Kandidat umgesetzt:

1. Per Reflection bestätigt: `PlayerRpcChannel` ist eine `UActorComponent`, als
   Default-Subobjekt an `ConZPlayerController` gehängt (`_isEnabled`-artige
   Properties fehlen hier, stattdessen klassische `UActorComponent`-Felder wie
   `PrimaryComponentTick`). Die reflektierte Funktion `Chat_Server_ProcessAdminCommand`
   existiert und wurde aufgelöst.
2. **Wichtig**: Es gibt **keine** benannte `UPROPERTY`-Referenz "PlayerRpcChannel" auf
   `ConZPlayerController` selbst (`GetPropertyByName` liefert `nullptr`) — die
   Komponente muss über ihren `Outer` (= die besitzende `PlayerController`-Instanz)
   gefunden werden, nicht über ein Property.
3. Aufruf über `rpc_channel->ProcessEvent(Chat_Server_ProcessAdminCommand, &params)`
   mit `commandText = "#SetGodMode true <steamId>"` (mit führendem `#`, da diese
   Funktion vermutlich den rohen Chat-Text erwartet) **lief fehlerfrei durch, hatte
   aber erneut keine sichtbare Wirkung** — bestätigt durch den Nutzer live im Spiel.

### Entscheidender Test: ProcessEvent-Capture während eines ECHTEN Herbie-Aufrufs

Um endgültig zu klären, ob SCUM für echte Admin-Befehle überhaupt eine reflektierte
`UFunction` durchläuft, wurde der bereits vorhandene `ProcessEvent`-Capture-Hook
(bisher nur während der eigenen `dispatch_command()`-Aufrufe aktiv) um zwei manuelle
RCON-Sentinels erweitert: `!capture_start` / `!capture_stop`. Ablauf:

1. `!capture_start` über unser eigenes RCON gesendet.
2. **Während die Aufzeichnung läuft**, `SetGodMode true <steamId>` über Herbies noch
   funktionierendes RCON ausgelöst (per `local_bridge`-HTTP-API) — Herbie meldete
   `"God mode set to true."`, der Nutzer bestätigte den sichtbaren Effekt im Spiel.
3. `!capture_stop`, Log geholt (40996 Zeilen / ~10,9 MB Rohdaten — auf eindeutige
   Funktionsnamen reduzierte Fassung:
   [`capture_log_setgodmode_via_herbie_2026-09-06_filtered.txt`](capture_log_setgodmode_via_herbie_2026-09-06_filtered.txt)).

**Ergebnis**: In keiner der ~41.000 aufgezeichneten `ProcessEvent`-Aufrufe (inkl.
sämtlicher Animations-/Tick-/Bewegungs-Funktionen, die in diesem Fenster liefen)
taucht auch nur ein einziger Treffer für `RpcChannel`, `AdminCommand`, `Chat_Server`
oder `GodMode` auf. **Herbies RCON löst den eigentlichen Befehl nachweislich NICHT
über eine reflektierte `UFunction`/`ProcessEvent` aus** — sonst müsste er in dieser
lückenlosen Aufzeichnung erscheinen.

Einziger relevanter Treffer im gesamten Fenster: `Prisoner:NetMulticast_UpdateAdminStates`
(genau 1× aufgerufen) — mit hoher Wahrscheinlichkeit die Multicast-RPC, die den
**bereits intern geänderten** Zustand an die Clients repliziert. Das ist die
*Folge* der eigentlichen Zustandsänderung, nicht deren Auslöser — aber ein
nützlicher, reflektierter Anker, um eine erfolgreiche Zustandsänderung künftig zu
erkennen/verifizieren (z. B. per Hook), unabhängig davon, wie der Befehl selbst
ausgelöst wird.

### Bedeutung

Damit ist ziemlich sicher: der eigentliche Befehls-Trigger ist **reiner, nicht
reflektierter nativer C++-Code** — Herbie ruft vermutlich eine per Adressauflösung
(AOB-Scan o. ä.) gefundene native Funktion (am ehesten etwas in Richtung
`AdminCommandRegistry`/`AdminCommandExecutor::Execute`, siehe die ursprüngliche
Analyse ganz oben in diesem Dokument) direkt über ihren Funktionszeiger auf,
**ohne** den Umweg über Unreals Reflection-/RPC-System. Das widerspricht nicht
zwingend seiner eigenen Aussage, keine "Memory Injection" zu betreiben (im Sinne
von: keinen neuen Code in den Prozess schreiben) — ein direkter Aufruf einer
bereits vorhandenen nativen Funktion über ihre aufgelöste Adresse ist technisch
etwas anderes als Code-Injektion, auch wenn beides "in-process" passiert.

**Konsequenz für die Architektur**: Der rein reflection-basierte Ansatz
(`ProcessEvent` auf eine gefundene `UFunction`) ist damit als Sackgasse zu
betrachten — nicht nur `Test_ProcessAdminCommand` (toter Stub), sondern auch der
vermeintlich "echte" `Chat_Server_ProcessAdminCommand`-Pfad zeigt keine Wirkung,
weil offenbar *keiner* der reflektierten Pfade tatsächlich verwendet wird, wenn
ein echter Admin einen Befehl auslöst. Der einzige verbleibende, durch diese
Session nicht widerlegte Weg ist der ursprünglich befürchtete: die
Autorisierungskette selbst nativ aufzurufen bzw. zu hooken (siehe die
Disassemblierung von `0x1418c7b60`/`0x141A45AA0` weiter oben) — vermutlich über
`AdminCommandRegistry`, dessen Instanz über `ConZGameInstance._adminCommandRegistry`
bereits bekannt ist.

## Noch offen (nächster Schritt) — Stand 2026-09-06

Durch den ProcessEvent-Capture-Test (siehe Update 2026-09-06 oben) ist jetzt
klar: **kein rein reflection-basierter Ansatz wird zum Ziel führen**, egal
welche `UFunction` wir aufrufen — Herbies eigener, nachweislich funktionierender
Aufruf durchläuft selbst keine einzige reflektierte Funktion für den Trigger.
Der nächste Schritt ist damit zwingend der native Weg:

- `AdminCommandRegistry`-Instanz über `ConZGameInstance._adminCommandRegistry`
  auflösen (bereits in einer früheren Session bestätigt erreichbar), darin das
  passende `AdminCommand_SetGodMode`-Objekt aus dem `_commands`-Array finden.
- Herausfinden, wie die native `Execute()`/`CanExecute()`-Kette (Funktion A,
  `0x1418c7b60`, siehe ganz oben) tatsächlich aufgerufen wird — vermutlich über
  eine weitere, bisher nicht gefundene Einstiegsfunktion auf
  `AdminCommandExecutor` oder `AdminCommandsStatics`, die den "Executor"
  (`RDX`/`R15` in Funktion A) korrekt aus einem `ConZPlayerController`
  konstruiert. Diese Einstiegsfunktion selbst nativ aufrufen (Funktionszeiger,
  korrekte x64-Calling-Convention emulieren) statt sie zu reflektieren.
- Die Helper-Funktionen `0x1426F00C0` (Registrierungscheck vor dem
  Vtable-Gate), `0x141A4A7E0`/`0x1412D59C0` (Set-Lookup-Hilfsfunktionen) und
  `0x142AF9AE0`/`0x141A2C770` (Stufe-3/4-Helfer) sind bisher nur aus ihrem
  Aufrufkontext heraus interpretiert, nicht selbst disassembliert — bei Bedarf
  mit demselben `dumpFunctionAt()`-Mechanismus nachholbar.
- `Prisoner:NetMulticast_UpdateAdminStates` als reflektierten Erfolgs-Anker im
  Hinterkopf behalten (per Hook beobachtbar, um zu verifizieren, ob ein eigener
  nativer Aufruf tatsächlich einen Zustand geändert hat).
- Damit rückt der ursprünglich befürchtete, aufwendigere Weg (nativer
  Funktionsaufruf statt Reflection, eigenständig per Disassemblierung
  gefunden — nicht von `DeveloperMode` übernommen) wieder in den Fokus. Ein
  In-Memory-Hook auf `0x141A45AA0` bleibt eine mögliche Fallback-Option, falls
  der direkte native Aufruf der Einstiegsfunktion nicht gelingt.

## Werkzeug

Das für diese Analyse gebaute Tool (`tools/pe_xref_scanner/`) ist wiederverwendbar
für weitere Xref-Suchen (z. B. für `0x141A45AA0` selbst, um herauszufinden, wer es
noch aufruft, oder für neue Strings). Aufruf:

```
PeXrefScanner.exe <Pfad-zur-exe> <Ausgabedatei.txt>
```

Sucht aktuell die vier oben genannten Strings hart codiert (`main.cpp`,
`targets`-Vektor) — für neue Suchen dort anpassen und neu bauen.
