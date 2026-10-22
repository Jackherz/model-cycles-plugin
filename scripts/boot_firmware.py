#!/usr/bin/env python3
"""Cold-boot a firmware build in digiemu to a settled UI snapshot (gui.snap),
with digiemu's own first-run recipe (emu/bootstrap.py): extract sections,
format a card, cold boot, intro, settle. For builds digiemu does not know,
put a devices/ overlay in ROOT with the build's sha256 (see data-st/).

    cd re/digiemu && uv run --no-sync python ../../scripts/boot_firmware.py ROOT SYX_NAME
"""
import os
import sys

P = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIGIEMU = os.path.join(P, 're', 'digiemu')
sys.path.insert(0, DIGIEMU)
sys.path.insert(0, os.path.join(DIGIEMU, 'tools'))

from emu import bootstrap  # noqa: E402


def main():
    root, syx = os.path.abspath(sys.argv[1]), sys.argv[2]
    paths = bootstrap.FirmwarePaths(root, syx, os.path.join(DIGIEMU, 'devices'))
    os.environ.update(paths.env())

    def progress(ev):
        if ev.text:
            print(ev.text, flush=True)
    out = bootstrap.first_run(paths, progress=progress)
    print('->', out)


if __name__ == '__main__':
    main()
