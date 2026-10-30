# MIDImix Drum Sampler – Implementierungsplan für eine Coding-KI

Dokumenttyp: verbindliche V1-Spezifikation und Entwicklungsplan (vor dem Coding)
Ziel: macOS / Apple Silicon · Plugin: VST3-Instrument · Framework: JUCE + CMake
Primärer Host: Audiotool über lokale VST Bridge · Controller: Akai MIDImix · Projektname: MIDImix Drum Sampler

## 0. Produktziel und V1-Grenzen

Entwickle ein eigenständiges 8-Spur-Drum-Sampler-VST3-Instrument mit integriertem 16-Step-Sequencer. Die MIDImix wird im Plugin ausgewählt und über macOS CoreMIDI (direkt oder über die JUCE-MIDI-API) angesprochen. Eine MIDI-Verbindung durch die DAW ist für die Hardware-Steuerung nicht erforderlich.

Der Anwender kann acht Drum-Samples laden, mit der MIDImix die 16 Steps der ausgewählten Spur programmieren, alle acht Spuren mischen und deren Klangparameter steuern. Der Sequencer folgt dem Host-Transport und Host-Tempo. Alle Funktionen bleiben zusätzlich über die Plugin-GUI bedienbar.

### 0.1 Verbindlicher Umfang V1

| Bereich | Festlegung |
|---|---|
| Plugin | VST3-Instrument, Stereo-Ausgang, kein Audioeingang erforderlich |
| Plattform | zunächst natives ARM64 für macOS / Apple Silicon |
| Host | Audiotool VST Bridge ist primärer Abnahme-Host |
| Sampler | exakt 8 Drum-Spuren, 1 Sample-Slot je Spur |
| Sequencer | 16 Steps pro Spur, 8 × 16 Pattern, 16tel-Raster |
| Pattern-Speicher | ein aktives Pattern; Kit- und Pattern-Presets separat |
| Sync | Host-Transport, BPM und PPQ; kein unabhängiger Sequencer-Transport |
| MIDImix | im Plugin auswählen; MIDI-In und MIDI-Out direkt öffnen |
| DAW-MIDI | optionaler zusätzlicher Note-Input zum direkten Triggern der acht Spuren |
| Plugin-MIDI-Out | nicht erforderlich in V1 |
| SysEx | nicht in V1; kein automatisches Überschreiben der Hardware-Konfiguration |
| Weitere Features | keine Pattern-Ketten, Song-Modus, MIDI-Clock-Slave oder Mehrzonen-Sampler |

Kernprinzip: Die MIDImix ist Mixer und physischer Step-Sequencer dieses Instruments, nicht ein allgemeiner DAW-Controller. Das Plugin muss auch ohne MIDImix vollständig funktionieren.

## 1. Verbindliches MIDImix-Bedienkonzept

Die Coding-KI darf keine alternative Belegung erfinden. Protokoll-IDs und LED-Kommandos werden jedoch erst nach Phase 0 festgeschrieben.

### 1.1 Feste Zuordnung

| MIDImix-Element | Plugin-Funktion |
|---|---|
| Kanal-Fader 1–8 | Gain der Drum-Spuren 1–8 |
| Master-Fader | Master-Gain |
| MUTE 1–8 | Steps 1–8 der ausgewählten Spur toggeln |
| REC ARM 1–8 | Steps 9–16 der ausgewählten Spur toggeln |
| SOLO halten + MUTE 1–8 | Spur 1–8 auswählen |
| BANK LEFT / RIGHT | Parameterseite zurück / vor |
| Drehregler-Reihen 1–3 | je nach Parameterseite acht Spurparameter steuern |
| SEND ALL | in V1 ignorieren; darf keine Plugin-Parameter verändern |

Die MUTE- und REC-ARM-Tasten werden in V1 nicht als DAW-Mute bzw. Record-Arm verwendet. Ein gültiger Tastendruck toggelt einen Step; Note-Off darf keinen zweiten Toggle auslösen.

### 1.2 Parameterseiten

Die acht Regler einer Reihe steuern jeweils denselben Parameter für die acht Drum-Spuren.

| Seite | Reihe 1 | Reihe 2 | Reihe 3 |
|---|---|---|---|
| A – Main | Pan | Pitch (Semitones) | Filter Cutoff |
| B – Filter/Shape | Filter Resonance | Amp Decay | Filter-Envelope-Amount |
| C – Amp Envelope | Amp Attack | Amp Sustain | Amp Release |

- BANK RIGHT: A → B → C → A; BANK LEFT: A ← C ← B ← A.
- Fader bleiben auf allen Seiten den Track-Gains zugeordnet.
- Pitch Fine/Cents und weitere Detailparameter werden in der GUI bedient.
- Aktive Parameterseite und ausgewählte Spur müssen in der GUI klar sichtbar sein. Bank-LEDs sind keine Voraussetzung.

### 1.3 Spur-Auswahl und Hardware-Gate

SOLO gedrückt halten, dann MUTE 1–8 drücken, um die entsprechende Spur auszuwählen. SOLO loslassen; danach arbeiten MUTE-Tasten wieder als Steps 1–8. REC ARM bleibt Steps 9–16.

Vor Implementierung verifizieren: Das reale MIDI-Verhalten muss SOLO-Hold von normalen MUTE-Presses unterscheiden lassen. Falls das nicht zuverlässig möglich ist, muss in Phase 0 eine getestete Ersatzbelegung beschlossen und dokumentiert werden. Die Coding-KI darf keine Ersatzbelegung selbst erfinden.

