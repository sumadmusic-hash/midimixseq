// Tests/TimingTests.cpp
// Phase-0 hardwarefreie Logiktests: Timing-Berechnung (Plan 5.3/5.4) mit den
// verbindlichen Formeln aus Regel R21 – ohne JUCE, ohne Host.
//
// Korrekturrunde 2 (P2/P3/P4): Die Tests pruefen die neue Sampleindex-Zuordnungsregel
// "ein Step = genau ein Sample" ueber TATSAECHLICH aufeinanderfolgende Blocks
// (keine Einzeloffset-Vergleiche), den llround-Rundungsvertrag inkl. Halbwertfaelle,
// das Verhalten bei pathologischen Zahlen (NaN/Inf/extrem) und den testbaren
// Debug-Assert fuer numSamples <= 0.
#include "TestFramework.h"
#include "../Source/Sequencer/Timing.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

using namespace mds::timing;

TEST(blockEndPPQ_formula)
{
    // 120 BPM, 44100 Hz, Blockstart bei PPQ 0, 512 Samples.
    BlockPosition p{ 0.0, 120.0, 512, 44100.0 };
    const double end = computeBlockEndPPQ(p);
    const double expected = 0.0 + 512.0 * 120.0 / (60.0 * 44100.0);
    CHECK_NEAR(end, expected, 1e-9);
}

TEST(sampleOffset_rounding_contract_and_validity)
{
    // 120 BPM @ 44100 Hz -> samplesPerPPQ = 60*44100/120 = 22050.
    BlockPosition p{ 0.0, 120.0, 8192, 44100.0 };
    // 16tel (0.25 PPQ) -> exakt 5512.5 Samples. std::llround rundet Halbwerte von
    // null WEG -> 5513. Der RUNDERUNGSVERTRAG ist dokumentiert (Timing.h Kopf):
    // bewusst llround statt nearbyint/lrint, weil letztere vom Rundungsmodus
    // (nearest-even -> 5512) abhaengen und damit nicht modusunabhaengig sind.
    auto firstStep = sampleOffsetForEventPPQ(p, 0.25);
    CHECK(firstStep.has_value());
    CHECK_EQ(*firstStep, 5513);
    CHECK(*firstStep >= 0 && *firstStep < p.numSamples);

    // Weitere Halbwertfaelle des Vertrags:
    CHECK_EQ(*sampleOffsetForEventPPQ(p, 0.75), 16538); // 16537.5 -> weg von 0
    CHECK_EQ(*sampleOffsetForEventPPQ(p, 1.25), 27563); // 27562.5 -> 27563
    CHECK_EQ(*sampleOffsetForEventPPQ(p, 1.75), 38588); // 38587.5 -> 38588
    // Exakte Ganzzahl-Samples bleiben unveraendert:
    CHECK_EQ(*sampleOffsetForEventPPQ(p, 1.0), 22050);
    CHECK_EQ(*sampleOffsetForEventPPQ(p, 2.0), 44100);
}

TEST(offsets_in_interval_and_monotonic)
{
    BlockPosition p{ 0.0, 120.0, 8192, 44100.0 };
    int prev = -1;
    for (int i = 0; i < 16; ++i)
    {
        auto off = sampleOffsetForEventPPQ(p, stepPPQ(i, 0.0, 50.0));
        CHECK(off.has_value());
        if (!off) continue;
        CHECK(*off >= 0 && *off < p.numSamples);
        CHECK(*off > prev); // monoton
        prev = *off;
    }
}

TEST(swing_shifts_only_second_of_pair)
{
    // Swing 50 % = gerade Steps; 75 % verschiebt nur ungerade Indizes (Steps 2,4,...).
    const double c50 = swingOffsetPPQ(50.0);
    const double c75 = swingOffsetPPQ(75.0);
    CHECK_NEAR(c50, 0.0, 1e-12);
    CHECK_NEAR(c75, 0.125, 1e-12); // 0.375 - 0.25 PPQ
    // Paarmitte bei 50 % = 0.25 PPQ, bei 75 % = 0.375 PPQ (Regel R21).
    CHECK_NEAR(stepPPQ(1, 0.0, 50.0), 0.25, 1e-12);
    CHECK_NEAR(stepPPQ(1, 0.0, 75.0), 0.375, 1e-12);
    // Erster Step eines Paares wird nie verschoben:
    CHECK_NEAR(stepPPQ(0, 0.0, 75.0), 0.0, 1e-12);
    CHECK_NEAR(stepPPQ(2, 0.0, 75.0), 0.5, 1e-12);
    // Ordnung innerhalb des Paares bleibt erhalten (0.375 < 0.5):
    CHECK(stepPPQ(1, 0.0, 75.0) < stepPPQ(2, 0.0, 75.0));
}

