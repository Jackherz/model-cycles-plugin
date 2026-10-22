"""MIDI -> the Model:Cycles engine's own inputs (NOTES.md, "Engine inputs").

What the firmware's MIDI handling does to the engine, reduced to the calls
the render depends on (checked bit for bit by scripts/driven.py):

  note on/off   FUN_4005894a(request), a 0x38-byte request (below)
  sound CC      kit record word = value << 8, then FUN_40059534(value << 8, track, index)
  FX CC         kit FX word = value << 8,     then FUN_40059534(value << 8, 6, index)
  track level   kit header word,              then FUN_40059594(track, value << 8)
  mute          FUN_400594f0(state) with the mute mask at state+0x14

Value clamping (pitch 40-88, machine 0-5, ...) is the firmware's; CLAMP holds
what has been measured so far (scripts/ccmap.py).
"""
import os
import struct

SETPARAM, SETLEVEL, SETMUTE, TRIG = 0x40059534, 0x40059594, 0x400594f0, 0x4005894a

# CC -> (record offset or None for FX, FX offset or None)
SOUND_CC = {74: 0x4c, 71: 0x4e,   # the track filter's cutoff and reso (step 17 builds)
            102: 0x16, 103: 0x18, 104: 0x1a, 106: 0x1e, 107: 0x20, 108: 0x22,
            109: 0x24, 70: 0x26, 65: 0x28, 16: 0x2a, 17: 0x2c, 18: 0x2e, 19: 0x30,
            66: 0x32, 67: 0x34, 80: 0x38, 7: 0x3a, 12: 0x3c, 13: 0x3e, 10: 0x40}
FX_CC = {85: 0x12, 86: 0x14, 87: 0x16, 88: 0x18}
LEVEL_CC = 95
MUTE_CC = 94
CLAMP = {65: (40, 88), 70: (0, int(os.environ.get('CYCLES_MACHINES', 6)) - 1), 66: (0, 1), 67: (0, 1), 103: (0, 23),
         106: (0, 6), 108: (0, 4)}

# Per-track trig flags from the pattern (FUN_4008171e reads them at
# pattern + track*0x2d2 + 0x2c6); 0x0681 on the factory pattern's tracks 1-3.
TRIG_FLAGS = 0x0681


def note_request(track, note, velocity, on):
    """The request FUN_4008171e (on) / FUN_4008145e (off) builds for MIDI."""
    w = [0] * 14
    w[0], w[1] = track, note
    if on:
        w[2], w[3], w[4], w[5], w[9], w[10] = velocity, 1, 0x30000 | 1 | TRIG_FLAGS, -1, -1, 0x7f
    else:
        w[3], w[5] = 2, -1
    w[8] = 0x10 if track == 0 else 0x8     # source word: not copied into the event
    return struct.pack('>14i', *w)


def actions(msg, kit, mute_state=None):
    """-> the actions (as capture.py logs them) one MIDI message causes."""
    status, ch = msg[0] & 0xF0, msg[0] & 0x0F
    track = ch
    out = []
    if status in (0x80, 0x90) and track < 6:
        on = status == 0x90 and msg[2] > 0
        out.append(('call', 'trig', TRIG, [0], note_request(track, msg[1], msg[2], on)))
    elif status == 0xB0 and track < 6:
        cc, v = msg[1], msg[2]
        lo, hi = CLAMP.get(cc, (0, 127))
        v = max(lo, min(hi, v))
        if cc in SOUND_CC:
            off = SOUND_CC[cc]
            out.append(('kitw', kit + 0x1c + 100 * track + off, 2, v << 8))
            out.append(('call', 'setparam', SETPARAM, [v << 8, track, (off - 0x14) // 2], b''))
        elif cc in FX_CC:
            off = FX_CC[cc]
            out.append(('kitw', kit + 0x274 + off, 2, v << 8))
            # FX words: index 9 is FX+0x12 (delay time) -> SRAM 0x8000174c
            out.append(('call', 'setparam', SETPARAM, [v << 8, 6, (off - 0x12) // 2 + 9], b''))
        elif cc == LEVEL_CC:
            out.append(('kitw', kit + 0x10 + 2 * track, 2, v << 8))
            out.append(('call', 'setlevel', SETLEVEL, [track, v << 8], b''))
        elif cc == MUTE_CC and mute_state is not None:
            mask = int.from_bytes(mute_state[0x14:0x18], 'big')
            mask = mask | (1 << track) if v else mask & ~(1 << track)
            state = mute_state[:0x14] + mask.to_bytes(4, 'big') + mute_state[0x18:]
            out.append(('call', 'setmute', SETMUTE, [0], state))
    return out