Beim Spurwechsel wird die GUI-Auswahl aktualisiert und die Anzeige der 16 Step-LEDs auf den Zustand der neuen Spur gebracht.

### 1.4 Bedienung ohne Controller

Die GUI muss alle Funktionen bereitstellen: Steps toggeln, Spur auswählen, Parameter ändern, Samples laden/entfernen, Presets verwalten sowie MIDI-Geräte verbinden/trennen. Das Plugin startet und arbeitet ohne angeschlossene MIDImix.

## 2. Technische Architektur

### 2.1 Framework und Plugin

- JUCE mit CMake verwenden; JUCE-Version fest pinnen und dokumentieren.
- V1 baut nur VST3. Plugin als Instrument mit Stereo-Ausgang und aktiviertem VST3-MIDI-Input konfigurieren.
- Keine MIDI-Ausgabe an die DAW in V1.
- Aktuelle JUCE-APIs verwenden, insbesondere AudioProcessor, AudioProcessorEditor, AudioProcessorValueTreeState und AudioPlayHead::PositionInfo; keine veralteten Playhead-APIs neu einführen.
- C++-Standard, Xcode-/SDK-Version und macOS Deployment Target im Build-Dokument festlegen.
- ARM64 ist das erste Ziel. Universal Binary nur ergänzen, wenn es die V1-Entwicklung nicht verzögert.

### 2.2 Komponenten und Verantwortlichkeiten

| Komponente | Aufgabe |
|---|---|
| PluginProcessor | AudioProcessor, processBlock, Host-Playhead, Audio-/DAW-MIDI-Verarbeitung |
| PluginEditor | GUI, Geräteauswahl, Pattern, Samples, Parameter und Status |
| ParameterState | stabile Parameter-IDs, Automation und Parameterzustand |
| SampleManager | Laden/Dekodieren und sichere Übergabe von Samples |
| DrumTrack / SamplerVoice | Track-Daten, Voice-Playback, Pitch, Envelope, Filter, Gain/Pan |
| PatternData / PatternController | 8 × 16 Pattern, Velocity, GUI-/MIDI-Änderungen und Snapshots |
| SequenceEngine | Host-synchronisierte Steps und Sample-Offsets |
| MidiMixDeviceManager | Geräte finden, öffnen, schließen, Auswahl und Disconnect |
| MidiMixParser | reale MIDImix-Meldungen dekodieren |
| MidiLearnManager | alternative MIDI-Zuordnungen erfassen und speichern |
| LedDriver | Step-LEDs über den MIDI-Ausgang stecken |
| StateSerializer | versionierten Plugin-State speichern/wiederherstellen |

Komponenten nach Verantwortung in getrennten Dateien/Ordnern halten. Keine monolithischen Quellcodedateien oder Vermischung von Audio-, MIDI-, GUI- und State-Logik.

### 2.3 Thread-Modell – zwingend

1. Audio-Thread: processBlock, Voice-Playback, DSP, Sequencer-Events und DAW-MIDI-Noten.
2. MIDI-Callback-Kontext: Empfang der CoreMIDI-/JUCE-MIDI-Nachrichten.
3. Message-/Worker-Thread: GUI, Geräteverwaltung, Dateidialoge, Sample-Laden und MIDI-LED-Ausgabe.

Regeln:
- MIDI-Callbacks ändern niemals direkt GUI-Komponenten. Sie schreiben Events in eine begrenzte, thread-sichere Queue.
- Der Audio-Thread verarbeitet MIDI-/Pattern-Kommandos ohne blockierende Locks.
- Kein Datei-I/O, keine Heap-Allokation, keine CoreMIDI-Aufrufe, keine GUI-Aufrufe und keine blockierenden Mutex-Aufrufe im processBlock().
- LED-Ausgabe läuft außerhalb des Audio-Threads über eine Queue.
- Samples werden außerhalb des Audio-Threads geladen und dekodiert. Neue Sample-Daten werden sicher per vorbereitetem Snapshot/Pointer-Wechsel übernommen; alte Daten werden nicht im Audio-Thread freigegeben.
- Queue-Überläufe müssen definiert behandelt und diagnostizierbar sein.

### 2.4 Mehrere Plugin-Instanzen

In V1 darf eine MIDImix nur einer Plugin-Instanz gleichzeitig gehören. Ein prozessweiter Geräte-Manager verwaltet den exklusiven Besitzer. Eine zweite Instanz zeigt „MIDImix wird bereits von einer anderen Plugin-Instanz verwendet“, bleibt aber als Sampler vollständig nutzbar. Beim Schließen/Disconnect wird das Gerät freigegeben. Es dürfen keine konkurrierenden LED-Ausgaben entstehen.

## 3. MIDImix-Anbindung und MIDI-Verarbeitung

### 3.1 Geräteauswahl