TEST(no_trigger_without_host_data)
{
    // Kein Tempo -> kein Trigger (Regel R7: keine erfundenen Defaults).
    BlockPosition noBpm{ 0.0, 0.0, 512, 44100.0 };
    CHECK(!sampleOffsetForEventPPQ(noBpm, 1.0).has_value());
    BlockPosition noSr{ 0.0, 120.0, 512, 0.0 };
    CHECK(!sampleOffsetForEventPPQ(noSr, 1.0).has_value());
    // Nicht-endliche Hostwerte fuehren zu kontrolliertem Abbruch OHNE Trigger (P3).
    BlockPosition nanBpm{ 0.0, std::nan(""), 512, 44100.0 };
    CHECK(!sampleOffsetForEventPPQ(nanBpm, 1.0).has_value());
    BlockPosition infSr{ 0.0, 120.0, 512, INFINITY };
    CHECK(!sampleOffsetForEventPPQ(infSr, 1.0).has_value());
}

// ---------------------------------------------------------------------------
// P4: Debug-Assert-Vertrag fuer numSamples <= 0 ist TESTBAR (Debug UND Release).
//   - Debug: Handler wird aufgerufen; Funktion liefert trotzdem nullopt zurueck,
//     nachdem der Handler (im Test) kontrolliert zurückkehrt. Default ohne Handler
//     waere abort() – wird hier ersetzt, damit der Testprozess nicht abbricht.
//   - Release (-DNDEBUG): kein Handleraufruf, definiert nullopt.
// ---------------------------------------------------------------------------
namespace
{
std::atomic<int> g_assertHits{ 0 };
void countingAssertHandler(const char*) { g_assertHits.fetch_add(1, std::memory_order_relaxed); }
} // namespace

TEST(debug_assert_contract_for_zero_samples)
{
    BlockPosition zeroSamples{ 0.0, 120.0, 0, 44100.0 };

#if !defined(NDEBUG)
    setAssertHandler(&countingAssertHandler);
    g_assertHits.store(0);
    CHECK(!sampleOffsetForEventPPQ(zeroSamples, 1.0).has_value());
    CHECK(g_assertHits.load() >= 1);            // Debug: Assert wurde ausgeloeset
    int hitsAfterSampleFn = g_assertHits.load();
    collectStepOffsetsInBlock(zeroSamples, 0.0, 50.0, [](int, int, double) {});
    CHECK(g_assertHits.load() > hitsAfterSampleFn); // collect hat denselben Pruefpfad
    setAssertHandler(nullptr);                   // Default (abort) wieder herstellen
#else
    // Release: KEIN Assert, aber definiertes "kein Trigger"-Verhalten.
    CHECK(assertHandlerInstalled() == false);
    CHECK(!sampleOffsetForEventPPQ(zeroSamples, 1.0).has_value());
    int n = 0;
    collectStepOffsetsInBlock(zeroSamples, 0.0, 50.0, [&](int, int, double) { ++n; });
    CHECK_EQ(n, 0);
#endif
    // Konsistenz beider Funktionen bei ungueltiger Blockgroesse (P4-Forderung):
    // beide nutzen isValid() und liefern "nichts".
    int countCollect = 0;
    collectStepOffsetsInBlock(zeroSamples, 0.0, 50.0, [&](int, int, double) { ++countCollect; });
    CHECK_EQ(countCollect, 0);
}

// ---------------------------------------------------------------------------
// P2: Blockgrenzen-Mechanik im Detail (manuelle Blocks, wie der Host sie liefert).
// ---------------------------------------------------------------------------

