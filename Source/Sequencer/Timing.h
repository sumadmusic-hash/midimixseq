// Source/Sequencer/Timing.h
// Host-synchronisierte Timing-Berechnung nach Planabschnitt 5.3 / 5.4.
// Reine, JUCE-freie Kernlogik -> in Phase 0 als Unit-Test pruefbar.
//
// WICHTIG: Diese Datei ist KEIN Produktcode fuer Sampler/GUI/MIDImix, sondern die
// isolierte, hardwarefreie Mathematik des Sequencers. Sie enthaelt keine erfundenen
// Defaults: bpm <= 0 oder fehlende Position liefern "kein Ereignis".
//
// Echtzeit-Hinweis (Audit): Alle Funktionen sind frei von Heap-Allokation, Sperr-
// operationen und Schleifen mit variabler, ungebundener Laufzeit (der Step-Scan ist
// gedeckelt). Der Aufrufer muss Blockpositionen aus einem einzigen Host-Query fuellen;
// Start-PPQ und Sampleanzahl duerfen nicht aus unterschiedlichen Quellen vermischt
// werden, da andernfalls Inkonsistenzen innerhalb eines Blocks moeglich sind.
//
// ---------------------------------------------------------------------------
// STABILITAETSRUNDE 2 – TRIGGERZUORDNUNG AN BLOCKGRENZEN (P2/P3/P4)
// ---------------------------------------------------------------------------
// ZUORDNUNGSVERTRAG (eindeutig, deterministisch, verlustfrei):
//
// 1. Jedes Ereignis wird auf die ABSOLUTE Sampleposition A(ev) = ev * samplesPerPPQ
//    abgebildet, wobei samplesPerPPQ = 60*sampleRate/bpm ein Festwert fuer die
//    Lebensdauer eines Pattern-Zyklus sein MUSS (Tempo aendert die Zuordnung; das ist
//    durch den Host-Vertrag so vorgesehen, siehe Punkt 6). Die Abbildung ev -> A(ev)
//    ist strikt monoton wachsend.
//
// 2. Jeder Audio-Block b besitzt das halboffene PPQ-Intervall
//        I_b = [startPPQ(b), endPPQ(b)),   startPPQ(b+1) := endPPQ(b).
//    Die Sample-Achse wird denselben Grenzen zugeordnet:
//        S_start(b) = llround(startPPQ(b)*sppq),  S_end(b) = S_start(b)+numSamples(b).
//    Ein Ereignis gehoert genau dann zu Block b, wenn sein gerundeter absoluter
//    Sampleindex s = llround(A(ev)) im Sample-Bereich [S_start(b), S_end(b)) liegt;
//    das Ausloese-Offset ist dann s - S_start(b) in [0, numSamples-1].
//    Das ist die kanonische Regel "ein Step = genau ein Sample": benachbarte Blocks
//    haben disjunkte, lueckenlos aneinandergrenzende Sample-Bereiche, also wird ein
//    Ereignis in HOECHSTENS EINEM und spaetestens in dem Block ausgeliefert, dessen
//    Sample-Bereich seinen gerundeten Index enthaelt – weder Verlust noch Duplikat.
//    Der fruehere Konflikt (Rundung auf numSamples im Vorblock => dort verworfen,
//    im Folgeblock wegen eventPPQ < startPPQ erneut abgelehnt) existiert nicht mehr,
//    weil der SAMPLE-INDEX entscheidet, nicht die PPQ-Schwelle.
//
//    MAXIMALE VERZOEGERUNG DURCH RUNDERUNG: hochstens 1 Sample. Ein Ereignis, das
//    gemaess exakter Rechnung hinter dem letzten Sample seines PPQ-Blocks liegen
//    wuerde, wird am letzten Sample DIESES Blocks ausgeliefert bzw. rutscht per
//    Sampleindex-Zuordnung in den Folgeblock bei Offset 0.
//
// 3. sampleOffsetForEventPPQ() liefert genau diese Zuordnung: erst Pruefung, ob der
//    absolute gerundete Sampleindex zum Block gehört, dann Offset = s - S_start(b).
//    Der fruehere Fehler (Ereignis rundete im Vorblock auf numSamples und wurde dort
//    verworfen, im Folgeblock wegen eventPPQ < startPPQ erneut abgelehnt) ist damit
//    strukturell ausgeschlossen: der Sampleindex entscheidet, nicht die PPQ-Schwelle.
//
// 4. collectStepOffsetsInBlock() prueft jeden Step des Rasters genau einmal pro
//    Block gegen dieselbe Regel. Der Scanbereich ist so gewaehlt, dass jeder Step,
//    dessen Sampleindex in den Block fallen KOENNNTE, erreicht wird; ueberfluessige
//    Kandidaten werden von der Zuordnungsregel sauber abgelehnt.
//
// 5. Diagnose-Zaehler (atomar, relaxed, lock-free – statischer Nachweis unten):
//      boundaryRoundUpToLastSample(): Ereignisse mit ev >= endPPQ(b) (also formal
//        hinter dem PPQ-Block), deren gerundeter Sampleindex dennoch in den
//        Sample-Bereich dieses Blocks faellt – Auslieferung am letzten Sample.
//      carriedFromPreviousBlock(): Ereignisse mit ev < startPPQ(b), deren
//        gerundeter Sampleindex erst in den Sample-Bereich dieses Blocks faellt
//        (der Vorblock hatte dort bereits S_end erreicht); Uebergabe bei Offset 0.
//      scanLimitTruncations(): erreichte Kosten-Deckel kMaxStepsPerBlockScan.
//      invalidInputCount(): kontrollierte Abbrüche wegen NaN/Inf/extremer Werte.
//
// 6. Tempo-/SampleRate-Aenderung: Sie verschiebt die Samplezuordnung aller
//    Ereignisse. Der Vertrag gilt pro konstantem Festwert; bei Aenderung an einer
//    Blockgrenze beginnt er ab diesem Block neu (Host setzt startPPQ fort). Fuer
//    Swing gilt analog: Swing aendert die PPQ-Positionen des Rasters und damit die
//    Zuordnung; innerhalb eines Zyklus muss der Wert konstant bleiben.
//
// RUNDERUNGSVERTRAG: std::llround() – Round-to-nearest, Halbwerte von null WEG.
// Referenzfall: 0.25 PPQ * 22050 Samples/PPQ = 5512.5 -> 5513.
// nearbyint()/lrint() folgen dem Rundungsmodus (nearest-even -> 5512) und sind damit
// NICHT modusunabhaengig deterministisch; sie werden bewusst nicht verwendet.
//
// PFADSICHERHEIT GEGEN PATHOLOGISCHE ZAHLEN (P3): Jede Gleitkommazahl wird vor jeder
// long-long-Konvertierung auf Endlichkeit UND Groessenordnung geprueft
// (kMaxSafeMagnitude = 2^52). Nicht endliche oder massive Werte fuehren zu definiertem
// Abbruch ohne Trigger und erhoehen invalidInputCount(). Der Scan-Kostendeckel
// kMaxStepsPerBlockScan bleibt bestehen und ist zaehlbar (scanLimitTruncations()).
//
// DEBUG-ASSERT UND TESTVERTRAG (P4): numSamples <= 0 bei ansonsten gueltigen
// Host-Daten ist ein interner Programmierfehler (Host liefert einen Block ohne
// Samples). Im Debug (ohne NDEBUG) wird ein testbarer Assert ausgeloeset:
// Standardmaessig fprintf + abort(); Tests koennen den Handler ersetzen
// (setAssertHandler) und so den Debug-Abbruch kontrolliert pruefen, ohne dass der
// Testprozess abbricht. Release (-DNDEBUG): kein Assert; alle betroffenen Funktionen
// liefern definiert "kein Trigger" (nullopt / leerer Scan). collectStepOffsetsInBlock()
// und sampleOffsetForEventPPQ() nutzen denselben Gueltigkeitspfad isValid() und
// verhalten sich bei ungueltiger Blockgroesse damit exakt identisch.
#pragma once

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

