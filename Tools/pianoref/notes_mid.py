"""The test notes for the piano comparison (30.09.2026, the user: "Du kannst es gerne mit Pianoteq vergleichen und daran
abstimmen"): a MIDI file of single notes across the keyboard at four velocities, each held for a while and released, the
sustain pedal up -- the same list parh_pianoprobe plays (--notes), so the two pianos are measured note for note.

    python Tools/pianoref/notes_mid.py out.mid [--list]

--list prints the list in parh_pianoprobe's --notes form instead (pitch:velocity:seconds, comma separated).
"""
import struct
import sys

PITCHES = [24, 36, 43, 48, 55, 60, 64, 67, 72, 79, 84, 96]   # C1 .. C7
VELOCITIES = [32, 64, 96, 124]
HOLD = 3.0      # seconds the key is down
GAP = 2.0       # seconds after the release (the damper and what rings on)
BPM = 120.0     # one beat = 0.5 s
TPQ = 480


def notes():
    """(start s, pitch, velocity 1..127, hold s) in playing order."""
    t, out = 0.5, []
    for p in PITCHES:
        for v in VELOCITIES:
            out.append((t, p, v, HOLD))
            t += HOLD + GAP
    return out


def vlq(n):
    b = [n & 0x7F]
    n >>= 7
    while n:
        b.append(0x80 | (n & 0x7F))
        n >>= 7
    return bytes(reversed(b))


def write_mid(path):
    ev = []
    for start, p, v, hold in notes():
        ev.append((start, bytes([0x90, p, v])))
        ev.append((start + hold, bytes([0x80, p, 0])))
    ev.sort(key=lambda e: e[0])
    ticks_per_s = TPQ * BPM / 60.0
    track = b"\x00\xff\x51\x03" + struct.pack(">I", int(60e6 / BPM))[1:]
    last = 0
    for t, msg in ev:
        tick = int(round(t * ticks_per_s))
        track += vlq(tick - last) + msg
        last = tick
    track += vlq(int(GAP * ticks_per_s)) + b"\xff\x2f\x00"
    with open(path, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, TPQ))
        f.write(b"MTrk" + struct.pack(">I", len(track)) + track)


if __name__ == "__main__":
    if "--list" in sys.argv:
        print(",".join("%d:%.3f:%.1f" % (p, v / 127.0, h) for _, p, v, h in notes()))
    else:
        write_mid(sys.argv[1])
        print("%d notes, %.0f s" % (len(notes()), notes()[-1][0] + HOLD + GAP))