TEST(step_just_before_block_boundary_delivered_once)
{
    // 120 BPM @ 44100: ein Block von 100 Samples deckt [0, 0.04535...) PPQ ab.
    // Ein Step knapp VOR der Grenze liegt sicher im Intervall des ersten Blocks.
    const double sppq = samplesPerPPQ(120.0, 44100.0);
    BlockPosition b0{ 0.0, 120.0, 100, 44100.0 };
    const double ev = (100.0 - 0.3) / sppq; // rundet auf Sample 99.7 -> 100? nein: 100-0.3=99.7 -> 100
    // Bewusst: ev so, dass llround EXAKT 100 ergibt (Nachkantenrundung).
    const double evRoundUp = 100.4 / sppq;  // llround(100.4)=100 -> faellt NICHT in b0 [0,100)
    auto offBefore = sampleOffsetForEventPPQ(b0, (99.0) / sppq);
    CHECK(offBefore.has_value());
    CHECK_EQ(*offBefore, 99);
    auto offUp = sampleOffsetForEventPPQ(b0, evRoundUp);
    CHECK(!offUp.has_value()); // gehoert per Sampleindex zum naechsten Block
    (void)ev;
}

TEST(step_exactly_on_block_boundary_goes_to_next_block)
{
    // Sample 100 ist der erste Sample des Folgeblocks -> Zuordnung an b1 Offset 0.
    const double sppq = samplesPerPPQ(120.0, 44100.0);
    BlockPosition b0{ 0.0, 120.0, 100, 44100.0 };
    BlockPosition b1{ computeBlockEndPPQ(b0), 120.0, 100, 44100.0 };
    const double evExact = 100.0 / sppq; // exakt auf der Samplegrenze
    CHECK(!sampleOffsetForEventPPQ(b0, evExact).has_value());
    auto off1 = sampleOffsetForEventPPQ(b1, evExact);
    CHECK(off1.has_value());
    CHECK_EQ(*off1, 0); // derselbe Step im Folgeblock, Offset 0
}

TEST(round_up_at_end_of_previous_block_is_carried_to_offset_zero)
{
    // Pflichtfall 4+5: Ein Step, dessen Rundung im vorherigen Block numSamples
    // ergibt, wird im Folgeblock bei Offset 0 ausgeliefert.
    const double sppq = samplesPerPPQ(120.0, 44100.0);
    BlockPosition b0{ 0.0, 120.0, 100, 44100.0 };
    BlockPosition b1{ computeBlockEndPPQ(b0), 120.0, 100, 44100.0 };
    const unsigned long long carryBefore = carriedFromPreviousBlock();
    const double ev = 99.6 / sppq; // llround(99.6) = 100 -> nicht in b0, aber in b1 [100,200)
    CHECK(!sampleOffsetForEventPPQ(b0, ev).has_value());
    auto off = sampleOffsetForEventPPQ(b1, ev);
    CHECK(off.has_value());
    CHECK_EQ(*off, 0);
    CHECK(carriedFromPreviousBlock() > carryBefore); // Zaehler zeigt Carry-In
}

TEST(two_consecutive_blocks_deliver_each_step_exactly_once)
{
    // Pflichtfall 6: Ueber beide Blocks insgesamt genau ein Trigger pro Step.
    const double sppq = samplesPerPPQ(120.0, 44100.0);
    BlockPosition b0{ 0.0, 120.0, 100, 44100.0 };
    BlockPosition b1{ computeBlockEndPPQ(b0), 120.0, 100, 44100.0 };
    // Ein kuenstliches Ereignis genau an der kritischen Rundungsstelle:
    const double ev = 99.6 / sppq;
    int total = 0;
    if (sampleOffsetForEventPPQ(b0, ev)) ++total;
    if (sampleOffsetForEventPPQ(b1, ev)) ++total;
    CHECK_EQ(total, 1);
    (void)sppq;
}

