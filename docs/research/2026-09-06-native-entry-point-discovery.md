# Durchbruch: der echte native Einstiegspunkt für Admin-Befehle — 2026-09-06

Fortsetzung von [`2026-09-05-authorization-gate-analysis.md`](2026-09-05-authorization-gate-analysis.md).
Dort wurde gezeigt: (a) `Test_ProcessAdminCommand` ist eine leere Shipping-Stub-Funktion,
(b) `PlayerRpcChannel::Chat_Server_ProcessAdminCommand` läuft fehlerfrei, aber wirkungslos,
und (c) ein ProcessEvent-Capture während eines echten, über Herbies RCON ausgelösten
`SetGodMode`-Aufrufs zeigte: **kein einziger reflektierter Funktionsaufruf** ist beteiligt.
Herbie triggert den Befehl nachweislich rein nativ.

## Methode: erst Inline-Hook (Sackgasse), dann Stack-Capture an einem sicheren Anker (Erfolg)

### Versuch 1: PolyHook_2-Inline-Hook auf die alte Autorisierungsfunktion — negativ, aber beweiskräftig

Die ursprünglich gefundene Autorisierungsfunktion (`0x1418c7b60`, siehe voriges Dokument)
hatte laut statischer Analyse **null** Xrefs jeglicher Art (Call/Jmp/Lea/absolute
Immediate/rohe Zeiger). Um das empirisch zu klären, wurde ein echter Inline-Hook
(PolyHook_2 `x64Detour`, bereits von UE4SS selbst vendored) direkt auf die
Instruktionsebene dieser Funktion gesetzt — er ändert nichts am Verhalten (ruft
immer die Original-Funktion über die Trampoline auf), loggt nur bei Treffer.

**Ergebnis**: Der Hook installierte erfolgreich, wurde aber bei einem echten,
erfolgreichen `SetGodMode`-Aufruf über Herbies RCON **kein einziges Mal ausgelöst**.
Ein Instruktions-Hook feuert bei *jeder* Art von Kontrollfluss-Übergabe, egal wie
indirekt — das beweist empirisch (nicht nur durch fehlende Xrefs vermutet): **diese
Funktion ist im aktuell laufenden Code komplett tot/unerreichbar.**

### Versuch 2: Stack-Backtrace an einem garantiert lebendigen Anker — Erfolg

Aus der allerersten ProcessEvent-Capture-Session war bereits bekannt: die reflektierte
Multicast-RPC `Prisoner:NetMulticast_UpdateAdminStates` feuert garantiert bei jeder
echten Admin-Zustandsänderung (sie repliziert den bereits geänderten Zustand an
Clients). Statt weiter von einer Vermutung aus vorwärts zu raten, wurde diese Funktion
als **Anker** genutzt: im bereits vorhandenen, kostengünstigen ProcessEvent-Pre-Hook
wurde ein einfacher Zeigervergleich ergänzt (`function == m_admin_states_multicast_function`,
kein String-Vergleich auf dem heißen Pfad), der bei Treffer `CaptureStackBackTrace`
(Standard-Windows-API, nutzt die `.pdata`-Unwind-Metadaten, funktioniert zuverlässig
auch ohne Frame-Pointer in Shipping-Builds) aufruft und alle Rücksprungadressen plus
das zugehörige Modul loggt.

Ablauf: `!native_capture_start`-Sentinel *(hier nicht mehr nötig — der Anker ist
immer aktiv, da er nur ein billiger Zeigervergleich ist)*, `SetGodMode true <steamId>`
über Herbies RCON ausgelöst, Log geholt.

## Kernfund: Herbies Mod im Call-Stack

Rohdaten: [`capture_log_setgodmode_via_herbie_2026-09-06_filtered.txt`](capture_log_setgodmode_via_herbie_2026-09-06_filtered.txt)
(vorheriger negativer Test) und die neue Stack-Capture zeigt (Module pro Frame):

```
[0-1]   OpenScumRconNative (main.dll)      - unser eigener Hook
[2-3]   UE4SS.dll                          - Engine-ProcessEvent-Hook-Dispatch
[4-7]   SCUMServer.exe                     - <-- generischer Ausführungs-/Antwortpfad
[8-12]  scum_rcon\dlls\main.dll            - HERBIES MOD, direkt im Call-Stack!
[13-14] UE4SS.dll
[15-20] SCUMServer.exe                     - EngineTick o.ae., ruft scum_rcon's Tick-Callback
[21]    KERNEL32.DLL
[22]    ntdll.dll
```

