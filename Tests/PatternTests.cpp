// Tests/PatternTests.cpp – T0.5 Pattern-Datenmodell und Bankseiten-Logik (T0.8)
#include "TestFramework.h"
#include "../Source/Sequencer/PatternData.h"

using namespace mds::pattern;

MDS_TEST(pattern_defaults)
{
    PatternController pc;
    CHECK_EQ(kNumTracks, 8);
    CHECK_EQ(kNumSteps, 16);
    for (int t = 0; t < kNumTracks; ++t)
        for (int s = 0; s < kNumSteps; ++s)
        {
            CHECK(!pc.isOn(t, s));
            CHECK_EQ(pc.velocity(t, s), kDefaultVelocity); // Startwert 100 (Plan 5.1)
        }
}

MDS_TEST(toggle_is_single_and_reversible)
{
    PatternController pc;
    CHECK(pc.toggleStep(2, 5));
    CHECK(pc.isOn(2, 5));
    CHECK(pc.toggleStep(2, 5));
    CHECK(!pc.isOn(2, 5));
}

MDS_TEST(out_of_range_is_rejected_not_crashing)
{
    PatternController pc;
    CHECK(!pc.toggleStep(-1, 0));
    CHECK(!pc.toggleStep(8, 0));
    CHECK(!pc.toggleStep(0, 16));
    CHECK(pc.setStep(7, 15, true, 200));    // Index gültig, Velocity wird auf 127 clampiert
    CHECK_EQ(pc.velocity(7, 15), 127);
    CHECK(!pc.setStep(8, 0, true, 100));    // Spur außerhalb -> abgelehnt
    CHECK(!pc.setVelocity(0, 16, 50));      // Step außerhalb -> abgelehnt
    CHECK(!pc.isOn(99, 99));
    CHECK_EQ(pc.velocity(99, 99), 0);
}

MDS_TEST(snapshot_rollback_for_live_editing)
{
    PatternController pc;
    pc.setStep(0, 0, true, 90);
    const auto before = pc.snapshot();       // Audio-Thread-Arbeitskopie
    pc.setStep(0, 1, true, 100);             // Live-Edit auf Message-Thread
    CHECK(pc.isOn(0, 1));
    pc.applySnapshot(before);                // Rollback möglich, keine Mischzustände
    CHECK(!pc.isOn(0, 1));
    CHECK(pc.isOn(0, 0));
}

MDS_TEST(copy_paste_clear)
{
    PatternController pc;
    pc.setStep(3, 4, true, 111);
    pc.copyTrack(3, 6);
    CHECK(pc.isOn(6, 4));
    CHECK_EQ(pc.velocity(6, 4), 111);
    pc.clearTrack(3);
    CHECK(!pc.isOn(3, 4));
    CHECK(pc.isOn(6, 4));
    pc.clearAll();
    CHECK(!pc.isOn(6, 4));
}

// --- Bankseiten (Plan 1.2): zyklische Seiten A/B/C -----------------------------------
namespace
{
enum class Page : int { A = 0, B = 1, C = 2 };
constexpr int kNumPages = 3;

Page bankRight(Page p) { return static_cast<Page>((static_cast<int>(p) + 1) % kNumPages); }
Page bankLeft(Page p) { return static_cast<Page>((static_cast<int>(p) + kNumPages - 1) % kNumPages); }
}

MDS_TEST(bank_right_A_B_C_A)
{
    Page p = Page::A;
    p = bankRight(p); CHECK(p == Page::B);
    p = bankRight(p); CHECK(p == Page::C);
    p = bankRight(p); CHECK(p == Page::A);
}

MDS_TEST(bank_left_A_C_B_A)
{
    Page p = Page::A;
    p = bankLeft(p); CHECK(p == Page::C);
    p = bankLeft(p); CHECK(p == Page::B);
    p = bankLeft(p); CHECK(p == Page::A);
}

MDS_TEST(mute_maps_to_steps_1_8_recarm_to_steps_9_16)
{
    // Plan 1.1: MUTE 1..8 -> Steps 1..8, REC ARM 1..8 -> Steps 9..16 der gewählten Spur.
    PatternController pc;
    const int track = 4;
    for (int btn = 0; btn < 8; ++btn)
    {
        const int muteStep = btn;          // Steps 1-8  (Index 0-7)
        const int recStep = btn + 8;       // Steps 9-16 (Index 8-15)
        CHECK(pc.toggleStep(track, muteStep));
        CHECK(pc.toggleStep(track, recStep));
    }
    for (int s = 0; s < 16; ++s) CHECK(pc.isOn(track, s));
}

int main() { return mds::test::runAll(); }
