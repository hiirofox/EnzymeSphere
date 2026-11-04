#pragma once
#include "define.h"
#include "loss.h"
#include "PianoDWG.h"
#include "optimizer.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <vector>




namespace EstimationOvertone {


	struct Peak {
		double frequency;
		double amplitude;
	};

	struct Match {
		int harmonic;
		double frequency;
		double amplitude;
	};

	struct Candidate {
		bool valid;
		double score;
		double fundamental;
		double inharmonicity;
		int prefixCount;
		std::vector<Match> matches;
		Candidate() : valid(false), score(-1.0e100), fundamental(0.0),
			inharmonicity(0.0), prefixCount(0) {
		}
	};

	struct YinResult {
		bool valid;
		double frequency;
		double cmnd;
		std::vector<double> cmndValues;
		int frameLength;
		YinResult() : valid(false), frequency(0.0), cmnd(1.0), frameLength(0) {}
	};

	static inline std::size_t NextPowerOfTwo(std::size_t x) {
		std::size_t n = 1;
		while (n < x) n <<= 1;
		return n;
	}

	static inline void FFT(std::vector<std::complex<double> >& a, bool inverse) {
		const std::size_t n = a.size();
		for (std::size_t i = 1, j = 0; i < n; ++i) {
			std::size_t bit = n >> 1;
			for (; j & bit; bit >>= 1) j ^= bit;
			j ^= bit;
			if (i < j) std::swap(a[i], a[j]);
		}

		const double pi = 3.1415926535897932384626433832795;
		for (std::size_t len = 2; len <= n; len <<= 1) {
			const double angle = (inverse ? 2.0 : -2.0) * pi /
				static_cast<double>(len);
			const std::complex<double> wLen(std::cos(angle), std::sin(angle));
			for (std::size_t i = 0; i < n; i += len) {
				std::complex<double> w(1.0, 0.0);
				const std::size_t half = len >> 1;
				for (std::size_t j = 0; j < half; ++j) {
					const std::complex<double> u = a[i + j];
					const std::complex<double> v = a[i + j + half] * w;
					a[i + j] = u + v;
					a[i + j + half] = u - v;
					w *= wLen;
				}
			}
		}

		if (inverse) {
			const double invN = 1.0 / static_cast<double>(n);
			for (std::size_t i = 0; i < n; ++i) a[i] *= invN;
		}
	}

	static inline int AnalysisStart(int numSamples, double sampleRate) {
		if (numSamples <= 1) return 0;
		const int preferred = static_cast<int>(std::floor(sampleRate * 0.01));
		return std::max(0, std::min(preferred, numSamples / 20));
	}