// Sicherer Bereich fuer double->long long-Konvertierungen (P3). 2^52: oberhalb
// dieser Groessenordnung verliert double Integer-Präzision zwischen
// aufeinanderfolgenden Darstellungen; Konversionen wären dort nicht mehr
// reproduzierbar. Unterhalb passt jeder gerundete Wert sicher in long long.
inline constexpr double kMaxSafeMagnitude = 4503599627370496.0; // 2^52

// --- Diagnose-Zaehler (atomar, relaxed fetch_add im Audio-Pfad) ----------------------

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

// Anzahl der Ereignisse, die formal hinter dem Blockende lagen (ev >= endPPQ), aber
// wegen Rundung auf das letzte Sample DESSESSELBEN Blocks uebernommen wurden
// (Vertrag Punkt 2/5; verhindert den Verlust an der Nachkante).
inline std::atomic<unsigned long long>& boundaryRoundUpCounter()
{
    static std::atomic<unsigned long long> c{ 0 };
    return c;
}
inline unsigned long long boundaryRoundUpToLastSample() noexcept
{
    return boundaryRoundUpCounter().load(std::memory_order_relaxed);
}
inline void resetBoundaryRoundUpCount() noexcept
{
    boundaryRoundUpCounter().store(0, std::memory_order_relaxed);
}

