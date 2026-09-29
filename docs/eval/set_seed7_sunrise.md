# Evaluationsbericht

Parhelion, `Tools/eval_report.py` (PLAN 13.5): der Plan gegen das Audio, je Track.

| Track | Stil | Tonart Plan/Audio | Grenzen gefunden/geplant | P | R | F1 | auf 8 Takten | Drops gehört | Abstand LU | Korridor |
|---|---|---|---|---|---|---|---|---|---|---|
| set T1 | Progressive | D#m / A#m ≈ | 17/7 | 0.41 | 1.00 | 0.58 | 82 % | 0/1 | 2.4 | 17/19 |
| set T2 | Progressive | D#m / F# | 29/7 | 0.17 | 0.71 | 0.28 | 24 % | 0/1 | 2.9 | 16/19 |
| set T3 | Progressive | G#m / G#m | 11/7 | 0.64 | 1.00 | 0.78 | 82 % | 1/1 | 3.2 | 15/19 |
| set T4 | Progressive | C#m / C#m | 18/7 | 0.33 | 0.86 | 0.48 | 44 % | 0/1 | 3.5 | 15/19 |
| set T5 | Progressive | F#m / A | 26/5 | 0.15 | 0.80 | 0.26 | 38 % | 0/1 | 4.4 | 16/19 |
| set T6 | Progressive | F#m / A | 19/5 | 0.26 | 1.00 | 0.42 | 47 % | 0/1 | 4.6 | 14/19 |
| set T7 | Dream House | F#m / A | 13/5 | 0.38 | 1.00 | 0.56 | 62 % | 0/1 | 4.8 | 16/19 |
| set T8 | Dream House | A / A | 15/5 | 0.33 | 1.00 | 0.50 | 73 % | 0/1 | - | 11/17 |
| set T9 | Dream House | A / A | 12/5 | 0.42 | 1.00 | 0.59 | 75 % | 1/1 | 6.7 | 9/19 |
| set T10 | Dream House | D / A ≈ | 10/7 | 0.60 | 0.86 | 0.71 | 100 % | 0/1 | 5.4 | 15/19 |
| set T11 | Dream House | G / Am ✗ | 21/5 | 0.24 | 1.00 | 0.38 | 62 % | 0/1 | 5.5 | 10/19 |
| set T12 | Dream House | Em / Am ≈ | 23/5 | 0.22 | 1.00 | 0.36 | 65 % | 0/1 | 5.2 | 10/19 |
| set T13 | Dream House | Am / Am | 16/5 | 0.31 | 1.00 | 0.48 | 69 % | 0/1 | 6.0 | 9/19 |
| set T14 | Dream House | Dm / Am ≈ | 14/7 | 0.36 | 0.71 | 0.48 | 50 % | 1/2 | 5.8 | 13/19 |
| set T15 | Dream House | Cm / Am ✗ | 14/7 | 0.50 | 1.00 | 0.67 | 64 % | 1/2 | - | 13/17 |
| set T16 | Dream House | Gm / Am ✗ | 17/7 | 0.29 | 0.71 | 0.42 | 53 % | 1/2 | 5.1 | 14/19 |
| set T17 | Dream House | Gm / Dm ≈ | 13/7 | 0.54 | 1.00 | 0.70 | 85 % | 1/2 | 4.3 | 10/19 |
| set T18 | Dream House | Gm / Gm | 9/7 | 0.67 | 0.86 | 0.75 | 67 % | 2/2 | 4.0 | 15/19 |
| set T19 | Uplifting | Cm / F ✗ | 8/7 | 0.62 | 0.71 | 0.67 | 62 % | 1/2 | 3.6 | 13/19 |
| set T20 | Uplifting | Fm / G# | 14/9 | 0.50 | 0.78 | 0.61 | 64 % | 2/3 | 3.4 | 17/19 |
| set T21 | Uplifting | F#m / F#m | 10/7 | 0.40 | 0.57 | 0.47 | 50 % | 1/2 | 3.2 | 16/19 |
| set T22 | Uplifting | F#m / A | 12/7 | 0.50 | 0.86 | 0.63 | 58 % | 1/2 | 2.3 | 16/19 |

## Zusammenfassung

- Sektionsgrenzen (Neuheitskurve gegen den Plan, ±1 Takt): F1 im Mittel 0.53
- Grenzen auf Vielfachen von 8 Takten: 63 %
- Drops hörbar (+3 dB): 13 von 32
- Tonart auf dem Audio wie geplant (oder die Parallele): 13 von 22; eine Quinte daneben (Camelot-Nachbar, ≈): 5
- Außerhalb des Korridors der Referenzen: b150_400 (17×), bass_offbeat (12×), b2k_5k (10×), b400_2k (9×), gap_lu (9×), b60_150 (9×), minutes (8×), centroid (7×), side_hi_db (7×), pump_mid (6×), b5k_16k (5×), correlation (5×), breakdown_share (5×), breakdown_bars (3×), lufs (2×)
