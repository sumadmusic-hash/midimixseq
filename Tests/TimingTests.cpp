// Tests/TimingTests.cpp – Pflichtabdeckung T0.1 bis T0.4 (docs/03_test_strategy.md)
#include "TestFramework.h"
#include "../Source/Sequencer/Timing.h"

#include <map>
#include <vector>

using namespace mds::timing;

MDS_TEST(blockEndPPQ_formula)
{
    // 128 Samples, 44100 Hz, 120 BPM -> exakt Formel aus Plan 5.3
    BlockPosition p{ 4.0, 120.0, 128, 44100.0 };
    const double expect = 4.0 + 128.0 * 120.0 / (60.0 * 44100.0);
    CHECK_NEAR(computeBlockEndPPQ(p), expect, 1e-12);
}

MDS_TEST(sampleOffset_clamped_and_valid)
{
    // Referenzfall mit ganzzahligen Werten: 120 BPM, 44100 Hz -> 1 PPQ = 17.7 samples,
    // eine 16tel-Note (0.25 PPQ) = 4.425 samples.
    BlockPosition p{ 0.0, 120.0, 512, 44100.0 };
    CHECK_NEAR(samplesPerPPQ(p.bpm, p.sampleRate), 60.0 * 44100.0 / 120.0, 1e-9); // 22050

    auto firstStep = sampleOffsetForEventPPQ(p, 0.0);
    CHECK(firstStep.has_value());
    CHECK_EQ(*firstStep, 0);                       // Blockbeginn ist immer einschließend

    auto secondStep = sampleOffsetForEventPPQ(p, 0.25);
    CHECK(secondStep.has_value());
    CHECK_EQ(*secondStep, 4);                      // round(0.25 * 22050) = 4

    // blockEndPPQ = 512 * 120 / (60 * 44100) = 0.0232199... -> Step bei 0.03 fällt hinaus
    CHECK(!sampleOffsetForEventPPQ(p, 0.03).has_value());
    CHECK(!sampleOffsetForEventPPQ(p, 10.0).has_value());
    CHECK(!sampleOffsetForEventPPQ(p, -0.01).has_value());

    // Exakter Blockgrenzenfall: Ereignis genau auf blockEndPPQ gehört in den Folgeblock.
    const double endPPQ = computeBlockEndPPQ(p);
    CHECK(!sampleOffsetForEventPPQ(p, endPPQ).has_value());
    BlockPosition next{ endPPQ, 120.0, 512, 44100.0 };
    CHECK(sampleOffsetForEventPPQ(next, endPPQ).has_value());

    // Der Offset wird nie außerhalb des Blocks erzeugt, auch bei sehr kleinen Blöcken.
    BlockPosition tiny{ 0.0, 120.0, 3, 44100.0 };
    for (double ppq : { 0.0, 0.0005, 0.001, 0.0012 })
    {
        if (auto c = sampleOffsetForEventPPQ(tiny, ppq))
            CHECK(c.value() >= 0 && c.value() < tiny.numSamples);
    }
}

MDS_TEST(no_trigger_without_host_data)
{
    BlockPosition noBpm{ 0.0, 0.0, 512, 48000.0 };   // Host liefert kein Tempo
    CHECK(!isValid(noBpm));
    CHECK(!sampleOffsetForEventPPQ(noBpm, 0.25).has_value());

    BlockPosition noRate{ 0.0, 120.0, 512, 0.0 };
    CHECK(!sampleOffsetForEventPPQ(noRate, 0.25).has_value());

    BlockPosition empty{ 0.0, 120.0, 0, 48000.0 };
    CHECK(!sampleOffsetForEventPPQ(empty, 0.25).has_value());
}

