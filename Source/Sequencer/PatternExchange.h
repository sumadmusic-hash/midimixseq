// Source/Sequencer/PatternExchange.h
// Thread-Sicheres Pattern-Ownership-Modell (Audit P1).
//
// Ownership:
//  - Der Message-/GUI-Thread besitzt das bearbeitbare Editier-Pattern (EditorPattern).
//  - Der Audio-Thread besitzt seine eigene aktive Kopie (ActivePattern) und verändert
//    sie ausschließlich selbst, an der Blockgrenze, aus einer fest dimensionierten
//    SPSC-Ringpuffer-Queue.
//  - CoreMIDI-Callback-Eingänge dürfen NICHT direkt in die Queue schreiben, solange es
//    mehrere Producer geben könnte; sie müssen auf den einen Producer-Thread
//    (Message-Thread) serialisiert werden. Die Queue ist bewusst SPSC ausgelegt –
//    Typen (ProducerHandle/ConsumerHandle) erzwingen die Rollenverteilung.
//
// Kein Feld wird jemals gleichzeitig von zwei Threads gelesen und geschrieben:
//  - EditorPattern: nur Producer-Thread.
//  - ActivePattern live_/stage_: nur Audio-Thread.
//  - Queue-Slots: Single-Producer/Single-Consumer-Ring mit acquire/release-Zählern.
//  - Feedback (getSnapshot): kopiert unter einem leichten Spinlock NUR auf der
//    Producer-Seite; der Audio-Thread nimmt niemals diesen Lock.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <utility>

#include "PatternData.h"

namespace mds::pattern
{

// Eine atomar übertragbare Pattern-Änderung. Vollständige Steps/Snaps vermeiden
// Teilaktualisierungen (on/velocity werden nie getrennt gelesen).
struct PatternMessage
{
    enum class Kind : std::uint8_t
    {
        SetStep,      // ein Step mit on+velocity setzen
        ToggleStep,   // genau einmal toggeln (Read-modify-write im Audio-Thread)
        ClearTrack,   // eine Spur zurücksetzen
        CopyTrack,    // Spur from -> to innerhalb der aktiven Kopie
        FullSnapshot  // vollständiges Pattern übernehmen (applySnapshot / pasteTrack)
    };

    Kind kind = Kind::SetStep;
    int track = 0;
    int step = 0;
    bool on = false;
    std::uint8_t velocity = kDefaultVelocity;
    int otherTrack = 0;     // CopyTrack: Quellspur
    Pattern snapshot{};     // FullSnapshot: vollständiger Zustand (POD-Kopie)

    static PatternMessage makeSetStep(int t, int s, bool on_, std::uint8_t vel) noexcept
    {
        PatternMessage m;
        m.kind = Kind::SetStep;
        m.track = t; m.step = s; m.on = on_; m.velocity = clampVelStatic(vel);
        return m;
    }
    static PatternMessage makeToggleStep(int t, int s) noexcept
    {
        PatternMessage m;
        m.kind = Kind::ToggleStep;
        m.track = t; m.step = s;
        return m;
    }
    static PatternMessage makeClearTrack(int t) noexcept
    {
        PatternMessage m;
        m.kind = Kind::ClearTrack;
        m.track = t;
        return m;
    }
    static PatternMessage makeCopyTrack(int from, int to) noexcept
    {
        PatternMessage m;
        m.kind = Kind::CopyTrack;
        m.track = to; m.otherTrack = from;
        return m;
    }
    static PatternMessage makeFullSnapshot(const Pattern& p) noexcept
    {
        PatternMessage m;
        m.kind = Kind::FullSnapshot;
        m.snapshot = p;
        return m;
    }

private:
    static std::uint8_t clampVelStatic(std::uint8_t v) noexcept { return v > 127 ? 127 : v; }
};

// Fest dimensionierter SPSC-Ringpuffer. Kapazität muss Zweierpotenz sein.
// push() (tryPush) ist nicht blockierend; bei voll liefert er false und zählt den
// Überlauf mit – es gibt keinen stillschweigenden Verlust.
template <std::size_t Capacity>
class SpscPatternQueue
{
    static_assert((Capacity & (Capacity - 1)) == 0 && Capacity > 1,
                  "Kapazität muss eine Zweierpotenz > 1 sein");
public:
    struct ProducerHandle
    {
        bool tryPush(const PatternMessage& m) noexcept { return q->tryPushImpl(m); }
        std::size_t droppedCount() const noexcept { return q->drops.load(std::memory_order_relaxed); }
        std::size_t capacity() const noexcept { return Capacity; }
        SpscPatternQueue* q;
    };
    struct ConsumerHandle
    {
        bool pop(PatternMessage& out) noexcept { return q->popImpl(out); }
        std::size_t size() const noexcept
        {
            return q->head.load(std::memory_order_acquire) - q->tail.load(std::memory_order_relaxed);
        }
        SpscPatternQueue* q;
    };

    std::pair<ProducerHandle, ConsumerHandle> handles() noexcept
    {
        return { ProducerHandle{ this }, ConsumerHandle{ this } };
    }