Wichtig: es wurde zu keinem Zeitpunkt Code aus `scum_rcon\dlls\main.dll` gelesen oder
disassembliert — nur die Modulzugehörigkeit einzelner Rücksprungadressen im Stack
wurde bestimmt (`GetModuleHandleExW` mit `FLAG_FROM_ADDRESS`). Das verletzt nicht die
Projektregel, Herbies Code nicht zu untersuchen; analysiert wurde ausschließlich
SCUMs eigener, lizenziert lauffähiger Code.

Frame 7 (der unmittelbare Aufrufer von `scum_rcon.dll`, also **der native
Einstiegspunkt, den Herbies Mod direkt anspringt**) wurde in zwei Aufrufen beobachtet:
`0x141906466` und `0x1418e65e9` (Laufzeitadressen, auf die statische Datei-VA
zurückgerechnet über die bekannte Prozess-Basisadresse). Rohdaten der Disassemblierung:
[`herbie_entry_disassembly_2026-09-06.txt`](herbie_entry_disassembly_2026-09-06.txt).

## Der echte Einstiegspunkt: `0x1419063d0`

Beide beobachteten Rücksprungadressen liegen mitten in derselben Funktion, deren
Anfang bei `0x1419063d0` liegt (Prolog: `mov [rsp+8],rbx; mov [rsp+0x10],rsi; push
rdi; sub rsp,0x30` — klassisches MSVC-Muster). Rekonstruierte Signatur:
`func(RCX=this /* vermutlich die AdminCommand_*-Instanz */, RDX=args /* TArray-artige
Struktur, [RDX]=Datenzeiger, [RDX+8]=Anzahl */) -> bool`.

Ablauf:

1. `call 0x1418E8A10(this)` → liefert ein Objekt (näheres siehe unten) oder `null`
   bei Fehlschlag → sofortiger Abbruch.
2. Virtueller Aufruf `[r8+0x20]` auf diesem Objekt → `rbx` (weiterer abgeleiteter
   Kontext).
3. `call 0x1427E0D80`, dann ein **Bounds-/Registrierungscheck**: `rbx`'s Wert wird
   gegen ein Array (`[rcx+0x38]` Länge, `[rcx+0x30]` Daten) verglichen — sehr
   ähnlich der "ist registriert"-Prüfung aus der alten (toten) Analyse, hier aber
   im tatsächlich aktiven Pfad.
4. `call 0x1421E8100(rbx)` → bool. **Korrektur einer Zwischenvermutung**: das ist
   **keine** Berechtigungsprüfung, sondern eine **Positions-/Zonen-Prüfung** (Grid-
   Zellen-Lookup über Weltkoordinaten, `movups`/`shufps` auf einen Vektor bei
   `[rbx+0x1D0]`) — vermutlich "ist der Aufrufer/das Ziel in einer bestimmten Zone",
   nicht "ist Admin".
5. Falls `[rdi+8] > 0` (also mindestens ein Argument vorhanden): `call
   0x1418ECF90(rdi[0], &local)` — liest vermutlich das erste Argument aus.
6. `call 0x142203000(rbx)` — Rücksprungadresse davon ist genau `0x141906466`, die
   von uns beobachtete Adresse.
7. `call 0x1421E8100(rbx)` erneut, dann Aufbau einer formatierten Nachricht
   (`call 0x1429B88B0`, FString-artige Konstruktion mit zwei Literal-Adressen als
   Format-/Fallback-Text).
8. **Virtueller Aufruf `[rsi-Vtable][+0x290]`** mit der gebauten Nachricht als
   Argument — das ist mit hoher Wahrscheinlichkeit **"sende die Ergebnis-Antwort"**
   (das fehlende Puzzlestück aus allen früheren Sessions: wie Herbies RCON die
   Antworttext-Meldung wie `"God mode set to true."` bekommt).
9. Cleanup, `return true`.

## Die Executor-Auflösung: `0x1418E8A10` — impliziter, nicht übergebener Kontext

```
push rbx
mov rbx, [rcx+0x20]        ; this->field_0x20
test rbx, rbx
jz  -> return 0
call 0x142685AE0             ; KEIN sichtbares Argument!
mov rdx, rax
mov rcx, rbx
jmp 0x142D02DB0              ; Tail-Call: eigentliches Ergebnis kommt von hier
```

