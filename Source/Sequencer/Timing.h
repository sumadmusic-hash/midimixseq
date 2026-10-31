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

#include <atomic>
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

// Scan-Kosten-Deckel fuer collectStepOffsetsInBlock (P3). Exportiert, damit Tests
// und Aufrufer das Limit pruefen koennen.
inline constexpr long long kMaxStepsPerBlockScan = 4096;

// Zaehlt Audio-Block-Scans, bei denen der Deckel erreicht wurde (verlorene Steps
// moeglich). Atomar, relaxed fetch_add, kein Assert, keine Schleifenverlaengerung.
inline std::atomic<unsigned long long>& scanLimitCounter()
{
    static std::atomic<unsigned long long> c{ 0 };
    return c;
}
inline unsigned long long scanLimitTruncations() noexcept
{
    return scanLimitCounter().load(std::memory_order_relaxed);
}
inline void resetScanLimitTruncations() noexcept
{
    scanLimitCounter().store(0, std::memory_order_relaxed);
}

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

// Diagnose-Zaehler (P3): Anzahl der Ereignisse, die wegen Rounding hinter das
// Blockende dem naechsten Block ueberlassen wurden. Atomar; im Audio-Pfad ein
// einziger relaxed fetch_add pro betroffenem Ereignis. Tests lesen und setzen ihn.
inline std::atomic<unsigned long long>& deferredToNextBlockCounter()
{
    static std::atomic<unsigned long long> counter{ 0 };
    return counter;
}
inline unsigned long long deferredToNextBlockCount() noexcept
{
    return deferredToNextBlockCounter().load(std::memory_order_relaxed);
}
inline void resetDeferredToNextBlockCount() noexcept
{
    deferredToNextBlockCounter().store(0, std::memory_order_relaxed);
}

// sampleOffset = round((eventPPQ - blockStartPPQ) * 60 * sampleRate / bpm)
// Gueltige Offsets liegen in [0, numSamples-1] (Plan 5.3 / Regel R21). Rueckgabe leeres
// Optional, wenn das Ereignis nicht innerhalb des halboffenen Blocks
// [blockStartPPQ, blockEndPPQ) liegt oder Daten fehlen.
//
// RUNDERUNG (Stabilitaetsrunde 2026-10-31): Es wird std::llround() verwendet –
// Round-to-nearest, Halbwerte von null WEG. Damit ist der Referenzfall
// 0.25 PPQ * 22050 Samples/PPQ = 5512.5 -> Offset 5513 deterministisch.
// Vergleich: std::nearbyint()/std::lrint() folgen dem aktuellen Rundungsmodus
// (ueblicherweise nearest-even -> 5512) und sind fuer exakte Halbwerte damit NICHT
// modus-unabhaengig deterministisch; std::round() liefert denselben Wert wie
// llround, aber als double mit anschließender Konvertierung. llround ist die
// tatsächlich implementierte, dokumentierte Wahl.
//
// Randfall-Korrekturen (Audit 2026-10-31 / Stabilitaetsrunde):
//  1. Kein Stillschweigen bei fehlenden Host-Daten: numSamples <= 0 ist ein
//     Konfigurationsfehler und wird per assert markiert (Release: "kein Trigger").
//     Die uebrigen Invaliditaetsgruende (bpm/sampleRate <= 0, nicht endliches startPPQ)
//     sind legitime "Host liefert keine Sync-Daten"-Faelle im Sinne von Regel R7 und
//     loesen kein assert aus.
//  2. Nach-Kanten-Rounding: Ein Ereignis innerhalb [start,end), dessen Rundung hinter
//     das letzte Sample (>= numSamples) faellt, wuerde beim Clamping auf numSamples-1
//     einen Doppeltrigger am Blockende erzeugen; es wird dem naechsten Block
//     ueberlassen. VERLUSTFREIHEIT: Der naechste Block beginnt host-seitig exakt bei
//     endPPQ (Positionsfortschreibung startPPQ(b+1) := endPPQ(b)). Liegt das Ereignis
//     formal noch vor dieser Grenze, liegt es im naechsten Block und wird dort ueber
//     seinen negativen relativen Offset an Offset 0 ausgeloeset (Fall 4). Liegt es auf
//     oder hinter der Grenze, gehoert es ohnehin in den naechsten Block. In beiden
//     Faellen triggert es genau einmal.
//     DIAGNOSE (P3): Dieser Fall war bisher als assert(false) ausgefuehrt – bei
//     Blockgroessen unter einer Schrittweite (z. B. Testblockgroesse 1) haette jeder
//     Debug-Lauf das Programm abgebrochen, obwohl das Verhalten definiert und
//     verlustfrei ist. Er zaehlt jetzt den Diagnose-Zaehler deferredToNextBlockCount()
//     und liefert nullopt ohne Assert.
//  3. Vor-Kanten-Rounding: Ein Ereignis, das formal erst im NAECHESTEN Block liegt
//     (eventPPQ >= endPPQ), darf durch Rundung nicht auf den letzten Sample-Offset
//     DIESES Blocks fallen – derselbe Doppeltrigger. Solche Ereignisse werden
//     abgewiesen; der naechste Block triggert sie an Offset 0 bzw. frueh.
//  4. Unterlauf-Rounding (neu, ergaenzt zu Fall 2): Ein Ereignis innerhalb
//     [start,end), das wegen float-Drift negativ rundet, wird auf Offset 0 gehoben –
//     gueltig, da eventPPQ >= startPPQ.
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
    const double rel = (eventPPQ - p.startPPQ) * sppq;
    long long off = std::llround(rel); // siehe RUNDERUNG-Kommentar oben
    if (off > static_cast<long long>(p.numSamples) - 1)
    {
        // Definiert und verlustfrei: Ereignis gehoert in den naechsten Block
        // (Fall 2). Kein Assert, kein Clamping auf numSamples-1 (Doppeltrigger-Schutz).
        deferredToNextBlockCounter().fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }
    if (off < 0)
        off = 0; // Rundung auf Blockanfang; gueltig, da eventPPQ >= startPPQ
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
    // Kosten-Deckel gegen pathologische Eingaben (z. B. Host mit extrem hoher BPM oder
    // defektem startPPQ): verhindert einen praktisch endlosen Loop im Audio-Thread.
    // Steps, die nicht geprueft werden koennen, gehen verloren statt den Thread zu
    // blockieren. Der Verlust ist DIAGNOSTIZIERBAR: kMaxStepsPerBlockScan ist
    // exportiert, und beim Erreichen des Limits wird scanLimitTruncations() erhoeht.
    // Ein Host, dessen Block mehr als kMaxStepsPerBlockScan Steps enthaelt, ist
    // Konfigurationsfehler bzw. Pathologie – die Zahl muss sichtbar bleiben.
    if (i1 - i0 >= kMaxStepsPerBlockScan)
    {
        i1 = i0 + kMaxStepsPerBlockScan - 1;
        scanLimitCounter().fetch_add(1, std::memory_order_relaxed);
    }

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
