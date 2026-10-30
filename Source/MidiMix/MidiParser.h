// Source/MidiMix/MidiParser.h
// Hardwarefreier Kern des MIDImix-Parsers (Plan 3.3).
//
// ACHTUNG – Mapping-Status: Alle konkreten Note-/CC-Nummern der MIDImix sind in V1
// UNVERIFIED und werden erst nach Phase-0-Captures festgelegt (Regel R6). Dieser Parser
// enthält deshalb bewusst KEINE fest codierten Werkswerte, sondern nur die protokollseitige
// Dekodierung sowie eine abbildbare Ereignisstruktur für Press/Release.
#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace mds::midiparse
{

enum class Msg : std::uint8_t
{
    None,
    NoteOn,       // Note-On mit Velocity > 0  -> Press
    NoteOff,      // echte Note-Off ODER Note-On mit Velocity 0
    ControlChange,
    ProgramChange,
    PitchBend,
    Incomplete,   // Daten reichen nicht -> Paket bleibt gepuffert
    Unknown
};

struct Event
{
    Msg type = Msg::None;
    int channel = -1;      // 0..15
    int data1 = -1;        // Note-Nummer / CC-Nummer
    int data2 = -1;        // Velocity / CC-Wert
    double timestampSamples = 0.0; // Host-/Device-Zeitstempel, soweit vorhanden
    bool isPress() const { return type == Msg::NoteOn; }
    bool isRelease() const { return type == Msg::NoteOff; }
};

inline int statusNibble(std::uint8_t b) { return b >> 4; }
inline int channelOf(std::uint8_t b) { return b & 0x0F; }

inline bool isStatus(std::uint8_t b) { return (b & 0x80) != 0; }

// Erwartete Gesamtlänge einer Nachricht inkl. Statusbyte (Running-Status hier
// nicht unterstützt – bewusste Einschränkung, dokumentiert in docs/03_test_strategy.md).
inline int expectedLength(std::uint8_t status)
{
    switch (statusNibble(status))
    {
        case 0x8: case 0x9: case 0xA: case 0xB: case 0xE: return 3;
        case 0xC: case 0xD: return 2;
        default: return 1; // SysEx/Realtime: separat zu behandeln, in V1 nicht benötigt
    }
}

// Dekodiert einen Byte-Strom vollständig in Events. Unvollständige Restpakete werden
// ignoriert (kein Absturz); SysEx wird verworfen, da SysEx nicht Teil von V1 ist (Plan 3.7).
inline std::vector<Event> parseStream(const std::uint8_t* data, std::size_t size)
{
    std::vector<Event> out;
    std::size_t i = 0;
    while (i < size)
    {
        const std::uint8_t st = data[i];
        if (!isStatus(st)) { ++i; continue; }          // verwaistes Datenbyte überspringen
        if (st == 0xF0)                                 // SysEx: bis F7 verwerfen
        {
            while (i < size && data[i] != 0xF7) ++i;
            if (i < size) ++i;
            continue;
        }
        const int len = expectedLength(st);
        if (static_cast<int>(size - i) < len) { break; } // Incomplete -> Rest verwerfen/puffern

        Event e;
        e.channel = channelOf(st);
        switch (statusNibble(st))
        {
            case 0x9: // Note On
                e.data1 = data[i + 1];
                e.data2 = data[i + 2];
                // Plan 3.3: Note-On mit Velocity 0 zählt als Note-Off.
                e.type = (e.data2 == 0) ? Msg::NoteOff : Msg::NoteOn;
                break;
            case 0x8: // Note Off
                e.type = Msg::NoteOff;
                e.data1 = data[i + 1];
                e.data2 = data[i + 2];
                break;
            case 0xB:
                e.type = Msg::ControlChange;
                e.data1 = data[i + 1];
                e.data2 = data[i + 2];
                break;
            case 0xC:
                e.type = Msg::ProgramChange;
                e.data1 = data[i + 1];
                break;
            case 0xE:
                e.type = Msg::PitchBend;
                e.data1 = data[i + 1];
                e.data2 = data[i + 2];
                break;
            default:
                e.type = Msg::Unknown;
                break;
        }
        if (e.type != Msg::Unknown)
            out.push_back(e);
        i += static_cast<std::size_t>(len);
    }
    return out;
}

// --- Taster-Semantik (Plan 1.1, 3.3) --------------------------------------------------
// Ein Tastendruck toggelt genau einmal. Release löst nichts aus. Doppelte Presses ohne
// dazwischenliegendes Release werden als Prellung behandelt und ignorieren den zweiten
// Press (Schutz gegen ungewollte Mehrfach-Toggles).
class ButtonEdgeDetector
{
public:
    explicit ButtonEdgeDetector(std::size_t numButtons) : pressed(numButtons, false) {}

    // true = gültiger Press (Aktion ausführen), false = Release oder unterdrückter Doppelpress.
    bool feed(const Event& e, int buttonIndex)
    {
        if (buttonIndex < 0 || static_cast<std::size_t>(buttonIndex) >= pressed.size())
            return false;
        if (e.isPress())
        {
            if (pressed[buttonIndex])
                return false;          // bereits gedrückt -> kein zweiter Toggle
            pressed[buttonIndex] = true;
            return true;
        }
        if (e.isRelease())
        {
            pressed[buttonIndex] = false;
            return false;              // Release toggelt nie
        }
        return false;
    }

    bool isHeld(int buttonIndex) const
    {
        return buttonIndex >= 0 && static_cast<std::size_t>(buttonIndex) < pressed.size()
            && pressed[static_cast<std::size_t>(buttonIndex)];
    }

    void resetAll() { pressed.assign(pressed.size(), false); }

private:
    std::vector<bool> pressed;
};

// --- Soft Takeover / Pickup (Plan 3.5) ------------------------------------------------
// Ein Hardware-Regler übernimmt erst, wenn sein physischer Wert den aktuellen Plugin-Wert
// erreicht oder kreuzt. Verhindert Parametersprünge beim Verbinden oder Seitenwechsel.
class PickupMonitor
{
public:
    // hasPosition == false: die reale Hardwareposition ist noch unbekannt (z. B. direkt
    // nach dem Verbinden). Dann lernt der erste Ereigniswert nur die Position und ändert
    // den Parameter nicht – eine erfundene Startannahme wäre ein verbotener Default.
    struct State { bool armed = true; bool hasPosition = false; float lastValue = 0.0f; };

    explicit PickupMonitor(std::size_t numControls) : states(numControls) {}

    // newRaw: 0..1 Rohwert der Hardware; paramValue: aktueller Plugin-Wert 0..1.
    // Rückgabe: Zu übertragender Wert oder nullopt, wenn noch nicht übernommen.
    std::optional<float> feed(int control, float newRaw, float paramValue)
    {
        if (control < 0 || static_cast<std::size_t>(control) >= states.size())
            return std::nullopt;
        auto& s = states[static_cast<std::size_t>(control)];

        if (s.armed)
        {
            if (!s.hasPosition)
            {
                // Erster Ereigniswert: Position lernen, Parameter unverändert lassen.
                s.hasPosition = true;
                s.lastValue = newRaw;
                return std::nullopt;
            }
            // Kreuzungstest gegen die letzte bekannte Hardwareposition: übernommen wird
            // erst, wenn der Regler den aktuellen Plugin-Wert erreicht oder überquert.
            const bool reached = crossedOrReached(s.lastValue, newRaw, paramValue);
            s.lastValue = newRaw;          // Referenz bleibt die reale Hardwareposition
            if (!reached)
                return std::nullopt;       // noch nicht übernommen -> kein Parametersprung
            s.armed = false;               // übernommen
        }
        else
        {
            s.lastValue = newRaw;
            s.hasPosition = true;
        }
        return newRaw;
    }

    // Neu verbinden, Spur-/Seitenwechsel -> wieder sperren.
    // Sperre aufheben und bei bekannter Hardware-Startposition referenzieren.
    void rearm(int control, float hardwareStartValue)
    {
        if (control >= 0 && static_cast<std::size_t>(control) < states.size())
        {
            auto& s = states[static_cast<std::size_t>(control)];
            s.armed = true;
            s.hasPosition = true;
            s.lastValue = hardwareStartValue;
        }
    }
    void rearmAll(float hardwareStartValue)
    {
        for (auto& s : states) { s.armed = true; s.hasPosition = true; s.lastValue = hardwareStartValue; }
    }
    // Sperren ohne bekannte Position: erster Ereigniswert lernt nur, er verändert nichts.
    void disarmToUnknownPosition(int control)
    {
        if (control >= 0 && static_cast<std::size_t>(control) < states.size())
        {
            auto& s = states[static_cast<std::size_t>(control)];
            s.armed = true;
            s.hasPosition = false;
        }
    }
    void disarmAllToUnknownPosition()
    {
        for (auto& s : states) { s.armed = true; s.hasPosition = false; }
    }
    bool isArmed(int control) const { return control >= 0 && static_cast<std::size_t>(control) < states.size() && states[static_cast<std::size_t>(control)].armed; }

private:
    static bool crossedOrReached(float prevRaw, float newRaw, float target)
    {
        if (prevRaw <= target && target <= newRaw) return true;
        if (newRaw <= target && target <= prevRaw) return true;
        // Der Startwert der Hardware liegt bereits auf dem Parameterwert: keine Bewegung
        // nötig, sofortige Übernahme möglich.
        return std::abs(prevRaw - target) < kPickupEpsilon;
    }

    static constexpr float kPickupEpsilon = 0.002f;

    std::vector<State> states;
};

} // namespace mds::midiparse
