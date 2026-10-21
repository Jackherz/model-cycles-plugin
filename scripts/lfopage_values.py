#!/usr/bin/env python3
"""Send each value of an LFO CC on channel 1, open the LFO page and keep the
screen row that shows it; writes one image with a row per value.

    cd re/digiemu && uv run --no-sync --with pillow python ../../scripts/lfopage_values.py CC ROW FIRST LAST OUT.png
    (ROW: 0 Wav, 1 Mul, 2 Dst, 3 Dep)
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import emuhost  # noqa: E402

LFO_KEY = 8


def main():
    cc, row, first, last, out = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]
    emu = emuhost.start(lambda m: None)
    time.sleep(1.5)
    from emu import panel
    from PIL import Image
    tmp = out + '.tmp.png'
    rows = []
    for v in range(first, last + 1):
        emu.midi_in.put(bytes([0xB0, cc, v]))
        time.sleep(0.4)
        emu.inbox.append(('press', LFO_KEY, 0))
        time.sleep(0.6)
        panel.write_png(emu._panel_latch, tmp, scale=3)
        im = Image.open(tmp)
        w, h = im.size
        rows.append(im.crop((200, row * h // 4, w, (row + 1) * h // 4)).convert('RGB'))
        emu.inbox.append(('release', LFO_KEY, 0))
        time.sleep(0.4)
    emuhost.stop(emu)
    os.remove(tmp)
    cols = 2
    per = (len(rows) + cols - 1) // cols
    W, H = rows[0].width, rows[0].height
    sheet = Image.new('RGB', (W * cols + 60 * cols, H * per), 'white')
    from PIL import ImageDraw
    d = ImageDraw.Draw(sheet)
    for i, r in enumerate(rows):
        x, y = (i // per) * (W + 60), (i % per) * H
        d.text((x + 5, y + H // 3), str(first + i), fill='black')
        sheet.paste(r, (x + 60, y))
    sheet.save(out)


if __name__ == '__main__':
    main()