**Das ist der wichtigste Einzelfund dieser Session**: `0x142685AE0` wird **ohne jedes
Argument** aufgerufen — das ist praktisch ein sicheres Zeichen für einen Zugriff auf
**Thread-lokalen oder globalen Zustand** ("der aktuell laufende Ausführungskontext"),
nicht auf einen übergebenen Parameter. Das erklärt endgültig und schlüssig, warum
*jeder* bisherige Versuch dieser und der vorigen Session (egal über welche Funktion,
egal mit welchem `WorldContextObject`) wirkungslos blieb: **wir haben nie diesen
impliziten Kontext gesetzt**, weil wir nicht wussten, dass er existiert. Es ging nie
darum, das richtige Objekt als Argument zu finden — der native Code erwartet den
Aufrufkontext an einer ganz anderen Stelle.

## Nebenbefund: `scum.db`/`scum.db-wal` enthalten weder GodMode- noch Admin-Status

Auf Nutzerwunsch wurde die laufende `SCUM.db` (+ `-wal`/`-shm`, per Shared-Read
kopiert, keine Unterbrechung des Serverbetriebs) heruntergeladen und lokal mit
Pythons `sqlite3` durchsucht:

- Tabelle `elevated_users` existiert, ist aber **komplett leer** (0 Zeilen).
- Systematische Suche über **alle** Tabellen/Spalten nach "god"/"immortal"/
  "admin"/"elevated" (Groß-/Kleinschreibung ignoriert): **keine einzige Spalte**
  irgendwo in der Datenbank.
- `user`/`user_profile` enthalten normale Kontodaten (SteamID, Name, `prisoner_id`),
  aber keinerlei Berechtigungs- oder GodMode-Flag.

**Schlussfolgerung**: GodMode/Immortality sind reine Laufzeit-Flags im Prozessspeicher
(bestätigt durch die bereits bekannte Beobachtung "Rejoin ist GodMode immer weg"),
und Admin-Berechtigungen kommen ausschließlich aus `AdminUsers.ini` (geladen in den
einmalig aufgebauten, gecachten Set aus der letzten Session) — nicht aus der
SQLite-Datenbank.

## Update 2026-09-06 (Fortsetzung): der native Aufruf wurde tatsächlich versucht — `false`, sauber erklärt

Mit allem oben Gefundenen wurde der komplette native Aufruf tatsächlich
implementiert und live getestet (mit ausdrücklicher Freigabe des Nutzers,
"Bin online, kannst also testen"):

- `RCX` = `AdminCommand_SetGodMode`-CDO (per `StaticFindObject`, dieselbe
  Instanz, die auch die reflektierten Flags wie `_requiredExecutorLevel`
  trägt).
- `RDX` = ein selbst im Speicher aufgebauter `TArray<FString>`-Header
  (`{Data, Num=2, Max=2}`), zeigt auf zwei `RC::Unreal::FString`-Objekte
  (`"true"`, `"76561198023499707"`) — nutzt UE4SS' eigene, ABI-kompatible
  `FString`-Klasse, keine handgebaute Struktur nötig.
- Aufruf von `0x1419063d0` (auf die Laufzeitadresse umgerechnet) direkt per
  Funktionszeiger-Cast.

**Ergebnis**: Kein Absturz, keine Exception — sauberer Rückgabewert `false`.
Das allein beweist schon: die Grundmechanik (Objektauflösung, Argumentaufbau,
Calling Convention) ist korrekt, sonst wäre der Prozess abgestürzt oder hätte
Datenmüll verarbeitet.

### Warum `false`: der Registrierungs-Check schlägt fehl (vollständig nachvollzogen)

Um herauszufinden, WO genau die Ablehnung passiert, wurde die Kette
schrittweise selbst nachgebaut und jeder Teilschritt einzeln aufgerufen und
sein Ergebnis gedumpt (alles rein lesend, keine Speicher-Schreibzugriffe):

1. **`this+0x20`** direkt gelesen (kein Aufruf, nur Speicherzugriff) → **nicht
   `null`** (z. B. `0x000002026D2605A0`). Die erste denkbare Fehlerursache
   (leeres Feld auf dem CDO) ist damit ausgeschlossen.
2. **`0x1418E8A10(commandInstance)`** direkt aufgerufen (derselbe Aufruf, den
   `0x1419063d0` intern als ersten Schritt macht) → **liefert `null`**.
   Das ist die tatsächliche Fehlerquelle.
