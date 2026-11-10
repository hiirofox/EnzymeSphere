#pragma once
#include "define.h"
#include "loss.h"
#include "PianoDWG2.h"
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
    static const int kPianoModelFitPartials = 8;
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
        double offset = (denom == 0.0) ? 0.0 : 0.5 * (left - right) / denom;
        offset = (std::max)(-0.5, (std::min)(0.5, offset));
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
    inline bool EstimateYinFrame(const std::vector<double>& signal, int start, int frameLength, double sampleRate, double& frequency, double& quality)
    {
        if (start < 0 || frameLength < 1024 || start + frameLength > static_cast<int>(signal.size()) || !(sampleRate > 0.0)) return false;
        const int nfft = NextPowerOfTwo(frameLength * 2);
        if (nfft <= 0) return false;
        std::vector<std::complex<double> > spectrum(static_cast<std::size_t>(nfft), std::complex<double>(0.0, 0.0));
        std::vector<double> prefix(static_cast<std::size_t>(frameLength + 1), 0.0);
        double mean = 0.0;
        for (int i = 0; i < frameLength; ++i) mean += signal[static_cast<std::size_t>(start + i)];
        mean /= frameLength;
        for (int i = 0; i < frameLength; ++i)
        {
            const double x = signal[static_cast<std::size_t>(start + i)] - mean;
            spectrum[static_cast<std::size_t>(i)] = std::complex<double>(x, 0.0);
            prefix[static_cast<std::size_t>(i + 1)] = prefix[static_cast<std::size_t>(i)] + x * x;
        }
        if (!(prefix.back() > 1.0e-20)) return false;
        FFT(spectrum);
        for (int i = 0; i < nfft; ++i)
        {
            const double power = std::norm(spectrum[static_cast<std::size_t>(i)]);
            spectrum[static_cast<std::size_t>(i)] = std::complex<double>(power, 0.0);
        }
        FFT(spectrum);
        const int minLag = (std::max)(2, static_cast<int>(sampleRate / 5000.0));
        const int maxLag = (std::min)(static_cast<int>(sampleRate / 20.0), frameLength / 2 - 1);
        if (maxLag <= minLag + 2) return false;
        std::vector<double> cmnd(static_cast<std::size_t>(maxLag + 1), 1.0);
        double cumulative = 0.0;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            const double leftEnergy = prefix[static_cast<std::size_t>(frameLength - tau)];
            const double rightEnergy = prefix[static_cast<std::size_t>(frameLength)] - prefix[static_cast<std::size_t>(tau)];
            const double correlation = spectrum[static_cast<std::size_t>(tau)].real() / nfft;
            const double difference = (std::max)(0.0, leftEnergy + rightEnergy - 2.0 * correlation);
            cumulative += difference;
            cmnd[static_cast<std::size_t>(tau)] = cumulative > 1.0e-20 ? difference * tau / cumulative : 1.0;
        }
        int selected = -1;
        for (int tau = minLag + 1; tau < maxLag; ++tau)
        {
            if (cmnd[static_cast<std::size_t>(tau)] < 0.20)
            {
                while (tau + 1 < maxLag && cmnd[static_cast<std::size_t>(tau + 1)] < cmnd[static_cast<std::size_t>(tau)]) ++tau;
                selected = tau;
                break;
            }
        }
        if (selected < 0)
        {
            double best = 0.40;
            for (int tau = minLag + 1; tau < maxLag; ++tau)
            {
                if (cmnd[static_cast<std::size_t>(tau)] <= cmnd[static_cast<std::size_t>(tau - 1)] && cmnd[static_cast<std::size_t>(tau)] < cmnd[static_cast<std::size_t>(tau + 1)] && cmnd[static_cast<std::size_t>(tau)] < best)
                {
                    best = cmnd[static_cast<std::size_t>(tau)];
                    selected = tau;
                }
            }
        }
        if (selected < 0) return false;
        const double y0 = cmnd[static_cast<std::size_t>(selected - 1)];
        const double y1 = cmnd[static_cast<std::size_t>(selected)];
        const double y2 = cmnd[static_cast<std::size_t>(selected + 1)];
        const double denominator = y0 - 2.0 * y1 + y2;
        double offset = std::fabs(denominator) > 1.0e-15 ? 0.5 * (y0 - y2) / denominator : 0.0;
        offset = (std::max)(-0.5, (std::min)(0.5, offset));
        const double lag = static_cast<double>(selected) + offset;
        frequency = sampleRate / lag;
        quality = (std::max)(0.0, y1 - 0.25 * (y0 - y2) * offset);
        return IsFinite(frequency) && frequency >= 20.0 && frequency <= 5000.0 && IsFinite(quality) && quality < 0.40;
    }
    inline bool EstimateFundamental(const std::vector<double>& segment, const std::vector<double>& magnitude, double sampleRate, int nfft, double& f1, double& initialB)
    {
        if (segment.size() < 1024 || magnitude.size() < 4 || !(sampleRate > 0.0) || nfft <= 0) return false;
        const int frameLength = static_cast<int>(segment.size());
        const int corrNfft = NextPowerOfTwo(frameLength * 2);
        if (corrNfft <= 0) return false;
        std::vector<std::complex<double> > corrSpectrum(static_cast<std::size_t>(corrNfft), std::complex<double>(0.0, 0.0));
        std::vector<double> prefix(static_cast<std::size_t>(frameLength + 1), 0.0);
        double mean = 0.0;
        for (int i = 0; i < frameLength; ++i) mean += segment[static_cast<std::size_t>(i)];
        mean /= frameLength;
        for (int i = 0; i < frameLength; ++i)
        {
            const double window = 0.5 - 0.5 * std::cos(kTwoPi * static_cast<double>(i) / (frameLength - 1));
            const double x = (segment[static_cast<std::size_t>(i)] - mean) * window;
            corrSpectrum[static_cast<std::size_t>(i)] = std::complex<double>(x, 0.0);
            prefix[static_cast<std::size_t>(i + 1)] = prefix[static_cast<std::size_t>(i)] + x * x;
        }
        if (!(prefix.back() > 1.0e-20)) return false;
        FFT(corrSpectrum);
        for (int i = 0; i < corrNfft; ++i)
        {
            const double power = std::norm(corrSpectrum[static_cast<std::size_t>(i)]);
            corrSpectrum[static_cast<std::size_t>(i)] = std::complex<double>(power, 0.0);
        }
        FFT(corrSpectrum);
        const double nyquist = 0.5 * sampleRate;
        const double binHz = sampleRate / static_cast<double>(nfft);
        const int minLag = (std::max)(2, static_cast<int>(std::floor(sampleRate / 5000.0)));
        const int maxLag = (std::min)(static_cast<int>(std::ceil(sampleRate / 20.0)), frameLength / 2 - 2);
        if (maxLag <= minLag + 2) return false;
        std::vector<double> correlation(static_cast<std::size_t>(maxLag + 2), 0.0);
        for (int lag = minLag; lag <= maxLag + 1; ++lag)
        {
            const double left = prefix[static_cast<std::size_t>(frameLength - lag)];
            const double right = prefix.back() - prefix[static_cast<std::size_t>(lag)];
            const double raw = corrSpectrum[static_cast<std::size_t>(lag)].real() / corrNfft;
            correlation[static_cast<std::size_t>(lag)] = (left > 1.0e-20 && right > 1.0e-20) ? raw / std::sqrt(left * right) : 0.0;
        }
        struct Candidate { double frequency; double correlation; };
        std::vector<Candidate> candidates;
        auto addCandidate = [&candidates, &correlation, minLag, maxLag, sampleRate](double frequency)
            {
                if (!(frequency >= 20.0 && frequency <= 5000.0) || !IsFinite(frequency)) return;
                const double lag = sampleRate / frequency;
                const int lo = static_cast<int>(std::floor(lag));
                const double fraction = lag - lo;
                const int i0 = (std::max)(minLag, (std::min)(maxLag, lo));
                const int i1 = (std::max)(minLag, (std::min)(maxLag, lo + 1));
                const double corr = correlation[static_cast<std::size_t>(i0)] * (1.0 - fraction) + correlation[static_cast<std::size_t>(i1)] * fraction;
                for (std::size_t i = 0; i < candidates.size(); ++i)
                {
                    if (std::fabs(1200.0 * std::log2(frequency / candidates[i].frequency)) < 2.0)
                    {
                        if (corr > candidates[i].correlation) candidates[i] = { frequency, corr };
                        return;
                    }
                }
                candidates.push_back({ frequency, corr });
            };
        for (int lag = minLag + 1; lag <= maxLag; ++lag)
        {
            const double c = correlation[static_cast<std::size_t>(lag)];
            if (c < correlation[static_cast<std::size_t>(lag - 1)] || c < correlation[static_cast<std::size_t>(lag + 1)] || c < 0.08) continue;
            const double y0 = correlation[static_cast<std::size_t>(lag - 1)];
            const double y1 = c;
            const double y2 = correlation[static_cast<std::size_t>(lag + 1)];
            const double denominator = y0 - 2.0 * y1 + y2;
            double offset = std::fabs(denominator) > 1.0e-15 ? 0.5 * (y0 - y2) / denominator : 0.0;
            offset = (std::max)(-0.5, (std::min)(0.5, offset));
            const double baseLag = static_cast<double>(lag) + offset;
            for (int divisor = 1; divisor <= 8; ++divisor)
            {
                const double candidateLag = baseLag * divisor;
                if (candidateLag > maxLag) break;
                const double frequency = sampleRate / candidateLag;
                addCandidate(frequency);
            }
        }
        for (int start = 0; start + 1024 <= frameLength; start += (std::max)(1, static_cast<int>(0.06 * sampleRate)))
        {
            const int length = (std::min)(frameLength - start, (std::max)(4096, static_cast<int>(0.24 * sampleRate)));
            double yinFrequency = 0.0;
            double yinQuality = 1.0;
            if (length >= 1024 && EstimateYinFrame(segment, start, length, sampleRate, yinFrequency, yinQuality))
            {
                addCandidate(yinFrequency);
                for (int divisor = 2; divisor <= 4; ++divisor) addCandidate(yinFrequency / divisor);
                for (int multiplier = 2; multiplier <= 3; ++multiplier) addCandidate(yinFrequency * multiplier);
            }
        }
        if (candidates.empty()) return false;
        double maxMagnitude = 0.0;
        for (std::size_t i = 1; i < magnitude.size(); ++i) maxMagnitude = (std::max)(maxMagnitude, magnitude[i]);
        if (!(maxMagnitude > 0.0)) return false;
        static const double bSeeds[] = { 0.0, 2.5e-5, 5.0e-5, 7.5e-5, 1.0e-4, 1.5e-4, 2.0e-4, 3.0e-4, 5.0e-4, 7.5e-4, 1.0e-3, 1.5e-3, 2.0e-3, 3.0e-3, 5.0e-3, 7.5e-3, 1.0e-2 };
        double bestScore = -1.0;
        double bestFrequency = 0.0;
        double bestB = kDefaultInitialB;
        for (std::size_t ci = 0; ci < candidates.size(); ++ci)
        {
            const double candidateFrequency = candidates[ci].frequency;
            for (std::size_t bi = 0; bi < sizeof(bSeeds) / sizeof(bSeeds[0]); ++bi)
            {
                const double seedB = bSeeds[bi];
                const int possible = (std::min)(24, static_cast<int>((nyquist - 2.0 * binHz) / candidateFrequency));
                if (possible <= 0) continue;
                double spectralScore = 0.0;
                double spectralWeight = 0.0;
                double oddScore = 0.0;
                double oddWeight = 0.0;
                double fundamentalScore = 0.0;
                int hits = 0;
                for (int n = 1; n <= possible; ++n)
                {
                    const double nn = static_cast<double>(n);
                    const double predicted = nn * candidateFrequency * std::sqrt((1.0 + seedB * nn * nn) / (1.0 + seedB));
                    const double tolerance = (std::max)(3.0 * binHz, (std::min)(0.22 * candidateFrequency, (std::max)(5.0, 0.004 * predicted)));
                    double peak = 0.0;
                    double amplitude = 0.0;
                    const double weight = 1.0 / std::sqrt(nn);
                    if (FindPeakInRange(magnitude, predicted - tolerance, predicted + tolerance, sampleRate, nfft, peak, &amplitude) && amplitude >= maxMagnitude * 1.0e-5)
                    {
                        const double error = std::fabs(peak - predicted) / tolerance;
                        const double evidence = std::sqrt(amplitude / maxMagnitude) * std::exp(-0.5 * error * error);
                        spectralScore += weight * evidence;
                        spectralWeight += weight;
                        ++hits;
                        if (n == 1) fundamentalScore = evidence;
                        if (n >= 3 && (n & 1))
                        {
                            oddScore += weight * evidence;
                            oddWeight += weight;
                        }
                    }
                }
                const double normalizedSpectral = spectralScore / ((possible > 0) ? (spectralWeight + 0.25 * (possible - hits) + 1.0e-12) : 1.0);
                const double normalizedOdd = oddWeight > 0.0 ? oddScore / oddWeight : 0.0;
                const double corrScore = (std::max)(0.0, (std::min)(1.0, candidates[ci].correlation));
                const double coverage = static_cast<double>(hits) / possible;
                const double candidateLag = sampleRate / candidateFrequency;
                const int doubleLag = static_cast<int>(std::nearbyint(candidateLag * 2.0));
                const double doublePeriodCorrelation = doubleLag <= maxLag ? correlation[static_cast<std::size_t>((std::max)(minLag, doubleLag))] : corrScore;
                const double subharmonicPenalty = (std::max)(0.0, doublePeriodCorrelation - corrScore);
                const double missingFundamentalPenalty = (fundamentalScore < 0.15 && normalizedOdd < 0.10) ? 0.18 : 0.0;
                const double score = 0.68 * corrScore + 0.20 * normalizedSpectral + 0.08 * normalizedOdd + 0.04 * fundamentalScore + 0.02 * coverage - 0.85 * subharmonicPenalty - missingFundamentalPenalty;
                if (score > bestScore + 1.0e-12 || (std::fabs(score - bestScore) <= 1.0e-12 && candidateFrequency > bestFrequency))
                {
                    bestScore = score;
                    bestFrequency = candidateFrequency;
                    bestB = seedB;
                }
            }
        }
        if (!(bestScore > 0.0) || !(bestFrequency >= 20.0) || !(bestFrequency <= 5000.0) || !IsFinite(bestFrequency)) return false;
        f1 = bestFrequency;
        initialB = bestB;
        return IsFinite(f1) && f1 >= 20.0 && f1 <= 5000.0 && IsFinite(initialB) && initialB >= 0.0;
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
        if (validCount < 3 || !(f1 > 0.0)) return false;
        double a = 1.0;
        double b = (std::max)(0.0, (std::min)(300.0, initialB * 1.0e4));
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
                const double trialB = (std::max)(0.0, (std::min)(300.0, b + scale * db));
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
        const int onsetSample = (std::max)(0, static_cast<int>(onsetTime * sampleRate));
        const int minimumLength = static_cast<int>(0.120 * sampleRate);
        const int maximumLength = static_cast<int>(1.8 * sampleRate);
        int activeEnd = (std::min)(numSamples, onsetSample + minimumLength);
        for (int first = onsetSample; first <= numSamples - frame; first += hop)
        {
            double sum = 0.0;
            for (int n = 0; n < frame; ++n)
            {
                const double x = target[first + n];
                sum += x * x;
            }
            if (std::sqrt(sum / frame) >= 0.015 * maxRms) activeEnd = (std::min)(numSamples, first + frame);
        }
        const int signalEnd = (std::min)(numSamples, (std::min)(onsetSample + maximumLength, (std::max)(activeEnd, onsetSample + minimumLength)));
        const int signalLength = signalEnd - onsetSample;
        if (onsetSample < 0 || signalLength < 1024) return false;
        std::vector<double> signal(static_cast<std::size_t>(signalLength));
        for (int i = 0; i < signalLength; ++i) signal[static_cast<std::size_t>(i)] = target[onsetSample + i];
        const int frameLengths[] = { static_cast<int>(0.20 * sampleRate), static_cast<int>(0.28 * sampleRate), static_cast<int>(0.36 * sampleRate), static_cast<int>(0.44 * sampleRate), static_cast<int>(0.50 * sampleRate) };
        int selectedStart = -1;
        int selectedLength = 0;
        double selectedScore = (std::numeric_limits<double>::max)();
        const double maxDelay = 0.45;
        for (std::size_t li = 0; li < sizeof(frameLengths) / sizeof(frameLengths[0]); ++li)
        {
            const int candidateLength = (std::min)(frameLengths[li], signalLength);
            if (candidateLength < 1024) continue;
            const int delayStep = (std::max)(1, static_cast<int>(0.025 * sampleRate));
            const int minDelay = static_cast<int>(0.100 * sampleRate);
            const int maxStart = (std::min)(static_cast<int>(maxDelay * sampleRate), signalLength - candidateLength);
            for (int candidateStart = minDelay; candidateStart <= maxStart; candidateStart += delayStep)
            {
                double energy = 0.0;
                for (int i = 0; i < candidateLength; ++i)
                {
                    const double x = signal[static_cast<std::size_t>(candidateStart + i)];
                    energy += x * x;
                }
                const double rms = std::sqrt(energy / candidateLength);
                if (rms < maxRms * 0.015) continue;
                double candidateFrequency = 0.0;
                double quality = 1.0;
                if (!EstimateYinFrame(signal, candidateStart, candidateLength, sampleRate, candidateFrequency, quality)) continue;
                const double energyPenalty = 0.025 * (1.0 - (std::min)(1.0, rms / maxRms));
                const double delayPenalty = 0.008 * (static_cast<double>(candidateStart) / (maxDelay * sampleRate));
                const double score = quality + energyPenalty + delayPenalty;
                if (score < selectedScore)
                {
                    selectedScore = score;
                    selectedStart = candidateStart;
                    selectedLength = candidateLength;
                }
            }
        }
        if (selectedStart < 0 || selectedLength < 1024) return false;
        const int start = onsetSample + selectedStart;
        const int segmentLength = selectedLength;
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
        if (!EstimateFundamental(segment, magnitude, sampleRate, nfft, f1, initialB)) return false;
        const int trackStart = (std::max)(0, (std::min)(numSamples - 1024, onsetSample + static_cast<int>(0.160 * sampleRate)));
        const int trackEnd = (std::min)(signalEnd, trackStart + static_cast<int>(1.25 * sampleRate));
        std::vector<double> trackingMagnitude = magnitude;
        if (trackEnd - trackStart > segmentLength)
        {
            std::vector<std::complex<double> > trackSpectrum(static_cast<std::size_t>(nfft), std::complex<double>(0.0, 0.0));
            double trackMean = 0.0;
            for (int i = trackStart; i < trackEnd; ++i) trackMean += target[i];
            trackMean /= trackEnd - trackStart;
            for (int i = trackStart; i < trackEnd; ++i)
            {
                const double x = 0.5 - 0.5 * std::cos(kTwoPi * static_cast<double>(i - trackStart) / (trackEnd - trackStart - 1));
                trackSpectrum[static_cast<std::size_t>(i - trackStart)] = std::complex<double>((target[i] - trackMean) * x, 0.0);
            }
            FFT(trackSpectrum);
            for (int i = 0; i <= nfft / 2; ++i) trackingMagnitude[static_cast<std::size_t>(i)] = std::abs(trackSpectrum[static_cast<std::size_t>(i)]);
        }
        const double binHz = sampleRate / static_cast<double>(nfft);
        const double nyquist = 0.5 * sampleRate;
        const double maximumFrequency = (std::min)(kMaxModelFrequency, nyquist - 2.0 * binHz);
        const double maxMagnitude = *std::max_element(trackingMagnitude.begin(), trackingMagnitude.end());
        if (!(maxMagnitude > 0.0)) return false;
        struct SpectralPeak { double frequency; double amplitude; double contrast; };
        std::vector<SpectralPeak> peaks;
        const int minBin = (std::max)(2, static_cast<int>(std::floor(18.0 / binHz)));
        const int maxBin = (std::min)(static_cast<int>(trackingMagnitude.size()) - 3, static_cast<int>(std::floor(maximumFrequency / binHz)));
        for (int bin = minBin; bin <= maxBin; ++bin)
        {
            const double amplitude = trackingMagnitude[static_cast<std::size_t>(bin)];
            if (amplitude < maxMagnitude * 2.0e-5 || amplitude < trackingMagnitude[static_cast<std::size_t>(bin - 1)] || amplitude <= trackingMagnitude[static_cast<std::size_t>(bin + 1)]) continue;
            const double frequency = InterpolatedPeakFrequency(trackingMagnitude, bin, sampleRate, nfft);
            if (frequency < 18.0 || frequency > maximumFrequency) continue;
            const int flank = (std::max)(4, static_cast<int>(std::ceil(9.0 / binHz)));
            const double left = trackingMagnitude[static_cast<std::size_t>((std::max)(0, bin - flank))];
            const double right = trackingMagnitude[static_cast<std::size_t>((std::min)(static_cast<int>(trackingMagnitude.size()) - 1, bin + flank))];
            const double contrast = amplitude / ((std::max)(left, right) + maxMagnitude * 1.0e-7);
            if (contrast >= 1.20) peaks.push_back({ frequency, amplitude, contrast });
        }
        if (peaks.empty()) return false;
        const auto model = [f1, initialB](int n) -> double
            {
                const double x = static_cast<double>(n);
                return x * f1 * std::sqrt((1.0 + initialB * x * x) / (1.0 + initialB));
            };
        const auto nearestPeak = [&peaks, maxMagnitude](double predicted, double interval, double halfWidth, double& found, double& strength) -> bool
            {
                double bestScore = -1.0e30;
                bool matched = false;
                for (std::size_t j = 0; j < peaks.size(); ++j)
                {
                    const double error = std::fabs(peaks[j].frequency - predicted);
                    if (error > halfWidth) continue;
                    const double relative = peaks[j].amplitude / maxMagnitude;
                    if (relative < 4.0e-5) continue;
                    const double displacement = error / ((std::max)(interval, 1.0));
                    const double score = 0.16 * std::log(1.0 + relative * 10000.0) + 0.04 * std::log(peaks[j].contrast) - 22.0 * displacement * displacement;
                    if (score > bestScore)
                    {
                        bestScore = score;
                        found = peaks[j].frequency;
                        strength = relative;
                        matched = true;
                    }
                }
                return matched;
            };
        std::vector<double> observed;
        std::vector<double> strengths;
        observed.reserve(kMaxMeasuredPartials);
        strengths.reserve(kMaxMeasuredPartials);
        double lastPeak = 0.0;
        double lastGap = f1;
        double gapSlope = 0.0;
        int lastIndex = 0;
        int gapCount = 0;
        for (int n = 1; n <= kMaxMeasuredPartials; ++n)
        {
            const double baseline = model(n);
            if (baseline > maximumFrequency && lastIndex == 0) break;
            const double baselineGap = model(n) - model(n - 1);
            double predicted = baseline;
            if (lastIndex > 0)
            {
                const int distance = n - lastIndex;
                const double propagated = lastPeak + lastGap * distance + 0.5 * gapSlope * distance * (distance + 1);
                const double anchoredModel = lastPeak + baseline - model(lastIndex);
                const double blend = distance == 1 ? 0.82 : (distance == 2 ? 0.60 : 0.25);
                predicted = blend * propagated + (1.0 - blend) * anchoredModel;
            }
            if (predicted > maximumFrequency && baseline > maximumFrequency) break;
            const double interval = (std::max)(binHz * 3.0, lastIndex > 0 ? (std::max)(0.7 * baselineGap, (std::min)(1.5 * baselineGap, lastGap)) : baselineGap);
            const double halfWidth = (std::max)(5.0 * binHz, 0.43 * interval);
            double found = 0.0, strength = 0.0;
            bool matched = predicted > 0.0 && predicted < maximumFrequency && nearestPeak(predicted, interval, halfWidth, found, strength);
            if (matched && n == 1 && std::fabs(found - f1) > (std::max)(0.8, 0.028 * f1)) matched = false;
            if (matched && lastIndex > 0)
            {
                const int distance = n - lastIndex;
                const double actualGap = (found - lastPeak) / distance;
                if (actualGap < (distance == 1 ? 0.78 : 0.68) * (distance == 1 ? lastGap : baselineGap) || actualGap >(distance == 1 ? 1.24 : 1.38) * (distance == 1 ? lastGap : baselineGap)) matched = false;
            }
            if (matched)
            {
                if (lastIndex > 0)
                {
                    const int distance = n - lastIndex;
                    const double newGap = (found - lastPeak) / distance;
                    if (distance == 1)
                    {
                        const double slope = newGap - lastGap;
                        if (lastIndex >= 2) gapSlope = 0.60 * gapSlope + 0.40 * (std::max)(-0.025 * baselineGap, (std::min)(0.025 * baselineGap, slope));
                        lastGap = 0.68 * newGap + 0.32 * lastGap;
                    }
                    else
                    {
                        lastGap = 0.60 * newGap + 0.40 * lastGap;
                        gapSlope *= 0.7;
                    }
                }
                lastIndex = n;
                lastPeak = found;
                gapCount = 0;
                observed.push_back(found);
                strengths.push_back(strength);
            }
            else
            {
                ++gapCount;
                if (gapCount > 3) gapSlope *= 0.7;
                observed.push_back(0.0);
                strengths.push_back(0.0);
            }
        }
        if (observed.empty()) return false;
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int i = 0; i < static_cast<int>(observed.size()); ++i)
            {
                if (observed[static_cast<std::size_t>(i)] > 0.0 && strengths[static_cast<std::size_t>(i)] > 0.002) continue;
                int left = i - 1, right = i + 1;
                while (left >= 0 && !(observed[static_cast<std::size_t>(left)] > 0.0)) --left;
                while (right < static_cast<int>(observed.size()) && !(observed[static_cast<std::size_t>(right)] > 0.0)) ++right;
                if (left < 0 || right >= static_cast<int>(observed.size()) || right - left > 12) continue;
                const double predicted = observed[static_cast<std::size_t>(left)] + (observed[static_cast<std::size_t>(right)] - observed[static_cast<std::size_t>(left)]) * (i - left) / static_cast<double>(right - left);
                const double gap = (observed[static_cast<std::size_t>(right)] - observed[static_cast<std::size_t>(left)]) / static_cast<double>(right - left);
                double found = 0.0, strength = 0.0;
                if (nearestPeak(predicted, gap, (std::max)(4.0 * binHz, 0.28 * gap), found, strength) && (found > observed[static_cast<std::size_t>(left)]) && found < observed[static_cast<std::size_t>(right)])
                {
                    observed[static_cast<std::size_t>(i)] = found;
                    strengths[static_cast<std::size_t>(i)] = strength;
                }
            }
        }
        double numerator = 0.0, denominator = 0.0;
        for (int n = 1; n <= (std::min)(12, static_cast<int>(observed.size())); ++n)
        {
            const double peak = observed[static_cast<std::size_t>(n - 1)];
            if (peak <= 0.0) continue;
            const double scale = model(n) / model(1);
            const double weight = 1.0 / static_cast<double>(n);
            numerator += weight * scale * peak;
            denominator += weight * scale * scale;
        }
        const double fittedFirst = denominator > 0.0 ? numerator / denominator : f1;
        F0 = fittedFirst / std::sqrt(1.0 + initialB);
        B = initialB;
        measuredPartials = observed;
        modelPartials.resize(observed.size());
        for (int i = 0; i < static_cast<int>(observed.size()); ++i)
        {
            if (observed[static_cast<std::size_t>(i)] > 0.0) { modelPartials[static_cast<std::size_t>(i)] = observed[static_cast<std::size_t>(i)]; continue; }
            int left = i - 1, right = i + 1;
            while (left >= 0 && !(observed[static_cast<std::size_t>(left)] > 0.0)) --left;
            while (right < static_cast<int>(observed.size()) && !(observed[static_cast<std::size_t>(right)] > 0.0)) ++right;
            if (i == 0) { modelPartials[static_cast<std::size_t>(i)] = fittedFirst; continue; }
            if (left >= 0 && right < static_cast<int>(observed.size())) modelPartials[static_cast<std::size_t>(i)] = observed[static_cast<std::size_t>(left)] + (observed[static_cast<std::size_t>(right)] - observed[static_cast<std::size_t>(left)]) * (i - left) / static_cast<double>(right - left);
            else if (left >= 0) modelPartials[static_cast<std::size_t>(i)] = observed[static_cast<std::size_t>(left)] + (model(i + 1) - model(left + 1));
            else if (right < static_cast<int>(observed.size())) modelPartials[static_cast<std::size_t>(i)] = observed[static_cast<std::size_t>(right)] - (model(right + 1) - model(i + 1));
            else modelPartials[static_cast<std::size_t>(i)] = model(i + 1);
        }
        for (int i = 0; i < static_cast<int>(modelPartials.size()); ++i)
        {
            if (!IsFinite(modelPartials[static_cast<std::size_t>(i)]) || modelPartials[static_cast<std::size_t>(i)] <= 0.0 || modelPartials[static_cast<std::size_t>(i)] > maximumFrequency || (i > 0 && !(modelPartials[static_cast<std::size_t>(i)] > modelPartials[static_cast<std::size_t>(i - 1)])))
            {
                modelPartials.resize(static_cast<std::size_t>(i));
                break;
            }
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
    EstimationDelayK(target, numSamples, dispFreqs, numDispFreqs, PianoDWG2::SampleRate);
}