	static inline YinResult EstimateYin(const float* target, int numSamples,
		double sampleRate) {
		YinResult result;
		const int start = AnalysisStart(numSamples, sampleRate);
		const int available = numSamples - start;
		const int frameLength = std::min(available,
			std::max(64, static_cast<int>(std::floor(sampleRate * 0.40))));
		if (frameLength < 64) return result;

		std::vector<double> x(static_cast<std::size_t>(frameLength), 0.0);
		double mean = 0.0;
		for (int i = 0; i < frameLength; ++i) {
			const double v = std::isfinite(target[start + i]) ? target[start + i] : 0.0;
			x[static_cast<std::size_t>(i)] = v;
			mean += v;
		}
		mean /= static_cast<double>(frameLength);

		double totalEnergy = 0.0;
		for (int i = 0; i < frameLength; ++i) {
			x[static_cast<std::size_t>(i)] -= mean;
			totalEnergy += x[static_cast<std::size_t>(i)] * x[static_cast<std::size_t>(i)];
		}
		if (totalEnergy < 1.0e-18) return result;

		const std::size_t fftSize = NextPowerOfTwo(static_cast<std::size_t>(frameLength) * 2);
		std::vector<std::complex<double> > spectrum(fftSize, std::complex<double>(0.0, 0.0));
		for (int i = 0; i < frameLength; ++i) {
			spectrum[static_cast<std::size_t>(i)] =
				std::complex<double>(x[static_cast<std::size_t>(i)], 0.0);
		}
		FFT(spectrum, false);
		for (std::size_t i = 0; i < fftSize; ++i) {
			const double re = spectrum[i].real();
			const double im = spectrum[i].imag();
			spectrum[i] = std::complex<double>(re * re + im * im, 0.0);
		}
		FFT(spectrum, true);

		std::vector<double> prefixEnergy(static_cast<std::size_t>(frameLength) + 1, 0.0);
		for (int i = 0; i < frameLength; ++i) {
			prefixEnergy[static_cast<std::size_t>(i + 1)] =
				prefixEnergy[static_cast<std::size_t>(i)] +
				x[static_cast<std::size_t>(i)] * x[static_cast<std::size_t>(i)];
		}

		const int maxLag = std::min(frameLength / 2,
			std::max(3, static_cast<int>(std::floor(sampleRate / 15.0))));
		if (maxLag < 4) return result;

		std::vector<double> difference(static_cast<std::size_t>(maxLag) + 1, 0.0);
		std::vector<double> cmnd(static_cast<std::size_t>(maxLag) + 1, 1.0);
		double running = 0.0;
		for (int tau = 1; tau <= maxLag; ++tau) {
			const double e1 = prefixEnergy[static_cast<std::size_t>(frameLength - tau)];
			const double e2 = prefixEnergy[static_cast<std::size_t>(frameLength)] -
				prefixEnergy[static_cast<std::size_t>(tau)];
			const double corr = spectrum[static_cast<std::size_t>(tau)].real();
			double d = e1 + e2 - 2.0 * corr;
			if (d < 0.0) d = 0.0;
			difference[static_cast<std::size_t>(tau)] = d;
			running += d;
			if (running > 1.0e-20) {
				cmnd[static_cast<std::size_t>(tau)] = d *
					static_cast<double>(tau) / running;
			}
		}

		int selectedLag = -1;
		int firstLocalMinimum = -1;
		for (int tau = 3; tau < maxLag; ++tau) {
			const double left = cmnd[static_cast<std::size_t>(tau - 1)];
			const double center = cmnd[static_cast<std::size_t>(tau)];
			const double right = cmnd[static_cast<std::size_t>(tau + 1)];
			if (center <= left && center < right) {
				if (firstLocalMinimum < 0) firstLocalMinimum = tau;
				if (center < 0.15) {
					selectedLag = tau;
					break;
				}
			}
		}
		if (selectedLag < 0) selectedLag = firstLocalMinimum;
		if (selectedLag < 0) {
			selectedLag = 2;
			for (int tau = 3; tau <= maxLag; ++tau) {
				if (cmnd[static_cast<std::size_t>(tau)] <
					cmnd[static_cast<std::size_t>(selectedLag)]) selectedLag = tau;
			}
		}

		double refinedLag = static_cast<double>(selectedLag);
		if (selectedLag > 1 && selectedLag < maxLag) {
			const double a = cmnd[static_cast<std::size_t>(selectedLag - 1)];
			const double b = cmnd[static_cast<std::size_t>(selectedLag)];
			const double c = cmnd[static_cast<std::size_t>(selectedLag + 1)];
			const double denominator = a - 2.0 * b + c;
			if (std::fabs(denominator) > 1.0e-15) {
				const double shift = 0.5 * (a - c) / denominator;
				refinedLag += std::max(-0.5, std::min(0.5, shift));
			}
		}
		if (refinedLag <= 0.0) return result;

		result.valid = true;
		result.frequency = sampleRate / refinedLag;
		result.cmnd = cmnd[static_cast<std::size_t>(selectedLag)];
		result.cmndValues.swap(cmnd);
		result.frameLength = frameLength;
		return result;
	}