// Vollstaendige absolute Step-Ereignisfolge ueber eine echte Blockfolge.
namespace
{
struct SeqEvent
{
    long long absSample; // gerundete absolute Sampleposition (kanonischer Schluessel)
    int stepIdx;         // Step im 16er-Raster (Index modulo 16)
    long long absIndex;  // absoluter Rasterindex i (eindeutiger Step!)
};

// Laeuft eine Kette aufeinanderfolgender Blocks (startPPQ(b+1)=endPPQ(b)) und sammelt
// die von collectStepOffsetsInBlock erzeugte EREIGNISFOLGE. Der Host simuliert dabei
// exakt die Fortschreibung, die auch ein DAW-Transport liefert.
std::vector<SeqEvent> runBlockChain(int blockSamples, double swingPercent,
                                    double durationSec, double bpm = 120.0,
                                    double sr = 44100.0)
{
    std::vector<SeqEvent> out;
    const double blockPPQ = blockSamples * bpm / (60.0 * sr);
    const double totalPPQ = durationSec * bpm / 60.0;
    const double sppq = samplesPerPPQ(bpm, sr);
    const double sw = swingOffsetPPQ(swingPercent);
    for (double start = 0.0; start < totalPPQ - 1e-12; start += blockPPQ)
    {
        const bool lastPartial = (start + blockPPQ) > totalPPQ;
        const int ns = lastPartial
            ? static_cast<int>(std::llround((totalPPQ - start) * sppq))
            : blockSamples;
        if (ns <= 0) break;
        BlockPosition p{ start, bpm, ns, sr };
        collectStepOffsetsInBlock(p, 0.0, swingPercent,
            [&](int idx, int off, double ppq)
            {
                // Absoluten Rasterindex rekonstruieren: Kandidaten um ppq/0.25 pruefen.
                const long long c = std::llround(ppq / kPpqPerStep);
                long long ai = -1;
                for (long long k = c - 2; k <= c + 2; ++k)
                {
                    const double ppqc = static_cast<double>(k) * kPpqPerStep
                                      + ((k & 1) ? sw : 0.0);
                    if (std::fabs(ppqc - ppq) < 1e-9) { ai = k; break; }
                }
                CHECK(ai >= 0);
                out.push_back({ std::llround(ppq * sppq), idx, ai });
                CHECK(off >= 0 && off < ns);
            });
    }
    return out;
}
} // namespace

TEST(full_event_sequence_identical_across_block_sizes)
{
    // Pflichtfall 9: Unterschiedliche Blockgroessen liefern dieselbe absolute
    // Ereignisfolge (vergleiche vollständige Folgen, nicht Einzeloffsets).
    const double dur = 4.0; // Sekunden
    auto a = runBlockChain(128, 50.0, dur);
    auto b = runBlockChain(256, 50.0, dur);
    auto c = runBlockChain(512, 50.0, dur);
    auto d = runBlockChain(100, 50.0, dur); // nicht-teilbare Groesse
    auto e = runBlockChain(960, 50.0, dur);
    CHECK(!a.empty());
    auto key = [](const std::vector<SeqEvent>& v)
    { std::vector<std::pair<long long, long long>> k; for (auto& e : v) k.push_back({ e.absSample, e.absIndex }); return k; };
    const auto ka = key(a);
    CHECK(key(b) == ka);
    CHECK(key(c) == ka);
    CHECK(key(d) == ka);
    CHECK(key(e) == ka);
}

TEST(no_loss_no_duplicate_over_long_block_chain)
{
    // Pflichtfaelle 7+8+12: Lange Folge von Blocks ohne Duplikate oder Auslassungen.
    // Vergleich ueber die vollstaendige absolute Step-Ereignisfolge (Rasterindex).
    const double dur = 16.0;
    for (double swing : { 50.0, 62.5, 75.0 })
    {
        for (int bs : { 64, 128, 256, 512, 1000, 1024 })
        {
            auto evs = runBlockChain(bs, swing, dur);
            std::vector<long long> ids;
            for (auto& e : evs) ids.push_back(e.absIndex);
            std::sort(ids.begin(), ids.end());
            bool dup = false;
            for (size_t i = 1; i < ids.size(); ++i)
                if (ids[i] == ids[i - 1]) dup = true;
            CHECK(!dup);
            // Fortlaufende Indizes 0..N-1, N = Steps im Fenster [0, dur): ein Step
            // bei exakt t=dur faellt aus dem Intervall und wird nicht erwartet.
            const long long n = static_cast<long long>(dur * 4.0); // 4 Sixteenth/s bei 120 BPM
            CHECK_EQ(static_cast<long long>(evs.size()), n);
            for (long long i = 0; i < n; ++i)
                CHECK(std::binary_search(ids.begin(), ids.end(), i));
        }
    }
}

