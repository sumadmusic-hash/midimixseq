# Phase 0 – Verifizierungsprotokoll (vor dem Coding)

Dokumenttyp: verbindliche Arbeitsanweisung für die Coding-KI
Status: **Phase 0 ist Pflicht und vor jeder Produktentwicklung abzuschließen.**

> Regel aus Abschnitt 11 des Implementierungsplans: „Phase 0 zuerst ausführen.
> Keine Annahmen zu MIDImix-IDs, LED-IDs, CoreMIDI in der Bridge oder Host-Transport treffen.“

Dieses Protokoll zerlegt Phase 0 in ausführbare Schritte mit konkreten Kommandos,
Messprotokollen und Exit-Kriterien. Erst wenn alle Punkte abgearbeitet und dokumentiert
sind, darf Phase 1 beginnen.

---

## 0. Grundregel für diese Umgebung

Die aktuelle Entwicklungsumgebung ist **Linux x86_64 ohne macOS, ohne Xcode, ohne
CoreAudio, ohne Audiotool VST Bridge und ohne angeschlossene Akai MIDImix**.

Das bedeutet zwingend:

1. Alle hardware- und hostabhängigen Punkte (0.3–0.10) können hier **nicht bestanden**,
   sondern nur **vorbereitet** werden (Tools, Messprotokolle, Templates).
2. Jeder nicht verifizierte Wert wird ausdrücklich mit `UNVERIFIED` markiert.
3. Die KI darf fehlende Hardware-/Host-Ergebnisse **nicht durch Annahmen ersetzen**
   (Abschnitt 8, Einleitung).
4. Der Umstieg auf einen macOS/Apple-Silicon-Arbeitsplatz ist Voraussetzung für das
   Phase-0-Exit und muss als Blocker dokumentiert bleiben.

Ergebnis dieser Umgebung: **Phase 0 = NICHT BESTANDEN (Blocker: keine Ziel-Hardware/-Host).**
Siehe `docs/00_project_audit.md`, Abschnitt „Offene Entscheidungen / Blocker“.

---

## 1. Aufgaben und Nachweise

### 0.1 Repository-Audit → `docs/00_project_audit.md`
- Bestand prüfen: Dateien, Ordner, Git-Historie, vorhandene docs/, MIDI-Maps, Research-Dateien.
- Ergebnis: Bestandsliste + offene Entscheidungen.
- Durchführbar in dieser Umgebung: **JA** (abgeschlossen).

### 0.2 Build-Umgebung → `docs/01_build_environment.md`
- JUCE-Version pinnen, CMake-Version, C++-Standard, macOS Deployment Target, SDK, Xcode.
- Lizenzmodell (JUCE ISC/GPL/commerziell) und Codesign-Fragen klären.
- Durchführbar in dieser Umgebung: **teilweise** – Linux-Buildkette prüfbar,
  macOS/Xcode/ARM64 nur spezifizierbar (`UNVERIFIED`).

### 0.3 Vollständige MIDImix-Capture → `research/midimix_capture.md` + Rohdaten
Für jedes Bedienelement einzeln und kombiniert notieren:
Nachrichtentyp · Kanal · Nummer/Wert · Velocity · Press/Release · Zeitfolge.

Methode (macOS):
```
./tools/midimix_capture.sh research/raw/capture_<datum>.txt
```
Das Tool liest alle CoreMIDI-Quellen und schreibt Timestamp-hex-MIDI-Zeilen.
Rohdaten unverändert archivieren; Auswertung in Tabellenform in `midimix_capture.md`.

Mindestmessreihe (eine Zeile pro Element, jeweils getrennt):
- [ ] Kanal-Fader 1–8 (vollständiger Weg, Mittelposition, Endpositionen)
- [ ] Master-Fader
- [ ] 24 Drehregler (Reihe 1/2/3, je 8)
- [ ] MUTE 1–8 (Press und Release separat)
- [ ] REC ARM 1–8 (Press und Release separat)
- [ ] SOLO 1–8 (Press und Release separat)
- [ ] BANK LEFT · BANK RIGHT
- [ ] SEND ALL
- [ ] SOLO halten + MUTE 1–8 (Kombination, Kernfrage der Spurwahl)
- [ ] Note-On mit Velocity 0 (falls vorhanden)

### 0.4 Verhalten der Taster
- Klären, ob Note-On-Velocity-0 als Note-Off gesendet wird oder echte Note-Offs.
- Klären, ob ein Tastenwechsel (Press/Release) zwei Events erzeugt → Toggle nur bei Press.
- **Kernfrage:** Ist SOLO-Hold von normalen MUTE-Presses zuverlässig unterscheidbar?
  - JA → Belegung aus Abschnitt 1.3 des Plans übernehmen.
  - NEIN → in Phase 0 eine **getestete Ersatzbelegung beschreiben und dokumentieren**.
    Die KI darf keine eigene Ersatzbelegung erfinden.