Die GUI zeigt einen MIDImix-Bereich mit Geräteauswahl, Connect/Disconnect, Rescan, MIDI-Learn-Status und Verbindungs-/LED-Status.
- MIDI-Eingang und MIDI-Ausgang separat enumerieren und öffnen. Ihre IDs dürfen nicht als identisch vorausgesetzt werden.
- Dem Anwender möglichst eine verständliche Geräteauswahl „Akai MIDImix“ anbieten; intern passende In-/Out-Endpunkte zuordnen.
- Kein beliebiges MIDI-Gerät stillschweigend als MIDImix auswählen.
- Ausgewähltes Gerät anhand von Name und stabilem Identifier speichern. Beim Laden nur dieses Gerät erneut versuchen; niemals automatisch auf ein anderes Gerät ausweichen.
- USB-Disconnect erkennen und Status aktualisieren. Reconnect nur für das zuvor ausgewählte Gerät versuchen bzw. nach klarer Benutzeraktion.
- Teilbetrieb (nur MIDI-In oder nur MIDI-Out verfügbar) verständlich anzeigen. MIDI-In darf auch ohne LED-Out funktionieren.

### 3.2 Datenquellen

- MIDImix-Eingabe: direkte CoreMIDI-Verbindung über JUCE-MIDI-Input/API.
- MIDImix-Ausgabe: direkte CoreMIDI-Verbindung über JUCE-MIDI-Output/API.
- DAW-MIDI-Noten: separater Eingang über den normalen VST3-MIDI-Bus.
- Hardware-MIDI und DAW-MIDI getrennt behandeln; keine doppelte Verarbeitung oder Verwechslung.
- DAW-MIDI-Routing ist keine Voraussetzung für die MIDImix-Steuerung.

### 3.3 Parser und Event-Verhalten

Der Parser behandelt Note-On, Note-On mit Velocity 0 als Note-Off, Note-Off, CC, MIDI-Kanal, Nummer/Wert und Zeitstempel soweit vorhanden. Für Taster gilt: Step-/Aktion-Event nur beim gültigen Press auslösen, nicht beim Release. SOLO-Hold-Zustand zuverlässig verfolgen. Doppelte/fehlende Events dürfen keine ungewollten Mehrfach-Toggles verursachen. CC- und Note-basierte Tastenbelegung muss grundsätzlich abbildbar sein.

### 3.4 Mapping und MIDI Learn

Zwei Ebenen:
1. MIDImix-Standardprofil aus vorhandener Recherche, anschließend mit echten Hardware-Captures bestätigt.
2. MIDI Learn für abweichende Hardware-Presets.

Lernbare Ziele: Steps 1–16, Spur 1–8, Parameterseiten, Fader, Drehregler und weitere Plugin-Parameter. Mapping-Eintrag enthält Nachrichtentyp, Kanal, Note-/CC-Nummer, Press-/Release-Verhalten und Zielaktion.

Keine MIDI-IDs als gesicherte Werkswerte fest codieren, bevor sie aufgenommen wurden. docs/01_research_midimix.md und weitere vorhandene Research-Dateien zuerst lesen und mit realer Hardware abgleichen. Nicht verifizierte Werte ausdrücklich UNVERIFIED markieren.

### 3.5 Soft Takeover

Fader und Drehregler verwenden Pickup/Soft Takeover, damit beim Verbinden, Parameterseitenwechsel oder Wechsel der Steuerquelle keine Parametersprünge entstehen. Ein Hardware-Regler übernimmt einen Parameter erst, wenn sein physischer Wert den aktuellen Plugin-Wert erreicht oder kreuzt. Pickup-Zustand je Control/Parameter führen und bei Bedarf in der GUI anzeigen. SEND ALL darf keine Parameteränderungen auslösen.

### 3.6 LED-Feedback

Die 16 Step-LEDs bilden den Pattern-Zustand der ausgewählten Spur ab:
- MUTE-LEDs: Steps 1–8; REC-ARM-LEDs: Steps 9–16.
- Step aktiv: LED an; inaktiv: LED aus.
- Trackwechsel: korrekten LED-Zustand aller 16 Steps der neuen Spur senden.
- Playhead darf kurz hervorheben; danach den tatsächlichen Step-Zustand wiederherstellen.
- Nur notwendige Änderungen senden. Ausgabe außerhalb des Audio-Threads.
- LED-Ausfall darf Audio und Sequencer nicht beeinträchtigen.

Hardware-Gate: LED-Output-IDs und Kommandos getrennt von Input-IDs auf echter Hardware testen. Wenn eine LED-Reihe nicht zuverlässig steuerbar ist, diese Ausgabe deaktivieren und die GUI als vollständige Statusanzeige verwenden. Keine LED-Funktion aus vermuteten Input-IDs ableiten.

### 3.7 SysEx

SysEx ist ausdrücklich nicht Teil von V1: kein automatisches Überschreiben des Hardware-Presets, kein Setup-Dialog und kein Roundtrip als Voraussetzung. Standardmapping/MIDI Learn müssen ohne SysEx funktionieren. SysEx später nur als gesonderte Erweiterung nach verifiziertem Byte-Layout und Hardware-Roundtrip.

## 4. Sampler- und DSP-Spezifikation

### 4.1 Track-Aufbau

Exakt acht unabhängige Drum-Tracks. Jeder Track besitzt einen Sample-Slot, eigene Voice-Verwaltung, 16 Steps sowie folgende Klangparameter:
- Gain und Pan;
- Pitch −24 bis +24 Semitones plus Fine-Tuning mindestens −100 bis +100 Cent;
- Amp Attack, Decay, Sustain und Release;
- Filter Cutoff, Resonance und Filter-Envelope-Amount.

### 4.2 Sample-Laden

