// Source/MidiMix/MidiParser.h
// Hardwarefreier Kern des MIDImix-Parsers (Plan 3.3).
//
// ACHTUNG – Mapping-Status: Alle konkreten Note-/CC-Nummern der MIDImix sind in V1
// UNVERIFIED und werden erst nach Phase-0-Captures festgelegt (Regel R6). Dieser Parser
// enthält deshalb bewusst KEINE fest codierten Werkswerte, sondern nur die protokollseitige
// Dekodierung sowie eine abbildbare Ereignisstruktur für Press/Release.
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
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

// Kapazitaets-Grenzen (P5): parseStreamBounded liefert hoechstens kMaxParseEvents
// pro Aufruf; StreamParser verarbeitet hoechstens kMaxParseEvents Nachrichten pro
// feed()-Aufruf. Alle Puffer sind fest dimensioniert – kein Heap-Wachstum und keine
// unkontrollierte Speichernutzung im Echtzeitpfad.
inline constexpr std::size_t kMaxParseEvents = 256;
// Obergrenze fuer zu verarbeitende Bytes pro parseStreamBounded-/feed()-Aufruf.
// Zusammen mit kMaxParseEvents folgt daraus: keine unbeschraenkte Arbeit und kein
// unkontrollierter Speicherverbrauch pro Aufruf, unabhaengig von der Eingabegroesse.
inline constexpr std::size_t kParseStreamByteLimit = 4096;

// ParseErgebnis: Anzahl geparster Events + Diagnose-Zaehler.
struct ParseResult
{
    int events = 0;          // gelieferte Events (<= kMaxParseEvents)
    int droppedOverflow = 0; // wegen Kapazitaetsgrenze verworfene Nachrichten
};

// Dekodiert einen Byte-Strom in einen FEST dimensionierten Event-Puffer
// (std::array, keine dynamische Vergroesserung). Running Status wird unterstuetzt:
// ein Datenbyte ohne vorangegangenen Status wird uebersprungen; nach einer
// Nachricht duerfen folgende Nachrichten derselben Kategorie das Statusbyte
// weglassen. Ueberschuessige Nachrichten (Kapazitaet erreicht) werden gezaehlt und
// verworfen – definiertes, ablesbares Ueberlaufverhalten statt unkontrolliertem
// Speicher. Unvollstaendige Restpakete koennen in diesem zustandslosen Pfad NICHT
// gepuffert werden; dafuer ist der zustaendige StreamParser zustaendig (unten).
// SysEx wird verworfen (Plan 3.7).
inline std::pair<ParseResult, std::array<Event, kMaxParseEvents>>
parseStreamBounded(const std::uint8_t* data, std::size_t size)
{
    std::array<Event, kMaxParseEvents> out{};
    ParseResult res;
    std::uint8_t running = 0; // 0 = kein Running-Status aktiv
    int consumed = 0;         // verbrauchte Nachrichten inkl. Ueberlauf (P5-Begrenzung)
    std::size_t i = 0;
    while (i < size)
    {
        const bool explicitStatus = isStatus(data[i]);
        std::uint8_t st;
        if (explicitStatus)
        {
            st = data[i];
            ++i; // echtes Statusbyte verbrauchen
        }
        else if (running != 0)
        {
            st = running; // Running Status: Statusbyte rekonstruieren, Datenbyte bleibt an i
        }
        else
        {
            ++i; // verwaistes Datenbyte ohne Status -> ueberspringen
            continue;
        }

        if (st == 0xF0) // SysEx: bis F7 verwerfen
        {
            while (i < size && data[i] != 0xF7) ++i;
            if (i < size) ++i;
            running = 0; // SysEx beendet jeden Running Status
            continue;
        }
        if ((st & 0xF0) == 0xF0) // uebrige System-Messages (Realtime/Clock etc.): 1 Byte
        {
            // Laufender, unvollstaendiger Channel-Befehl wird durch Statusbytes
            // abgebrochen; Running-Status-Tabelle bleibt laut Spec erhalten,
            // hier bewusst konservativ: nichts zuruecksetzen ausser explizitem Reset.
            continue;
        }

        const std::size_t need = static_cast<std::size_t>(expectedLength(st))
                               - (explicitStatus ? 1u : 0u); // noch benoetigte Datenbytes
        if (size - i < need)
            break; // Incomplete: Rest kann hier nicht gepuffert werden -> Stromende

        Event e;
        e.channel = channelOf(st);
        switch (st >> 4)
        {
            case 0x9:
                e.data1 = data[i];
                e.data2 = data[i + 1];
                e.type = (e.data2 == 0) ? Msg::NoteOff : Msg::NoteOn; // Plan 3.3
                break;
            case 0x8:
                e.type = Msg::NoteOff;
                e.data1 = data[i];
                e.data2 = data[i + 1];
                break;
            case 0xB:
                e.type = Msg::ControlChange;
                e.data1 = data[i];
                e.data2 = data[i + 1];
                break;
            case 0xC:
                e.type = Msg::ProgramChange;
                e.data1 = data[i];
                break;
            case 0xE:
                e.type = Msg::PitchBend;
                e.data1 = data[i];
                e.data2 = data[i + 1];
                break;
            default: // Aftertouch (0xA), Channel Pressure (0xD): V1 nicht benoetigt
                e.type = Msg::Unknown;
                break;
        }
        i += need;
        // P5-HARTE GRENZE: pro Aufruf werden hoechstens kMaxParseEvents Nachrichten
        // VERBRAUCHT (auch unbekannte). Ein Running-Status-Strom ueber beliebig viele
        // Nachrichten kann dadurch keine unbeschraenkte Arbeit und keinen
        // unkontrollierten Speicherverbrauch erzeugen; der Rest des Stroms wird
        // gezaehlt verworfen. out[] ist fest (std::array), res.events <= Kapazitaet.
        if (++consumed > static_cast<int>(kMaxParseEvents))
        {
            res.droppedOverflow += static_cast<int>((size - i + 2u) / 2u);
            break;
        }
        if (e.type == Msg::Unknown)
            continue;
        if (static_cast<std::size_t>(res.events) < kMaxParseEvents)
        {
            out[static_cast<std::size_t>(res.events)] = e;
            ++res.events;
        }
        else
        {
            ++res.droppedOverflow; // defensive Doppelgrenze; regulär über consumed erreicht
        }
        running = st; // Running Status merken (Channel-Messages)
    }
    return { res, out };
}

