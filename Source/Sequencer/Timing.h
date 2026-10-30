// Source/Sequencer/Timing.h
// Host-synchronisierte Timing-Berechnung nach Planabschnitt 5.3 / 5.4.
// Reine, JUCE-freie Kernlogik -> in Phase 0 als Unit-Test prüfbar.
//
// WICHTIG: Diese Datei ist KEIN Produktcode für Sampler/GUI/MIDImix, sondern die
// isolierte, hardwarefreie Mathematik des Sequencers. Sie enthält keine erfundenen
// Defaults: bpm <= 0 oder fehlende Position liefern "kein Ereignis".
//
// Echtzeit-Hinweis (Audit): Alle Funktionen sind frei von Heap-Allokation, Sperr-
// operationen und Schleifen mit variabler, ungebundener Laufzeit (der Step-Scan ist
// gedeckelt). Der Aufrufer muss Blockpositionen aus einem einzigen Host-Query füllen;
// Start-PPQ und Sampleanzahl dürfen nicht aus unterschiedlichen Quellen vermischt
// werden, da andernfalls Inkonsistenzen innerhalb eines Blocks möglich sind.
#pragma once

#include <cassert>
#include <cmath>
#include <cstdint>
#include <optional>

namespace mds::timing
{

// Eine Step-Position = eine 16tel-Note = 0.25 PPQ (Plan 5.1).
inline constexpr double kPpqPerStep = 0.25;
inline constexpr int kNumSteps = 16;
inline constexpr int kNumTracks = 8;

struct BlockPosition
{
    double startPPQ = 0.0; // absolute Host-PPQ-Position bei Blockbeginn
    double bpm = 0.0;      // Host-Tempo; <= 0 bedeutet "nicht verfügbar"
    int numSamples = 0;
    double sampleRate = 0.0;
};

inline bool isValid(const BlockPosition& p)
{
    return p.bpm > 0.0 && p.sampleRate > 0.0 && p.numSamples > 0
        && std::isfinite(p.startPPQ);
}

// blockEndPPQ = blockStartPPQ + numSamples * bpm / (60 * sampleRate)   [Plan 5.3]
inline double computeBlockEndPPQ(const BlockPosition& p)
{
    if (!isValid(p))
        return p.startPPQ;
    return p.startPPQ + static_cast<double>(p.numSamples) * p.bpm / (60.0 * p.sampleRate);
}

// samplesPerPPQ = 60 * sampleRate / bpm
inline double samplesPerPPQ(double bpm, double sampleRate)
{
    if (bpm <= 0.0 || sampleRate <= 0.0)
        return 0.0;
    return 60.0 * sampleRate / bpm;
}

// sampleOffset = round((eventPPQ - blockStartPPQ) * 60 * sampleRate / bpm)
// Begrenzt auf [0, numSamples-1] (Plan 5.3 / Regel R21). Rückgabe leeres Optional, wenn
// das Ereignis nicht innerhalb des halboffenen Blocks [blockStartPPQ, blockEndPPQ) liegt
// oder Daten fehlen.
//
// Randfall-Korrekturen (Audit 2026-10-31):
//  1. Kein Stillschweigen bei fehlenden Host-Daten: numSamples <= 0 ist ein
//     Konfigurationsfehler und wird per assert markiert (Release: "kein Trigger").
//     Die übrigen Invaliditätsgründe (bpm/sampleRate <= 0, nicht endliches startPPQ)
//     sind legitime "Host liefert keine Sync-Daten"-Fälle im Sinne von Regel R7 und
//     lösen kein assert aus.
//  2. Nach-Kanten-Rounding: Ein Ereignis innerhalb [start,end), dessen Rundung hinter das
//     letzte Sample fällt, würde beim Clamping auf numSamples-1 einen Doppeltrigger am
//     Blockende erzeugen; es wird dem nächsten Block überlassen. Der Fall tritt nur bei
//     Blöcken kleiner als ca. 1 Sample pro Schrittweite auf und ist asserted.
//  3. Vor-Kanten-Rounding (neu): Symmetrisch dazu darf ein Ereignis, das formal erst im
//     NACHSTEN Block liegt (eventPPQ >= endPPQ), durch Rundung nicht auf den letzten
//     Sample-Offset DIESES Blocks fallen – das wäre exakt derselbe Doppeltrigger. Solche
//     Ereignisse werden abgewiesen; der nächste Block triggert sie an Offset 0 bzw. früh.
inline std::optional<int> sampleOffsetForEventPPQ(const BlockPosition& p, double eventPPQ)
{
    if (!isValid(p))
    {
        assert(!(p.bpm > 0.0 && p.sampleRate > 0.0 && std::isfinite(p.startPPQ) && p.numSamples <= 0)
               && "Audio-Block ohne Samples (Regel R7/R12)");
        return std::nullopt;
    }

    const double endPPQ = computeBlockEndPPQ(p);
    // halboffenes Intervall: Start inclusive, Ende exclusive -> keine Doppeltrigger
    // an Blockgrenzen (Plan 5.3).
    if (eventPPQ < p.startPPQ || eventPPQ >= endPPQ)
        return std::nullopt;

    const double sppq = samplesPerPPQ(p.bpm, p.sampleRate);
    long long off = std::llround((eventPPQ - p.startPPQ) * sppq);
    if (off > static_cast<long long>(p.numSamples) - 1)
    {
        assert(false && "Ereignis rundet hinter das Blockende -> nächster Block (Doppeltrigger-Schutz)");
        return std::nullopt;
    }
    if (off < 0)
        off = 0; // Rundung auf Blockanfang; gültig, da eventPPQ >= startPPQ
    return static_cast<int>(off);
}

// Swing: global 50..75 % (Plan 5.4). Verzögert den zweiten Step jedes 16tel-Paares.
// pairStart + 0.25 PPQ bei 50 %, pairStart + 0.375 PPQ bei 75 %.
// Schrittweite eines Paares = 0.5 PPQ; Offset = (swing-0.5) * 0.5 PPQ.
inline double swingOffsetPPQ(double swingPercent)
{
    double s = swingPercent;
    if (s < 50.0) s = 50.0;
    if (s > 75.0) s = 75.0;
    return (s - 50.0) / 100.0 * 0.5; // 0.0 .. 0.125 PPQ
}

// PPQ-Position eines Steps (0-basierter Index) innerhalb eines Pattern-Zyklus,
// inklusive Swing-Verschiebung für ungerade Indizes (also Steps 2,4,...,16).
// cycleStartPPQ: PPQ des ersten Steps des aktuellen 16-Step-Zyklus.
inline double stepPPQ(int stepIndex, double cycleStartPPQ, double swingPercent)
{
    double ppq = cycleStartPPQ + static_cast<double>(stepIndex) * kPpqPerStep;
    if ((stepIndex & 1) == 1) // zweiter Step eines Paares
        ppq += swingOffsetPPQ(swingPercent);
    return ppq;
}

// Alle Step-Grenzen, die in den halboffenen Block fallen, als Sample-Offsets.
// Liefert exakt einmal pro Step – unabhängig von der Blockgröße.
template <typename Sink>
void collectStepOffsetsInBlock(const BlockPosition& p,
                               double cycleStartPPQ,
                               double swingPercent,
                               Sink&& sink)
{
    if (!isValid(p))
        return;
    const double endPPQ = computeBlockEndPPQ(p);

    // Zu untersuchender Schrittindex-Bereich: alle Steps, deren (ggf. geswungene)
    // Position in das halboffene Intervall [blockStartPPQ, blockEndPPQ) fallen kann.
    // Der Pattern-Zyklus darf sich beliebig oft wiederholen; pro Block werden nur die
    // tatsächlich anliegenden Indizes geprüft (+ Rand für Swing und Rundung).
    const double maxSwing = swingOffsetPPQ(swingPercent);
    long long i0 = static_cast<long long>(std::floor((p.startPPQ - cycleStartPPQ - maxSwing) / kPpqPerStep));
    long long i1 = static_cast<long long>(std::ceil((endPPQ - cycleStartPPQ) / kPpqPerStep));
    if (i0 < 0) i0 = 0;
    if (i1 < i0) i1 = i0;
    // Kosten-Deckel gegen pathologische Eingaben (z. B. Host mit ppm-großer BPM oder
    // defektem startPPQ): verhindert einen praktisch endlosen Loop im Audio-Thread.
    // Steps, die nicht geprüft werden können, gehen verloren statt den Thread zu blockieren.
    constexpr long long kMaxStepsPerBlockScan = 4096;
    if (i1 - i0 > kMaxStepsPerBlockScan)
        i1 = i0 + kMaxStepsPerBlockScan;

    for (long long i = i0; i <= i1; ++i)
    {
        const int idx = static_cast<int>(((i % kNumSteps) + kNumSteps) % kNumSteps);
        const bool secondOfPair = (i & 1LL) != 0;   // Bezug auf die absolute Zyklusposition
        const double ppq = cycleStartPPQ + static_cast<double>(i) * kPpqPerStep
                         + (secondOfPair ? maxSwing : 0.0);
        if (ppq < p.startPPQ || ppq >= endPPQ)
            continue;
        if (auto off = sampleOffsetForEventPPQ(p, ppq))
            sink(idx, *off, ppq);
    }
}

} // namespace mds::timing