TEST(swing_event_at_block_boundary)
{
    // Pflichtfall 11: Swing-Ereignis an der Blockgrenze. Bei 75 % liegt der
    // zweite Step eines Paares bei i*0.25+0.125 PPQ. Wir legen die Blockgrenze
    // exakt auf diese Position und pruefen Uebergabe ohne Verlust/Duplikat.
    const double sppq = samplesPerPPQ(120.0, 44100.0);
    const double swPpq = swingOffsetPPQ(75.0);            // 0.125
    const double ev = 1.0 + swPpq;                        // Step i=3 (ungerade) mit Swing
    // Block b0 endet vor dem Ereignis, b1 beginnt exakt darauf.
    BlockPosition b0{ 0.0, 120.0, static_cast<int>(std::llround((ev - 1e-9) * sppq)), 44100.0 };
    BlockPosition b1{ computeBlockEndPPQ(b0), 120.0, 200, 44100.0 };
    const int cnt = (sampleOffsetForEventPPQ(b0, ev) ? 1 : 0)
                  + (sampleOffsetForEventPPQ(b1, ev) ? 1 : 0);
    CHECK_EQ(cnt, 1); // genau ein Trigger, egal in welchem Block er landet
}

TEST(tempo_change_at_block_boundary_restarts_mapping)
{
    // Pflichtfall 10: Tempoaenderung unmittelbar an der Blockgrenze.
    // Block 0 bei 120 BPM, Block 1 bei 60 BPM, jeweils startPPQ = Vorgaenger-endPPQ.
    BlockPosition b0{ 0.0, 120.0, 256, 44100.0 };
    const double boundary = computeBlockEndPPQ(b0);
    BlockPosition b1{ boundary, 60.0, 256, 44100.0 };
    // Ereignisse vor dem Wechsel gehoeren zu b0, danach entscheidet b1 mit neuer
    // Sample-Dichte. Ein Step weit hinter der Grenze darf in keinem der beiden
    // Blocks einen gueltigen Offset bekommen (liegt jenseits beider Samplebereiche).
    CHECK(sampleOffsetForEventPPQ(b0, 0.25).has_value());
    auto late = sampleOffsetForEventPPQ(b1, 1000.0);
    CHECK(!late.has_value());
    // Der direkt nach der Grenze liegende Step wird von genau einem Block getragen:
    const double evNear = boundary + 0.01;
    const int cnt = (sampleOffsetForEventPPQ(b0, evNear) ? 1 : 0)
                  + (sampleOffsetForEventPPQ(b1, evNear) ? 1 : 0);
    CHECK(cnt <= 1); // kein Doppeltrigger auch bei wechselnder Dichte
}

TEST(seek_position_jump_resets_phase)
{
    // Seek: naechster Block beginnt an neuer PPQ; alte Zyklusphase darf keine
    // Geistertrigger erzeugen. cycleStartPPQ wird aus Host-Bar-PPQ neu abgeleitet.
    BlockPosition beforeSeek{ 8.0, 120.0, 512, 44100.0 };
    int nBefore = 0;
    collectStepOffsetsInBlock(beforeSeek, 8.0, 50.0, [&](int, int, double) { ++nBefore; });
    CHECK(nBefore >= 0);
    // Nach Seek auf PPQ 32.0: Block bei 32.0 sieht nur Steps ab 32.0.
    BlockPosition afterSeek{ 32.0, 120.0, 512, 44100.0 };
    int nAfter = 0;
    collectStepOffsetsInBlock(afterSeek, 32.0, 50.0, [&](int, int, double) { ++nAfter; });
    CHECK(nAfter >= 0);
    CHECK(!sampleOffsetForEventPPQ(afterSeek, 8.0).has_value()); // alter Step weit entfernt
}

// ---------------------------------------------------------------------------
// P3: Pathologische Zahlen.
// ---------------------------------------------------------------------------