- WAV ist Pflichtformat; Mono und Stereo unterstützen.
- Weitere Formate nur ergänzen, wenn JUCE sie ohne problematische Zusatzlizenz zuverlässig unterstützt.
- Abweichende Sample- und Host-Samplerates korrekt behandeln.
- Laden und Dekodieren asynchron außerhalb des Audio-Threads.
- Samples im RAM halten; leere Spur bleibt still.
- Fehlerhafte/fehlende Dateien verständlich melden; Plugin darf nicht abstürzen.
- V1 speichert Pfad, Dateiname, Kanalzahl, Sample-Rate und Identifikator/Prüfwert, bettet Audiodaten aber nicht in den Plugin-State ein. Fehlende/verschobene Dateien als „Sample fehlt“ anzeigen und manuelle Neuverknüpfung anbieten. Portable Sample-Packs sind spätere Erweiterung.

### 4.3 Voice-Verhalten

- Drum-Samples sind One-Shots; MIDI-Note-Off beendet Playback nicht.
- Amp-ADSR formt die Lautstärke unabhängig vom MIDI-Note-Off.
- Maximal 4 überlappende Voices pro Track, damit schnelle Retrigger und Ausklänge nicht hart abgeschnitten werden.
- Bei Voice-Limit älteste Voice derselben Spur kontrolliert stehlen.
- Neue Voice startet am Sample-Anfang; Sample-Ende beendet sie automatisch.

### 4.4 Pitch, Envelope und Filter

- Pitch-Faktor korrekt aus Semitones und Cents berechnen; Änderungen während einer Voice glätten.
- Amp-ADSR samplegenau, ohne blockierende Operationen.
- V1-Filter: resonanzfähiger Low-Pass, bevorzugt TPT State Variable Filter (SVF), pro Voice mit eigenem Filterzustand.
- Cutoff und Resonance glätten.
- Filter-Envelope nutzt in V1 die Amp-Envelope als Modulationsquelle; Filter-Envelope-Amount bestimmt die Stärke der Cutoff-Modulation. Separates Filter-ADSR und weitere Filtertypen sind nicht V1.
- Lineare Interpolation ist für V1 zulässig; höhere Qualität später nach Messung ergänzen.

### 4.5 Mixer und Trigger

- Track-Gain, Pan und Master-Gain; Stereo-Summierung ohne unkontrolliertes Clipping.
- Softclip/Safety-Limiter nur als Schutz, nicht als dauerhaft starke Klangbearbeitung.
- Hörbar sprunghafte Änderungen (Gain, Pan, Pitch, Filter) glätten.
- DAW-MIDI-Note-Input zusätzlich zur internen Sequencer-Wiedergabe: konfigurierbare Zuordnung von acht Notennummern zu Tracks; Startbelegung 36–43, in GUI sichtbar und änderbar.
- Note-On triggert samplegenau anhand des MIDI-Event-Offsets; Velocity skaliert Trigger-Lautstärke. Note-Off beendet One-Shot nicht.

## 5. Step-Sequencer und Host-Synchronisation

### 5.1 Pattern und Umfang

Ein Pattern mit 8 Tracks × 16 Steps. Jeder Step speichert On/Off und Velocity (Startwert 100); Velocity kann in der GUI geändert werden. Globaler Swing. Keine Pattern-Ketten, Song-Modus, Polymeter, Probability, Ratchets oder Flam-Generator in V1.

Ein Step = Sechzehntelnote; 16 Steps = vier Viertelnoten. Das Pattern ist damit in 4/4 ein Takt. In anderen Taktarten bleibt es ein 16-Step-Zyklus; keine automatische Streckung auf die Taktlänge.

### 5.2 Host-Playhead

Im processBlock() AudioProcessor::getPlayHead() und die aktuelle PositionInfo-API verwenden. Nur vorhandene Werte auslesen; insbesondere Playing, BPM und PPQ können fehlen. Playhead ausschließlich im Audio-Thread während des Callback-Aufrufs abfragen und Pointer/Positionsobjekte nicht dauerhaft halten.

Fehlen erforderliche Transport-/Zeitdaten, darf die Engine keine erfundenen Defaults wie 120 BPM oder PPQ 0 verwenden. Sequencer bleibt gestoppt und GUI zeigt „Host-Synchronisation nicht verfügbar“. Dieses Verhalten muss im Audiotool-Bridge-Test nachgewiesen werden.

### 5.3 Samplegenaues Timing

- Sequenzphase aus absoluter Host-PPQ-Position berechnen, nicht aus einem nur intern hochzählenden Step-Zähler.
- Step-Grenzen innerhalb eines Audio-Blocks bestimmen und Ereignisse am exakten Sample-Offset triggern; nicht pauschal an den Blockanfang verschieben.
- DAW-MIDI-Noten ebenfalls anhand ihrer Event-Offsets verarbeiten.
- Variable Blockgrößen unterstützen; Tempoänderungen zwischen Blöcken berücksichtigen.
- Ereignisse in halboffenen Blockintervallen [blockStartPPQ, blockEndPPQ) verarbeiten, damit Blockgrenzen weder doppelte noch verlorene Trigger verursachen.

Bei innerhalb eines Blocks konstantem Tempo:
```
blockEndPPQ = blockStartPPQ + numSamples * bpm / (60 * sampleRate)
sampleOffset = round((eventPPQ - blockStartPPQ) * 60 * sampleRate / bpm)
```
Offset auf 0..numSamples-1 begrenzen und die Zuordnung an Blockgrenzen konsistent halten. Keine feste Audio-Blockgröße voraussetzen.

