# 7 Entwicklungsphasen mit verbindlichen Abnahmekriterien

Statuslegende: `ERREICHT` · `TEILWEISE` · `OFFEN` · `BLOCKIERT (Grund)`

Aktueller Gesamtstatus (2026-10-31, Linux-Container ohne MIDImix/macOS/Audiotool):

| Phase | Status |
|---|---|
| 0 | **BLOCKIERT** – Hardware/Host nicht verfügbar; vorbereitet: Protokolle, Tools, Templates |
| 1–7 | **OFFEN / gesperrt** – Start erst nach bestandenem Phase-0-Gate (Regel R1/R5) |

---

## Phase 0 – Repository, Hardware und Host verifizieren (kein Produktcode)

Arbeitsanweisung: `docs/00_phase0_verification_protocol.md`

| ID | Aufgabe | Artefakt | Status |
|---|---|---|---|
| 0.1 | Repository, docs/, MIDI-Maps, Research, Builds, offene Entscheidungen prüfen | `docs/00_project_audit.md` | ERREICHT |
| 0.2 | JUCE/CMake/Xcode, Targets, C++-Standard, Lizenz/Codesign klären | `docs/01_build_environment.md` | TEILWEISE (macOS-Teil BLOCKIERT) |
| 0.3 | Jedes MIDImix-Element erfassen (Typ, Kanal, Nummer, Wert, Press/Release, Zeitfolge) | `research/midimix_capture.md` + Rohdaten | BLOCKIERT (keine Hardware); Vorlage + Capture-Tool vorhanden |
| 0.4 | MUTE, REC ARM, SOLO, SOLO+MUTE, BANK L/R, SEND ALL testen | verifiziertes Mapping | BLOCKIERT (keine Hardware) |
| 0.5 | Jede MUTE-/REC-ARM-LED separat ansteuern; Input-/Output-IDs trennen | `research/midimix_led_test.md` | BLOCKIERT (keine Hardware) |
| 0.6 | Minimaler JUCE-VST3-Prototyp, Geräte auflisten, In/Out öffnen | `research/coremidi_plugin_probe.md` | BLOCKIERT (kein macOS) |
| 0.7 | CoreMIDI in Audiotool-Bridge inkl. Disconnect/Reconnect/Berechtigungen | Bridge-CoreMIDI-Bericht | BLOCKIERT (kein Host) |
| 0.8 | Playing/Stop, BPM, PPQ, Seek, Loop, Tempo, Blockgröße untersuchen | Bridge-Transport-Bericht | BLOCKIERT (kein Host) |
| 0.9 | VST3-State-Roundtrip in Audiotool | State-Testbericht | BLOCKIERT (kein Host) |
| 0.10 | Sample-Rate und reale Blockgrößen (inkl. 128) dokumentieren | Host-Testdaten | BLOCKIERT (kein Host) |

**Abnahmekriterium Phase 0 (alle nötig):**
1. Reales Mapping liegt vor und ist mit Rohdaten belegt.
2. SOLO+MUTE ist verifiziert **oder** eine getestete Ersatzbelegung ist beschlossen.
3. MIDI-In und MIDI-Out funktionieren aus dem Plugin-Prototyp in der Audiotool-Bridge.
4. Transportdaten (BPM/PPQ) sind ausreichend **oder** Einschränkungen sind dokumentiert.
5. State-Roundtrip wurde grundsätzlich getestet.
6. Build-Umgebung ist vollständig geklärt und dokumentiert.

Verbotene Abkürzungen: Ohne funktionierenden CoreMIDI-Zugriff gilt Hardware-Integration als
**nicht machbar nachgewiesen**; ohne verlässliche PPQ-/Tempo-Daten gilt Host-Sync als
**nicht fertig**. Beides darf nicht als Erfolg verbucht werden.

---

## Phase 1 – JUCE-Projekt und Grundgerüst

Arbeit: VST3-Instrument (Stereo-Out, MIDI-In), PluginProcessor/PluginEditor, stabile
APVTS-Parameter für 8 Tracks + Master + Swing, versioniertes State-Grundgerüst, minimale GUI
(Header, Tracks, Step-Matrix, Status), reproduzierbare Build-/Test-Skripte.

**Abnahmekriterien:**
- [ ] `./tools/build.sh plugin` erzeugt auf Apple Silicon ein VST3-Bundle (arm64).
- [ ] Plugin lädt in Audiotool-Bridge und in einem zweiten Diagnose-Host.
- [ ] Alle spezifizierten Parameter-IDs existieren, sind sichtbar, änderbar, automatisierbar.
- [ ] Geänderte Parameter überleben Speichern/Laden des Projekts (Roundtrip getestet).
- [ ] GUI zeigt Host-Sync-Status und MIDImix-Status ehrlich (auch „nicht verfügbar“).
- [ ] Unit-Tests für State-Grundgerüst grün; keine Warnung „unknown parameter“ im Host.