	static inline bool BuildSpectrum(const float* target, int numSamples,
		double sampleRate, double seconds,
		std::vector<Peak>& peaks,
		double& maxPeakAmplitude) {
		peaks.clear();
		maxPeakAmplitude = 0.0;
		const int start = AnalysisStart(numSamples, sampleRate);
		const int available = numSamples - start;
		const int frameLength = std::min(available,
			std::max(64, static_cast<int>(std::floor(sampleRate * seconds))));
		if (frameLength < 64) return false;

		std::vector<double> frame(static_cast<std::size_t>(frameLength), 0.0);
		double mean = 0.0;
		for (int i = 0; i < frameLength; ++i) {
			const double v = std::isfinite(target[start + i]) ? target[start + i] : 0.0;
			frame[static_cast<std::size_t>(i)] = v;
			mean += v;
		}
		mean /= static_cast<double>(frameLength);

		const std::size_t fftSize = NextPowerOfTwo(
			std::max<std::size_t>(131072, static_cast<std::size_t>(frameLength)));
		std::vector<std::complex<double> > spectrum(fftSize, std::complex<double>(0.0, 0.0));
		const double twoPi = 6.283185307179586476925286766559;
		for (int i = 0; i < frameLength; ++i) {
			const double w = (frameLength > 1)
				? 0.5 - 0.5 * std::cos(twoPi * static_cast<double>(i) /
					static_cast<double>(frameLength - 1))
				: 1.0;
			const double v = (frame[static_cast<std::size_t>(i)] - mean) * w;
			spectrum[static_cast<std::size_t>(i)] = std::complex<double>(v, 0.0);
		}
		FFT(spectrum, false);

		const std::size_t half = fftSize / 2;
		std::vector<double> magnitude(half + 1, 0.0);
		double globalMax = 0.0;
		for (std::size_t k = 0; k <= half; ++k) {
			const double re = spectrum[k].real();
			const double im = spectrum[k].imag();
			const double a = std::sqrt(re * re + im * im);
			magnitude[k] = a;
			if (a > globalMax) globalMax = a;
		}
		if (globalMax <= 1.0e-15) return false;

		const double binHz = sampleRate / static_cast<double>(fftSize);
		const double minPeakAmplitude = globalMax * 0.0003;
		for (std::size_t k = 2; k < half; ++k) {
			const double a = magnitude[k];
			if (a < minPeakAmplitude || a < magnitude[k - 1] || a <= magnitude[k + 1]) {
				continue;
			}
			const double frequency = static_cast<double>(k) * binHz;
			if (frequency < 15.0 || frequency >= sampleRate * 0.5) continue;

			const double y0 = std::log(std::max(1.0e-20, magnitude[k - 1]));
			const double y1 = std::log(std::max(1.0e-20, magnitude[k]));
			const double y2 = std::log(std::max(1.0e-20, magnitude[k + 1]));
			const double denominator = y0 - 2.0 * y1 + y2;
			double delta = 0.0;
			if (std::fabs(denominator) > 1.0e-15) {
				delta = 0.5 * (y0 - y2) / denominator;
				delta = std::max(-0.5, std::min(0.5, delta));
			}
			const double refinedFrequency = (static_cast<double>(k) + delta) * binHz;
			const double refinedAmplitude = a *
				std::exp(0.25 * (y0 - y2) * delta);
			Peak p;
			p.frequency = refinedFrequency;
			p.amplitude = refinedAmplitude;
			peaks.push_back(p);
		}

		if (peaks.empty()) return false;
		if (peaks.size() > 4096) {
			std::partial_sort(peaks.begin(), peaks.begin() + 4096, peaks.end(),
				[](const Peak& a, const Peak& b) { return a.amplitude > b.amplitude; });
			peaks.resize(4096);
		}
		std::sort(peaks.begin(), peaks.end(),
			[](const Peak& a, const Peak& b) { return a.frequency < b.frequency; });

		for (std::size_t i = 0; i < peaks.size(); ++i) {
			if (peaks[i].amplitude > maxPeakAmplitude) {
				maxPeakAmplitude = peaks[i].amplitude;
			}
		}
		return maxPeakAmplitude > 1.0e-15;
	}