### 5.4 Swing

Global 50–75 %; 50 % = gerade 16tel. Swing verzögert den zweiten Step jedes 16tel-Paares, ohne Reihenfolge oder nächsten Paarbeginn zu überschreiten. Berechnung in PPQ:
- Ein Paar umfasst 0,5 PPQ.
- Bei Swing 50 % liegt der zweite Step bei pairStart + 0,25 PPQ.
- Bei Swing 75 % liegt er bei pairStart + 0,375 PPQ.
- Der erste Step des Paares bleibt unverändert.

Damit werden die Steps 2, 4, 6, 8, 10, 12, 14 und 16 verzögert. Swing anschließend in Sample-Offsets umrechnen.

### 5.5 Transport und Live-Editing

Behandeln und testen: Start, Stop, Seek, Host-Loop, Tempoänderung, variable Blockgröße und Plugin-Neuinitialisierung während der Wiedergabe. Bei Seek/Loop Sequenzphase aus Host-PPQ neu bestimmen; keinen veralteten Step-Zähler fortsetzen. Live-Step-Änderungen aus GUI und MIDImix thread-sicher übernehmen und spätestens ab dem nächsten Audio-Block wirksam machen. Laufende Voices dürfen durch Pattern-Editing nicht beschädigt werden. GUI und LEDs müssen den geänderten Zustand synchron anzeigen.

## 6. Parameter, State und Presets

### 6.1 Automatisierbare Parameter

Alle kontinuierlichen Klangparameter als stabile JUCE-Parameter über AudioProcessorValueTreeState führen. Pro Track mindestens:
`trackNN.gain`, `trackNN.pan`, `trackNN.pitch`, `trackNN.pitchFine`, `trackNN.ampAttack`, `trackNN.ampDecay`, `trackNN.ampSustain`, `trackNN.ampRelease`, `trackNN.filterCutoff`, `trackNN.filterResonance`, `trackNN.filterEnvAmount` (NN = 01–08). Dazu `master.gain` und `sequencer.swing`.

Parameter-IDs, Bereiche, Skalen und Defaultwerte zentral definieren. IDs nach Veröffentlichung nicht willkürlich umbenennen. GUI, MIDImix und DAW-Automation müssen dieselbe Parameterquelle verwenden.

### 6.2 Nicht-Parameter-State

Versioniert speichern: 8 × 16 Step-Zustände und Velocity, Sample-Pfade/Metadaten, MIDImix-Auswahl, MIDI-Learn-Mapping, ausgewählte Spur, aktive Parameterseite, Note-to-Track-Zuordnung sowie notwendige GUI-/Sequencer-Einstellungen.

### 6.3 Presets und Restore

Zwei Preset-Arten:
1. Drum-Kit: acht Sample-Zuordnungen und Klangparameter.
2. Pattern: 8 × 16 Steps, Velocity und Swing.

Der vollständige DAW-Plugin-State speichert Kit, Pattern, Parameter, Hardware-Auswahl und Mapping gemeinsam. Format mit Versionsnummer. Unbekannte/fehlende Felder dürfen keinen Absturz verursachen. Fehlende Samples als fehlend anzeigen, keine Ersatzsamples laden. Fehlendes MIDI-Gerät darf Plugin-Laden und Audio nicht verhindern; nur das zuvor ausgewählte Gerät erneut suchen.

## 7. GUI und Bedienbarkeit

### 7.1 Hauptbereiche

1. Header: Plugin-Name, Host-Sync-/Transportstatus, BPM/PPQ sofern vorhanden, MIDImix-Status.
2. Sequencer: große 8 × 16 Step-Matrix, Track-Namen und Step-Nummern.
3. Tracks/Samples: Sample laden/entfernen, Dateiname, Lade-/Fehlerzustand, Spurwahl.
4. Sound: Gain, Pan, Pitch, Amp-ADSR und Filterparameter der ausgewählten Spur; zusätzlich übersichtliche 8-Spur-Mixeransicht.
5. MIDImix: Geräteauswahl, Connect/Disconnect, Rescan, MIDI Learn und aktive Parameterseite.
6. Presets: Kit und Pattern getrennt laden/speichern.

### 7.2 Lesbarkeit

- Hoher Kontrast; Zustände nie ausschließlich durch Farbe unterscheiden.
- Standardtexte gut lesbar (Zielbereich mindestens 14–16 px, wichtige Statuswerte größer); keine winzigen Labels.
- Große, klar unterscheidbare Step-Zellen; Mindestfenstergröße so wählen, dass 8 × 16 Matrix ohne Gedränge lesbar ist.
- GUI skalierbar; ausgewählte Spur, aktive Parameterseite, Playhead, Host-Sync und Hardwarestatus klar hervorheben.
- Bedienelemente mit verständlichen Namen und Tooltips.
- Fehlende Samples, Gerätefehler und fehlende Host-Zeit als Text melden.
- Step On/Off, ausgewählter Track und Playhead müssen visuell klar voneinander unterscheidbar sein.

### 7.3 Bedienverhalten

Klick auf Step-Zelle toggelt On/Off. Velocity ist pro Step über eine verständliche GUI-Funktion editierbar. Änderungen aus GUI und MIDImix sofort synchronisieren. MIDI-Hardware ist kein Pflichtbestandteil für Bedienung oder Projektwiederherstellung.

