#!/usr/bin/env python3
"""Synthesize DynaHack's web sound effects and town music (RVIP 6b).
Our own work, no samples: public domain (CC0).  Usage: mksounds.py OUTDIR
Names match web/webwin.c (msgsnd, sound_level)."""
import math, random, struct, sys, wave, os

R = 22050
random.seed(7)

def env(i, n, a=0.005, rel=0.3):
    t, d = i / R, n / R
    return min(1, t / a) * min(1, (d - t) / (d * rel))

def tone(f0, f1, dur, wav='sine', vol=0.6, noise=0.0):
    n, ph, out = int(R * dur), 0.0, []
    for i in range(n):
        f = f0 + (f1 - f0) * i / n
        ph += 2 * math.pi * f / R
        s = math.sin(ph) if wav == 'sine' else (1 if math.sin(ph) > 0 else -1) * 0.5
        s = s * (1 - noise) + random.uniform(-1, 1) * noise
        out.append(s * vol * env(i, n))
    return out

def cat(*parts):
    return [x for p in parts for x in p]

def notes(seq, dur, wav='sine', vol=0.4):
    return cat(*[tone(f, f, dur, wav, vol) if f else [0.0] * int(R * dur) for f in seq])

def write(path, samples):
    with wave.open(path, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(R)
        w.writeframes(b''.join(struct.pack('<h', int(max(-1, min(1, s)) * 32000)) for s in samples))

C4, D4, E4, F4, G4, A4, B4, C5, D5, E5 = 262, 294, 330, 349, 392, 440, 494, 523, 587, 659
SOUNDS = {
    'hit': tone(180, 60, 0.12, noise=0.6),
    'kill': cat(tone(200, 50, 0.15, noise=0.5), tone(90, 40, 0.2, noise=0.3)),
    'miss': tone(900, 300, 0.12, noise=0.9, vol=0.3),
    'hurt': tone(140, 70, 0.18, 'square', noise=0.4),
    'die': notes([G4, F4, E4, D4, C4], 0.3, vol=0.5),
    'levelup': notes([C5, E5, G4 * 2], 0.12, 'square', 0.3),
    'hear': tone(70, 60, 0.35, noise=0.2, vol=0.4),
    'door': tone(120, 90, 0.2, 'square', noise=0.5, vol=0.4),
    'gold': cat(tone(1800, 1800, 0.06, vol=0.3), tone(2400, 2400, 0.1, vol=0.3)),
    'stairs': cat(*[tone(f, f, 0.07, noise=0.5, vol=0.35) for f in (300, 260, 220, 190)]),
    # town: a gentle 8-bar loop
    'town': notes([C4, E4, G4, E4, F4, A4, C5, A4, G4, B4, D5, B4, C5, G4, E4, 0] * 2, 0.3, vol=0.25),
}

if __name__ == '__main__':
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for k, v in SOUNDS.items():
        write(os.path.join(out, k + '.wav'), v)
    print(len(SOUNDS), 'sounds in', out)
