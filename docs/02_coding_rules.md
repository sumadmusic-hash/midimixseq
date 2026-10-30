# Verbindliche Regeln für die Coding-KI

Diese Datei ist die operative Fassung der Planabschnitte 2.3, 8, 10 und 11.
Sie gilt ohne Ausnahme. Bei Regelkonflikt gewinnt die strengere Regel.

## A. Reihenfolge und Gates

R1. **Phase 0 zuerst.** Vor dem Phase-0-Gate (siehe `docs/00_phase0_verification_protocol.md`)
    wird kein Produktcode geschrieben. Erlaubt sind nur Dokumentation, Diagnose-Tools und
    Testskelette für hardwarefreie Logik.
R2. Phasen nacheinander umsetzen. Vor jedem Phasenwechsel: Build ausführen, Tests ausführen,
    Exit-Kriterium schriftlich belegen (`docs/NN_phase_NN_report.md`).
R3. Bei nicht bestandenem Gate: **stoppen**, Fehler reproduzierbar dokumentieren, keine
    spekulative Implementierung fortsetzen.
R4. Nach jeder Phase dokumentieren: geänderte Dateien, Entscheidungen, Test-/Build-Ergebnisse,
    offene Risiken, nächster Schritt.
R5. `tools/build.sh` verweigert den Produktbau, solange das Phase-0-Gate offen ist. Dieses
    Verhalten darf nicht umgangen, deaktiviert oder „vorübergehend“ auskommentiert werden.

## B. Verbot von Annahmen

R6. Keine MIDImix-Note-IDs, CC-Nummern, Kanäle oder LED-Kommandos fest codieren, bevor sie
    durch reale Captures bestätigt sind. Nicht verifizierte Werte immer mit `UNVERIFIED` markieren.
R7. Keine erfundenen Host-Defaults. Wenn Playing/BPM/PPQ fehlen, bleibt der Sequencer gestoppt
    und die GUI meldet „Host-Synchronisation nicht verfügbar“. 120 BPM oder PPQ 0 als Fallback
    ist untersagt.
R8. Keine eigene Ersatzbelegung für SOLO+MUTE erfinden; Ersatzbelegung ist eine dokumentierte
    Phase-0-Entscheidung nach Hardwaretest.
R9. Kein beliebiges MIDI-Gerät stillschweigend als MIDImix auswählen. Kein automatischer
    Geräteausweichwechsel beim Laden.
R10. Keine stillen Platzhalter, keine leeren TODO-Funktionen in ausgelieferten Codepfaden,
    keine Features außerhalb des V1-Umfangs, kein SysEx – ohne ausdrückliche Freigabe.

## C. Audio-Thread-Sicherheit (zwingend)

R11. In `processBlock()` verboten: Datei-I/O, Heap-Allokation (außer vorbereiteten Pools),
     CoreMIDI-Aufrufe, GUI-Aufrufe, blockierende Mutexe, `jassert`-Ausnahmen, Logging.
R12. Playhead nur im Audio-Thread während des Callbacks abfragen; `PositionInfo` nicht speichern.
R13. MIDI-Callbacks (Hardware) schreiben ausschließlich in eine begrenzte, thread-sichere Queue
     und ändern niemals direkt GUI-Komponenten.
R14. LED-Ausgabe erfolgt außerhalb des Audio-Threads über eine Queue. Queue-Überlauf muss
     definiert behandelt und zählbar diagnostiziert werden.
R15. Sample-Laden/Dekodieren auf Worker-Thread; Übergabe per vorbereitetem Snapshot/atomarem
     Pointer-Wechsel. Alte Sample-Daten werden nie im Audio-Thread freigegeben.
R16. Pattern-/Step-Änderungen aus GUI und MIDImix werden als Snapshots/Commands übergeben und
     sind spätestens ab dem nächsten Audio-Block wirksam. Laufende Voices dürfen nicht beschädigt
     werden.
R17. Eine MIDImix gehört pro Prozess genau einer Plugin-Instanz (exklusiver Besitz). Zweite
     Instanz zeigt Hinweis und bleibt als Sampler nutzbar.

## D. Architektur und Codequalität

R18. Getrennte Dateien/Ordner nach Verantwortung: `Source/Parameters`, `Sampler`, `Sequencer`,
     `MidiMix`, `State`, `UI`. Keine monolithischen Sammeldateien, keine Vermischung von
     Audio-, MIDI-, GUI- und State-Logik.
R19. Aktuelle JUCE-APIs verwenden: `AudioProcessor`, `AudioProcessorEditor`,
     `AudioProcessorValueTreeState`, `AudioPlayHead::PositionInfo`. Veraltete Playhead-APIs
     nicht neu einführen.
R20. Parameter-IDs exakt wie spezifiziert (`trackNN.gain`, `master.gain`, `sequencer.swing`, …);
     zentral definiert; nach Veröffentlichung nicht umbenennen. GUI, MIDImix und Automation
     lesen/schreiben dieselbe APVTS-Quelle.
R21. Timing-Formeln unverändert übernehmen:
     `blockEndPPQ = blockStartPPQ + numSamples * bpm / (60 * sampleRate)`;
     `sampleOffset = round((eventPPQ - blockStartPPQ) * 60 * sampleRate / bpm)`, begrenzt auf
     `0 .. numSamples-1`; halboffene Intervalle `[blockStartPPQ, blockEndPPQ)`; Swing-Paarmitte
     0,25 PPQ bei 50 % bis 0,375 PPQ bei 75 %.
R22. Voice-Regeln: One-Shots, Note-Off beendet Playback nicht, max. 4 Voices/Track mit
     kontrolliertem Voice-Stealing (älteste Voice derselben Spur), TPT-SVF-Low-Pass mit
     eigenem Filterzustand pro Voice, Smoothing für Pitch/Cutoff/Resonance/Gain/Pan.
R23. Tastenereignisse nur beim gültigen Press toggeln; Note-On mit Velocity 0 als Note-Off
     interpretieren; Release erzeugt keinen zweiten Toggle.

## E. Testpflicht

R24. Parser-, Timing-, Pattern-, DSP- und State-Logik braucht automatisierte Tests, bevor die
     zugehörige Phase als abgeschlossen gilt.
R25. Jeder Bugfix erhält einen reproduzierenden Test.
R26. Externe Abhängigkeiten (Host, Hardware) werden nicht gemockt und dann als bestanden
     verbucht. Host-/Hardware-Tests bleiben als offene Punkte sichtbar, bis sie real ausgeführt
     wurden.

## F. Definition of Done (je Aufgabe)

Implementierung vorhanden · passende Tests erfolgreich · Plugin baut · externe Funktionen bei
Bedarf im echten Host/auf echter Hardware geprüft · keine neue ungeklärte Annahme ·
Dokumentation aktualisiert.