// Abwaerts-kompatibler Wrapper: lieferte bisher std::vector<Event>. Der Vektor
// wachst NUR hier am Rand (GUI-/Setup-Pfad), nie im Parser selbst; der
// Echtzeit-Pfad verwendet parseStreamBounded bzw. StreamParser.
inline std::vector<Event> parseStream(const std::uint8_t* data, std::size_t size)
{
    auto [res, arr] = parseStreamBounded(data, size);
    std::vector<Event> out;
    out.reserve(static_cast<std::size_t>(res.events));
    for (int i = 0; i < res.events; ++i)
        out.push_back(arr[static_cast<std::size_t>(i)]);
    return out;
}

// --- Zustandsbehafteter Stream-Parser (P5) -------------------------------------------
// Begrenzte Zustandsmaschine, die
//  1. unvollstaendige Nachrichten UBER AUFRUFGRENZEN hinweg korrekt puffert
//     (der zustandslose parseStream-Pfad konnte das bisher nicht),
//  2. Running Status ueber Aufrufe hinweg erhaelt,
//  3. ausschliesslich mit festen Array-Puffern arbeitet (kein Heap-Wachstum),
//  4. pro feed()-Aufruf hoechstens kMaxParseEvents Nachrichten liefert und den
//     Rest zaehlt (overflowEvents()) – definiertes Ueberlaufverhalten,
//  5. nach Ueberlauf oder Stoerung am naechsten Statusbyte wieder aufnimmt.
class StreamParser
{
public:
    using Sink = void (*)(void* ctx, const Event&);