// Anzahl der Ereignisse, die formal vor dem Blockanfang lagen (ev < startPPQ), aber
// wegen Ueberlauf-Rundung des VORHERIGEN Blocks erst an Offset 0 dieses Blocks
// ausgeliefert werden (Carry-In, Vertrag Punkt 2/5).
inline std::atomic<unsigned long long>& carryInCounter()
{
    static std::atomic<unsigned long long> c{ 0 };
    return c;
}
inline unsigned long long carriedFromPreviousBlock() noexcept
{
    return carryInCounter().load(std::memory_order_relaxed);
}
inline void resetCarryInCount() noexcept
{
    carryInCounter().store(0, std::memory_order_relaxed);
}

// Anzahl der kontrollierten Abbruege wegen nicht-endlicher oder massiver Werte
// (P3). Kein stillschweigender Uebergang in scheinbar gueltige Trigger.
inline std::atomic<unsigned long long>& invalidInputCounter()
{
    static std::atomic<unsigned long long> c{ 0 };
    return c;
}
inline unsigned long long invalidInputCount() noexcept
{
    return invalidInputCounter().load(std::memory_order_relaxed);
}
inline void resetInvalidInputCount() noexcept
{
    invalidInputCounter().store(0, std::memory_order_relaxed);
}

// Lock-free-Nachweis (P3): Alle Diagnose-Zaehler verwenden
// std::atomic<unsigned long long>. Dieser statische Nachweis stellt sicher, dass
// die Implementierung auf der Zielplattform IMMER lock-free ist; andernfalls
// kompiliert die Datei nicht (fail-fast statt unbemerktem Library-Lock-Fallback
// im Audio-Thread).
static_assert(std::atomic<unsigned long long>::is_always_lock_free,
              "Timing-Diagnose-Zaehler müssen auf der Zielplattform immer lock-free sein");

// --- Testbarer Debug-Assert (P4) ------------------------------------------------------
// Vertrag:
//  - Debug (ohne NDEBUG): numSamples <= 0 bei gueltigen Host-Daten -> assertHandler
//    wird aufgerufen. Default-Handler: fprintf + abort() (klassischer Abbruch).
//    Tests setzen einen eigenen Handler und pruefen so den Debug-Abbruch
//    kontrolliert (P4: Assert bleibt erhalten und ist testbar).
//  - Release (-DNDEBUG): kein Handleraufruf; die Funktion liefert definiert
//    "kein Trigger" (nullopt / leerer Scan).
using AssertHandler = void (*)(const char* msg);
inline AssertHandler& assertHandlerSlot()
{
    static AssertHandler h = nullptr;
    return h;
}
inline void setAssertHandler(AssertHandler h) noexcept { assertHandlerSlot() = h; }
inline bool assertHandlerInstalled() noexcept { return assertHandlerSlot() != nullptr; }

