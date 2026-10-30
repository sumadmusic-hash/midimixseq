// Tests/MidiParserTests.cpp – T0.6, T0.7, T0.9 (Parser, Taster, Pickup)
#include "TestFramework.h"
#include "../Source/MidiMix/MidiParser.h"

#include <array>
#include <vector>

using namespace mds::midiparse;

static std::vector<Event> parse(const std::vector<std::uint8_t>& bytes)
{
    return parseStream(bytes.data(), bytes.size());
}

MDS_TEST(note_on_and_off_with_channel)
{
    auto ev = parse({ 0x92, 0x30, 0x64, 0x82, 0x30, 0x40 });
    CHECK_EQ(ev.size(), 2u);
    CHECK(ev[0].type == Msg::NoteOn);
    CHECK_EQ(ev[0].channel, 2);
    CHECK_EQ(ev[0].data1, 0x30);
    CHECK_EQ(ev[0].data2, 0x64);
    CHECK(ev[1].type == Msg::NoteOff);
    CHECK_EQ(ev[1].channel, 2);
}

MDS_TEST(note_on_velocity_zero_is_note_off) // Plan 3.3
{
    auto ev = parse({ 0x99, 0x24, 0x00 });
    CHECK_EQ(ev.size(), 1u);
    CHECK(ev[0].type == Msg::NoteOff);
    CHECK(ev[0].isRelease());
    CHECK(!ev[0].isPress());
}

MDS_TEST(control_change_fader)
{
    auto ev = parse({ 0xB0, 0x07, 0x5A });
    CHECK_EQ(ev.size(), 1u);
    CHECK(ev[0].type == Msg::ControlChange);
    CHECK_EQ(ev[0].data1, 0x07);
    CHECK_EQ(ev[0].data2, 0x5A);
    CHECK_EQ(ev[0].channel, 0);
}

MDS_TEST(incomplete_packet_does_not_crash)
{
    auto ev = parse({ 0x90, 0x30 });            // 2 von 3 Bytes
    CHECK_EQ(ev.size(), 0u);
    auto ev2 = parse({ 0xB0, 0x01 });           // CC abgeschnitten
    CHECK_EQ(ev2.size(), 0u);
    auto ev3 = parse({});                       // leer
    CHECK_EQ(ev3.size(), 0u);
}

MDS_TEST(orphan_data_byte_skipped_without_corruption)
{
    // führendes Datenbyte, dann gültige Note
    auto ev = parse({ 0x40, 0x91, 0x33, 0x22 });
    CHECK_EQ(ev.size(), 1u);
    CHECK(ev[0].type == Msg::NoteOn);
    CHECK_EQ(ev[0].channel, 1);
    CHECK_EQ(ev[0].data1, 0x33);
}

MDS_TEST(sysex_is_discarded_v1_has_no_sysex) // Plan 3.7
{
    auto ev = parse({ 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7, 0x90, 0x3C, 0x7F });
    CHECK_EQ(ev.size(), 1u);              // nur die Note danach
    CHECK(ev[0].type == Msg::NoteOn);
}

MDS_TEST(program_and_pitchbend_lengths)
{
    CHECK_EQ(expectedLength(0xC0), 2);
    CHECK_EQ(expectedLength(0xE0), 3);
    CHECK_EQ(expectedLength(0x90), 3);
    auto ev = parse({ 0xC3, 0x05, 0xE4, 0x00, 0x40 });
    CHECK_EQ(ev.size(), 2u);
    CHECK(ev[0].type == Msg::ProgramChange);
    CHECK(ev[1].type == Msg::PitchBend);
    CHECK_EQ(ev[1].channel, 4);
}

// --- Taster: genau ein Toggle pro Press, Release nie ---------------------------------
MDS_TEST(button_press_toggles_once_release_never)
{
    ButtonEdgeDetector det(8);
    const Event press{ Msg::NoteOn, 0, 10, 100 };
    const Event release{ Msg::NoteOff, 0, 10, 0 };

    int toggles = 0;
    if (det.feed(press, 3)) ++toggles;
    if (det.feed(release, 3)) ++toggles;   // Release -> kein Toggle (Plan 1.1)
    CHECK_EQ(toggles, 1);
}

MDS_TEST(double_press_without_release_is_ignored) // Prellschutz, Plan 3.3
{
    ButtonEdgeDetector det(8);
    const Event press{ Msg::NoteOn, 0, 10, 100 };
    CHECK(det.feed(press, 0));
    CHECK(!det.feed(press, 0));            // zweiter Press ohne Release -> unterdrückt
    CHECK(det.isHeld(0));
    const Event release{ Msg::NoteOff, 0, 10, 0 };
    CHECK(!det.feed(release, 0));
    CHECK(!det.isHeld(0));
    CHECK(det.feed(press, 0));             // danach wieder gültig
}