    // Verarbeitet `size` Bytes und ruft fuer jedes fertige Event sink(ctx, e) auf.
    // Rueckgabe: Anzahl gelieferter Events (<= kMaxParseEvents). overflowEvents()/
    // droppedBytes() liefern die Diagnose. Keine Heap-Allokation, keine Sperren,
    // terminiert nach genau `size` Iterationen.
    std::size_t feed(const std::uint8_t* data, std::size_t size, Sink sink, void* ctx)
    {
        std::size_t delivered = 0;
        std::size_t consumedMsgs = 0; // P5: feste Verarbeitungsgrenze pro Aufruf
        for (std::size_t i = 0; i < size; ++i)
        {
            // Harte Grenzen pro Aufruf: hoechstens kMaxParseEvents verbrauchte
            // Nachrichten und kParseStreamByteLimit Bytes. Ueberschuessige Bytes
            // werden gezaehlt verworfen; der naechste feed()-Aufruf nimmt am
            // Statusbyte wieder auf (Resync), der Parser bleibt benutzbar.
            if (consumedMsgs >= kMaxParseEvents || i >= kParseStreamByteLimit)
            {
                // Rest des Stroms gezaehlt verwerfen; naechster feed()-Aufruf nimmt
                // am naechsten Statusbyte wieder auf (Resync), Parser bleibt benutzbar.
                ++overflowEvents_;
                droppedBytes_ += (size - i);
                break;
            }
            const std::uint8_t b = data[i];

            // System-Realtime-Bytes (0xF8..0xFB, 0xFE, 0xFF) duerfen ueberall
            // eingefuegt sein; sie beenden keine laufende Channel-Nachricht und
            // beeinflussen Running Status nicht. WICHTIG (P5-Korrektur): Sie werden
            // ignoriert, BEVOR Datenbytes gesammelt werden – ein Realtime-Byte ist
            // kein Datenbyte und darf eine Nachricht weder vervollstaendigen noch
            // ein erwartetes Datenbyte "verbrauchen".
            if (b >= 0xF8)
                continue;

            if (inSysex_)
            {
                if (b == 0xF7) inSysex_ = false; // SysEx-Ende; Inhalt wird verworfen (V1)
                continue;
            }
            if (b == 0xF0) { inSysex_ = true; msgLen_ = 0; continue; }
            if (isStatus(b))
            {
                if ((b & 0xF0) == 0xF0) // andere System-Message: 1 Byte, ignorieren
                {
                    continue;
                }
                status_ = b;            // neuer Status; halbfertige Nachricht verwerfen
                msgLen_ = 0;
                continue;
            }

            // Datenbyte: benoetigt letzten gueltigen Channel-Status
            if (status_ == 0)
                continue; // verwaistes Datenbyte ohne Status -> ueberspringen

            if (msgLen_ < sizeof(msgData_))
                msgData_[msgLen_++] = b;
            else
            {
                // Sicherheitspfad: mehr Datenbytes als das Laengenmodell erlaubt
                // (kann bei konsistentem expectedLength nicht eintreten). Resync.
                ++droppedBytes_;
                msgLen_ = 0;
                continue;
            }

            const std::size_t need = static_cast<std::size_t>(expectedLength(status_)) - 1u;
            if (msgLen_ < need)
                continue; // unvollstaendig -> bleibt gepuffert (ueber Aufrufgrenzen!)

            Event e;
            e.channel = channelOf(status_);
            switch (status_ >> 4)
            {
                case 0x9:
                    e.data1 = msgData_[0];
                    e.data2 = msgData_[1];
                    e.type = (e.data2 == 0) ? Msg::NoteOff : Msg::NoteOn;
                    break;
                case 0x8: e.type = Msg::NoteOff;       e.data1 = msgData_[0]; e.data2 = msgData_[1]; break;
                case 0xB: e.type = Msg::ControlChange; e.data1 = msgData_[0]; e.data2 = msgData_[1]; break;
                case 0xC: e.type = Msg::ProgramChange; e.data1 = msgData_[0]; break;
                case 0xE: e.type = Msg::PitchBend;     e.data1 = msgData_[0]; e.data2 = msgData_[1]; break;
                default:  e.type = Msg::Unknown; break; // 0xA/0xD: V1 nicht benoetigt
            }
            msgLen_ = 0; // Nachricht abgeschlossen; Running Status (status_) bleibt aktiv
            ++consumedMsgs;
            if (e.type == Msg::Unknown)
                continue;
            sink(ctx, e);
            ++delivered;
        }
        totalDelivered_ += delivered;
        return delivered;
    }

    std::size_t overflowEvents() const noexcept { return overflowEvents_; }
    std::size_t droppedBytes() const noexcept { return droppedBytes_; }
    std::size_t totalDelivered() const noexcept { return totalDelivered_; }
    bool hasPendingMessage() const noexcept { return msgLen_ > 0; }
    bool inSysex() const noexcept { return inSysex_; }

    // Expliziter Reset (z. B. Device-Disconnect): Zustand vollstaendig unbekannt.
    void reset() noexcept
    {
        status_ = 0; msgLen_ = 0; inSysex_ = false;
    }

private:
    std::uint8_t status_ = 0;              // letzter gueltiger Channel-Status (Running Status)
    std::uint8_t msgData_[2] = { 0, 0 };   // groesste Channel-Nachricht = 2 Datenbytes
    std::size_t msgLen_ = 0;
    bool inSysex_ = false;
    std::size_t overflowEvents_ = 0;
    std::size_t droppedBytes_ = 0;
    std::size_t totalDelivered_ = 0;
};

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
