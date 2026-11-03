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

namespace estimation_delay_k_detail
{
	static const double kPi = 3.141592653589793238462643383279502884;
	static const double kTwoPi = 2.0 * kPi;
	static const double kDefaultInitialB = 1.2e-4;
	static const int kPianoModelFitPartials = 20;
	static const int kMaxMeasuredPartials = 200;
	static const double kMaxModelFrequency = 10000.0;
	static const double kSoftL1Scale = 0.15;
	inline bool IsFinite(double x) { return std::isfinite(x) != 0; }
	inline int NextPowerOfTwo(int n)
	{
		int p = 1;
		while (p < n && p > 0 && p <= (std::numeric_limits<int>::max)() / 2)
			p <<= 1;
		return p;
	}
	inline void FFT(std::vector<std::complex<double> >& a)
	{
		const std::size_t n = a.size();
		for (std::size_t i = 1, j = 0; i < n; ++i)
		{
			std::size_t bit = n >> 1;
			for (; j & bit; bit >>= 1) j ^= bit;
			j ^= bit;
			if (i < j) std::swap(a[i], a[j]);
		}
		for (std::size_t len = 2; len <= n; len <<= 1)
		{
			const double angle = -kTwoPi / static_cast<double>(len);
			const std::complex<double> wlen(std::cos(angle), std::sin(angle));
			for (std::size_t i = 0; i < n; i += len)
			{
				std::complex<double> w(1.0, 0.0);
				const std::size_t half = len >> 1;
				for (std::size_t j = 0; j < half; ++j)
				{
					const std::complex<double> u = a[i + j];
					const std::complex<double> v = a[i + j + half] * w;
					a[i + j] = u + v;
					a[i + j + half] = u - v;
					w *= wlen;
				}
			}
			if (len > n / 2) break;
		}
	}
	inline double InterpolatedPeakFrequency(const std::vector<double>& mag, int bin, double sampleRate, int nfft)
	{
		if (bin <= 0 || bin >= static_cast<int>(mag.size()) - 1)
			return static_cast<double>(bin) * sampleRate / nfft;
		const double left = mag[static_cast<std::size_t>(bin - 1)];
		const double center = mag[static_cast<std::size_t>(bin)];
		const double right = mag[static_cast<std::size_t>(bin + 1)];
		const double denom = left - 2.0 * center + right;
		const double offset = (denom == 0.0) ? 0.0 : 0.5 * (left - right) / denom;
		return (static_cast<double>(bin) + offset) * sampleRate / nfft;
	}
	inline bool FindPeakInRange(const std::vector<double>& mag, double lowHz, double highHz, double sampleRate, int nfft, double& peakHz, double* peakMagnitude = 0)
	{
		if (!(sampleRate > 0.0) || !(highHz >= lowHz) || mag.empty()) return false;
		int first = static_cast<int>(std::ceil(lowHz * nfft / sampleRate));
		int last = static_cast<int>(std::floor(highHz * nfft / sampleRate));
		first = (std::max)(0, first);
		last = (std::min)(static_cast<int>(mag.size()) - 1, last);
		if (first > last) return false;
		int best = first;
		for (int i = first + 1; i <= last; ++i)
			if (mag[static_cast<std::size_t>(i)] > mag[static_cast<std::size_t>(best)])
				best = i;
		peakHz = InterpolatedPeakFrequency(mag, best, sampleRate, nfft);
		if (peakMagnitude) *peakMagnitude = mag[static_cast<std::size_t>(best)];
		return IsFinite(peakHz);
	}
	inline bool EstimateFundamental(const std::vector<double>& mag, double sampleRate, int nfft, double& f1, double& initialB)
	{
		if (mag.size() < 4 || !(sampleRate > 0.0) || nfft <= 0) return false;
		double maxMagnitude = 0.0;
		for (std::size_t i = 0; i < mag.size(); ++i) maxMagnitude = (std::max)(maxMagnitude, mag[i]);
		if (!(maxMagnitude > 0.0)) return false;
		const double nyquist = 0.5 * sampleRate;
		const double binHz = sampleRate / static_cast<double>(nfft);
		const int firstBin = (std::max)(1, static_cast<int>(std::ceil(20.0 / binHz)));
		const int lastBin = (std::min)(static_cast<int>(mag.size()) - 2, static_cast<int>(std::floor(nyquist / binHz)));
		std::vector<int> peakBins;
		for (int i = firstBin; i <= lastBin; ++i)
		{
			const double v = mag[static_cast<std::size_t>(i)];
			if (v >= maxMagnitude * 1.0e-5 && v >= mag[static_cast<std::size_t>(i - 1)] && v >= mag[static_cast<std::size_t>(i + 1)]) peakBins.push_back(i);
		}
		if (peakBins.empty()) return false;
		std::sort(peakBins.begin(), peakBins.end(), [&mag](int a, int b) { return mag[static_cast<std::size_t>(a)] > mag[static_cast<std::size_t>(b)]; });
		if (peakBins.size() > 96) peakBins.resize(96);
		static const double bSeeds[] = { 0.0, 5.0e-5, 1.0e-4, 1.5e-4, 2.5e-4, 4.0e-4, 5.0e-4, 7.0e-4, 1.0e-3, 1.25e-3, 1.5e-3, 2.0e-3, 2.5e-3, 3.0e-3, 4.0e-3, 5.0e-3, 7.5e-3, 1.0e-2 };
		double bestScore = -1.0;
		double bestF1 = 0.0;
		double bestB = kDefaultInitialB;
		for (std::size_t p = 0; p < peakBins.size(); ++p)
		{
			const double peakHz = InterpolatedPeakFrequency(mag, peakBins[p], sampleRate, nfft);
			const int maxHarmonic = (std::min)(200, static_cast<int>(peakHz / 20.0));
			for (int harmonic = 1; harmonic <= maxHarmonic; ++harmonic)
			{
				const double hh = static_cast<double>(harmonic);
				for (std::size_t bi = 0; bi < sizeof(bSeeds) / sizeof(bSeeds[0]); ++bi)
				{
					const double seedB = bSeeds[bi];
					const double candidateF1 = peakHz / (hh * std::sqrt((1.0 + seedB * hh * hh) / (1.0 + seedB)));
					if (candidateF1 < 20.0 || candidateF1 > 5000.0) continue;
					const int possible = (std::min)(kPianoModelFitPartials, static_cast<int>((nyquist - 1.0) / candidateF1));
					if (possible <= 0) continue;
					double weightedScore = 0.0;
					double totalWeight = 0.0;
					double fundamentalMagnitude = 0.0;
					int hits = 0;
					for (int n = 1; n <= possible; ++n)
					{
						const double nn = static_cast<double>(n);
						const double weight = 1.0 / nn;
						const double predicted = nn * candidateF1 * std::sqrt((1.0 + seedB * nn * nn) / (1.0 + seedB));
						const double tolerance = (std::max)(3.0 * binHz, 0.003 * predicted);
						totalWeight += weight;
						double foundHz = 0.0;
						double amplitude = 0.0;
						if (FindPeakInRange(mag, predicted - tolerance, predicted + tolerance, sampleRate, nfft, foundHz, &amplitude) && amplitude >= maxMagnitude * 1.0e-3)
						{
							const double error = std::fabs(foundHz - predicted) / tolerance;
							const double frequencyScore = std::exp(-0.5 * error * error);
							weightedScore += weight * std::sqrt(amplitude / maxMagnitude) * frequencyScore;
							++hits;
							if (n == 1) fundamentalMagnitude = amplitude / maxMagnitude;
						}
					}
					if (hits < 2 && fundamentalMagnitude < 0.15) continue;
					double score = weightedScore / (totalWeight + 1.0e-12);
					if (fundamentalMagnitude < 0.15) score *= 0.8;
					if (score > bestScore || (std::fabs(score - bestScore) <= 1.0e-12 && candidateF1 > bestF1))
					{
						bestScore = score;
						bestF1 = candidateF1;
						bestB = seedB;
					}
				}
			}
		}
		if (!(bestScore > 0.0) || !(bestF1 >= 20.0) || !IsFinite(bestF1)) return false;
		for (int pass = 0; pass < 4; ++pass)
		{
			double lowerSupport = 0.0;
			double lowerF1 = 0.0;
			double lowerB = bestB;
			for (int divisor = 2; divisor <= 8; ++divisor)
			{
				for (std::size_t bi = 0; bi < sizeof(bSeeds) / sizeof(bSeeds[0]); ++bi)
				{
					const double subB = bSeeds[bi];
					const double divisorScale = static_cast<double>(divisor) * std::sqrt((1.0 + subB * divisor * divisor) / (1.0 + subB));
					const double subF1 = bestF1 / divisorScale;
					if (subF1 < 20.0 || subF1 >= bestF1) continue;
					const int possible = (std::min)(24, static_cast<int>((nyquist - 1.0) / subF1));
					double support = 0.0;
					int hits = 0;
					for (int n = 1; n <= possible; ++n)
					{
						if (n % divisor == 0) continue;
						const double nn = static_cast<double>(n);
						const double predicted = nn * subF1 * std::sqrt((1.0 + subB * nn * nn) / (1.0 + subB));
						const double tolerance = (std::max)(3.0 * binHz, 0.003 * predicted);
						double foundHz = 0.0;
						double amplitude = 0.0;
						if (FindPeakInRange(mag, predicted - tolerance, predicted + tolerance, sampleRate, nfft, foundHz, &amplitude) && amplitude >= maxMagnitude * 0.05)
						{
							const double error = std::fabs(foundHz - predicted) / tolerance;
							const double frequencyScore = std::exp(-0.5 * error * error);
							if (frequencyScore >= 0.25)
							{
								support += std::sqrt(amplitude / maxMagnitude) * frequencyScore / std::sqrt(nn);
								++hits;
							}
						}
					}
					if (hits >= 2 && support > lowerSupport)
					{
						lowerSupport = support;
						lowerF1 = subF1;
						lowerB = subB;
					}
				}
			}
			if (lowerSupport < 0.20) break;
			bestF1 = lowerF1;
			bestB = lowerB;
		}
		f1 = bestF1;
		initialB = bestB;
		return IsFinite(f1) && f1 >= 20.0 && IsFinite(initialB) && initialB >= 0.0;
	}
	inline double PianoFitCost(double a, double b, const std::vector<double>& observed, double f1)
	{
		double cost = 0.0;
		const int fitCount = (std::min)(kPianoModelFitPartials, static_cast<int>(observed.size()));
		for (int n = 1; n <= fitCount; ++n)
		{
			const double measured = observed[static_cast<std::size_t>(n - 1)];
			if (!(measured > 0.0)) continue;
			const double nn = static_cast<double>(n);
			const double s = std::sqrt(1.0 + b * 1.0e-4 * nn * nn);
			const double r = f1 * a * s - measured / nn;
			const double x = r / kSoftL1Scale;
			cost += 2.0 * kSoftL1Scale * kSoftL1Scale * (std::sqrt(1.0 + x * x) - 1.0);
		}
		return cost;
	}
	inline bool FitPianoF0B(const std::vector<double>& observed, double f1, double& F0, double& B, double initialB)
	{
		const int fitCount = (std::min)(kPianoModelFitPartials, static_cast<int>(observed.size()));
		int validCount = 0;
		for (int n = 0; n < fitCount; ++n) if (observed[static_cast<std::size_t>(n)] > 0.0) ++validCount;
		if (validCount < 4 || !(f1 > 0.0)) return false;
		double a = 1.0;
		double b = (std::max)(0.0, (std::min)(100.0, initialB * 1.0e4));
		double cost = PianoFitCost(a, b, observed, f1);
		for (int iter = 0; iter < 100; ++iter)
		{
			double h00 = 0.0, h01 = 0.0, h11 = 0.0;
			double g0 = 0.0, g1 = 0.0;
			for (int n = 1; n <= fitCount; ++n)
			{
				const double measured = observed[static_cast<std::size_t>(n - 1)];
				if (!(measured > 0.0)) continue;
				const double nn = static_cast<double>(n);
				const double nn2 = nn * nn;
				const double s = std::sqrt(1.0 + b * 1.0e-4 * nn2);
				const double r = f1 * a * s - measured / nn;
				const double x = r / kSoftL1Scale;
				const double weight = 1.0 / std::sqrt(1.0 + x * x);
				const double j0 = f1 * s;
				const double j1 = f1 * a * (1.0e-4 * nn2) / (2.0 * s);
				h00 += weight * j0 * j0;
				h01 += weight * j0 * j1;
				h11 += weight * j1 * j1;
				g0 += weight * j0 * r;
				g1 += weight * j1 * r;
			}
			const double det = h00 * h11 - h01 * h01;
			if (!(det > 1.0e-24) || !IsFinite(det)) break;
			const double da = (-g0 * h11 + g1 * h01) / det;
			const double db = (-g1 * h00 + g0 * h01) / det;
			if (!IsFinite(da) || !IsFinite(db)) break;
			bool accepted = false;
			double scale = 1.0;
			for (int ls = 0; ls < 24; ++ls)
			{
				const double trialA = (std::max)(0.9, (std::min)(1.1, a + scale * da));
				const double trialB = (std::max)(0.0, (std::min)(100.0, b + scale * db));
				const double trialCost = PianoFitCost(trialA, trialB, observed, f1);
				if (trialCost < cost)
				{
					a = trialA;
					b = trialB;
					cost = trialCost;
					accepted = true;
					if (std::fabs(scale * da) < 1.0e-11 && std::fabs(scale * db) < 1.0e-9) iter = 100;
					break;
				}
				scale *= 0.5;
			}
			if (!accepted) break;
		}
		F0 = f1 * a;
		B = b * 1.0e-4;
		return IsFinite(F0) && IsFinite(B) && F0 > 0.0 && B >= 0.0;
	}
	inline bool EstimatePianoPartials(const float* target, int numSamples, double sampleRate, std::vector<double>& measuredPartials, std::vector<double>& modelPartials, double& f1, double& F0, double& B)
	{
		if (!target || numSamples <= 0 || !(sampleRate > 0.0)) return false;
		const int frame = static_cast<int>(0.020 * sampleRate);
		const int hop = static_cast<int>(0.005 * sampleRate);
		if (frame <= 0 || hop <= 0 || numSamples < frame) return false;
		double maxRms = 0.0;
		double onsetTime = 0.0;
		bool onsetFound = false;
		for (int first = 0; first <= numSamples - frame; first += hop)
		{
			double sum = 0.0;
			for (int n = 0; n < frame; ++n)
			{
				const double x = target[first + n];
				sum += x * x;
			}
			const double rms = std::sqrt(sum / frame);
			if (rms > maxRms) maxRms = rms;
		}
		if (!(maxRms > 0.0)) return false;
		for (int first = 0; first <= numSamples - frame; first += hop)
		{
			double sum = 0.0;
			for (int n = 0; n < frame; ++n)
			{
				const double x = target[first + n];
				sum += x * x;
			}
			const double rms = std::sqrt(sum / frame);
			if (rms >= 0.20 * maxRms)
			{
				onsetTime = (static_cast<double>(first) + 0.5 * frame) / sampleRate;
				onsetFound = true;
				break;
			}
		}
		if (!onsetFound) return false;
		const int start = static_cast<int>((onsetTime + 0.010) * sampleRate);
		const int minimumLength = static_cast<int>(0.250 * sampleRate);
		const int maximumLength = static_cast<int>(1.8 * sampleRate);
		int activeEnd = (std::min)(numSamples, start + minimumLength);
		for (int first = 0; first <= numSamples - frame; first += hop)
		{
			const double centerTime = (static_cast<double>(first) + 0.5 * frame) / sampleRate;
			if (centerTime < onsetTime) continue;
			double sum = 0.0;
			for (int n = 0; n < frame; ++n)
			{
				const double x = target[first + n];
				sum += x * x;
			}
			if (std::sqrt(sum / frame) >= 0.02 * maxRms) activeEnd = (std::min)(numSamples, first + frame);
		}
		const int end = (std::min)(numSamples, (std::min)(start + maximumLength, (std::max)(activeEnd, start + minimumLength)));
		const int segmentLength = end - start;
		if (start < 0 || segmentLength < 1024) return false;
		std::vector<double> segment(static_cast<std::size_t>(segmentLength));
		double mean = 0.0;
		for (int i = 0; i < segmentLength; ++i)
		{
			segment[static_cast<std::size_t>(i)] = target[start + i];
			mean += segment[static_cast<std::size_t>(i)];
		}
		mean /= segmentLength;
		int nfft = (std::max)(1 << 19, NextPowerOfTwo(segmentLength));
		if (nfft <= 0) return false;
		std::vector<std::complex<double> > spectrum(static_cast<std::size_t>(nfft), std::complex<double>(0.0, 0.0));
		for (int i = 0; i < segmentLength; ++i)
		{
			const double hann = (segmentLength <= 1) ? 1.0 : 0.5 - 0.5 * std::cos(kTwoPi * static_cast<double>(i) / (segmentLength - 1));
			spectrum[static_cast<std::size_t>(i)] = std::complex<double>((segment[static_cast<std::size_t>(i)] - mean) * hann, 0.0);
		}
		FFT(spectrum);
		std::vector<double> magnitude(static_cast<std::size_t>(nfft / 2 + 1));
		for (int i = 0; i <= nfft / 2; ++i)
			magnitude[static_cast<std::size_t>(i)] = std::abs(spectrum[static_cast<std::size_t>(i)]);
		double initialB = kDefaultInitialB;
		if (!EstimateFundamental(magnitude, sampleRate, nfft, f1, initialB)) return false;
		std::vector<double> observed;
		observed.reserve(kMaxMeasuredPartials);
		const double binHz = sampleRate / static_cast<double>(nfft);
		double maxMagnitude = 0.0;
		for (std::size_t i = 0; i < magnitude.size(); ++i) if (magnitude[i] > maxMagnitude) maxMagnitude = magnitude[i];
		for (int i = 1; i <= kMaxMeasuredPartials; ++i)
		{
			const double ii = static_cast<double>(i);
			const double predicted = ii * f1 * std::sqrt((1.0 + initialB * ii * ii) / (1.0 + initialB));
			const double halfWidth = (std::max)(6.0 * binHz, (std::min)(0.25 * f1, (std::max)(8.0, 0.006 * predicted)));
			if (predicted - halfWidth >= 0.5 * sampleRate || predicted + halfWidth >= 0.5 * sampleRate) break;
			double peak = 0.0;
			double peakMagnitude = 0.0;
			if (FindPeakInRange(magnitude, predicted - halfWidth, predicted + halfWidth, sampleRate, nfft, peak, &peakMagnitude) && peakMagnitude > maxMagnitude * 1.0e-5) observed.push_back(peak);
			else observed.push_back(0.0);
		}
		if (observed.size() < 2) return false;
		std::vector<double> firstFit(observed.begin(), observed.begin() + (std::min)(kPianoModelFitPartials, static_cast<int>(observed.size())));
		int validFitPartials = 0;
		for (std::size_t i = 0; i < firstFit.size(); ++i) if (firstFit[i] > 0.0) ++validFitPartials;
		if (validFitPartials < 4 || !FitPianoF0B(firstFit, f1, F0, B, initialB))
		{
			F0 = f1;
			B = validFitPartials < 4 ? kDefaultInitialB : initialB;
		}
		measuredPartials.swap(observed);
		modelPartials.clear();
		for (int i = 1; ; ++i)
		{
			const double ii = static_cast<double>(i);
			const double frequency = ii * F0 * std::sqrt(1.0 + B * ii * ii);
			if (!(frequency <= kMaxModelFrequency)) break;
			modelPartials.push_back(frequency);
			if (i > 10000) break;
		}
		return !modelPartials.empty() && !measuredPartials.empty();
	}
}
inline void EstimationDelayK(float* target, int numSamples, float* dispFreqs, int& numDispFreqs, float sampleRate)
{
	using namespace estimation_delay_k_detail;
	const int capacity = numDispFreqs;
	const float nanValue = (std::numeric_limits<float>::quiet_NaN)();
	numDispFreqs = 0;
	if (dispFreqs && capacity > 0) for (int i = 0; i < capacity; ++i) dispFreqs[i] = nanValue;
	if (!target || !dispFreqs || numSamples <= 0 || capacity <= 0 || !(sampleRate > 0.0f) || !IsFinite(sampleRate)) return;
	std::vector<double> measuredPartials;
	std::vector<double> estimatedPartials;
	double f1 = 0.0, F0 = 0.0, B = 0.0;
	if (!EstimatePianoPartials(target, numSamples, sampleRate, measuredPartials, estimatedPartials, f1, F0, B)) return;
	const int outputCount = (std::min)(capacity, static_cast<int>(estimatedPartials.size()));
	for (int i = 0; i < outputCount; ++i) dispFreqs[i] = static_cast<float>(estimatedPartials[static_cast<std::size_t>(i)]);
	numDispFreqs = outputCount;
}
inline void EstimationDelayK(float* target, int numSamples, float* dispFreqs, int& numDispFreqs)
{
	EstimationDelayK(target, numSamples, dispFreqs, numDispFreqs, PianoDWG::SampleRate);
}


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