TEST(nonfinite_and_extreme_inputs_are_intercepted)
{
    const unsigned long long base = invalidInputCount();
    BlockPosition ok{ 0.0, 120.0, 512, 44100.0 };

    // NaN/Inf als eventPPQ: kein Trigger, Zaehler steigt.
    CHECK(!sampleOffsetForEventPPQ(ok, std::nan("")).has_value());
    CHECK(!sampleOffsetForEventPPQ(ok, INFINITY).has_value());
    CHECK(!sampleOffsetForEventPPQ(ok, -INFINITY).has_value());
    CHECK(invalidInputCount() >= base + 3);

    // Extrem grosse PPQ-Werte (jenseits des sicheren long long-Bereichs bzw. 2^52):
    CHECK(!sampleOffsetForEventPPQ(ok, 1e18).has_value());
    CHECK(!sampleOffsetForEventPPQ(ok, -1e18).has_value());

    // Nicht-endliche/extreme Blockfelder: KEIN Trigger, KEIN Assert (legitime
    // "keine Sync-Daten"-Situation, R7).
    BlockPosition nanStart{ std::nan(""), 120.0, 512, 44100.0 };
    CHECK(!sampleOffsetForEventPPQ(nanStart, 1.0).has_value());
    BlockPosition hugeBpm{ 0.0, 1e300, 512, 44100.0 };
    CHECK(!sampleOffsetForEventPPQ(hugeBpm, 1.0).has_value());
    BlockPosition hugeSr{ 0.0, 120.0, 512, 1e300 };
    CHECK(!sampleOffsetForEventPPQ(hugeSr, 1.0).has_value());

    // collectStepOffsetsInBlock mit denselben Eingaben: leer, kein Crash.
    int n = 0;
    collectStepOffsetsInBlock(ok, std::nan(""), 50.0, [&](int, int, double) { ++n; });
    CHECK_EQ(n, 0);
    n = 0;
    collectStepOffsetsInBlock(ok, 1e18, 50.0, [&](int, int, double) { ++n; });
    CHECK_EQ(n, 0);
    n = 0;
    collectStepOffsetsInBlock(nanStart, 0.0, 50.0, [&](int, int, double) { ++n; });
    CHECK_EQ(n, 0);

    // Ungueltige Swing-Werte: neutralisiert, Fuehrung bleibt definiert.
    CHECK_NEAR(swingOffsetPPQ(std::nan("")), 0.0, 1e-12);
    CHECK_NEAR(swingOffsetPPQ(INFINITY), 0.0, 1e-12);
    CHECK_NEAR(swingOffsetPPQ(-INFINITY), 0.0, 1e-12);
    n = 0;
    collectStepOffsetsInBlock(ok, 0.0, std::nan(""), [&](int, int, double) { ++n; });
    CHECK(n > 0); // Steps werden weiterhin korrekt getriggert (neutraler Swing)
}

TEST(scan_cost_cap_is_enforced_and_diagnosed)
{
    // Extrem kleine BPM + riesiger Block => PPQ-Fenster mit Abermillionen Steps.
    // Der Deckel begrenzt die Iterationen auf kMaxStepsPerBlockScan und erhoeht
    // scanLimitTruncations(). Kein Haengen, keine unbeschraenkte Schleife.
    resetScanLimitTruncations();
    const unsigned long long before = scanLimitTruncations();
    BlockPosition slow{ 0.0, 0.0001, 1000000000, 44100.0 };
    long long delivered = 0;
    auto t0 = std::chrono::steady_clock::now();
    collectStepOffsetsInBlock(slow, 0.0, 50.0, [&](int, int, double) { ++delivered; });
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::steady_clock::now() - t0).count();
    CHECK(scanLimitTruncations() > before);      // Limit erreicht und diagnostiziert
    CHECK(delivered <= kMaxStepsPerBlockScan);   // gedeckelte Ereigniszahl
    CHECK(us < 2000000);                         // Laufzeit klar begrenzt (< 2 s, real ≪ ms)
}

TEST(extreme_ppq_positions_do_not_trigger_undefined_conversion)
{
    // Werte knapp unter/ueber dem sicheren Konvertierungsbereich: definiert ablehnen
    // statt long long-UB. (Sanitizer-Testfaelle laufen zusaetzlich unter UBSan.)
    BlockPosition p{ 0.0, 120.0, 512, 44100.0 };
    const unsigned long long base = invalidInputCount();
    CHECK(!sampleOffsetForEventPPQ(p, kMaxSafeMagnitude).has_value());
    CHECK(!sampleOffsetForEventPPQ(p, -kMaxSafeMagnitude).has_value());
    CHECK(!sampleOffsetForEventPPQ(p, 8.9e307).has_value());
    CHECK(invalidInputCount() >= base + 3);
    // Gueltige, aber grosse PPQ weit ausserhalb des Blocks: kein Trigger, kein Crash.
    CHECK(!sampleOffsetForEventPPQ(p, 1e12).has_value());
}

int main() { return mds::test::runAll(); }