## 8. Entwicklungsphasen und verbindliche Gates

Jede Phase endet mit Build, Tests, dokumentiertem Ergebnis und erfülltem Exit-Kriterium. Fehlende Hardware-/Host-Ergebnisse dürfen nicht durch Annahmen ersetzt werden.

### Phase 0 – Repository, Hardware und Host verifizieren (noch kein Produktcode)

| ID | Aufgabe | Ergebnis |
|---|---|---|
| 0.1 | Repository, vorhandene docs/, MIDI-Maps, Research-Dateien, Builds und offene Entscheidungen prüfen. | docs/00_project_audit.md |
| 0.2 | JUCE/CMake/Xcode, macOS Target, C++-Standard, JUCE-Version und Lizenz-/Codesign-Fragen klären. | docs/01_build_environment.md |
| 0.3 | Jedes MIDImix-Element aufnehmen: Nachrichtentyp, Kanal, Nummer, Wert/Velocity, Press/Release und Zeitfolge. Rohdaten sichern. | research/midimix_capture.md + Capture |
| 0.4 | MUTE, REC ARM, SOLO, SOLO+MUTE, BANK LEFT/RIGHT und SEND ALL einzeln/kombiniert testen. | verifiziertes Mapping |
| 0.5 | Jede MUTE-/REC-ARM-LED separat ansteuern; Input- und Output-IDs dokumentieren. | research/midimix_led_test.md |
| 0.6 | Minimalen JUCE-VST3-Prototyp erstellen, MIDI-Geräte auflisten und MIDImix-In/Out öffnen. | research/coremidi_plugin_probe.md |
| 0.7 | Im Audiotool-Bridge-Prozess CoreMIDI-In/Out, USB-Disconnect/Reconnect und Fehler/Berechtigungen testen. | Bridge-CoreMIDI-Bericht |
| 0.8 | Host Playing/Stop, BPM, PPQ, Seek, Loop, Tempoänderung und Blockgröße untersuchen. | Bridge-Transport-Bericht |
| 0.9 | VST3-State-Roundtrip in Audiotool testen: ändern, Projekt speichern, schließen, neu laden. | State-Testbericht |
| 0.10 | Sample-Rate und reale Blockgrößen (einschließlich 128 Samples, soweit verfügbar) dokumentieren. | Host-Testdaten |

Exit Phase 0: Reales Mapping liegt vor; SOLO+MUTE funktioniert oder eine Ersatzbelegung ist beschlossen; MIDI-In/Out funktionieren aus dem Plugin-Prototyp in Audiotool Bridge; Transportdaten sind ausreichend oder Einschränkungen dokumentiert; State-Roundtrip grundsätzlich getestet. Ohne funktionierenden CoreMIDI-Zugriff darf Hardware-Integration nicht als machbar behauptet werden. Ohne verlässliche PPQ-/Tempo-Daten darf Host-Sync nicht als fertig gelten.

### Phase 1 – JUCE-Projekt und Grundgerüst
- VST3-Instrument mit PluginProcessor/PluginEditor, Stereo-Out und MIDI-In erstellen.
- Stabile APVTS-Parameter für acht Tracks und Master definieren.
- Versioniertes State-Grundgerüst und Tests einrichten.
- Minimale GUI mit Header, Tracks, Step-Matrix und Statusbereichen erstellen.
- Reproduzierbare Build-/Test-Skripte ergänzen.

Exit: Plugin lädt in Audiotool Bridge und Diagnose-Host; Parameter können geändert, automatisiert und gespeichert werden.

### Phase 2 – Sampler-Core
- WAV-Loader, Metadaten und asynchrones Laden implementieren.
- Acht Track-Slots und sichere Sample-Übergabe erstellen.
- One-Shot-Voices, max. vier Voices/Track, Pitch, Amp-ADSR, TPT-SVF, Gain/Pan/Master und Smoothing implementieren.
- DAW-MIDI-Noten-Trigger mit konfigurierbarem Note-Mapping und samplegenauen Offsets ergänzen.
- Fehlende/ungültige Sample-Dateien robust behandeln.

Exit: Acht Samples unabhängig laden und über GUI oder DAW-MIDI abspielen; keine Dateioperation im Audio-Thread.

### Phase 3 – Sequencer-Engine
- Pattern 8 × 16 mit Step-Velocity implementieren.
- Host-PositionInfo und fehlende Werte erkennen.
- PPQ-16tel-Berechnung, samplegenaue Block-Offsets und Swing ergänzen.
- Start/Stop, Seek, Loop, Tempoänderung und variable Blockgrößen korrekt behandeln.
- Pattern-Snapshots/Commands thread-sicher übergeben; Playhead in GUI anzeigen.

Exit: Pattern läuft in Audiotool host-synchron; keine doppelten/verschluckten Steps bei Blockgrenzen, Seek, Loop oder Tempoänderungen.

### Phase 4 – MIDImix-Eingabe und Steuerung
- Geräteauswahl, Öffnen/Schließen, Disconnect und Exklusivbesitz implementieren.
- Parser mit verifizierten Captures und Unit-Tests erstellen.
- MIDI Learn und speicherbare Mappings ergänzen.
- Step-Tasten, Spurwahl, Bankseiten, Fader, Regler und Soft Takeover implementieren.
- Hardware- und GUI-Zustände synchron halten.