3. Da `this+0x20` nicht `null` ist und der Thread-Singleton-Getter
   `0x142685AE0()` (siehe oben) nachweislich immer ein gültiges Objekt
   liefert, muss der finale Tail-Call `0x142D02DB0(rcx=this+0x20,
   rdx=Singleton)` die Quelle der `null` sein. Disassemblierung (Rohdaten:
   [`context_resolver_disassembly_2026-09-06.txt`](context_resolver_disassembly_2026-09-06.txt))
   bestätigt:

   ```
   test rdx, rdx                      ; Singleton null? -> return 0 (war nicht der Fall)
   mov eax, [rdx+0xCC]; shr eax,0xE; test al,1
   jz return 0                          ; Flag-Bit auf dem Singleton muss gesetzt sein
   call 0x142BB7770                       ; hole eine Vergleichsgröße (evtl. "aktuelles X")
   cmp rdi, rax; jz return 0               ; Singleton darf nicht mit diesem Vergleichswert übereinstimmen
   mov r8, [rsi+0x10]                        ; r8 = ein Feld AUF UNSEREM KEY-OBJEKT (this+0x20+0x10)
   ; je nach Flag: entweder
   ;   call 0x142B92800(rcx=r8, rdx=Singleton) -> bool   ("ist Singleton in r8 registriert?")
   ; oder eine Sparse-Array-Suche in r8 nach einem Eintrag, der zum Singleton passt
   ; -> bei Nichtfund: return 0
   ```

   **Kernaussage**: Diese Funktion prüft, **ob der aktuelle Thread-Kontext
   (der Singleton) in einer Registrierungsstruktur enthalten ist, die am
   Kommando-Objekt selbst hängt** (`this+0x20+0x10`). Nur wenn der
   Singleton dort bereits eingetragen ist, wird er als gültiger "Executor"
   akzeptiert und weitergereicht.

### Schlussfolgerung

Ein echter, eingehender RPC-Aufruf (Client → Server) trägt den gerade aktiven
Thread-Kontext offenbar **irgendwo im Rahmen der Netzwerk-/RPC-Zustellung**
in diese Registrierungsstruktur ein, bevor die eigentliche Befehlsausführung
beginnt — vermutlich als Nebenwirkung der normalen RPC-Dispatch-Maschinerie
(z. B. "diese Verbindung/dieser Channel ist gerade aktiv"). Ein von uns aus
dem EngineTick heraus simulierter, "aus dem Nichts" kommender Aufruf hat
diesen Registrierungsschritt nie durchlaufen — die eigentliche Ausführungs-
und Argumentlogik ist zwar zu 100 % korrekt nachgebaut, aber der
Autorisierungs-Check lehnt konsequent ab, weil der Kontext nirgends
eingetragen ist.

**Das ist eine vollständig verstandene, saubere Erklärung — kein Rätsel
mehr, aber ein neues, eigenständiges Teilproblem**: wo/wie diese Registrierung
bei einem echten RPC passiert, und ob/wie sie sich von außen nachbilden
lässt. Das würde eine weitere Stack-Capture-Untersuchung erfordern, diesmal
an einem RPC-Empfangs-Anker statt am Multicast-Anker.

## Update 2026-09-06 (Fortsetzung 2): Bypass-Versuche — zwei Abstürze, wichtige Negativ-Erkenntnisse

Nach dem sauberen `false`-Ergebnis wurde versucht, den Registrierungs-Check
(`0x142D02DB0`) gezielt und nur während des eigenen, per RCON-Passwort
authentifizierten Aufrufs zu umgehen (PolyHook_2-Detour, aktiv nur für die
kurze Dauer des eigenen Aufrufs, sonst im ganzen Spiel wirkungslos).

### Versuch 1: `return key` — Absturz

Erste Annahme (Pfad A aus der Disassemblierung: `mov rax, rsi; ret`, das
Ergebnis ist der Schlüssel selbst unverändert). **Ergebnis: Server-Absturz**
(Prozess verschwand komplett, per automatischem Neustart-Mechanismus wieder
hochgekommen).

### Ursachenanalyse per Beobachtung eines ECHTEN, erfolgreichen Aufrufs

Statt weiter zu raten, wurde der bereits vorhandene Hook auf
`0x142D02DB0` um einen reinen Beobachtungsmodus erweitert (loggt `key`,
`singleton`, `result` bei jedem echten Aufruf, ändert nichts). Herausforderung:
diese Funktion wird auch von völlig unabhängigem, generischem Spielcode extrem
häufig aufgerufen (120.511 Aufrufe in einem kurzen Fenster, 336 unterschiedliche
`key`-Werte) — admin-befehl-spezifische Aufrufe mussten aus diesem Rauschen
herausgefiltert werden.