namespace detail
{
inline void defaultAbortAssert(const char* msg)
{
    std::fprintf(stderr, "MDS_TIMING_ASSERT failed: %s\n", msg);
    std::abort();
}
inline void timingAssert(bool cond, const char* msg)
{
#if !defined(NDEBUG)
    if (!cond)
    {
        if (AssertHandler h = assertHandlerSlot())
            h(msg);
        else
            defaultAbortAssert(msg);
    }
#else
    (void)cond;
    (void)msg;
#endif
}
} // namespace detail

struct BlockPosition
{
    double startPPQ = 0.0; // absolute Host-PPQ-Position bei Blockbeginn
    double bpm = 0.0;      // Host-Tempo; <= 0 bedeutet "nicht verfügbar"
    int numSamples = 0;
    double sampleRate = 0.0;
};

// Host-Sync-Daten vorhanden und im sicheren Zahlenbereich? bpm/sampleRate <= 0
// oder nicht-endliche/extreme Werte beschreiben die legitime Host-Situation
// "keine Sync-Daten" (Regel R7) -> KEIN Assert.
inline bool hasHostSyncData(const BlockPosition& p)
{
    return p.bpm > 0.0 && p.sampleRate > 0.0 && std::isfinite(p.startPPQ)
        && std::fabs(p.startPPQ) < kMaxSafeMagnitude
        && std::isfinite(p.bpm) && std::fabs(p.bpm) < kMaxSafeMagnitude
        && std::isfinite(p.sampleRate) && std::fabs(p.sampleRate) < kMaxSafeMagnitude;
}

// Vollstaendige Blockgueltigkeit inkl. Sampleanzahl. numSamples <= 0 bei
// ansonsten gueltigen Host-Daten ist ein interner Fehler (P4) und loest den
// testbaren Debug-Assert aus; danach (und in Release generell) gilt der Block als
// ungueltig: isValid()==false, alle Funktionen tun definiert "nichts".
inline bool isValid(const BlockPosition& p)
{
    if (!hasHostSyncData(p))
        return false;
    if (p.numSamples <= 0)
    {
        detail::timingAssert(false, "Audio-Block ohne Samples bei gueltigen Host-Daten (R7/R12)");
        return false;
    }
    return true;
}

// blockEndPPQ = blockStartPPQ + numSamples * bpm / (60 * sampleRate)   [Plan 5.3]
inline double computeBlockEndPPQ(const BlockPosition& p)
{
    if (!isValid(p))
        return p.startPPQ;
    return p.startPPQ + static_cast<double>(p.numSamples) * p.bpm / (60.0 * p.sampleRate);
}

// samplesPerPPQ = 60 * sampleRate / bpm; 0.0 bei ungueltigen Eingaben (P3).
inline double samplesPerPPQ(double bpm, double sampleRate)
{
    if (!(bpm > 0.0) || !(sampleRate > 0.0) || !std::isfinite(bpm) || !std::isfinite(sampleRate))
        return 0.0;
    const double v = 60.0 * sampleRate / bpm;
    return (std::isfinite(v) && v < kMaxSafeMagnitude) ? v : 0.0;
}

// Sichere double->long long-Konvertierung (P3): nur endliche Werte innerhalb
// (-kMaxSafeMagnitude, +kMaxSafeMagnitude) werden konvertiert. Rueckgabe false,
// wenn der Wert außerhalb liegt; der Aufrufer bricht definiert ab und zaehlt.
inline bool toLLSafe(double v, long long& out) noexcept
{
    if (!std::isfinite(v) || std::fabs(v) >= kMaxSafeMagnitude)
        return false;
    out = static_cast<long long>(v);
    return true;
}

