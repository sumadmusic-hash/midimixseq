# Teststrategie (verbindlich)

Bezug: Planabschnitt 9. Diese Strategie gilt für alle Phasen. Ein Test gilt nur als bestanden,
wenn er reproduzierbar automatisiert ausgeführt wurde oder – bei Hardware/Host – mit
Messprotokoll, Datum und Umgebung dokumentiert ist.

## 1. Testpyramide und Zuordnung zu Phasen

| Ebene | Inhalt | Werkzeuge | Läuft wo | Phase |
|---|---|---|---|---|
| T0 Core-Unit | Timing, PPQ, Swing, Pattern, Parser, Pickup, State-Model | eigener Header-only-Testrunner (C++20), CMake/CTest | Linux + macOS + CI | ab jetzt |
| T1 JUCE-Unit | APVTS-Parameter, SampleManager, Voice-DSP, Serialisierung | `juce_modules` + Catch2/JUCE-Tests | macOS | ab Phase 1 |
| T2 Audio offline | Renderpfad über Block-/Sample-Rate-Matrix, keine Live-Audio-Geräte | Offline-Renderer im Test | macOS/CI | ab Phase 2 |
| T3 Host manuell | Audiotool VST Bridge, Transport, State, Disconnect | Checklisten in `research/` | macOS real | ab Phase 0/3 |
| T4 Hardware manuell | MIDImix-Elemente, LEDs, Presets | Capture-Tools, Protokolle | real | Phase 0/4/5 |
| T5 Safety/Perf | Thread-Safety-Review, ASan/TSan, CPU/Speicher | Sanitizer, Profiler | macOS | Phase 7 |

Regel: **T3/T4 können nie durch T0–T2 ersetzt werden.** Mocks ersetzen keine reale Abnahme
(Regel R26). Umgekehrt gilt: Was als T0 testbar ist, muss automatisiert sein (Regel R24).

## 2. Pflichtabdeckung T0 (Core-Unit, bereits eingerichtet)

Implementiert unter `Tests/`, ausgeführt mit `./tools/test.sh`:

1. **PPQ ↔ Samples**
   - `blockEndPPQ = blockStartPPQ + numSamples * bpm / (60 * sampleRate)`
   - `sampleOffset = round((eventPPQ - blockStartPPQ) * 60 * sampleRate / bpm)`
   - Begrenzung auf `0 .. numSamples-1`; bpm ≤ 0 und ungültige Werte liefern „kein Trigger“.
2. **Halboffene Blockintervalle** `[start, end)`: ein Step an einer Blockgrenze wird genau
   einmal ausgelöst – über viele Blöcke und mehrere Blockgrößen hinweg gezählt.
3. **Step-Sequenz über variable Blockgrößen**: 16 Steps bei 32/64/128/256/512/1024 Samples,
   identische Trefferliste, keine Duplikate, keine Auslassungen.
4. **Swing**: 50 % = gerade; 75 % → zweiter Step bei `pairStart + 0.375` PPQ; nur gerade
   Steps (2,4,…,16) verschoben; Paarordnung und nächster Paarbeginn unverändert.
5. **Pattern-Datenmodell**: 8×16, On/Off, Velocity (Default 100), Snapshot/Rollback.
6. **MIDI-Parser**: Statusbyte, Note-On/Off, Note-On mit Velocity 0 als Off, CC, Kanal aus
   Statusbyte, Running-Status-Limitierung, unvollständige Pakete ohne Absturz.
7. **Tasten-/Toggle-Logik**: Press toggelt genau einmal, Release nicht; SOLO-Hold-Zustand
   getrennt von MUTE-Presses; keine Mehrfach-Toggles bei doppelten Events.
8. **Bankseiten**: RIGHT A→B→C→A, LEFT A←C←B←A.
9. **Soft Takeover/Pickup**: Sprungübernahme erst bei Erreichen/Kreuzen des Plugin-Werts.
10. **Pitch-Faktor**: Semitones/Cents → Rate-Faktor (+12 st = ×2, ±100 ct = ×1.0057…).
11. **State-Versionierung**: unbekanntes Feld ignorierbar, fehlende Felder Default ohne Crash.

Jeder Fehlerfix erhält einen neuen Test (Regressionstest-Pflicht).

## 3. T2 Audio-Matrix (ab Phase 2, Pflichtkombinationen)