MDS_TEST(half_open_interval_no_double_trigger_at_block_boundary)
{
    // Ein Step liegt exakt auf einer Blockgrenze. Er darf über zwei hintereinander
    // folgende Blöcke insgesamt genau einmal ausgelöst werden (Plan 5.3).
    const double bpm = 120.0, sr = 48000.0;
    const int n = 512;
    const double blockPPQ = n * bpm / (60.0 * sr); // PPQ pro Block
    const double stepPPQv = 2.0 * blockPPQ;        // ein Step genau nach 2 Blöcken

    int totalTriggers = 0;
    for (int b = 0; b < 6; ++b)
    {
        BlockPosition p{ b * blockPPQ, bpm, n, sr };
        if (sampleOffsetForEventPPQ(p, stepPPQv))
            ++totalTriggers;
    }
    CHECK_EQ(totalTriggers, 1);
}

// Zählt für jede Blockgröße die auslösenden Schritte über eine feste Zeitdauer.
// Erwartung: jeder der 16 Steps feuert genau `cycles`-mal – unabhängig davon, wie
// unglücklich die Blockgröße zum Raster passt (Plan 5.3, Release-Kriterium 9.5 Nr. 1).
struct RunResult { std::map<int,int> hits; int total = 0; };

// Dauer so gewählt, dass sie ein ganzzahliges Vielfaches des 16-Step-Zyklus ist
// (Zyklus = 4 PPQ = 2 s bei 120 BPM). Damit gibt es keinen Randabschnitt, und jede
// Blockgröße muss exakt dieselbe Trefferzahl liefern.
static RunResult runPattern(int blockSize, double swing, double durationSec = 8.0)
{
    const double bpm = 120.0, sr = 44100.0;
    RunResult r;
    const double blockPPQ = blockSize * bpm / (60.0 * sr);
    const double totalPPQ = durationSec * bpm / 60.0;
    const long blocks = static_cast<long>(std::ceil(totalPPQ / blockPPQ));

    for (long b = 0; b < blocks; ++b)
    {
        BlockPosition p{ b * blockPPQ, bpm, blockSize, sr };
        collectStepOffsetsInBlock(p, 0.0, swing, [&](int idx, int off, double)
        {
            CHECK(off >= 0 && off < blockSize);
            r.hits[idx] += 1;
            r.total += 1;
        });
    }
    return r;
}

MDS_TEST(no_duplicate_or_missed_steps_for_every_block_size)
{
    const int sizes[] = { 32, 64, 128, 256, 512, 1024, 960, 777, 100 };
    const double dur = 8.0;   // 8 s bei 120 BPM = 16 PPQ = exakt 4 Pattern-Zyklen
    const int cycles = 4;     // ganzzahlig -> kein Randabschnitt, keine Toleranz nötig
    const int expectedTotal = cycles * kNumSteps;
    // Hinweis: Die Zählung ist exakt, weil jeder Zyklus vollständig durchlaufen wird;
    // Rundungsverluste beim Sample-Offset ändern die Anzahl der Schritte nicht.
    for (int bs : sizes)
    {
        auto r = runPattern(bs, 50.0, dur);

        // Keine Auslassung: alle 16 Step-Indizes wurden erreicht.
        CHECK_EQ(static_cast<int>(r.hits.size()), kNumSteps);
        // Keine Duplikate und keine verschluckten Steps: exakt 16 Treffer pro Zyklus.
        CHECK_EQ(r.total, expectedTotal);
        // Und zwar vollständig gleichverteilt: jeder Step genau `cycles`-mal.
        for (auto& [idx, h] : r.hits)
            CHECK_EQ(h, cycles);
    }
}

MDS_TEST(step_order_monotonic_within_block)
{
    const double bpm = 100.0, sr = 44100.0;
    BlockPosition p{ 0.0, bpm, 1024, sr };
    int lastOff = -1;
    int count = 0;
    collectStepOffsetsInBlock(p, 0.0, 50.0, [&](int, int off, double)
    {
        CHECK(off > lastOff); // innerhalb eines Blocks streng monoton
        lastOff = off;
        ++count;
    });
    CHECK(count > 0);
}