	static inline double FindStrongHighCluster(const std::vector<Peak>& peaks,
		double maxPeakAmplitude,
		double yinFrequency,
		bool& useCluster) {
		useCluster = false;
		if (peaks.empty() || maxPeakAmplitude <= 0.0) return 0.0;

		const Peak* strongestHigh = nullptr;
		for (std::size_t i = 0; i < peaks.size(); ++i) {
			if (peaks[i].frequency > 2500.0 &&
				peaks[i].amplitude >= maxPeakAmplitude * 0.20) {
				if (!strongestHigh || peaks[i].amplitude > strongestHigh->amplitude) {
					strongestHigh = &peaks[i];
				}
			}
		}
		if (!strongestHigh) return 0.0;

		bool nearYinPeak = false;
		const double nearTolerance = std::max(1.0, yinFrequency * 0.05);
		for (std::size_t i = 0; i < peaks.size(); ++i) {
			if (std::fabs(peaks[i].frequency - yinFrequency) <= nearTolerance &&
				peaks[i].amplitude >= maxPeakAmplitude * 0.15) {
				nearYinPeak = true;
				break;
			}
		}

		const bool nearSamePitch =
			std::fabs(strongestHigh->frequency - yinFrequency) <= yinFrequency * 0.10;
		if (!nearSamePitch && nearYinPeak) return 0.0;

		double weightedFrequency = 0.0;
		double weightSum = 0.0;
		const double clusterRadius = strongestHigh->frequency * 0.012;
		for (std::size_t i = 0; i < peaks.size(); ++i) {
			if (peaks[i].frequency > 2500.0 &&
				peaks[i].amplitude >= maxPeakAmplitude * 0.20 &&
				std::fabs(peaks[i].frequency - strongestHigh->frequency) <= clusterRadius) {
				weightedFrequency += peaks[i].frequency * peaks[i].amplitude;
				weightSum += peaks[i].amplitude;
			}
		}
		if (weightSum <= 0.0) return 0.0;
		useCluster = true;
		return weightedFrequency / weightSum;
	}

	static inline const Peak* FindStrongestPeakInRange(const std::vector<Peak>& peaks,
		double lowFrequency,
		double highFrequency) {
		std::vector<Peak>::const_iterator first = std::lower_bound(
			peaks.begin(), peaks.end(), lowFrequency,
			[](const Peak& p, double f) { return p.frequency < f; });
		std::vector<Peak>::const_iterator last = std::upper_bound(
			first, peaks.end(), highFrequency,
			[](double f, const Peak& p) { return f < p.frequency; });
		if (first == last) return nullptr;
		const Peak* strongest = &(*first);
		for (std::vector<Peak>::const_iterator it = first; it != last; ++it) {
			if (it->amplitude > strongest->amplitude) strongest = &(*it);
		}
		return strongest;
	}

	static inline void BuildWindowedAutocorrelation(const float* target,
		int numSamples,
		double sampleRate,
		double seconds,
		std::vector<double>& autocorrelation) {
		autocorrelation.clear();
		const int start = AnalysisStart(numSamples, sampleRate);
		const int available = numSamples - start;
		const int frameLength = std::min(available,
			std::max(64, static_cast<int>(std::floor(sampleRate * seconds))));
		if (frameLength < 64) return;

		std::vector<std::complex<double> > data(
			NextPowerOfTwo(static_cast<std::size_t>(frameLength) * 2),
			std::complex<double>(0.0, 0.0));
		double mean = 0.0;
		for (int i = 0; i < frameLength; ++i) {
			const double v = std::isfinite(target[start + i]) ? target[start + i] : 0.0;
			mean += v;
		}
		mean /= static_cast<double>(frameLength);
		const double twoPi = 6.283185307179586476925286766559;
		for (int i = 0; i < frameLength; ++i) {
			const double w = 0.5 - 0.5 * std::cos(twoPi * static_cast<double>(i) /
				static_cast<double>(frameLength - 1));
			const double raw = std::isfinite(target[start + i]) ? target[start + i] : 0.0;
			const double v = (raw - mean) * w;
			data[static_cast<std::size_t>(i)] = std::complex<double>(v, 0.0);
		}
		FFT(data, false);
		for (std::size_t i = 0; i < data.size(); ++i) {
			const double re = data[i].real();
			const double im = data[i].imag();
			data[i] = std::complex<double>(re * re + im * im, 0.0);
		}
		FFT(data, true);

		autocorrelation.resize(static_cast<std::size_t>(frameLength), 0.0);
		const double zero = data[0].real();
		if (zero <= 1.0e-20) {
			autocorrelation.clear();
			return;
		}
		for (int i = 0; i < frameLength; ++i) {
			autocorrelation[static_cast<std::size_t>(i)] =
				data[static_cast<std::size_t>(i)].real() / zero;
		}
	}