    void reset() noexcept
    {
        head.store(0, std::memory_order_relaxed);
        tail.store(0, std::memory_order_relaxed);
        drops.store(0, std::memory_order_relaxed);
    }

private:
    friend struct ProducerHandle;
    friend struct ConsumerHandle;

    bool tryPushImpl(const PatternMessage& m) noexcept
    {
        const std::size_t h = head.load(std::memory_order_relaxed);
        const std::size_t t = tail.load(std::memory_order_acquire);
        if (h - t >= Capacity)
        {
            drops.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        slots_[h & (Capacity - 1)] = m;
        head.store(h + 1, std::memory_order_release);
        return true;
    }

    bool popImpl(PatternMessage& out) noexcept
    {
        const std::size_t t = tail.load(std::memory_order_relaxed);
        const std::size_t h = head.load(std::memory_order_acquire);
        if (t == h)
            return false;
        out = slots_[t & (Capacity - 1)];
        tail.store(t + 1, std::memory_order_release);
        return true;
    }

    alignas(64) std::atomic<std::size_t> head{ 0 }; // schreibt nur Producer
    alignas(64) std::atomic<std::size_t> tail{ 0 }; // schreibt nur Consumer
    std::atomic<std::size_t> drops{ 0 };            // schreibt nur Producer
    PatternMessage slots_[Capacity]{};
};

inline constexpr std::size_t kPatternQueueCapacity = 256;
// Begrenztes Entnehmen pro Audio-Block: garantiert terminierende Arbeit im Audio-Thread.
inline constexpr std::size_t kMaxMessagesPerBlock = 64;

using PatternQueue = SpscPatternQueue<kPatternQueueCapacity>;

// Die aktive Audio-Kopie. Wird AUSSCHLIESSLICH vom Audio-Thread verändert/gelesen.
// processMessages() schreibt in die Stage-Kopie; commitBoundary() hebt sie an der
// Blockgrenze auf die Live-Kopie. Innerhalb eines Blocks sieht der Sequencer damit
// immer einen konsistenten Gesamtzustand (Plan 5.5: "entweder alt oder vollständig neu").
class ActivePattern
{
public:
    bool isOn(int track, int step) const noexcept { return live_[track][step].on; }
    std::uint8_t velocity(int track, int step) const noexcept { return live_[track][step].velocity; }
    const Pattern& livePattern() const noexcept { return live_; }
    Pattern getSnapshot() const noexcept { return stage_; }

    void processMessage(const PatternMessage& m) noexcept
    {
        using K = PatternMessage::Kind;
        switch (m.kind)
        {
            case K::SetStep:
                if (valid(m.track, m.step))
                    stage_[m.track][m.step] = Step{ m.on, clampVel(m.velocity) };
                break;
            case K::ToggleStep:
                if (valid(m.track, m.step))
                    stage_[m.track][m.step].on = !stage_[m.track][m.step].on;
                break;
            case K::ClearTrack:
                if (validIdx(m.track))
                    for (auto& s : stage_[m.track]) s = Step{};
                break;
            case K::CopyTrack:
                if (validIdx(m.otherTrack) && validIdx(m.track))
                    stage_[m.track] = stage_[m.otherTrack];
                break;
            case K::FullSnapshot:
                stage_ = m.snapshot;
                break;
        }
    }

    // Einmal pro Audio-Block an der Blockgrenze aufrufen (vor der Trigger-Berechnung).
    void commitBoundary() noexcept { live_ = stage_; }

private:
    static bool valid(int track, int step) noexcept
    { return track >= 0 && track < kNumTracks && step >= 0 && step < kNumSteps; }
    static bool validIdx(int track) noexcept { return track >= 0 && track < kNumTracks; }
    static std::uint8_t clampVel(std::uint8_t v) noexcept { return v > 127 ? 127 : v; }

    Pattern live_{};
    Pattern stage_{};
};

// Producer-Seitige Spin-Fahne (test-and-set, kein Sleep, keine Heap-Allokation).
// Wird ausschließlich VOM PRODUCER benutzt; der Audio-Thread nimmt sie nie.
class ProducerLock
{
public:
    void lock() noexcept
    {
        while (flag_.exchange(true, std::memory_order_acquire))
        {
            while (flag_.load(std::memory_order_relaxed))
            {
#if defined(__x86_64__) || defined(__i386__)
                __builtin_ia32_pause();
#endif
            }
        }
    }
    void unlock() noexcept { flag_.store(false, std::memory_order_release); }

private:
    std::atomic<bool> flag_{ false };
};

// Editor-Pattern: Besitz des Message-/GUI-Threads. Alle Lesezugriffe des Audio-
// Systems laufen über die Queue/ActivePattern; dieses Objekt wird NIEMALS vom
// Audio-Thread angefasst. applySnapshot/getSnapshot sind über ProducerLock gegen
// weitere Producer-seitige Zugriffe (Dispatcher) serialisiert.
class EditorPattern
{
public:
    explicit EditorPattern(PatternQueue::ProducerHandle producer) noexcept
        : prod_(producer) {}

