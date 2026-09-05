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

## Noch offen (nächster Schritt)

- `0x141A45AA0` und `0x141A4D050` selbst disassemblieren (waren nicht Teil dieses
  ersten Scans — nur die Aufrufstellen wurden gefunden, nicht die Zielfunktionen
  verfolgt) — das ist der eigentliche Kern der Berechtigungsprüfung.
- Klären, was `RDX`/`R15` (das zweite Argument von Funktion A) tatsächlich ist —
  Arbeitshypothese: der "Executor" (vgl. `UAdminCommandExecutor` aus der
  Reflection-Analyse vom Vortag). Falls bestätigt: das wäre das fehlende Bindeglied
  zwischen `Test_ProcessAdminCommand`s `WorldContextObject`-Parameter und dieser
  nativen Prüfung — vermutlich wird intern aus dem `WorldContextObject` erst ein
  `UAdminCommandExecutor` konstruiert/aufgelöst, und *diese Auflösung* liefert bei
  unserem synthetischen Aufruf vermutlich ein Objekt, das die Prüfung in
  `0x141A45AA0` nicht besteht (oder `RBP`/`R15`-Kette bleibt leer/ungültig).
- Sobald die Semantik von `0x141A45AA0` klar ist: entweder (a) einen Weg finden,
  einen validen Executor synthetisch zu erzeugen, der die Prüfung besteht, oder
  (b) einen In-Memory-Hook auf `0x141A45AA0` selbst setzen, der für unsere eigenen
  Aufrufe `true` erzwingt (das klassische, von DeveloperMode konzeptionell
  beschriebene Vorgehen — hier aber eigenständig anhand unserer eigenen Analyse
  gefunden, nicht von dort übernommen).

## Werkzeug

Das für diese Analyse gebaute Tool (`tools/pe_xref_scanner/`) ist wiederverwendbar
für weitere Xref-Suchen (z. B. für `0x141A45AA0` selbst, um herauszufinden, wer es
noch aufruft, oder für neue Strings). Aufruf:

```
PeXrefScanner.exe <Pfad-zur-exe> <Ausgabedatei.txt>
```

Sucht aktuell die vier oben genannten Strings hart codiert (`main.cpp`,
`targets`-Vektor) — für neue Suchen dort anpassen und neu bauen.