---

## Phase 2 – Sampler-Core

Arbeit: WAV-Loader mit Metadaten, asynchrones Laden, 8 Track-Slots, sichere Sample-Übergabe,
One-Shot-Voices (max. 4/Track, Voice-Stealing), Pitch (Semitones + Cents), Amp-ADSR, TPT-SVF,
Gain/Pan/Master, Smoothing, DAW-MIDI-Noten-Trigger mit konfigurierbarem Mapping (Start 36–43)
und samplegenauen Offsets, robuste Fehlerbehandlung.

**Abnahmekriterien:**
- [ ] Acht verschiedene Samples unabhängig ladbar; leere Spur bleibt still.
- [ ] Trigger über GUI und über DAW-MIDI (Noten 36–43, änderbar, Velocity wirksam).
- [ ] Note-Off beendet One-Shot nicht; ADSR formt die Hüllkurve.
- [ ] Bei 5. Retrigger desselben Tracks wird die älteste Voice gestohlen, kein Hartcut.
- [ ] Mono-/Stereo-Samples und abweichende Sample-Rates korrekt wiedergegeben (44,1/48/96 kHz).
- [ ] Defekte/fehlende Dateien erzeugen lesbare Meldung, keinen Absturz.
- [ ] Static-Review + Test belegen: kein Datei-I/O, keine Allokation, kein Lock im `processBlock()`.
- [ ] Pitch-Faktor-Test: +12 Semitones = exakte Frequenzverdopplung; ±100 Cent wirksam.

---

## Phase 3 – Sequencer-Engine

Arbeit: Pattern 8×16 mit Step-Velocity, PositionInfo-Auswertung inkl. Fehl-Erkennung,
PPQ-16tel-Berechnung, samplegenaue Block-Offsets, Swing, Transport-Behandlung
(Start/Stop/Seek/Loop/Tempo/variable Blockgröße), thread-sichere Pattern-Snapshots, Playhead-Anzeige.

**Abnahmekriterien:**
- [ ] Pattern läuft in Audiotool exakt host-synchron über mindestens 8 Takte.
- [ ] Über alle Blockgrenzen: keine doppelten und keine verschluckten Steps (Zähltest).
- [ ] Nach Seek/Loop wird die Phase aus Host-PPQ neu bestimmt (kein alter Step-Zähler).
- [ ] Tempoänderung während Wiedergabe bleibt rhythmisch korrekt.
- [ ] Fehlen BPM/PPQ: Sequencer stoppt und GUI meldet „Host-Synchronisation nicht verfügbar“.
- [ ] Swing 50 % = gerade; 75 % verschiebt nur Steps 2,4,6,…,16; Paarordnung bleibt erhalten.
- [ ] Live-Editing von GUI und MIDImix ist spätestens ab nächstem Block wirksam, ohne Voice-Defekte.
- [ ] Unit-Tests zu `blockEndPPQ`, `sampleOffset`, halboffenen Intervallen, Swing grün.

---

## Phase 4 – MIDImix-Eingabe und Steuerung

Arbeit: Geräteauswahl (In/Out getrennt), Öffnen/Schließen, Disconnect-Erkennung,
Exklusivbesitz, Parser auf Basis verifizierter Captures, MIDI Learn, speicherbare Mappings,
Step-Tasten, Spurwahl, Bankseiten A/B/C, Fader/Drehregler, Soft Takeover/Pickup.

**Abnahmekriterien:**
- [ ] MIDImix wird im Plugin ausgewählt und steuert ohne jedes DAW-MIDI-Routing.
- [ ] MUTE 1–8 toggeln Steps 1–8, REC ARM 1–8 toggeln Steps 9–16 der gewählten Spur.
- [ ] Ein Tastendruck = genau ein Toggle (Release löst nichts aus; keine Mehrfach-Toggles).
- [ ] SOLO halten + MUTE wählt Spur 1–8 (oder beschlossene Ersatzbelegung funktioniert).
- [ ] BANK RIGHT: A→B→C→A; BANK LEFT umgekehrt; Reglerreihen folgen der Seite.
- [ ] Fader 1–8 = Track-Gain, Master-Fader = Master-Gain, auf allen Seiten.
- [ ] Kein Parametersprung beim Verbinden, beim Seitenwechsel oder bei SEND ALL (Pickup aktiv).
- [ ] USB-Disconnect erkannt; Reconnect ohne Neustart; zweite Instanz abgewiesen mit Hinweis.
- [ ] Parser-Unit-Tests mit echten Capture-Rohdaten grün.

