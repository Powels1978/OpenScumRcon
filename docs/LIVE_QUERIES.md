# Live queries / Live-Abfragen

[English](#english) | [Deutsch](#deutsch)

## English

Authenticate over Source RCON, then use:

```text
ListPlayers
GetWeather
GetTimeOfDay
```

These read-only queries also work on an empty server. `ListPlayers` includes each
connected player's SteamID, name and available profile ID, balances, ping, coordinates
and connection IP. `upid` comes from a validated `DbIntegerId` return value.
`ip` comes directly from the connection's native address getter, with
`ipSource=connection`. It is the IP seen by the server, without a port. A different
connection implementation or an unsupported native signature yields
`ipStatus=unavailable`, not a historical or fabricated address. Actual player records
and IPs are private runtime data and must never be included in public examples or commits.
The line format remains compatible with the tested client parser; it is not byte-identical
to Herbie and contains additional fields.

`GetTimeOfDay` returns JSON with `timeOfDay` (hours), `timeOfDayText` (HH:MM),
`timeOfDaySpeed` and `source=live_scum_weather_controller`.
`GetWeather` adds current `windAzimuth`, `windIntensity`, `rainIntensity`,
`fogDensity`, and available sunrise/sunset, base air/water temperature and cloud coverage.
Values come from the active weather controller, not saved database values, configured
overrides or a rain estimate from clouds. Weather values describe that controller;
they are not a separate measurement of every player's local visual conditions.

Optional unavailable values are `null`. Wind direction/intensity are native fields;
`windSpeedKph` remains `null` until conversion to current km/h is verified.
`maxWindSpeedKph` is the configured maximum only. `CheckServerTime` is a separate
native command reporting the operating system's local clock, not InGame time.

The supported build is SCUM **1.3.3.1.145413**. Queries require authentication,
reject additional arguments, and do not change weather, time or player rights.
Native address calls validate class, connection ownership, vtable and function bytes;
weather reads require one unambiguous active controller. No online executor is
needed for these queries. General native command execution still requires one.

Validation: Shipping build, 96 compatible UE4SS imports, four standalone test
executables, empty/occupied live queries, profile comparison against Herbie, and
connection-IP comparison against SCUM's login record. Herbie stays enabled until
the required replacement functions are validated; it was not disabled for these tests.

---

## Deutsch

Über Source RCON anmelden und anschließend verwenden:

```text
ListPlayers
GetWeather
GetTimeOfDay
```

Diese rein lesenden Abfragen funktionieren auch auf einem leeren Server.
`ListPlayers` enthält SteamID, Name und verfügbare Profil-ID, Kontostände, Ping,
Koordinaten und Verbindungs-IP jedes verbundenen Spielers. `upid` stammt aus einem
geprüften `DbIntegerId`-Rückgabewert. `ip` kommt direkt aus der nativen Adressabfrage
der Verbindung, mit `ipSource=connection`. Dies ist die vom Server gesehene IP ohne Port.
Andere Verbindungsimplementierungen oder unbekannte native Signaturen liefern
`ipStatus=unavailable` statt einer historischen oder erfundenen Adresse. Echte
Spielerdatensätze und IPs sind private Laufzeitdaten und gehören niemals in
öffentliche Beispiele oder Commits. Das Zeilenformat bleibt zum geprüften Client-Parser
kompatibel; es ist nicht bytegleich zu Herbie und enthält zusätzliche Felder.

`GetTimeOfDay` liefert JSON mit `timeOfDay` (Stunden), `timeOfDayText` (HH:MM),
`timeOfDaySpeed` und `source=live_scum_weather_controller`.
`GetWeather` ergänzt aktuelle Werte für `windAzimuth`, `windIntensity`,
`rainIntensity`, `fogDensity` sowie verfügbare Sonnenaufgangs-/Untergangszeiten,
Basis-Luft-/Wassertemperatur und Wolkenanteile. Die Werte stammen aus dem aktiven
Wettercontroller, nicht aus gespeicherten Datenbankwerten, eingestellten Überschreibungen
oder einer Regenschätzung aus Wolkenanteilen. Die Wetterwerte beschreiben diesen
Controller; sie messen nicht separat die lokale Wetterdarstellung jedes Spielers.

Fehlende optionale Werte sind `null`. Windrichtung/-stärke sind native Felder;
`windSpeedKph` bleibt bis zur geprüften Umrechnung in aktuelle km/h `null`.
`maxWindSpeedKph` ist nur das eingestellte Maximum. Der separate native Befehl
`CheckServerTime` meldet die lokale Betriebssystemzeit, nicht die InGame-Zeit.

Unterstützt ist SCUM-Build **1.3.3.1.145413**. Abfragen erfordern Anmeldung,
weisen zusätzliche Argumente ab und verändern weder Wetter noch Zeit oder
Spielerrechte. Native Adressaufrufe prüfen Klasse, Verbindungszuordnung, Vtable
und Funktionsbytes; Wetterabfragen benötigen einen eindeutigen aktiven Controller.
Für diese Abfragen ist kein Online-Executor erforderlich. Die allgemeine native
Befehlsausführung benötigt weiterhin einen.

Prüfungen: Shipping-Build, 96 kompatible UE4SS-Importe, vier eigenständige Testprogramme,
Live-Abfragen bei leerem/belegtem Server, Profilvergleich mit Herbie und Vergleich
der Verbindungs-IP mit SCUMs Login-Eintrag. Herbie bleibt bis zur geprüften benötigten
Funktionsgleichheit aktiv und wurde für diese Tests nicht deaktiviert.