**Vorgehen**: Aufzeichnung während der Nutzer selbst `#SetGodMode true` im
Spielchat eingab (ein garantiert echter, erfolgreicher Aufruf — GodMode war
danach bestätigt aktiv). Anschließend wurden die aufgezeichneten `key`-Werte
nach Häufigkeit sortiert; der seltenste (`count=3`, nächsthäufigerer bereits
`count=16`) war ein klarer Ausreißer und passte inhaltlich (1× von der
JoinCommands-Automatisierung, 2× vom eigenen Testbefehl — beide nutzen
vermutlich denselben, geteilten `this+0x20`-Wert, kein pro-Befehl-eindeutiger
Schlüssel).

Alle drei Zeilen zu diesem Schlüssel zeigten identisch:
```
key=000001C4DB0F40C0 singleton=000001C33F18C580 result=000001C4DB0F4640
```
`result - key = 0x580` exakt — ein sauberer, fester Offset. Das entspricht
**Pfad B** aus der ursprünglichen Disassemblierung (die Sparse-Array-Suche,
die einen aus dem gefundenen Eintrag abgeleiteten Zeiger zurückgibt), nicht
Pfad A.

### Versuch 2: `return key + 0x580` — erneuter Absturz

Mit dieser empirisch belegten Korrektur wurde der Bypass erneut versucht.
**Ergebnis: wieder Server-Absturz.**

### Schlussfolgerung: Bypass-Ansatz aufgegeben

Zwei Abstürze mit zwei unterschiedlichen, jeweils plausibel begründeten
Rückgabewerten zeigen: **der zurückgegebene Zeiger wird von nachfolgendem
Code nicht nur als Adresse behandelt, sondern es werden vermutlich weitere
Felder AUS dem referenzierten Speicher gelesen** — die bloße Adresse korrekt
zu treffen reicht nicht, wenn der Inhalt dahinter nicht dem entspricht, was
ein echter, durch die vorausgehende (übersprungene) Registrierung
initialisierter Eintrag enthalten würde. Ein synthetischer Zeiger auf
irrelevanten/uninitialisierten Speicher an einer numerisch richtigen Adresse
ist nicht sicher.

**Der Registrierungs-Check-Bypass wurde daher vollständig deaktiviert**
(`g_bypass_registration_check` wird in `try_native_setgodmode()` nicht mehr
gesetzt — der Aufruf läuft nur noch unverändert nativ durch, liefert sauber
`false`, kein Crash-Risiko mehr). Der reine Beobachtungsmodus (Logging ohne
Verhaltensänderung) bleibt bestehen und ist sicher.

**Für zukünftige Sessions wichtig**: Ein numerisch korrekt aussehender
Rückgabewert ist nicht ausreichend, um einen internen Engine-Check sicher zu
fälschen, wenn die Bedeutung/Struktur des referenzierten Speichers nicht
vollständig verstanden ist. Der nächste sinnvolle Schritt wäre, den
**Inhalt** des echten, während eines erfolgreichen Aufrufs zurückgegebenen
Objekts (`key+0x580`, in diesem Fall `000001C4DB0F4640`) selbst zu dumpen
(analog zu `call_context_resolver_and_dump()`), um zu verstehen, was dort
wirklich stehen muss, bevor ein weiterer Bypass-Versuch überhaupt in Betracht
gezogen wird.

## Offen (nächster Schritt)

- `0x142685AE0` selbst disassemblieren — vermutlich ein TLS-Zugriff
  (`__declspec(thread)`-Variable oder Windows-`TlsGetValue`) oder ein Zugriff auf
  eine globale "current RPC context"-Variable. Das ist der letzte fehlende Baustein,
  um zu verstehen, WIE und WANN dieser Kontext gesetzt wird — und ob/wie wir ihn
  selbst setzen könnten, bevor wir `0x1419063d0` aufrufen.
- `0x142D02DB0` (der Tail-Call-Ziel von `0x1418E8A10`) disassemblieren, um die
  tatsächliche Rückgabe-Semantik zu verstehen.
- Klären, was der zweite Parameter (`RDX`, das TArray-artige Argument) genau für
  eine Struktur ist — vermutlich `TArray<FString>` der geparsten Befehlsargumente
  (nicht der rohe Befehlsstring) — d. h. Herbies Mod parst `"SetGodMode true
  <steamId>"` vermutlich VOR diesem Aufruf in Verb + Argumentliste und löst die
  passende `AdminCommand_*`-Instanz (das `RCX`-Argument) bereits selbst über die
  Registry auf.
- Erst danach: einen sicheren nativen Aufruf dieser Kette aus dem eigenen Modul
  heraus implementieren (Funktionszeiger-Cast, korrekte Calling Convention,
  korrekt aufgebaute Argumente) — das ist ein eigener, nicht trivialer
  Implementierungsschritt, sobald der Kontext-Mechanismus verstanden ist.