    // --- Producer-seitig bearbeitbares Muster -------------------------------------
    bool toggleStep(int track, int step) noexcept
    {
        if (!PatternController::valid(track, step))
            return false;
        auto& s = current_[track][step];
        s.on = !s.on;
        send(PatternMessage::makeToggleStep(track, step));
        return true;
    }

    bool setStep(int track, int step, bool on, std::uint8_t vel) noexcept
    {
        if (!PatternController::valid(track, step))
            return false;
        current_[track][step] = Step{ on, PatternController::clampVel(vel) };
        send(PatternMessage::makeSetStep(track, step, on, vel));
        return true;
    }

    void clearTrack(int track) noexcept
    {
        if (!PatternController::validIdx(track)) return;
        for (auto& s : current_[track]) s = Step{};
        send(PatternMessage::makeClearTrack(track));
    }

    void clearAll() noexcept
    {
        current_ = Pattern{};
        send(PatternMessage::makeFullSnapshot(current_));
    }

    void copyTrack(int from, int to) noexcept
    {
        if (!PatternController::validIdx(from) || !PatternController::validIdx(to)) return;
        current_[to] = current_[from];
        send(PatternMessage::makeCopyTrack(from, to));
    }

    void pasteTrack(const std::array<Step, kNumSteps>& src, int to) noexcept
    {
        if (!PatternController::validIdx(to)) return;
        current_[to] = src;
        send(PatternMessage::makeFullSnapshot(current_)); // Track gilt ab nächstem Block
    }

    std::array<Step, kNumSteps> trackSnapshot(int track) const noexcept
    {
        if (!PatternController::validIdx(track)) return {};
        return current_[track];
    }

    // Kopie, keine Referenz auf interne Daten (Anforderung 2.2).
    Pattern snapshot() const noexcept { return current_; }

    void applySnapshot(const Pattern& p) noexcept
    {
        current_ = p;
        send(PatternMessage::makeFullSnapshot(p));
    }

    // --- Queue-Übertragung ----------------------------------------------------------
    // trySend: nie blockierend; false + Zähler bei vollem Queue. Aufrufer (Producer)
    // muss dann synchronisieren, z. B. per sendSnapshotCompensation().
    bool trySend(const PatternMessage& m) noexcept { return prod_.tryPush(m); }

    // Kompensation nach Überlauf: vollständigen aktuellen Zustand als EINE Nachricht
    // senden. Schlägt auch diese fehl (Queue persistent voll), bleibt der Zustand
    // zählbar verloren (droppedCount steigt) – definiert, dokumentiert, getestet.
    bool sendSnapshotCompensation() noexcept { return prod_.tryPush(PatternMessage::makeFullSnapshot(current_)); }

    std::size_t droppedCount() const noexcept { return prod_.droppedCount(); }

private:
    void send(const PatternMessage& m) noexcept
    {
        // Einzelner Producer: direkte Zustellung; bei vollem Queue zählt der
        // Overflow und die nächste FullSnapshot-Kompensation holt den Zustand.
        if (!prod_.tryPush(m))
            pendingOverflow_ = true;
    }

    PatternQueue::ProducerHandle prod_;
    Pattern current_{};
    bool pendingOverflow_ = false; // für Diagnose; Kompensation erfolgt explizit durch den Caller
};

// Audio-seitiger Konsument: entnimmt begrenzt pro Block und aktualisiert die aktive
// Kopie. Keine Locks, keine Allokation, keine Freigabe, terminiert immer.
class AudioPatternConsumer
{
public:
    explicit AudioPatternConsumer(PatternQueue::ConsumerHandle consumer) noexcept
        : cons_(consumer) {}

    // In processBlock() AN DER BLOCKGRENZE AUFFRUFEN, vor der Trigger-Berechnung.
    // Verarbeitet höchstens kMaxMessagesPerBlock Nachrichten; der Rest bleibt in der
    // Queue und wird im nächsten Block verarbeitet (definiert, kein Verlust).
    void beginBlock() noexcept
    {
        PatternMessage m;
        std::size_t n = 0;
        while (n < kMaxMessagesPerBlock && cons_.pop(m))
        {
            active_.processMessage(m);
            ++n;
        }
        consumedTotal_ += n;
        active_.commitBoundary();
    }

    const ActivePattern& active() const noexcept { return active_; }
    ActivePattern& active() noexcept { return active_; }
    std::size_t queueDepth() const noexcept { return cons_.size(); }
    std::size_t consumedTotal() const noexcept { return consumedTotal_; }

private:
    PatternQueue::ConsumerHandle cons_;
    ActivePattern active_;
    std::size_t consumedTotal_ = 0;
};

// Komfortabler Aufbau: ein Queue-Objekt, beide Handles, Editor + Audio-Konsument.
// Hinweis: Das Queue-Objekt muss leben, solange Editor/Audio-Consumer verwendet werden.
struct PatternExchange
{
    PatternQueue queue;
    EditorPattern editor;
    AudioPatternConsumer audio;

    PatternExchange()
        : queue(), editor(queue.handles().first), audio(queue.handles().second) {}
};

} // namespace mds::pattern
