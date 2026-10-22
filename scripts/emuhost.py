"""Run digiemu's GUI emulator headless for our tools.

Shared by renderprof.py and capture.py: the paths and environment, the stub
sound card, MIDI delivered on arrival, and a callback that gets the machine
as soon as it is built (to install hooks before it runs).
"""
import os
import sys
import time

P = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIGIEMU = os.path.join(P, 're', 'digiemu')
sys.path.insert(0, DIGIEMU)
sys.path.insert(0, os.path.join(DIGIEMU, 'tools'))

# CYCLES_FW=ROOT:SYX_NAME runs a firmware folder made by boot_firmware.py
# (its settled gui.snap, card, sections and devices overlay) instead of the
# stock 1.13 in data/: e.g. CYCLES_FW=data-st:model-cycles_OS1.13_syntakt5.syx
if os.environ.get('CYCLES_FW'):
    from emu import bootstrap
    _root, _syx = os.environ['CYCLES_FW'].split(':')
    _paths = bootstrap.FirmwarePaths(os.path.join(P, _root), _syx, os.path.join(DIGIEMU, 'devices'))
    os.environ.update(_paths.env())
    SNAPSHOT = _paths.resume if os.path.exists(_paths.resume) else _paths.gui
    CARD = _paths.card
    DATA = _paths.root
else:
    os.environ.setdefault('DT2_SYX', os.path.join(P, 'firmware', 'model-cycles_OS1.13.syx'))
    os.environ.setdefault('DT2_SECTIONS', os.path.join(P, 'data', 'sections'))
    SNAPSHOT = os.path.join(P, 'data', 'resume.snap')
    CARD = os.path.join(P, 'data', 'plusdrive.img')
    DATA = os.path.join(P, 'data')
RENDER_ISR = 0x40058c5e     # forced-interrupt block handler (NOTES.md)


def parse_n(s):
    mult = {'K': 10**3, 'M': 10**6, 'G': 10**9}
    return int(float(s[:-1]) * mult[s[-1].upper()]) if s[-1].upper() in mult else int(s, 0)


def start(on_machine, snapshot=SNAPSHOT, ips=None):
    """Start the emulator paced; `on_machine(m)` runs on the emulator thread
    once the machine is built. -> the gui.Emulator, ready."""
    from emu import gui, midi
    import livecheck
    gui.audioout.WaveOut = livecheck.StubOut

    attach = midi.MidiIn.attach

    def attach_and_hook(self, m, ips_):
        on_machine(m)
        return attach(self, m, ips_)
    midi.MidiIn.attach = attach_and_hook

    # MidiIn maps host time onto emulated time assuming the emulator keeps up
    # with the host; under our hooks it runs several times slower, so every
    # byte was scheduled into an emulated future the run never reached.
    # Offline, a byte goes in when it arrives: pin the offset to "now".
    def track_now(self, done):
        now = time.monotonic()
        self._offset, self._offset_t = done - now * self.ips, now
        return now
    midi.MidiIn._track = track_now

    emu = gui.Emulator(snapshot, realtime=True)
    emu.start()
    emu.ready.wait(180)
    if ips is not None:
        emu._pending_ips.append((emu.stats['instrs'], ips))
    return emu


def send_ccs(emu, specs, settle=1.0):
    """CH:CC:VAL (CH 1-6 or 'all'), paced, then wait for the firmware."""
    for spec in specs:
        ch, cc, val = spec.split(':')
        for c in (range(1, 7) if ch == 'all' else (int(ch),)):
            emu.midi_in.put(bytes([0xB0 | (c - 1), int(cc), int(val)]))
            time.sleep(0.02)
    if specs:
        time.sleep(settle)


def note_ons(emu, specs):
    for spec in specs:
        ch, note, vel = spec.split(':')
        for c in (range(1, 7) if ch == 'all' else (int(ch),)):
            emu.midi_in.put(bytes([0x90 | (c - 1), int(note), int(vel)]))


def stop(emu):
    emu.stop_flag.set()
    emu.join(15)
