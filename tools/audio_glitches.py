#!/usr/bin/env python3
"""Find the glitches in a recording of the sound card's output (TP_AUDIO_REC=<file>: 44.1 kHz stereo float32,
what sb16_play handed the app) or of what went out (TP_AUDIO_REC_OUT): sample-to-sample jumps larger than any
sound of the game's makes, and stretches of exact silence inside sound (the output ran dry). Prints each with its
time from the start of the recording, to set against the log's "tick gap" lines (TP_SOUND_LOG) and the "card:"
stats (TP_DEBUG). No game data needed.

  tools/audio_glitches.py work/card.f32 [--jump 0.5] [--gap-ms 3] [--rate 44100] [--max 50]

A jump is |x[n] - x[n-1]| above --jump (full scale is 1.0; the game's 8-bit effects, held or interpolated to
44.1 kHz, step by a few percent at most; 0.5 is well clear of a sample's own steepest edge). A gap is a run of
exact zeros after sound lasting --gap-ms or more but under 500 ms (longer: a pause between sounds; the output ran
dry for tens of milliseconds at a time). Exit status 1 when any were found."""
import argparse
import struct
import sys
from pathlib import Path


def analyse(samples, rate, jump, gap_ms):
    """samples: list of (l, r). Returns (jumps, gaps): [(time, size)], [(time, length_ms)]."""
    jumps, gaps = [], []
    prev = None
    zero_run_start, last_sound = None, None
    for n, (l, r) in enumerate(samples):
        if prev is not None:
            d = max(abs(l - prev[0]), abs(r - prev[1]))
            if d > jump:
                jumps.append((n / rate, d))
        prev = (l, r)
        silent = l == 0.0 and r == 0.0
        if silent:
            if zero_run_start is None:
                zero_run_start = n
        else:
            if zero_run_start is not None and last_sound is not None:
                ms = (n - zero_run_start) * 1000.0 / rate
                if gap_ms <= ms < 500:
                    gaps.append((zero_run_start / rate, ms))
            zero_run_start = None
            last_sound = n
    return jumps, gaps


def read_f32_stereo(path):
    data = Path(path).read_bytes()
    n = len(data) // 8
    flat = struct.unpack('<%df' % (2 * n), data[:8 * n])
    return list(zip(flat[0::2], flat[1::2]))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('recording', type=Path)
    ap.add_argument('--rate', type=float, default=44100)
    ap.add_argument('--jump', type=float, default=0.5)
    ap.add_argument('--gap-ms', type=float, default=3)
    ap.add_argument('--max', type=int, default=50, help='lines printed per kind')
    a = ap.parse_args()
    samples = read_f32_stereo(a.recording)
    jumps, gaps = analyse(samples, a.rate, a.jump, a.gap_ms)
    print(f'{a.recording}: {len(samples) / a.rate:.1f} s, {len(jumps)} jumps over {a.jump}, {len(gaps)} gaps of {a.gap_ms} ms or more')
    for t, d in jumps[:a.max]:
        print(f'  jump  {t:9.3f} s  {d:.2f}')
    for t, ms in gaps[:a.max]:
        print(f'  gap   {t:9.3f} s  {ms:.1f} ms')
    return 1 if jumps or gaps else 0


if __name__ == '__main__':
    sys.exit(main())