MDS_TEST(solo_hold_distinguishes_from_plain_mute_press)
{
    // Kernfrage aus Plan 1.3: SOLO-Hold muss von normalen MUTE-Presses unterscheidbar sein.
    // Modellhaft: Detektor hält den SOLO-Zustand; während SOLO gehalten wird, interpretiert
    // der Parser MUTE-Presses als Spurwahl statt als Step-Toggle. Ob die REAL Hardware dieses
    // Event-Muster liefert, ist UNVERIFIED und muss in Phase 0.4 mit Captures belegt werden.
    ButtonEdgeDetector det(16);            // 0..7 MUTE, 8..15 SOLO
    const Event soloPress{ Msg::NoteOn, 0, 80, 100 };
    const Event mutePress{ Msg::NoteOn, 0, 10, 100 };
    const Event muteRelease{ Msg::NoteOff, 0, 10, 0 };

    det.feed(soloPress, 8);
    CHECK(det.isHeld(8));                       // SOLO wird gehalten
    CHECK(det.feed(mutePress, 2));              // gültiger Press -> würde Spur 3 wählen
    det.feed(muteRelease, 2);
    det.feed(soloPress, 8);                     // bereits held -> kein Re-Trigger
    // Nach SOLO-Release arbeiten MUTE-Tasten wieder als Steps:
    const Event soloRelease{ Msg::NoteOff, 0, 80, 0 };
    det.feed(soloRelease, 8);
    CHECK(!det.isHeld(8));
    CHECK(det.feed(mutePress, 2));              // jetzt Step-Toggle
}

// --- Soft Takeover / Pickup (Plan 3.5) -------------------------------------------------
MDS_TEST(pickup_blocks_jump_until_value_reached)
{
    PickupMonitor pm(8);
    // Plugin-Wert 0.8, Hardware steht bei 0.1 -> erste Bewegungen dürfen nichts ändern.
    CHECK(!pm.feed(0, 0.1f, 0.8f).has_value());
    CHECK(!pm.feed(0, 0.3f, 0.8f).has_value());
    CHECK(pm.isArmed(0));
    // Überholung des Plugin-Werts -> Übernahme
    auto v = pm.feed(0, 0.85f, 0.8f);
    CHECK(v.has_value());
    CHECK_NEAR(*v, 0.85f, 1e-6);
    CHECK(!pm.isArmed(0));
    // Danach folgen normale Änderungen
    CHECK(pm.feed(0, 0.9f, 0.85f).has_value());
}

MDS_TEST(pickup_rearms_on_page_or_device_change)
{
    PickupMonitor pm(4);
    pm.feed(1, 0.5f, 0.2f);               // übernommen
    pm.rearmAll(0.9f);                    // Seitenwechsel; Regler steht real bei 0.9
    CHECK(pm.isArmed(1));
    // Plugin-Wert 0.5, Regler noch 0.9 -> keine Änderung, kein Sprung
    CHECK(!pm.feed(1, 0.9f, 0.5f).has_value());
    // Abwärtsbewegung kreuzt 0.5 -> jetzt darf übernommen werden
    auto v = pm.feed(1, 0.4f, 0.5f);
    CHECK(v.has_value());
    CHECK_NEAR(*v, 0.4f, 1e-6);
    CHECK(!pm.isArmed(1));
}

MDS_TEST(pickup_no_assumption_without_known_start_position)
{
    // Nach dem Verbinden ist die Reglerposition unbekannt, bis ein erstes Event eintrifft.
    PickupMonitor pm(2);
    pm.disarmToUnknownPosition(0);
    CHECK(pm.isArmed(0));
    // Erstes Event: Referenz ist der alte Default 0 -> bewusste, dokumentierte Annahme
    // vermeiden wir, indem das erste Event nur die Position lernt, wenn sie nicht auf dem
    // Parameterwert liegt.
    auto first = pm.feed(0, 0.7f, 0.2f);
    CHECK(!first.has_value());            // kein Sprung auf 0.7
    auto second = pm.feed(0, 0.75f, 0.2f);
    CHECK(!second.has_value());           // weiterhin gesperrt
    auto third = pm.feed(0, 0.2f, 0.2f);  // erreicht den Plugin-Wert
    CHECK(third.has_value());
}

int main() { return mds::test::runAll(); }
