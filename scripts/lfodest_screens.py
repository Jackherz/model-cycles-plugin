#!/usr/bin/env python3
"""Set track 1's LFO destination to each accepted value (CC 105), open the
LFO page and save the screen, so the firmware names each destination.

    cd re/digiemu && uv run --no-sync python ../../scripts/lfodest_screens.py
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import emuhost  # noqa: E402

LFO_KEY = 8


def main():
    emu = emuhost.start(lambda m: None)
    time.sleep(1.5)
    from emu import panel
    out = os.path.join(emuhost.P, 'notes', 'lfodest')
    os.makedirs(out, exist_ok=True)
    for v in [0] + list(range(10, 23)):
        emu.midi_in.put(bytes([0xB0, 105, v]))
        time.sleep(0.4)
        emu.inbox.append(('press', LFO_KEY, 0))
        time.sleep(0.6)
        if emu._panel_latch is not None:
            panel.write_png(emu._panel_latch, os.path.join(out, 'dest%02d.png' % v), scale=3)
        emu.inbox.append(('release', LFO_KEY, 0))
        time.sleep(0.4)
    emuhost.stop(emu)


if __name__ == '__main__':
    main()
