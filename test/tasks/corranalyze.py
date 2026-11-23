#!/usr/bin/env python3
"""
计算音频的平方差相关度量并绘图。

用法:
    python corranalyze.py xxx.wav

默认参数:
    分析窗长 N = 8192
    分析起点 = 2.4 秒
    多声道文件只使用左声道；单声道文件直接使用该声道。

计算:
    s[i] = sum((y[start+i+j] - y[start+j])**2 for j in range(N))
    i = 0, 1, ..., N-1

输出:
    在输入 WAV 文件同目录下生成 <文件名>_corr.png
"""

import argparse
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import soundfile as sf


WINDOW_LENGTH = 8192
START_TIME_SEC = 2.4


def analyze_wav(wav_path: Path, start_time_sec: float = START_TIME_SEC) -> Path:
    audio, sample_rate = sf.read(str(wav_path), always_2d=True)

    # 只使用左声道（第 0 列）；单声道文件也兼容。
    y = audio[:, 0].astype(np.float64, copy=False)

    start = int(round(start_time_sec * sample_rate))
    required_length = 2 * WINDOW_LENGTH - 1

    if start < 0:
        raise ValueError("分析起点不能小于 0 秒。")

    segment = y[start:start + required_length]
    if len(segment) != required_length:
        available = len(segment)
        raise ValueError(
            f"音频长度不足以完成计算：从 {start_time_sec:.3f} 秒开始，"
            f"需要 {required_length} 个采样点，实际只有 {available} 个。"
        )

    # 参考窗固定为 y[start : start + N]。
    reference = segment[:WINDOW_LENGTH]

    # 对每个 lag i 计算 N 项平方差之和。
    s = np.empty(WINDOW_LENGTH, dtype=np.float64)
    for i in range(WINDOW_LENGTH):
        difference = segment[i:i + WINDOW_LENGTH] - reference
        s[i] = np.dot(difference, difference)

    lag = np.arange(WINDOW_LENGTH)

    # 科学绘图：纵轴保持线性幅度，不转 dB、不归一化、不平滑。
    output_path = wav_path.with_name(wav_path.stem + "_corr.png")

    plt.figure(figsize=(11, 5.6), dpi=180)
    plt.plot(
        lag, s,
        linestyle="None",
        marker=".",
        markersize=2.0
    )
    plt.xlabel("Lag i (samples)", fontsize=11)
    plt.ylabel(r"$s[i]$ (linear amplitude$^2$ sum)", fontsize=11)
    plt.title(
        f"{wav_path.stem} — Left-channel squared-difference measure "
        f"(N = {WINDOW_LENGTH})",
        fontsize=13,
    )
    plt.xlim(0, WINDOW_LENGTH - 1)
    plt.grid(True, which="major", linewidth=0.55, alpha=0.35)
    plt.ticklabel_format(axis="y", style="sci", scilimits=(0, 0))
    plt.tight_layout()
    plt.savefig(output_path, dpi=300, bbox_inches="tight")
    plt.show()
    plt.close()

    print(f"输入文件: {wav_path}")
    print("使用声道: 左声道 (channel 0；单声道文件使用唯一声道)")
    print(f"采样率: {sample_rate} Hz")
    print(f"音频时长: {len(y) / sample_rate:.4f} s")
    print(f"分析起点: {start_time_sec:.3f} s (sample {start})")
    print(f"分析窗长: {WINDOW_LENGTH}")
    print(f"每个 lag 的求和项数: {WINDOW_LENGTH}")
    print(f"输出 s 长度: {len(s)}")
    print(f"s[0]: {s[0]:.10g}")
    print(f"s 最小值: {s.min():.8g}")
    print(f"s 最大值: {s.max():.8g}")
    print(f"绘图已保存: {output_path}")

    return output_path


def main() -> None:
    parser = argparse.ArgumentParser(
        description="计算 WAV 文件的平方差相关度量并绘制线性幅度曲线。"
    )
    parser.add_argument("wav", type=Path, help="输入 WAV 文件路径")
    parser.add_argument(
        "--start",
        type=float,
        default=START_TIME_SEC,
        help=f"分析起点，单位秒，默认 {START_TIME_SEC}",
    )
    args = parser.parse_args()

    if not args.wav.is_file():
        parser.error(f"找不到输入文件：{args.wav}")

    try:
        analyze_wav(args.wav, args.start)
    except (RuntimeError, ValueError, OSError) as exc:
        parser.exit(1, f"错误：{exc}\n")


if __name__ == "__main__":
    main()