// llround mit Vorabpruefung (P3): gibt false zurueck, statt eine Konvertierung
// außerhalb des darstellbaren Bereichs auszuloesen (UB bei NaN/Inf/extrem).
inline bool safeLlround(double v, long long& out) noexcept
{
    if (!std::isfinite(v) || std::fabs(v) >= kMaxSafeMagnitude)
        return false;
    out = std::llround(v);
    return true;
}

// Zuordnungsregel (Vertrag Punkt 2): gehoert der absolute, gerundete Sampleindex
// s = llround(eventPPQ * sppq) zum Sample-Bereich [S_start, S_start+numSamples)
// des Blocks? Wenn ja, wird der relative Offset geliefert.
//
// sampleOffsetForEventPPQ() liefert das Sample-Offset [0, numSamples-1] genau dann,
// wenn das Ereignis gemaess Vertrag zu DIESEM Block gehoert; sonst leeres Optional.
// Rueckgabe ist ebenfalls leer, wenn keine Host-Daten vorliegen oder ein Wert
// jenseits des sicheren Konvertierungsbereichs liegt (P3; invalidInputCount steigt).
inline std::optional<int> sampleOffsetForEventPPQ(const BlockPosition& p, double eventPPQ)
{
    if (!isValid(p))
        return std::nullopt; // numSamples<=0 bei gueltigen Hostdaten: Assert in isValid (Debug)
    if (!std::isfinite(eventPPQ) || std::fabs(eventPPQ) >= kMaxSafeMagnitude)
    {
        invalidInputCounter().fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }

    const double sppq = samplesPerPPQ(p.bpm, p.sampleRate);
    if (!(sppq > 0.0))
    {
        invalidInputCounter().fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }

    // Absolute Sampleposition des Ereignisses und Blockanfang in Samples.
    long long sAbs = 0, sStart = 0;
    if (!safeLlround(eventPPQ * sppq, sAbs) || !safeLlround(p.startPPQ * sppq, sStart))
    {
        invalidInputCounter().fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }

    const long long ns = static_cast<long long>(p.numSamples);
    const long long rel = sAbs - sStart; // Differenz zweier |.|<2^52-Werte: overflowfrei

    if (rel >= 0 && rel < ns)
    {
        // Sample-Bereichs-Zuordnung: Der Block ist genau dann verantwortlich, wenn
        // der gerundete Index hierhaelt. Faellt die PPQ-Intervallentscheidung anders
        // aus (Nachkanten-Rundung ueber endPPQ bzw. Vorkanten-Carry unter startPPQ),
        // zaehlen die Diagnose-Zaehler den Sonderfall – die Auslieferung selbst ist
        // der Normalfall des Vertrags und kein Datenverlust.
        const double endPPQ = computeBlockEndPPQ(p);
        if (eventPPQ >= endPPQ)
            boundaryRoundUpCounter().fetch_add(1, std::memory_order_relaxed);
        else if (eventPPQ < p.startPPQ)
            carryInCounter().fetch_add(1, std::memory_order_relaxed);
        return static_cast<int>(rel);
    }
    return std::nullopt;
}

// Swing: global 50..75 % (Plan 5.4). Verzögert den zweiten Step jedes 16tel-Paares.
// pairStart + 0.25 PPQ bei 50 %, pairStart + 0.375 PPQ bei 75 %.
// Schrittweite eines Paares = 0.5 PPQ; Offset = (swing-0.5) * 0.5 PPQ.
inline double swingOffsetPPQ(double swingPercent)
{
    // P3: NaN/Unendlichkeit -> neutral 0.0 Verschiebung, damit ungueltige
    // Swing-Werte keine ungültigen Konvertierungen ausloesen.
    if (!std::isfinite(swingPercent))
        return 0.0;
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
    if (!std::isfinite(cycleStartPPQ))
        return cycleStartPPQ; // bleibt nicht endlich; Scan prueft (P3)
    double ppq = cycleStartPPQ + static_cast<double>(stepIndex) * kPpqPerStep;
    if ((stepIndex & 1) == 1) // zweiter Step eines Paares
        ppq += swingOffsetPPQ(swingPercent);
    return ppq;
}