- Ergebnis: `research/midimix_button_behavior.md` (Template vorhanden).

### 0.5 LED-Test → `research/midimix_led_test.md`
- Jede MUTE-LED und jede REC-ARM-LED **separat** ansteuern.
- Input-IDs und Output-IDs **getrennt** dokumentieren (dürfen nicht gleichgesetzt werden).
- Bei unzuverlässiger Steuerbarkeit: Ausgabe deaktivieren, GUI als vollständige
  Statusanzeige nutzen (Abschnitt 3.6).
- Testreihenfolge im Template vorgegeben.

### 0.6 CoreMIDI-Prototyp → `research/coremidi_plugin_probe.md`
- Minimalen JUCE-VST3-Prototyp bauen, MIDI-Geräte auflisten, MIDImix-In und -Out öffnen.
- Nachweis, dass Geräteerkennung und Öffnen aus einem Plugin-Prozess heraus funktionieren.
- In dieser Umgebung vorbereitbar: Probe-Programm mit identischer API-Nutzung
  (`tools/midimix_probe.cpp`, JUCE-frei über ALSA-Sequencer als struktureller Äquivalenttest).

### 0.7 CoreMIDI in der Audiotool-Bridge
Tests im **Bridge-Prozess**, nicht nur in einer Standalone-App:
- [ ] MIDI-In-Enumeration sichtbar
- [ ] MIDI-Out-Enumeration sichtbar
- [ ] MIDImix öffnen/schließen
- [ ] USB-Disconnect während Playback → Ereigniserkennung
- [ ] Reconnect → Gerät wieder nutzbar
- [ ] macOS-Berechtigungen / Fehlermeldungen dokumentieren
Ergebnis: Bridge-CoreMIDI-Bericht (Template `research/bridge_coremidi_report.md`).

### 0.8 Host-Transport → `research/bridge_transport_report.md`
Pro Messung dokumentieren: Playing/Stop-Flag · BPM vorhanden? · PPQ vorhanden? ·
Seek · Loop · Tempoänderung · Sample-Rate · Blockgröße (inkl. 128, soweit verfügbar).
- Wenn PPQ/BPM fehlen: **Host-Sync gilt als nicht machbar nachgewiesen** und muss
  als Einschränkung dokumentiert werden; Sequencer bleibt dann gestoppt mit
  „Host-Synchronisation nicht verfügbar“ (Abschnitt 5.2). Erfundene Defaults verboten.

### 0.9 State-Roundtrip → `research/state_roundtrip_report.md`
Parameter ändern → Projekt speichern → Host schließen → neu laden → vergleichen.
Parameter-IDs und Reihenfolge müssen stabil bleiben (Abschnitt 6.1).

### 0.10 Host-Testdaten
Sample-Rate und reale Blockgrößen dokumentieren (in 0.8 enthalten).

---

## 2. Exit-Kriterium Phase 0 (Wortlaut Plan, Abschnitt 8)

Alle Punkte müssen mit Messprotokoll belegt sein:

- [ ] Reales Mapping liegt vor (0.3/0.4).
- [ ] SOLO+MUTE funktioniert **oder** Ersatzbelegung ist beschlossen und getestet (0.4).
- [ ] MIDI-In/Out funktionieren aus dem Plugin-Prototyp in der Audiotool-Bridge (0.6/0.7).
- [ ] Transportdaten sind ausreichend **oder** Einschränkungen sind dokumentiert (0.8).
- [ ] State-Roundtrip wurde grundsätzlich getestet (0.9).
- [ ] JUCE-Version, Targets, C++-Standard, Lizenz/Codesign geklärt (0.2).

Verbotene Kurzschlüsse:
- Ohne funktionierenden CoreMIDI-Zugriff darf Hardware-Integration **nicht** als machbar
  behauptet werden.
- Ohne verlässliche PPQ-/Tempo-Daten darf Host-Sync **nicht** als fertig gelten.
- Bei nicht bestandenem Gate: stoppen, Fehler reproduzierbar dokumentieren, keine
  spekulative Implementierung fortsetzen (Abschnitt 11, Regel 12).

---

## 3. Übergabe an Phase 1

Phase 1 darf erst starten, wenn Abschnitt 2 vollständig abgehakt ist. Bis dahin sind
ausschließlich Dokumentation, Testskelette und Diagnose-Werkzeuge erlaubt – kein
Produktcode für Sampler, Sequencer oder MIDImix-Integration.