	static inline double AutocorrelationAtFrequency(const std::vector<double>& ac,
		double sampleRate,
		double frequency) {
		if (ac.size() < 4 || frequency <= 0.0) return 0.0;
		const double lag = sampleRate / frequency;
		if (lag < 2.0 || lag >= static_cast<double>(ac.size() - 1)) return 0.0;
		const int i = static_cast<int>(lag);
		const double frac = lag - static_cast<double>(i);
		double value = ac[static_cast<std::size_t>(i)] * (1.0 - frac) +
			ac[static_cast<std::size_t>(i + 1)] * frac;
		const double correction = static_cast<double>(ac.size()) /
			(static_cast<double>(ac.size()) - lag);
		value *= correction;
		return std::max(-1.0, std::min(1.0, value));
	}

	static inline double CmndAtFrequency(const YinResult& yin,
		double sampleRate,
		double frequency) {
		if (yin.cmndValues.size() < 3 || frequency <= 0.0) return 1.0;
		const double lag = sampleRate / frequency;
		if (lag < 1.0 || lag >= static_cast<double>(yin.cmndValues.size() - 1)) return 1.0;
		const int i = static_cast<int>(lag);
		const double frac = lag - static_cast<double>(i);
		return yin.cmndValues[static_cast<std::size_t>(i)] * (1.0 - frac) +
			yin.cmndValues[static_cast<std::size_t>(i + 1)] * frac;
	}


