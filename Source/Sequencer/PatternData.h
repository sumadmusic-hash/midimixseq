// Source/Sequencer/PatternData.h
// Reines Datenmodell 8 Tracks x 16 Steps mit Velocity (Plan 5.1, 6.2).
// Thread-Modell: Der Audio-Thread liest einen Snapshot; geschrieben wird auf dem
// Message-Thread. Hier bewusst ohne JUCE und ohne Locks testbar gehalten.
#pragma once

#include <array>
#include <cstdint>

namespace mds::pattern
{

inline constexpr int kNumTracks = 8;
inline constexpr int kNumSteps = 16;
inline constexpr std::uint8_t kDefaultVelocity = 100; // Plan 5.1 Startwert

struct Step
{
    bool on = false;
    std::uint8_t velocity = kDefaultVelocity;
};

using Pattern = std::array<std::array<Step, kNumSteps>, kNumTracks>;

class PatternController
{
public:
    const Pattern& snapshot() const noexcept { return current; }

    bool isOn(int track, int step) const noexcept
    {
        return valid(track, step) && current[track][step].on;
    }

    std::uint8_t velocity(int track, int step) const noexcept
    {
        return valid(track, step) ? current[track][step].velocity : 0;
    }

    // Toggle genau einmal pro gültigem Press-Aufruf (Plan 1.1 / 3.3).
    bool toggleStep(int track, int step) noexcept
    {
        if (!valid(track, step))
            return false;
        auto& s = current[track][step];
        s.on = !s.on;
        return true;
    }

    bool setStep(int track, int step, bool on, std::uint8_t vel) noexcept
    {
        if (!valid(track, step))
            return false;
        current[track][step] = Step{ on, clampVel(vel) };
        return true;
    }

    bool setVelocity(int track, int step, std::uint8_t vel) noexcept
    {
        if (!valid(track, step))
            return false;
        current[track][step].velocity = clampVel(vel);
        return true;
    }

    void clearTrack(int track) noexcept
    {
        if (track < 0 || track >= kNumTracks) return;
        for (auto& s : current[track]) s = Step{};
    }

    void clearAll() noexcept { current = Pattern{}; }

    void copyTrack(int from, int to) noexcept
    {
        if (!validIdx(from) || !validIdx(to)) return;
        current[to] = current[from];
    }

    void pasteTrack(const std::array<Step, kNumSteps>& src, int to) noexcept
    {
        if (!validIdx(to)) return;
        current[to] = src;
    }

    std::array<Step, kNumSteps> trackSnapshot(int track) const noexcept
    {
        if (!validIdx(track)) return {};
        return current[track];
    }

    // Live-Editing: Änderungen werden als neuer Snapshot übernommen; der Audio-Thread
    // sieht entweder alt oder vollständig neu, nie einen Mischzustand (Plan 5.5).
    void applySnapshot(const Pattern& p) noexcept { current = p; }

    static bool valid(int track, int step) noexcept
    {
        return track >= 0 && track < kNumTracks && step >= 0 && step < kNumSteps;
    }
    static bool validIdx(int track) noexcept { return track >= 0 && track < kNumTracks; }
    static std::uint8_t clampVel(std::uint8_t v) noexcept { return v > 127 ? 127 : v; }

private:
    Pattern current{};
};

} // namespace mds::pattern
