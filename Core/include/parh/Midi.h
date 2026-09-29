/**
 * @file Midi.h
 * @brief Standard MIDI File export of a score (PLAN 9).
 *
 * Format 1, 960 ticks per quarter note. Track 0 is the conductor track: name, 4/4, key signature, the tempo map, the
 * markers and the form's operations (as markers "op: add open hat"). Every part with notes gets its own track: the
 * kick and the kit's lanes on channel 10 with their General MIDI notes, so a DAW's drum rack recognises them, one
 * track per lane so each can be muted; the sub on channel 1, the ping 2, the bass 3, the 303 line 4, the chord 5, the
 * drone 6, the texture 7 (a note per bar where it plays).
 *
 * Tempo ramps are written as one tempo event per beat whose value makes that beat last exactly as long as it does in
 * the ramp (Phosphene's and Ephemeris' rule). Automation, when the parameters are given, goes on a track "controls",
 * channel 16: every automated knob a controller number from 20 on, in the order of the parameter ids, its value
 * 0..127 every thirty-second note while it moves.
 *
 * @note The encoder is copied from Ephemeris `Core/src/Midi.cpp` at d047d79 (27.09.2026); parts and controllers are
 *       Totality's.
 * @note Copied from Totality `Core/include/tot/Midi.h` at 4d3c0d2 (29.09.2026); namespace parh, prefix PARH_.
 */
#pragma once
#include "parh/Params.h"
#include "parh/Score.h"
#include <cstdint>
#include <vector>

namespace parh {

constexpr int kMidiPpq = 960;   ///< ticks per quarter note in exported files

/** @brief MIDI channel (0-based) a part is written to. */
int midiChannelOf(Part part);

/** @brief Encodes a score as a Standard MIDI File (format 1). Notes need not be sorted. */
std::vector<uint8_t> encodeMidi(const Score& score, const char* title = "Parhelion", const ParamStore* params = nullptr);

/** @brief Writes encodeMidi() to a file; false if it cannot be written. */
bool writeMidiFile(const Score& score, const char* path, const char* title = "Parhelion", const ParamStore* params = nullptr);

} // namespace parh
