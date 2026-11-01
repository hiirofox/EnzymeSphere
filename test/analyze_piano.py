#!/usr/bin/env python3
"""Reproduce inharmonic piano partial estimates, spectra, and CSV from C4.wav.

Run:
    python -m pip install -r requirements.txt
    python analyze_piano.py

Note: despite its filename, this recording's measured fundamental is ~130.7 Hz
(C3). This script does NOT mistakenly assume the fundamental is ~261.6 Hz.
"""
from __future__ import annotations
import argparse
import csv
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import soundfile as sf
from scipy.fft import rfft, rfftfreq
from scipy.optimize import least_squares


def interp_peak(magnitude: np.ndarray, k: int, sample_rate: int, nfft: int) -> float:
    """Parabolic interpolation of a magnitude-spectrum peak."""
    if k <= 0 or k >= len(magnitude) - 1:
        return k * sample_rate / nfft
    left, center, right = magnitude[k - 1:k + 2]
    denom = left - 2 * center + right
    offset = 0.0 if denom == 0.0 else 0.5 * (left - right) / denom
    return (k + offset) * sample_rate / nfft


def analyze(audio_path: Path, output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    audio, sr = sf.read(audio_path, dtype="float64")
    if audio.ndim == 2:
        audio = audio.mean(axis=1)

    # Short-time RMS onset detection: 20 ms frames, 5 ms hops.
    frame, hop = int(0.020 * sr), int(0.005 * sr)
    rms, rms_times = [], []
    for first in range(0, len(audio) - frame + 1, hop):
        piece = audio[first:first + frame]
        rms.append(np.sqrt(np.mean(piece ** 2)))
        rms_times.append((first + frame / 2) / sr)
    rms = np.asarray(rms)
    rms_times = np.asarray(rms_times)
    onset = float(rms_times[np.flatnonzero(rms >= 0.20 * rms.max())[0]])

    # Skip 150 ms of attack after the detected onset.
    start = int((onset + 0.150) * sr)
    end = min(len(audio), start + int(1.8 * sr))
    segment = audio[start:end].copy()
    if len(segment) < 1024:
        raise ValueError("Sample is too short after attack removal")
    segment -= np.mean(segment)

    nfft = max(2**19, 2**int(np.ceil(np.log2(len(segment)))))
    magnitude = np.abs(rfft(segment * np.hanning(len(segment)), nfft))
    frequencies = rfftfreq(nfft, d=1.0 / sr)
    magnitude_db = 20 * np.log10(np.maximum(magnitude, 1e-12) / magnitude.max())

    # Important: the dominant fundamental of this provided file is ~130.7 Hz,
    # even though its filename says C4. Locate it in the C3 region.
    region = np.flatnonzero((frequencies >= 105) & (frequencies <= 165))
    k = region[np.argmax(magnitude[region])]
    f1 = interp_peak(magnitude, int(k), sr, nfft)

    # Pick first 20 true partials using an initial weak-inharmonicity guide.
    # This avoids accidentally following ONLY the even partials, which would
    # lead to an erroneous ~261 Hz estimate and misplaced blue lines.
    initial_B = 1.2e-4
    n_fit = 20
    indices = np.arange(1, n_fit + 1, dtype=float)
    observed = []
    for i in indices:
        predicted = i * f1 * np.sqrt((1 + initial_B * i*i) / (1 + initial_B))
        half_width = max(12.0, 0.01 * predicted)
        candidates = np.flatnonzero(
            (frequencies >= predicted - half_width)
            & (frequencies <= predicted + half_width)
        )
        k = candidates[np.argmax(magnitude[candidates])]
        observed.append(interp_peak(magnitude, int(k), sr, nfft))
    observed = np.array(observed)

    # Fit f_i = i*F0*sqrt(1 + B*i^2). Normalize by i to avoid allowing high
    # partials to dominate simply due to their higher absolute frequencies.
    def residual(parameters):
        F0, B = parameters
        return F0 * np.sqrt(1 + B * indices**2) - observed / indices

    fitted = least_squares(
        residual,
        x0=[f1, 1.2e-4],
        bounds=([f1 * 0.90, 0.0], [f1 * 1.10, 0.01]),
        loss="soft_l1", f_scale=0.15,
    )
    F0, B = (float(x) for x in fitted.x)

    def model(i):
        return i * F0 * np.sqrt(1 + B*i*i)

    # Continue model estimates up to 5000 Hz; beyond fitted n_fit these are
    # extrapolations. Nearby maxima are presented for reference only.
    max_frequency = 5000
    partials = []
    i = 1
    while model(i) <= max_frequency:
        prediction = model(i)
        half_width = max(8.0, 0.006 * prediction)
        candidates = np.flatnonzero(
            (frequencies >= prediction - half_width)
            & (frequencies <= prediction + half_width)
        )
        peak_frequency = float('nan')
        peak_db = float('nan')
        if len(candidates):
            k = candidates[np.argmax(magnitude[candidates])]
            peak_frequency = interp_peak(magnitude, int(k), sr, nfft)
            peak_db = float(np.interp(peak_frequency, frequencies, magnitude_db))
        partials.append((i, prediction, peak_frequency,
                         peak_frequency-prediction, peak_db,
                         "fit" if i <= n_fit else "extrapolated"))
        i += 1

    with (output_dir / "estimated_partials.csv").open("w", newline="", encoding="utf-8") as file:
        writer = csv.writer(file)
        writer.writerow(["partial_number", "estimated_Hz", "nearby_spectral_peak_Hz",
                         "peak_minus_estimate_Hz", "peak_dB_relative", "model_region"])
        for i, pred, actual, diff, level, scope in partials:
            writer.writerow([i, f"{pred:.5f}", f"{actual:.5f}",
                             f"{diff:.5f}", f"{level:.2f}", scope])

    def make_plot(limit, filename):
        fig, ax = plt.subplots(figsize=(15, 6.5), dpi=170)
        use = frequencies <= limit
        ax.plot(frequencies[use], magnitude_db[use], color="#115b95", lw=0.9,
                label="Recorded spectrum (attack removed)")
        light_blue = "#78cee9"
        for index, predicted, *_ in partials:
            if predicted > limit:
                continue
            ax.axvline(predicted, color=light_blue, lw=1.1, alpha=.93,
                       label="Predicted partial positions" if index == 1 else None)
            ax.text(predicted, -3, str(index), color="#277caa", fontsize=7.2,
                    va="top", ha="center", rotation=90 if limit > 3000 else 0)
        ax.set_xlim(0, limit)
        ax.set_ylim(-135, 4)
        ax.set_xlabel("Frequency (Hz)")
        ax.set_ylabel("Magnitude (dB relative to peak)")
        ax.set_title(f"Piano sample spectrum | transient removed: {start/sr:.3f}-{end/sr:.3f} s"
                     f" | F0={F0:.3f} Hz, B={B:.6g}")
        ax.legend(loc="upper right")
        ax.grid(axis="y", alpha=0.14)
        fig.tight_layout()
        fig.savefig(output_dir / filename, dpi=170, bbox_inches="tight")
        plt.close(fig)

    make_plot(5000, "piano_spectrum_with_partials.png")
    make_plot(2200, "piano_spectrum_zoom_2200Hz.png")
    fit_rmse_hz = float(np.sqrt(np.mean((observed - np.array([model(i) for i in indices]))**2)))
    print(f"Audio: {audio_path}, sample rate = {sr} Hz")
    print(f"Attack removed; analyzed {start/sr:.3f} to {end/sr:.3f} seconds")
    print(f"Measured first partial: {f1:.4f} Hz (about musical C3)")
    print(f"Fitted on partials 1-{n_fit}: F0 = {F0:.6f} Hz; B = {B:.9f}")
    print(f"Fit RMSE for partials 1-{n_fit} = {fit_rmse_hz:.3f} Hz")
    print(f"Created {len(partials)} model positions up to {max_frequency} Hz")
    print(f"Saved results in {output_dir.resolve()}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--audio", type=Path, default=Path(__file__).with_name("C4.wav"))
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    analyze(args.audio, args.output)
