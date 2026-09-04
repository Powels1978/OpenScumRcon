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