// Alle Step-Grenzen, die gemaess Vertrag zu diesem Block gehoeren, als Sample-Offsets.
// Liefert exakt einmal pro Step – unabhaengig von der Blockgroesse (P2).
template <typename Sink>
void collectStepOffsetsInBlock(const BlockPosition& p,
                               double cycleStartPPQ,
                               double swingPercent,
                               Sink&& sink)
{
    if (!isValid(p))
        return;
    if (!std::isfinite(cycleStartPPQ) || std::fabs(cycleStartPPQ) >= kMaxSafeMagnitude)
    {
        invalidInputCounter().fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const double sppq = samplesPerPPQ(p.bpm, p.sampleRate);
    if (!(sppq > 0.0))
    {
        invalidInputCounter().fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // Zu untersuchender Schrittindex-Bereich: jeder Step i hat die Position
    //   ppq(i) = cycleStart + i*0.25 (+ Swing, 0..0.125 PPQ, nur ungerade i).
    // Er kann nur dann in den Block fallen, wenn seine absolute Sampleposition im
    // Sample-Bereich [sStart, sStart+ns) liegt. Da ppq monotom in i ist, ergibt sich
    // der Kandidatenbereich durch Aufloesen nach i (mit Sicherheitsrand fuer Swing und
    // Rundung). Steps auflerhalb des Rasters werden von der Zuordnungsregel verworfen.
    long long sStart = 0;
    if (!safeLlround(p.startPPQ * sppq, sStart))
    {
        invalidInputCounter().fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const double maxSwing = swingOffsetPPQ(swingPercent);
    const double stepSecPPQ = kPpqPerStep; // 0.25
    // ppq_min(i) = cycleStart + i*0.25 ; ppq_max(i) = ppq_min(i) + maxSwing
    // Bedingung ppq(i) >= startPPQ - delta  ->  i >= (startPPQ - delta - cycleStart - maxSwing)/0.25
    // Bedingung ppq(i) <  endPPQ  + delta'  ->  i <  (endPPQ + delta' - cycleStart)/0.25
    // delta/delta' beruecksichtigen die +-1 Sample Rundungsunsicherheit (1/sppq PPQ).
    long long i0 = 0, i1 = 0;
    if (!toLLSafe(std::floor((p.startPPQ - cycleStartPPQ - maxSwing - 1.0 / sppq) / stepSecPPQ), i0))
    {
        invalidInputCounter().fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!toLLSafe(std::ceil((computeBlockEndPPQ(p) - cycleStartPPQ + 1.0 / sppq) / stepSecPPQ), i1))
    {
        invalidInputCounter().fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (i0 < 0) i0 = 0;
    if (i1 < i0) i1 = i0;
    // Kosten-Deckel gegen pathologische Eingaben (z. B. Host mit extrem hoher BPM oder
    // defektem startPPQ): verhindert einen praktisch endlosen Loop im Audio-Thread.
    // Steps, die nicht geprueft werden koennen, gehen verloren statt den Thread zu
    // blockieren. Der Verlust ist DIAGNOSTIZIERBAR: kMaxStepsPerBlockScan ist
    // exportiert, und beim Erreichen des Limits wird scanLimitTruncations() erhoeht.
    // Overflow-frei: beide Grenzen stammen aus toLLSafe (|wert| < 2^52), die
    // Differenz liegt damit weit innerhalb von long long.
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
        // Die Zuordnungsregel selbst entscheidet; der erweiterte Scanbereich
        // garantiert, dass jeder Kandidat des Sample-Bereichs erreicht wird.
        if (auto off = sampleOffsetForEventPPQ(p, ppq))
            sink(idx, *off, ppq);
    }
    (void)sStart; // sStart wird ueber sampleOffsetForEventPPQ erneut berechnet; hier
                  // nur als Guard gegen ungueltige startPPQ*sppq-Produkte benutzt.
}

} // namespace mds::timing