	inline void EstimationDelayK(float* target,
		int numSamples,
		float* overtoneFreqs,
		int& numOvertoneFreqs,
		float sampleRate) {
		const int requested = numOvertoneFreqs;
		numOvertoneFreqs = 0;

		if (!target || !overtoneFreqs || numSamples < 64 || requested <= 0 ||
			!std::isfinite(sampleRate) || sampleRate <= 0.0f) {
			return;
		}

		const double sr = static_cast<double>(sampleRate);
		const double nyquist = 0.5 * sr;
		if (nyquist <= 20.0) return;

		YinResult yin = EstimateYin(target, numSamples, sr);
		if (!yin.valid || yin.frequency <= 0.0) return;

		// Longer windows help resolve low/mid notes; very high notes decay
		// quickly, so their spectrum is taken from the attack/sustain onset.
		double spectralSeconds = 0.35;
		if (yin.frequency < 500.0) spectralSeconds = 1.20;
		else if (yin.frequency < 1500.0) spectralSeconds = 0.80;

		std::vector<Peak> peaks;
		double maxPeakAmplitude = 0.0;
		if (!BuildSpectrum(target, numSamples, sr, spectralSeconds,
			peaks, maxPeakAmplitude) || peaks.empty()) {
			return;
		}

		// Candidate fundamentals are normally the YIN estimate. If YIN's
		// first period is ambiguous, also test its octave-down alternative.
		// A strong, isolated high-frequency partial cluster can override a
		// low-frequency YIN result when no corresponding narrow spectral
		// peak exists at the YIN frequency (common with very high piano notes).
		std::vector<double> bases;
		std::vector<int> baseDivisors;
		bool highClusterOverride = false;
		const double highClusterFrequency = FindStrongHighCluster(
			peaks, maxPeakAmplitude, yin.frequency, highClusterOverride);
		const double cmndAtYin = CmndAtFrequency(yin, sr, yin.frequency);

		if (highClusterOverride && highClusterFrequency > 15.0 &&
			highClusterFrequency < nyquist) {
			bases.push_back(highClusterFrequency);
			baseDivisors.push_back(1);
		}
		else {
			bases.push_back(yin.frequency);
			baseDivisors.push_back(1);
			// Check octave/subharmonic ambiguities only when the longer
			// period has a distinctly better YIN minimum. This avoids
			// promoting arbitrary low-frequency noise peaks.
			for (int divisor = 2; divisor <= 4; ++divisor) {
				const double alternate = yin.frequency / static_cast<double>(divisor);
				if (alternate < 20.0) continue;
				const double alternateCmnd = CmndAtFrequency(yin, sr, alternate);
				if (alternateCmnd + 0.03 < cmndAtYin) {
					bases.push_back(alternate);
					baseDivisors.push_back(divisor);
				}
			}
		}

		const int analysisHarmonics = std::min(128, std::max(16, requested));
		const double bGrid[] = {
			0.0, 0.00002, 0.00005, 0.00010, 0.00015, 0.00020,
			0.00030, 0.00040, 0.00050, 0.00075, 0.00100, 0.00125,
			0.00150, 0.00175, 0.00200, 0.00250, 0.00300, 0.00350,
			0.00400, 0.00450, 0.00500, 0.00550, 0.00600, 0.00650,
			0.00700, 0.00750, 0.00800, 0.00900, 0.01000, 0.01200,
			0.01400, 0.01600, 0.01800, 0.02000, 0.02400, 0.02800,
			0.03200, 0.04000, 0.05000
		};
		const int bCount = static_cast<int>(sizeof(bGrid) / sizeof(bGrid[0]));

		std::vector<double> ac;
		double acSampleRate = sr;
		BuildWindowedAutocorrelation(target, numSamples, sr, 0.40, ac);

		Candidate best;
		best.valid = false;

		for (std::size_t bi = 0; bi < bases.size(); ++bi) {
			const double base = bases[bi];
			const int divisor = baseDivisors[bi];
			if (!(base >= 15.0 && base < nyquist)) continue;
			const bool isSubharmonicCandidate = divisor > 1;

			for (int bIndex = 0; bIndex < bCount; ++bIndex) {
				const double B = bGrid[bIndex];
				for (int step = -30; step <= 30; ++step) {
					const double fraction = static_cast<double>(step) * 0.002;
					const double f1 = base * (1.0 + fraction);
					if (!(f1 >= 15.0 && f1 < nyquist)) continue;

					Candidate current;
					current.valid = true;
					current.fundamental = f1;
					current.inharmonicity = B;
					current.prefixCount = 0;
					current.score = 0.0;
					current.matches.clear();

					for (int h = 1; h <= analysisHarmonics; ++h) {
						const double hD = static_cast<double>(h);
						const double predicted = f1 * hD *
							std::sqrt((1.0 + B * hD * hD) / (1.0 + B));
						if (predicted >= nyquist) break;

						const double tolerance = std::max(0.8, predicted * 0.0035);
						const Peak* peak = FindStrongestPeakInRange(
							peaks, predicted - tolerance, predicted + tolerance);
						if (peak && peak->amplitude >= maxPeakAmplitude * 0.001) {
							Match m;
							m.harmonic = h;
							m.frequency = peak->frequency;
							m.amplitude = peak->amplitude;
							current.matches.push_back(m);
						}
					}

					// Only the initial uninterrupted sequence is eligible
					// for output. Later matches after a gap are not promoted.
					for (std::size_t mi = 0; mi < current.matches.size(); ++mi) {
						if (current.matches[mi].harmonic == current.prefixCount + 1) {
							++current.prefixCount;
						}
						else if (current.matches[mi].harmonic > current.prefixCount + 1) {
							break;
						}
					}

					current.score = current.prefixCount * 4.0 +
						current.matches.size() * 0.25;
					for (std::size_t mi = 0; mi < current.matches.size(); ++mi) {
						const Match& m = current.matches[mi];
						const double relative = std::max(0.0, m.amplitude / maxPeakAmplitude);
						current.score += (m.harmonic <= 8 ? 1.2 : 0.2) *
							std::sqrt(relative);
					}
					if (current.prefixCount == 0) current.score -= 10.0;
					if (!current.matches.empty()) {
						const int lastH = current.matches.back().harmonic;
						current.score -= 0.5 *
							static_cast<double>(lastH - static_cast<int>(current.matches.size()));
					}

					// YIN/AC evidence is useful below ~1.5 kHz. At higher
					// pitches integer-lag quantization becomes too coarse.
					if (current.prefixCount >= 1 && f1 <= 1500.0 && !ac.empty()) {
						current.score += 4.0 * AutocorrelationAtFrequency(
							ac, acSampleRate, f1);

						// An octave-down candidate is accepted only when its
						// time-domain difference minimum is materially better.
						if (isSubharmonicCandidate) {
							const double cmndAtCandidate = CmndAtFrequency(
								yin, acSampleRate, f1);
							current.score += 150.0 *
								(cmndAtYin - std::min(1.0, std::max(0.0, cmndAtCandidate)));
						}
					}

					current.score -= 2.0 * std::fabs(std::log(f1 / base));

					if (!best.valid || current.score > best.score) {
						best = current;
					}
				}
			}
		}

		if (!best.valid || best.prefixCount <= 0 || best.matches.empty()) return;

		const int outputCount = std::min(requested, best.prefixCount);
		int written = 0;
		for (int h = 1; h <= outputCount; ++h) {
			// matches are in increasing harmonic order; only write exact
			// consecutive indices 1..prefixCount.
			const Match* found = nullptr;
			for (std::size_t mi = 0; mi < best.matches.size(); ++mi) {
				if (best.matches[mi].harmonic == h) {
					found = &best.matches[mi];
					break;
				}
			}
			if (!found || !std::isfinite(found->frequency) || found->frequency <= 0.0) {
				break;
			}
			overtoneFreqs[written++] = static_cast<float>(found->frequency);
		}
		numOvertoneFreqs = written;
	}