Exit: MIDImix wird im Plugin gewählt und steuert Steps, Tracks und definierte Parameter ohne DAW-MIDI-Routing.

### Phase 5 – LED-Feedback
- LedDriver mit verifizierten Output-IDs implementieren.
- Step-Zustand beim Editieren und Trackwechsel ausgeben.
- Playhead-Hervorhebung samt Wiederherstellung des Pattern-Zustands ergänzen.
- Output-Queue, Disconnect und Fehlerfälle behandeln.

Exit: Hardware zeigt die ausgewählte Spur korrekt, soweit durch Phase 0 bestätigt; LED-Ausfall beeinträchtigt das Instrument nicht.

### Phase 6 – Presets und GUI vervollständigen
- Drum-Kit- und Pattern-Presets sowie kompletten Plugin-State implementieren.
- Fehlende Samples und Neuverknüpfung unterstützen.
- MIDI-Learn-GUI, Step-Matrix, Sample- und Parameteransichten finalisieren.
- Copy/Paste/Clear für Pattern ergänzen; Undo/Redo für GUI-/Pattern-Änderungen, sofern State-Modell stabil ist.

Exit: Projekt, Kits, Pattern, Sample-Pfade und MIDImix-Mapping werden konsistent gespeichert und wiederhergestellt.

### Phase 7 – Release-Qualität
- Audio-Thread-Safety-Review und verfügbare ASan/TSan-/Sanitizer-Läufe.
- CPU-/Speicherprofiling mit acht Tracks und maximaler Voice-Anzahl; Testbedingungen dokumentieren.
- Audiotool-Bridge-Abnahme auf Apple Silicon; zweiter VST3-Host (z. B. Reaper) als Diagnosetest.
- Codesignierung, Installationsskript und kurze Bedienungsanleitung/Cheat-Sheet.

Exit: Release-Build funktioniert in Audiotool; Audio-, Sequencer-, State- und Hardware-Tests bestehen; Disconnect/Reconnect bleibt stabil.

## 9. Verbindliche Test- und Abnahmestrategie

### 9.1 Unit-Tests
MIDI-Parser mit echten Captures; Note-On/Off und Velocity 0; SOLO-Hold/Spurwahl; Step 1–16; Bankseiten; MIDI Learn und Pickup; Pattern/Velocity; PPQ und Swing; Blockgrenzen/variable Blockgrößen; Seek/Loop/Tempoänderungen; Pitch/Sample-Rate; ADSR/Filter; State Save/Load und Versionsmigration.

### 9.2 Audio-Tests
44,1/48/96 kHz; mehrere Blockgrößen einschließlich 128 Samples; Mono-/Stereo-, kurze und lange Samples; vier Voices pro Track; gleichzeitige Tracks; Parameteränderung während Wiedergabe; Sample-Wechsel während Wiedergabe; leere/fehlende Dateien.

### 9.3 Hardware-Tests
24 Drehregler; acht Kanal-Fader plus Master; alle 16 Step-Tasten; SOLO allein und mit jeder MUTE-Taste; Bank-Tasten; SEND ALL; beide LED-Reihen; USB-Disconnect während Playback und Reconnect; zweite Plugin-Instanz; abweichendes MIDImix-Editor-Preset.

### 9.4 Audiotool-Abnahme
VST Bridge laden; MIDImix auswählen/verbinden; Start/Stop; Tempo; Seek; Loop; Pattern-Editing während Wiedergabe; Projekt speichern/neu laden; Controller abziehen/verbinden; Projekt ohne angeschlossene MIDImix laden.

### 9.5 Release-Mindestkriterien
Keine doppelten oder übersprungenen Steps bei regulärer Host-Wiedergabe; keine blockierenden MIDI-/GUI-/Dateioperationen im Audio-Thread; keine Parametersprünge beim Verbinden/Pickup; kein Absturz bei fehlendem Gerät/Sample; Plugin ohne Hardware vollständig bedienbar; State nach Save/Load konsistent.

## 10. Repository-Zielstruktur

```
/workspace
├── CMakeLists.txt
├── README.md
├── docs/                 # Audit, Build, Produkt, Mapping, Audio, Sync, Testbericht
├── research/             # MIDImix-Captures, LED-Test, CoreMIDI-Probe, Bridge-Tests
├── Source/
│   ├── PluginProcessor.h/.cpp
│   ├── PluginEditor.h/.cpp
│   ├── Parameters/
│   ├── Sampler/
│   ├── Sequencer/
│   ├── MidiMix/
│   ├── State/
│   └── UI/
├── Tests/                # MIDI, Pattern, Timing, Sampler, State
└── tools/                # build.sh, test.sh
```

Struktur darf an JUCE-Konventionen angepasst werden, muss aber Sampler, Sequencer, MIDImix, State und UI getrennt halten.

## 11. Verbindliche Arbeitsregeln für die Coding-KI

