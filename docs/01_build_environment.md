# 0.2 Build-Umgebung und Toolchain-Entscheidungen (Phase 0)

Datum: 2026-10-31 · Status: **teilweise geklärt, Rest `UNVERIFIED` (kein macOS verfügbar)**

## 1. Zielplattform (Zielsetzung des Plans)

| Punkt | Festlegung | Status |
|---|---|---|
| Plattform | macOS / Apple Silicon, natives ARM64 als erstes Ziel | geplant |
| Universal Binary | nur ergänzend, wenn V1 nicht verzögert | optional |
| Plugin-Typ | ausschließlich VST3-Instrument, Stereo-Out, MIDI-In aktiv | geplant |
| Primärer Host | Audiotool über lokale VST Bridge | geplant |
| Deployment Target | macOS 12.0 oder höher (Diskussion: VST3-Support der Bridge) | **UNVERIFIED** – mit Bridge-Version abgleichen |
| Xcode / SDK | aktuelle stabile Xcode-Version + neuestes macOS-SDK | **UNVERIFIED** – vor Phase 1 festhalten |
| Codesignierung | Ad-hoc-Signatur für Tests; Developer-ID + Notarization für Release | **UNVERIFIED** – Account vorhanden? |

## 2. Framework und Versionierung

| Punkt | Festlegung | Status |
|---|---|---|
| Framework | JUCE, CMake-Build (`juce_add_plugin`) | geplant |
| JUCE-Version | **fest pinnen** auf ein Tag (Empfehlung: latest stable 8.x zum Zeitpunkt von Phase 1). Pin erfolgt als Git-Submodule/`FetchContent`-TAG in `cmake/JUCE.cmake`. Der konkrete Tag wird erst in Phase 1 gesetzt und hier nachgetragen. | offen (Blocker B4) |
| C++-Standard | C++20 (JUCE 8 unterstützt C++17/20; 20 für `std::atomic`-Referenzsemantik und durchgängige `constexpr`-Tabellen) | Entscheidungsvorschlag, zu bestätigen |
| CMake | ≥ 3.22 erforderlich für moderne JUCE-CMake-Integration | lokal getestet: 3.25.1 |
| Compiler (macOS) | Apple-Clang aus gepinnter Xcode-Version | **UNVERIFIED** |
| Lizenz | JUCE-Lizenzmodell vor Release prüfen (ISC vs. GPL vs. kommerziell) | offen (Blocker B6) |

Pinning-Regel: Die gewählte JUCE-Revision wird als Commit-/Tag-Hash in
`cmake/JUCE.cmake` dokumentiert und darf während V1 nicht still aktualisiert werden.

## 3. Real geprüfte Umgebung (diese Sitzung)

```
$ uname -a
Linux c-6ae50e74-01b29cc4-04253eff1627 4.19.91-c8dfc93.al7.x86_64 x86_64 GNU/Linux
$ cmake --version      → cmake version 3.25.1
$ g++ --version        → g++ (Debian 12.2.0-14+deb12u1) 12.2.0
$ python3 --version    → Python 3.12.10
```

Verfügbar: Linux, CMake 3.25.1, GCC 12.2, Python 3.12.
**Nicht verfügbar:** macOS, Xcode, Apple-Clang, CoreAudio, CoreMIDI, VST3-Host, Audiotool Bridge, MIDImix-Hardware.

Daraus folgt: Ein VST3-Build ist in dieser Umgebung **nicht möglich und wurde nicht behauptet**.
Was hier geprüft werden kann, sind die hardware-/hostfreien Kernlogiken (Timing, PPQ, Swing,
Pattern, Parser) als kompilierbare Unit-Tests mit GCC/CMake. Diese Tests sind unter
`Tests/` eingerichtet und laufen mit `tools/test.sh`.

## 4. Geplante Build-Anordnung (Phase 1, noch nicht ausgeführt)

```
CMakeLists.txt
├── add_subdirectory(cmake/JUCE)              # gepinnte JUCE-Version
├── juce_add_plugin(MIDImixDrumSampler)       # VST3 only, isSynth, Stereo, MIDI-In
│     └── Source/{PluginProcessor,PluginEditor,Parameters,Sampler,Sequencer,MidiMix,State,UI}
├── add_executable(MIDImixCoreTests Tests/…)  # core-only, ohne JUCE, läuft auf CI/Linux
└── tools/check_phase0.sh                     # Gate vor jedem Produktbau
```

Build-Kommandos (macOS, nach Gate):
```
./tools/build.sh configure   # cmake -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES=arm64
./tools/build.sh plugin      # cmake --build build --config Release --target MIDImixDrumSampler
./tools/build.sh vst3validator
```

Diagnose-Hosts für Abnahme: Audiotool VST Bridge (primär), zusätzlich Reaper arm64 als
zweiter Host (Planabschnitt 8, Phase 7).

## 5. Konsequenz für das Phase-0-Gate

Abschnitt 0.2 ist nur vollständig bestanden, wenn auf dem Zielrechner dokumentiert sind:
exakte Xcode-Version, SDK-Build, Deployment Target, ARM64-Buildnachweis eines minimalen
JUCE-VST3-Protyps, Codesign-Ergebnis und Lizenzentscheidung. Bis dahin bleibt 0.2
**NICHT BESTANDEN** und alle genannten Werte bleiben `UNVERIFIED`.