MDS_TEST(swing_offsets_per_plan)
{
    CHECK_NEAR(swingOffsetPPQ(50.0), 0.0, 1e-12);
    CHECK_NEAR(swingOffsetPPQ(75.0), 0.125, 1e-12);   // pairStart+0.375 statt +0.25
    CHECK_NEAR(swingOffsetPPQ(62.5), 0.0625, 1e-12);
    // Clamp gegen unsinnige Werte statt erfundener Defaults
    CHECK_NEAR(swingOffsetPPQ(10.0), 0.0, 1e-12);
    CHECK_NEAR(swingOffsetPPQ(99.0), 0.125, 1e-12);

    // 50 %: zweiter Step bei pairStart + 0.25 PPQ
    CHECK_NEAR(stepPPQ(1, 0.0, 50.0), 0.25, 1e-12);
    // 75 %: zweiter Step bei pairStart + 0.375 PPQ
    CHECK_NEAR(stepPPQ(1, 0.0, 75.0), 0.375, 1e-12);
    // erster Step eines Paares bleibt unverändert
    CHECK_NEAR(stepPPQ(0, 0.0, 75.0), 0.0, 1e-12);
    CHECK_NEAR(stepPPQ(2, 0.0, 75.0), 0.5, 1e-12);
}

MDS_TEST(swing_only_shifts_even_steps_and_keeps_pair_order)
{
    const double bpm = 120.0, sr = 48000.0;
    std::vector<std::pair<int, double>> seen; // (stepIdx, ppq)
    const double cyclePPQ = kNumSteps * kPpqPerStep;
    const double blockPPQ = 256 * bpm / (60.0 * sr);
    const int blocks = static_cast<int>(cyclePPQ / blockPPQ) + 2;
    for (int b = 0; b < blocks; ++b)
    {
        BlockPosition p{ b * blockPPQ, bpm, 256, sr };
        collectStepOffsetsInBlock(p, 0.0, 75.0, [&](int idx, int, double ppq)
        {
            if (ppq < cyclePPQ) seen.push_back({ idx, ppq });
        });
    }
    double prev = -1.0;
    for (auto& [idx, ppq] : seen)
    {
        CHECK(ppq > prev);           // Reihenfolge bleibt erhalten
        prev = ppq;
        if ((idx & 1) == 0)          // ungerade Indizes (Steps 2,4,..) sind nicht verschoben
            CHECK_NEAR(ppq, (idx * 0.25), 1e-12);
        else
            CHECK_NEAR(ppq, (idx * 0.25) + 0.125, 1e-12);
    }
    // nächster Paarbeginn (Step 2, Index 2) bleibt bei 0.5 PPQ
    bool foundPairStart = false;
    for (auto& [idx, ppq] : seen)
        if (idx == 2) { foundPairStart = true; CHECK_NEAR(ppq, 0.5, 1e-12); }
    CHECK(foundPairStart);
}

MDS_TEST(tempo_change_between_blocks_stays_consistent)
{
    // Block 1 mit 120 BPM, Block 2 mit 180 BPM; Start-PPQ wird jeweils vom Host gesetzt.
    BlockPosition a{ 0.0, 120.0, 512, 48000.0 };
    const double endA = computeBlockEndPPQ(a);
    BlockPosition b{ endA, 180.0, 512, 48000.0 };

    // Ein Ereignis kurz nach Blockbeginn von b muss in b liegen und in a nicht.
    const double ev = endA + 0.001;
    CHECK(!sampleOffsetForEventPPQ(a, ev).has_value());
    CHECK(sampleOffsetForEventPPQ(b, ev).has_value());
    // Und die Grenzen berühren sich ohne Lücke/Doppelung
    CHECK_NEAR(endA, computeBlockEndPPQ(a), 1e-15);
    CHECK_NEAR(b.startPPQ, endA, 1e-15);
}

int main() { return mds::test::runAll(); }