1. Vor Coding Repository und vorhandene Dokumentation vollständig untersuchen; nichts blind überschreiben.
2. Phase 0 zuerst ausführen. Keine Annahmen zu MIDImix-IDs, LED-IDs, CoreMIDI in der Bridge oder Host-Transport treffen.
3. Nicht verifizierte Protokollwerte mit UNVERIFIED markieren und nicht als Produktionsmapping verwenden.
4. Phasen nacheinander umsetzen; vor Phasenwechsel Build, Tests und Exit-Kriterien prüfen.
5. Kleine, verantwortungsgetrennte Dateien/Klassen bevorzugen; keine riesigen Sammeldateien.
6. Parser-, Timing-, DSP- und State-Logik mit automatisierten Tests absichern.
7. Keine stillen Platzhalter oder erfundenen Defaults. Nicht verfügbare Funktionen/Host-Daten sichtbar melden.
8. Audio-Thread-Regeln aus Abschnitt 2.3 strikt einhalten.
9. Hardware-MIDI-In und LED-Out getrennt behandeln; IDs können unterschiedlich sein.
10. Kein SysEx und keine Features außerhalb des V1-Umfangs ohne ausdrückliche Freigabe.
11. Nach jeder Phase geänderte Dateien, Entscheidungen, Test-/Build-Ergebnisse, offene Risiken und nächsten Schritt dokumentieren.
12. Bei nicht bestandenem Gate stoppen, Fehler reproduzierbar dokumentieren und keine spekulative Implementierung fortsetzen.

Definition of Done je Aufgabe: Implementierung vorhanden, passende Tests erfolgreich, Plugin baut, externe Funktionen bei Bedarf im echten Host/auf Hardware geprüft, keine neue ungeklärte Annahme und Dokumentation aktualisiert.

## 12. Risiken und Gegenmaßnahmen

| Risiko | Gegenmaßnahme |
|---|---|
| CoreMIDI innerhalb Audiotool Bridge nicht verfügbar | JUCE-VST3-Prototyp in Phase 0; ohne Nachweis keine Hardware-Implementierung freigeben |
| Host liefert kein PPQ/Tempo | Bridge-Test; klarer Sync-Fehler statt erfundener Defaults |
| MIDImix-Mapping geändert | reale Captures und MIDI Learn |
| LED-Protokoll anders als Input-Mapping | separater LED-Test; GUI bleibt vollständige Anzeige |
| mehrere Plugin-Instanzen | exklusiver Hardware-Eigentümer |
| Sample-Datei fehlt | State-Pfadprüfung und manuelle Neuverknüpfung |
| CPU-Spitzen durch Voices | festes Voice-Limit, Profiling und Stress-Tests |
| JUCE-Lizenz/Distribution | Lizenzmodell vor Release prüfen und dokumentieren |

## 13. Technische Referenzen

Als Recherchequellen verwenden, nicht als Ersatz für Tests in der realen Zielumgebung:
- Audiotool VST Bridge: https://www.audiotool.com/help/manuals/vst-bridge.html
- JUCE AudioProcessor: https://docs.juce.com/master/classjuce_1_1AudioProcessor.html
- JUCE AudioPlayHead: https://docs.juce.com/master/classjuce_1_1AudioPlayHead.html
- JUCE MidiInput: https://docs.juce.com/master/classjuce_1_1MidiInput.html
- JUCE MidiOutput: https://docs.juce.com/master/classjuce_1_1MidiOutput.html
- JUCE AudioProcessorValueTreeState: https://docs.juce.com/master/classjuce_1_1AudioProcessorValueTreeState.html
- Akai MIDImix User Guide: https://cdn.inmusicbrands.com/akai/attachments/MIDIMIX/MIDImix-UserGuide-v1.0.pdf
- Akai MIDImix Editor: https://support.akaipro.com/en/support/solutions/articles/69000856691-akai-pro-midimix-using-the-editor-for-customisation

Die exakten MIDI-IDs, LED-Kommandos und das Verhalten innerhalb Audiotool Bridge sind nicht allein aus diesen Referenzen abzuleiten. Sie müssen in Phase 0 mit der tatsächlich verwendeten Hardware und Host-Version geprüft werden.

## 14. Endgültige V1-Abnahme

- [ ] VST3-Instrument lädt in Audiotool über die VST Bridge.
- [ ] MIDImix kann im Plugin ausgewählt und direkt verbunden werden.
- [ ] Acht Drum-Samples lassen sich laden und wiedergeben.
- [ ] Jede Spur besitzt eine eigene 16-Step-Reihe.
- [ ] MUTE/REC ARM schalten Steps 1–16 der ausgewählten Spur.
- [ ] SOLO+MUTE oder die in Phase 0 beschlossene Ersatzbelegung wählt alle acht Spuren.
- [ ] BANK LEFT/RIGHT wechselt Seiten A/B/C.
- [ ] Fader und Drehregler steuern Parameter ohne unerwünschte Sprünge.
- [ ] Sequencer folgt Host-Start, Stop, BPM und PPQ; Seek/Loop/Tempoänderungen sind getestet.
- [ ] Step-Trigger erfolgen samplegenau innerhalb des Audio-Blocks.
- [ ] LEDs zeigen den Pattern-Zustand, soweit Hardware-Tests dies bestätigen.
- [ ] GUI ist ohne MIDImix vollständig bedienbar.
- [ ] Kit, Pattern, Mapping und Plugin-State lassen sich speichern und laden.
- [ ] Fehlendes Gerät oder Sample führt nicht zum Absturz.
- [ ] Audio-Thread-, Stress- und Release-Tests sind bestanden.

V1-Ergebnis: Ein eigenständig nutzbares 8-Spur-VST3-Drum-Sampler-Instrument mit integriertem, host-synchronisiertem 16-Step-Sequencer und direkt im Plugin auswählbarer Akai MIDImix.