| Dimension | Werte |
|---|---|
| Sample-Rate | 44100, 48000, 96000 |
| Blockgröße | 128, 256, 512, 1024 (128 ist Pflicht, sofern Host sie liefert) |
| Sample-Typ | Mono, Stereo, kurz (<1 Block), lang (>Block), 8 Tracks gleichzeitig |
| Voices | 1..4 pro Track, 5. Retrigger = Stealing, alle 8 Tracks × 4 Voices Volllast |
| Parameter | Gain/Pan/Pitch/Cutoff/Res während Wiedergabe, Sample-Wechsel während Wiedergabe |
| Fehlerfall | leere Spur, fehlende Datei, defekte WAV, Pfadwechsel |

Erwartet: kein Click durch unglättete Parameter, keine XRUN, kein Clipping ohne Limiter-Einsatz,
keine harten Abschnitte beim Voice-Stealing, keine Allokation/IO im Audio-Pfad.

## 4. T3 Audiotool-Bridge (manuell, protokollierpflichtig)

Checkliste (Ergebnis je Punkt: PASS/FAIL/BLOCKED + Notiz):
- [ ] VST3 in der Bridge sichtbar und lädt
- [ ] MIDImix im Plugin auswählen und verbinden
- [ ] Start/Stop folgt Host
- [ ] BPM/PPQ verfügbar? (sonst Sync-Fehlermeldung, gem. Regel R7)
- [ ] Seek und Loop korrekt
- [ ] Tempoänderung live
- [ ] Pattern-Editing während Wiedergabe
- [ ] Projekt speichern → schließen → neu laden → Vergleich
- [ ] Controller während Playback abziehen und wieder verbinden
- [ ] Projekt ohne angeschlossene MIDImix laden und bedienen
Protokoll: `research/bridge_transport_report.md`, `research/state_roundtrip_report.md`.

## 5. T4 Hardware (manuell, Rohdatenpflicht)

- Alle 24 Drehregler, 8 Kanal-Fader + Master, 16 Step-Tasten, SOLO allein und mit jeder
  MUTE-Taste, BANK L/R, SEND ALL, beide LED-Reihen, USB-Disconnect/Reconnect,
  zweite Plugin-Instanz, abweichendes Editor-Preset.
- Jede Messung erzeugt eine Zeile in `research/midimix_capture.md` plus Rohcapture-Datei.
- LED-Output wird **getrennt** von Input verifiziert (`research/midimix_led_test.md`).

## 6. T5 Safety und Performance (Phase 7)

- Review-Checkliste `processBlock()` gegen Regeln R11–R16, Ergebnis dokumentiert.
- ASan/TSan über Core-Tests und Offline-Render; Befunde müssen 0 sein.
- CPU-Profiling Volllast (8 Tracks × 4 Voices), messbare Zahlen mit Rechnerangabe.
- Langzeittest ≥ 30 min Playback mit Disconnect/Reconnect-Zyklen.

## 7. Release-Mindestkriterien (Plan 9.5, alle nötig)

1. Keine doppelten oder übersprungenen Steps bei regulärer Host-Wiedergabe.
2. Keine blockierenden MIDI-/GUI-/Dateioperationen im Audio-Thread.
3. Keine Parametersprünge beim Verbinden/Pickup.
4. Kein Absturz bei fehlendem Gerät oder Sample.
5. Plugin ohne Hardware vollständig bedienbar.
6. State nach Save/Load konsistent.

## 7.1 Freigabe-Regeln

- Eine Phase darf nur freigegeben werden, wenn ihre T0/T1/T2-Tests grün sind **und** die
  betroffenen T3/T4-Punkte entweder bestanden oder ausdrücklich als BLOCKIERT dokumentiert sind.
- BLOCKIERT ≠ bestanden. Blockierte Punkte bleiben bis zur Realmessung offen.
- Testläufe sind im Terminal-Output zu archivieren (`build/testlogs/<datum>.txt`).

## 8. Ausführen

```
./tools/test.sh              # T0-Core-Tests (Linux/macOS, keine Hardware nötig)
./tools/check_phase0.sh      # Gate-Status Phase 0 anzeigen
./tools/build.sh tests       # Tests über CMake/CTest bauen und laufen lassen
./tools/build.sh plugin      # Produktbau – wird bei offenem Phase-0-Gate verweigert
```