	inline void EstimationDelayK(float* target,
		int numSamples,
		float* overtoneFreqs,
		int& numOvertoneFreqs)
	{
		EstimationDelayK(target, numSamples, overtoneFreqs, numOvertoneFreqs, PianoDWG::SampleRate);
	}
};

using namespace EstimationOvertone;


namespace OptimizeMK
{
#define PI 3.14159265358979323846
#define TWO_PI (2.0 * PI)
	typedef struct
	{
		double phase;
		double slope;
	} PhaseSlope;
	static inline PhaseSlope stage(double theta, double slope_theta, double k)
	{
		const double s = sin(theta);
		const double c = cos(theta);
		const double den = 1.0 + 2.0 * k * c + k * k;
		PhaseSlope out;
		out.phase = theta - 2.0 * atan2(k * s, 1.0 + k * c);
		out.slope = ((1.0 - k * k) / den) * slope_theta;
		return out;
	}
	static inline PhaseSlope lattice_phase_slope(double omega, double k0, double k1, double k2, double k3)
	{
		const PhaseSlope p3 = stage(omega, 1.0, k3);
		const PhaseSlope p2 = stage(omega + p3.phase, 1.0 + p3.slope, k2);
		const PhaseSlope p1 = stage(omega + p2.phase, 1.0 + p2.slope, k1);
		const PhaseSlope p0 = stage(omega + p1.phase, 1.0 + p1.slope, k0);
		return p0;
	}
	static inline PhaseSlope cascade_phase_slope(double omega, double k0, double k1, double k2, double k3)
	{
		const PhaseSlope p0 = stage(omega, 1.0, k0);
		const PhaseSlope p1 = stage(omega, 1.0, k1);
		const PhaseSlope p2 = stage(omega, 1.0, k2);
		const PhaseSlope p3 = stage(omega, 1.0, k3);
		return { p0.phase + p1.phase + p2.phase + p3.phase, p0.slope + p1.slope + p2.slope + p3.slope };
	}
#define HOMOTOPY_STEPS 20
#define NEWTON_STEPS 20
	inline double ks_freq(int i, double m, double k0, double k1, double k2, double k3, double fs)
	{
		double w = TWO_PI * (double)i / ((double)m + 4.0);
		for (int tstep = 1; tstep <= HOMOTOPY_STEPS; ++tstep)
		{
			const double t = (double)tstep / (double)HOMOTOPY_STEPS;
			for (int step = 0; step < NEWTON_STEPS; ++step)
			{
				const PhaseSlope p = cascade_phase_slope(w, t * k0, t * k1, t * k2, t * k3);
				const double F = (double)m * w + p.phase - TWO_PI * (double)i;
				const double dF = (double)m + p.slope;
				w -= F / dF;
			}
		}
		return w * fs / TWO_PI;
	}
	inline float ks_freq_params(int i, float* params)
	{
		return ks_freq(i, params[0], params[1], params[2], params[3], params[4], PianoDWG::SampleRate);
	}
	inline float m_lockf0(float f0, float k0, float k1, float k2, float k3)
	{
		const double omega = 6.28318530717958647692 * f0 / PianoDWG::SampleRate;
		const auto p = OptimizeMK::cascade_phase_slope(omega, k0, k1, k2, k3);
		return static_cast<float>((6.28318530717958647692 - p.phase) / omega);
	}
}