---

## Phase 5 – LED-Feedback

Arbeit: LedDriver mit verifizierten Output-IDs, Step-Zustandsausgabe bei Editierung und
Trackwechsel, Playhead-Hervorhebung mit Wiederherstellung, Output-Queue, Fehlerfälle.

**Abnahmekriterien:**
- [ ] Nach Spurwechsel zeigen alle 16 LEDs den korrekten Pattern-Zustand der neuen Spur.
- [ ] Jeder Step-Toggle (GUI oder Hardware) aktualisiert die zugehörige LED.
- [ ] Nur geänderte LEDs werden gesendet (Messung der Ereignisrate).
- [ ] Playhead-Hervorhebung stört nicht; tatsächlicher Zustand wird wiederhergestellt.
- [ ] LED-Ausgabe geschieht außerhalb des Audio-Threads; Queue-Überlauf ist zählbar.
- [ ] Ist eine LED-Reihe laut Phase 0 unzuverlässig, ist ihre Ausgabe deaktiviert und die GUI
      zeigt vollständigen Status (dokumentiert, nicht stillschweigend).
- [ ] LED-Ausfall (Gerät entfernt) beeinflusst Audio und Sequencer nicht.

---

## Phase 6 – Presets und GUI vervollständigen

Arbeit: Drum-Kit- und Pattern-Presets, kompletter Plugin-State, fehlende Samples/Neuverknüpfung,
MIDI-Learn-GUI, finale Step-Matrix-, Sample- und Parameteransichten, Copy/Paste/Clear,
Undo/Redo (nur bei stabilem State-Modell).

**Abnahmekriterien:**
- [ ] Kit-Preset speichert 8 Sample-Zuordnungen + Klangparameter; Pattern-Preset speichert
      8×16 Steps + Velocity + Swing; beide getrennt laden/speichern.
- [ ] Vollständiger Plugin-State enthält Kit, Pattern, Parameter, Geräteauswahl und Mapping
      mit Versionsnummer.
- [ ] Unbekannte/fehlende State-Felder verursachen keinen Absturz (Migrationstest).
- [ ] Fehlendes Sample wird als „Sample fehlt“ angezeigt; Neuverknüpfung funktioniert;
      es werden keine Ersatzsamples geladen.
- [ ] Fehlendes MIDI-Gerät verhindert weder Plugin-Laden noch Audio.
- [ ] Projekt laden ohne angeschlossene MIDImix ist vollständig bedienbar.
- [ ] Copy/Paste/Clear für Pattern und Undo/Redo getestet.
- [ ] Lesbarkeitsregeln erfüllt (Kontrast, ≥14–16 px, Zustände nicht nur farbcodiert).

---

## Phase 7 – Release-Qualität

Arbeit: Audio-Thread-Safety-Review, Sanitizer-Läufe (ASan/TSan soweit verfügbar),
CPU-/Speicherprofiling, Audiotool-Bridge-Abnahme, zweiter Host (Reaper), Codesignierung,
Installationsskript, Bedienungsanleitung/Cheat-Sheet.

**Abnahmekriterien:**
- [ ] Review aller `processBlock()`-Pfade auf verbotene Operationen dokumentiert und bestanden.
- [ ] ASan/TSan-Lauf (Core-Tests und Host-Lasttest) ohne Befunde.
- [ ] Profiling mit 8 Tracks × 4 Voices dokumentiert (CPU%, XRUN-frei, Testbedingungen genannt).
- [ ] Audiotool-Bridge-Abnahme komplett bestanden (Planabschnitt 9.4, alle Punkte).
- [ ] Zweiter VST3-Host als Diagnosetest bestanden.
- [ ] Codesignierte, installierbare Version auf Apple Silicon installiert und geladen.
- [ ] Disconnect/Reconnect über längere Session stabil; keine LED-Konkurrenz.
- [ ] Release-Mindestkriterien aus Planabschnitt 9.5 alle erfüllt.
- [ ] Cheat-Sheet und Anleitung liegen im Repo bei.

---

## Hinweis zum Fortschritt

Diese Tabelle ist bei jedem Phasenwechsel zu aktualisieren. Ein Phase gilt nur dann als
abgeschlossen, wenn alle Abnahmekriterien mit Datum, Umgebung und Beleg (Log, Screenshot,
Testoutput oder Messprotokoll) dokumentiert sind. Nicht belegbare Kriterien bleiben OFFEN.