inline double softfloor(double x)
{
	const int N = 10;
	double y = x - 0.5;
	for (int n = 1; n <= N; ++n)
		y += sin(2.0 * 3.141592653589 * n * x) / (3.141592653589 * n);
	return y;
}
inline float LossMK(float* params, float* tgfreqs, int numTargets)
{
	double mse = 0.;
	float k0 = params[0];
	float k1 = params[1];
	float k2 = params[2];
	float k3 = params[3];
	float mv = OptimizeMK::m_lockf0(tgfreqs[0], k0, k1, k2, k3);
	float m = mv > 1. ? mv : 1.;

	for (int i = 2; i < numTargets; ++i)
	{
		double tgf = tgfreqs[i - 1];
		double ksf = OptimizeMK::ks_freq(i, m, k0, k1, k2, k3, PianoDWG::SampleRate);
		double v = tgf - ksf;
		mse += v * v * std::sqrt((float)i);
	}
	return mse / numTargets * 0.001;
}

inline std::tuple<float, float, float, float, float> mkEstim(float* dispFreqs, int NumDispFreqs)//估计基频与色散
{
	AdamW<float> optmk(0.005, 0.01, 0.9, 0.9999);
	float mkparams[4] = { -0.51,-0.52,-0.53,-0.54 };
	float mkbestparams[4];
	float mkbestloss = 9999999999.;
	float mkgrads[4];

	float bestbasinloss = 9999999999.;
	for (int basin = -40; basin <= -20; basin++)
	{
		AdamW<float> optbasin(0.005, 0.01, 0.9, 0.9999);
		float k = (float)basin / 41.;
		float mkbasinparams[4] = { k + .01f,k + .02f,k + .03f,k + .04f };
		float mkbasingrads[4];
		for (int iter = 0; iter < 80; ++iter)
		{
			float loss = gradientMK(mkbasinparams, mkbasingrads, 4, dispFreqs, NumDispFreqs);
			float lr = optbasin.Update(mkbasinparams, mkbasingrads, 4, loss);
			for (int i = 0; i < 4; ++i)
			{
				auto& v = mkbasinparams[i];
				if (v > 0.999995)v = 0.999995;
				if (v < -0.999995)v = -0.999995;
			}
			if (bestbasinloss > loss)
			{
				bestbasinloss = loss;
				for (int i = 0; i < 4; ++i)mkparams[i] = mkbasinparams[i];
			}
		}
	}

	for (int iter = 0; iter < 2000; ++iter)
	{
		float loss = gradientMK(mkparams, mkgrads, 4, dispFreqs, NumDispFreqs);
		float lr = optmk.Update(mkparams, mkgrads, 4, loss);
		for (int i = 0; i < 4; ++i)
		{
			auto& v = mkparams[i];
			if (v > 0.999995)v = 0.999995;
			if (v < -0.999995)v = -0.999995;
		}
		if (mkbestloss > loss)
		{
			mkbestloss = loss;
			for (int i = 0; i < 4; ++i)mkbestparams[i] = mkparams[i];
		}
		//printf("iter:%d loss:%.5f\n", iter, loss);
	}
	float estim = OptimizeMK::m_lockf0(dispFreqs[0], mkbestparams[0], mkbestparams[1], mkbestparams[2], mkbestparams[3]);
	printf("m:%f,k={%f, %f, %f, %f},loss:%f\n", estim, mkbestparams[0], mkbestparams[1], mkbestparams[2], mkbestparams[3], mkbestloss);
	return { estim, mkbestparams[0], mkbestparams[1], mkbestparams[2], mkbestparams[3] };
}