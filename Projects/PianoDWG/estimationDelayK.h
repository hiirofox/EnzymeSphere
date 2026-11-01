#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

namespace EstimationDelayKDetail
{
    constexpr double Pi = 3.14159265358979323846;
    constexpr double SampleRate = 48000.0;
    constexpr int Sections = 4;
    // Stored loop output: one sample. Default bridge IIR: one sample.
    // A trained bridge with frequency-dependent phase needs separate compensation.
    constexpr double OtherLoopDelay = 2.0;
    constexpr double MinPitchHz = 27.5;
    constexpr double MaxPitchHz = 4200.0;
    constexpr double MaxPartialHz = 12000.0;
    constexpr int MaxPartials = 64;
    constexpr double AttackSkipSeconds = 0.500; // Skip 100 ms after detected onset.
    constexpr int MaxAnalysisSamples = 96000; // Two seconds, excluding attack.
    constexpr int HighPartialStart = 6;
    constexpr double MaxHighPartialWeight = 16.0;
    constexpr double MinPeakConfidence = 0.5;
    constexpr double FundamentalToleranceCents = 3.0;
    constexpr double FundamentalWeight = 100.0;
    constexpr double MaxDelayRatio = 10000.0; // Bounds every k strictly inside (-1,1).
    constexpr int AnalyticFitIterations = 60;
    constexpr int FitStarts = 4; // Unequal starts avoid the equal-section symmetry trap.
    static_assert(FundamentalToleranceCents >= 0.0 && FundamentalWeight > 0.0,
        "Fundamental tolerance must be nonnegative and its weight positive");
    static_assert(MaxDelayRatio > 1.0 && AnalyticFitIterations > 0,
        "Invalid analytic solver configuration");
    static_assert(Sections == 4 && FitStarts == 4, "This parameterization uses four cascade sections");
    static_assert(AttackSkipSeconds >= 0.0 && AttackSkipSeconds <= 60.0,
        "Attack skip must be between 0 and 60 seconds");

    // Double precision resolves small partial detunings. Analysis only.
    inline void FFT(std::vector<std::complex<double>>& x, bool inverse)
    {
        const int n = static_cast<int>(x.size());
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(x[i], x[j]);
        }
        for (int length = 2; length <= n; length *= 2)
        {
            const double angle = (inverse ? 2.0 : -2.0) * Pi / length;
            const std::complex<double> step(std::cos(angle), std::sin(angle));
            for (int start = 0; start < n; start += length)
            {
                std::complex<double> w(1.0, 0.0);
                for (int j = 0; j < length / 2; ++j)
                {
                    const auto a = x[start + j];
                    const auto b = w * x[start + j + length / 2];
                    x[start + j] = a + b;
                    x[start + j + length / 2] = a - b;
                    w *= step;
                }
            }
            if (length == n) break;
        }
        if (inverse)
            for (auto& value : x) value /= n;
    }

    inline double Median(std::vector<double> values)
    {
        std::sort(values.begin(), values.end());
        const auto middle = values.size() / 2;
        return values.size() % 2 ? values[middle]
            : 0.5 * (values[middle - 1] + values[middle]);
    }

    struct Partial
    {
        int number;
        double frequency;
        double confidence;
    };

    inline double PartialWeight(const Partial& partial)
    {
        if (partial.number == 1) return partial.confidence * FundamentalWeight;
        const double order = static_cast<double>(partial.number) / HighPartialStart;
        return partial.confidence * (std::clamp)(order * order, 1.0, MaxHighPartialWeight);
    }

    using FourValues = std::array<double, Sections>;
    using Matrix4 = std::array<FourValues, Sections>;

    // Single-string feedback with four independent first-order allpasses.
    // For constant 0<g<1 the magnitude peaks satisfy phase=2*pi*m.
    inline double LoopPhase(double frequency, int delay, const FourValues& r)
    {
        const double w = 2.0 * Pi * frequency / SampleRate;
        const double t = std::tan(w * 0.5);
        double phase = w * (delay + OtherLoopDelay);
        for (double value : r) phase += 2.0 * std::atan(value * t);
        return phase;
    }

    inline double LoopGroupDelay(double frequency, int delay, const FourValues& r)
    {
        const double t = std::tan(Pi * frequency / SampleRate);
        double groupDelay = delay + OtherLoopDelay;
        for (double value : r)
            groupDelay += value * (1.0 + t * t) / (1.0 + value * value * t * t);
        return groupDelay;
    }

    inline double PartialFrequency(int number, int delay, const FourValues& r, double hint = 0.0)
    {
        const double targetPhase = 2.0 * Pi * number;
        if (targetPhase >= Pi * (delay + OtherLoopDelay + Sections)) return 0.0;
        double dcDelay = delay + OtherLoopDelay;
        for (double value : r) dcDelay += value;
        double low = 0.0, high = SampleRate * 0.5;
        double frequency = hint > 0.0 ? hint : number * SampleRate / dcDelay;
        frequency = (std::clamp)(frequency, 1e-9, high - 1e-9);
        for (int step = 0; step < 64; ++step)
        {
            const double error = LoopPhase(frequency, delay, r) - targetPhase;
            if (std::abs(error) < 1e-12) return frequency;
            if (error < 0.0) low = frequency;
            else high = frequency;
            const double derivative = 2.0 * Pi / SampleRate * LoopGroupDelay(frequency, delay, r);
            const double next = frequency - error / derivative;
            frequency = next > low && next < high ? next : 0.5 * (low + high);
        }
        return 0.5 * (low + high);
    }

    struct ModelState
    {
        bool valid = false;
        FourValues r{};
        Matrix4 logRatioJacobian{};
        double fundamental = 0.0;
    };

    // Three logits distribute the allpass phase at f1; a fourth variable is
    // f1 detuning in cents. Thus independent k's cannot silently detune f1.
    // The last logit is fixed at zero to remove softmax's redundant direction.
    inline ModelState BuildModel(int delay, double targetF1, const FourValues& variables)
    {
        ModelState state;
        const double centsToLog = std::log(2.0) / 1200.0;
        state.fundamental = targetF1 * std::exp(variables[3] * centsToLog);
        const double w = 2.0 * Pi * state.fundamental / SampleRate;
        const double t = std::tan(w * 0.5);
        const double minR = 1.0 / MaxDelayRatio;
        const double minPhase = 2.0 * std::atan(minR * t);
        const double minPhaseSlope = minR * (1.0 + t * t) / (1.0 + minR * minR * t * t);
        const double remaining = 2.0 * Pi - w * (delay + OtherLoopDelay) - Sections * minPhase;
        if (remaining < -1e-12) return state;
        const double extraPhase = (std::max)(0.0, remaining);
        FourValues fraction{ variables[0], variables[1], variables[2], 0.0 };
        const double largest = *std::max_element(fraction.begin(), fraction.end());
        double denominator = 0.0;
        for (double& value : fraction)
        {
            value = std::exp(value - largest);
            denominator += value;
        }
        for (double& value : fraction) value /= denominator;
        for (int j = 0; j < Sections; ++j)
        {
            const double phase = minPhase + extraPhase * fraction[j];
            const double maxPhase = 2.0 * std::atan(MaxDelayRatio * t);
            if (phase <= 0.0 || phase > maxPhase || phase >= Pi) return state;
            state.r[j] = std::tan(phase * 0.5) / t;
            if (!std::isfinite(state.r[j]) || state.r[j] < minR * (1.0 - 1e-10)
                || state.r[j] > MaxDelayRatio * (1.0 + 1e-10)) return state;
            for (int i = 0; i < Sections - 1; ++i)
                state.logRatioJacobian[j][i] = extraPhase * fraction[j]
                    * ((j == i ? 1.0 : 0.0) - fraction[i]) / std::sin(phase);
            const double phaseW = minPhaseSlope
                - (delay + OtherLoopDelay + Sections * minPhaseSlope) * fraction[j];
            state.logRatioJacobian[j][3] = (phaseW / std::sin(phase) - 1.0 / std::sin(w))
                * w * centsToLog;
        }
        state.valid = true;
        return state;
    }

    struct PeakFit
    {
        bool valid = false;
        double cost = 1e300;
        FourValues gradient{};
        Matrix4 curvature{};
        double meanAbsCents = 0.0;
        double rmsCents = 0.0;
    };

    inline PeakFit EvaluatePeaks(const std::vector<Partial>& partials, int delay, const ModelState& state)
    {
        PeakFit result;
        if (!state.valid) return result;
        const double centsScale = 1200.0 / std::log(2.0);
        double sumWeight = 0.0, sumSquared = 0.0, sumAbs = 0.0;
        for (const auto& p : partials)
        {
            const double frequency = p.number == 1 ? state.fundamental
                : PartialFrequency(p.number, delay, state.r, p.frequency);
            if (frequency <= 0.0 || !std::isfinite(frequency)) return PeakFit{};
            const double cents = centsScale * std::log(frequency / p.frequency);
            FourValues jacobian{};
            if (p.number == 1) jacobian[3] = 1.0;
            else
            {
                const double w = 2.0 * Pi * frequency / SampleRate;
                const double t = std::tan(w * 0.5);
                const double scale = -centsScale / (w * LoopGroupDelay(frequency, delay, state.r));
                for (int j = 0; j < Sections; ++j)
                {
                    const double rt = state.r[j] * t;
                    const double phaseQ = 2.0 * rt / (1.0 + rt * rt);
                    for (int i = 0; i < Sections; ++i)
                        jacobian[i] += scale * phaseQ * state.logRatioJacobian[j][i];
                }
            }
            const double weight = PartialWeight(p);
            sumSquared += weight * cents * cents;
            sumAbs += weight * std::abs(cents);
            for (int i = 0; i < Sections; ++i)
            {
                result.gradient[i] += weight * cents * jacobian[i];
                for (int j = 0; j < Sections; ++j)
                    result.curvature[i][j] += weight * jacobian[i] * jacobian[j];
            }
            sumWeight += weight;
        }
        if (sumWeight <= 0.0 || !std::isfinite(sumSquared)) return PeakFit{};
        for (int i = 0; i < Sections; ++i)
        {
            result.gradient[i] /= sumWeight;
            for (int j = 0; j < Sections; ++j) result.curvature[i][j] /= sumWeight;
        }
        result.valid = true;
        result.cost = sumSquared / sumWeight;
        result.meanAbsCents = sumAbs / sumWeight;
        result.rmsCents = std::sqrt(result.cost);
        return result;
    }

    inline bool SolveStep(Matrix4 matrix, FourValues rhs, FourValues& step)
    {
        for (int column = 0; column < Sections; ++column)
        {
            int pivot = column;
            for (int row = column + 1; row < Sections; ++row)
                if (std::abs(matrix[row][column]) > std::abs(matrix[pivot][column])) pivot = row;
            if (!std::isfinite(matrix[pivot][column]) || std::abs(matrix[pivot][column]) < 1e-20) return false;
            std::swap(matrix[pivot], matrix[column]);
            std::swap(rhs[pivot], rhs[column]);
            for (int row = column + 1; row < Sections; ++row)
            {
                const double factor = matrix[row][column] / matrix[column][column];
                for (int j = column; j < Sections; ++j) matrix[row][j] -= factor * matrix[column][j];
                rhs[row] -= factor * rhs[column];
            }
        }
        for (int i = Sections - 1; i >= 0; --i)
        {
            double value = rhs[i];
            for (int j = i + 1; j < Sections; ++j) value -= matrix[i][j] * step[j];
            step[i] = value / matrix[i][i];
            if (!std::isfinite(step[i])) return false;
        }
        return true;
    }

    struct IntegerFit
    {
        int delay = 0;
        FourValues r{};
        PeakFit peaks;
    };

    inline IntegerFit FitIntegerDelay(const std::vector<Partial>& partials, int delay)
    {
        IntegerFit best;
        best.delay = delay;
        const double f1 = partials.front().frequency;
        FourValues minimumRatios{};
        minimumRatios.fill(1.0 / MaxDelayRatio);
        const double maxF1 = PartialFrequency(1, delay, minimumRatios, f1);
        if (maxF1 <= 0.0) return best;
        const double upperDetune = (std::min)(FundamentalToleranceCents, 1200.0 * std::log2(maxF1 / f1));
        if (upperDetune < -FundamentalToleranceCents) return best;
        // Equal initialization alone leaves all four section gradients equal.
        // Deliberately asymmetric phase allocations create distinct corner frequencies.
        const double seeds[FitStarts][Sections] = {
            { 1.0, 1.0, 1.0, 1.0 },
            { 0.7, 0.9, 1.1, 1.3 },
            { 0.4, 0.4, 1.6, 1.6 },
            { 0.15, 0.65, 1.3, 1.9 }
        };
        for (int start = 0; start < FitStarts; ++start)
        {
            FourValues variables{};
            for (int j = 0; j < Sections - 1; ++j)
                variables[j] = std::log(seeds[start][j] / seeds[start][3]);
            variables[3] = (std::clamp)(0.0, -FundamentalToleranceCents, upperDetune);
            ModelState state = BuildModel(delay, f1, variables);
            PeakFit current = EvaluatePeaks(partials, delay, state);
            if (!current.valid) continue;
            double damping = 1e-3;
            for (int iteration = 0; iteration < AnalyticFitIterations; ++iteration)
            {
                Matrix4 matrix = current.curvature;
                FourValues rhs{}, step{};
                for (int i = 0; i < Sections; ++i)
                {
                    matrix[i][i] += damping * (std::max)(current.curvature[i][i], 1e-6) + 1e-12;
                    rhs[i] = -current.gradient[i];
                }
                if (!SolveStep(matrix, rhs, step)) { damping *= 10.0; continue; }
                // At an active pitch bound, solve the three remaining directions
                // with detuning fixed, rather than clipping a coupled 4D step.
                if ((variables[3] <= -FundamentalToleranceCents + 1e-10 && step[3] < 0.0)
                    || (variables[3] >= upperDetune - 1e-10 && step[3] > 0.0))
                {
                    matrix = current.curvature;
                    for (int i = 0; i < Sections; ++i)
                    {
                        matrix[i][i] += damping * (std::max)(current.curvature[i][i], 1e-6) + 1e-12;
                        rhs[i] = -current.gradient[i];
                        matrix[3][i] = matrix[i][3] = 0.0;
                    }
                    matrix[3][3] = 1.0;
                    rhs[3] = 0.0;
                    if (!SolveStep(matrix, rhs, step)) { damping *= 10.0; continue; }
                }
                // Scale the entire step to preserve its direction.
                double fraction = 1.0;
                for (int i = 0; i < Sections; ++i)
                {
                    const double limit = i == 3 ? 1.0 : 0.75;
                    if (std::abs(step[i]) > limit) fraction = (std::min)(fraction, limit / std::abs(step[i]));
                }
                FourValues trial = variables;
                for (int i = 0; i < Sections; ++i) trial[i] += fraction * step[i];
                trial[3] = (std::clamp)(trial[3], -FundamentalToleranceCents, upperDetune);
                ModelState nextState = BuildModel(delay, f1, trial);
                PeakFit next = EvaluatePeaks(partials, delay, nextState);
                if (next.valid && next.cost < current.cost)
                {
                    const double improvement = current.cost - next.cost;
                    variables = trial;
                    state = nextState;
                    current = next;
                    damping = (std::max)(1e-9, damping * 0.25);
                    if (improvement < 1e-10 * (1.0 + current.cost)) break;
                }
                else
                {
                    damping *= 10.0;
                    if (damping > 1e12) break;
                }
            }
            const double fittedF1 = PartialFrequency(1, delay, state.r, f1);
            if (fittedF1 <= 0.0 || std::abs(1200.0 * std::log2(fittedF1 / f1))
                > FundamentalToleranceCents + 1e-6) continue;
            if (current.cost < best.peaks.cost)
            {
                best.r = state.r;
                best.peaks = current;
            }
        }
        return best;
    }

    inline FourValues SortedK(const FourValues& r)
    {
        FourValues k{};
        for (int i = 0; i < Sections; ++i) k[i] = (1.0 - r[i]) / (1.0 + r[i]);
        // Cascade order does not affect the transfer function; canonicalize output.
        std::sort(k.begin(), k.end());
        return k;
    }
}
// Four independent first-order cascade sections, not a four-layer lattice.
// Assumes a monophonic piano note and positive feedback-loop polarity.
// The single-string fit does not identify arbitrary bridge phase or three-string
// coupling; its four stable coefficients initialize the existing cascade.
// numDispFreqs is the output-buffer capacity on entry and actual count on return.
// Exports fitted-loop resonances in ascending Hz, including the fundamental,
// up to capacity/Nyquist (not merely the target peaks used to fit the model).
// On failure the output count is zero. Reset it to capacity before another call.
inline void EstimationDelayK(float* target, int numSamples, float* dispFreqs, int& numDispFreqs)
{
    using namespace EstimationDelayKDetail;
    const int capacity = numDispFreqs;
    numDispFreqs = 0;
    const auto fail = [](const char* reason)
        {
            std::printf("EstimationDelayK: %s; no reliable estimate.\n", reason);
        };
    if (capacity < 0 || (capacity > 0 && !dispFreqs))
    {
        fail("invalid dispersion frequency output buffer/capacity");
        return;
    }
    if (!target || numSamples < 1024)
    {
        fail("null target or fewer than 1024 samples");
        return;
    }
    double peak = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        if (!std::isfinite(target[i]))
        {
            fail("target contains NaN/Inf");
            return;
        }
        peak = (std::max)(peak, std::abs(static_cast<double>(target[i])));
    }
    if (peak <= 1e-12)
    {
        fail("silent target");
        return;
    }
    int onset = 0;
    while (onset < numSamples && std::abs(target[onset]) < peak * 0.01) ++onset;
    const int attackSamples = static_cast<int>(std::ceil(AttackSkipSeconds * SampleRate));
    if (numSamples - onset < attackSamples || numSamples - onset - attackSamples < 1024)
    {
        fail("too little sustain audio after removing the configured attack");
        return;
    }
    // Never fall back to including the attack when the recording is too short.
    // Both pitch detection and partial extraction below use this same segment.
    const int offset = onset + attackSamples;
    const int count = (std::min)(numSamples - offset, MaxAnalysisSamples);
    if (count < 1024)
    {
        fail("too little audio after onset");
        return;
    }
    std::printf("Analysis: onset=%.3f s, attack removed=%.3f s, sustain=[%.3f, %.3f) s.\n",
        onset / SampleRate, attackSamples / SampleRate, offset / SampleRate, (offset + count) / SampleRate);
    std::vector<double> samples(count), energy(count + 1, 0.0);
    double mean = 0.0;
    for (int i = 0; i < count; ++i) mean += target[offset + i] / peak;
    mean /= count;
    for (int i = 0; i < count; ++i)
    {
        samples[i] = target[offset + i] / peak - mean;
        energy[i + 1] = energy[i] + samples[i] * samples[i];
    }
    if (energy.back() <= 1e-12)
    {
        fail("target is constant after DC removal");
        return;
    }

    int n = 1;
    while (n < 2 * count) n *= 2;
    std::vector<std::complex<double>> spectrum(n);
    for (int i = 0; i < count; ++i) spectrum[i] = samples[i];
    FFT(spectrum, false);
    for (auto& value : spectrum) value = std::norm(value);
    FFT(spectrum, true); // Zero-padded linear autocorrelation.

    // YIN-style cumulative normalized squared differences. Normalize overlap
    // first so long lags cannot win solely because their overlap is shorter.
    // Leave one neighboring lag for interpolation at both configured bounds.
    const int minLag = (std::max)(2, static_cast<int>(SampleRate / MaxPitchHz) - 1);
    const int maxLag = (std::min)(count / 4, static_cast<int>(std::ceil(SampleRate / MinPitchHz)) + 1);
    if (maxLag <= minLag + 2)
    {
        fail("analysis segment too short for pitch detection");
        return;
    }
    std::vector<double> difference(maxLag + 1, 1.0);
    double sum = 0.0;
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        const double squared = energy[count - lag] + energy[count] - energy[lag]
            - 2.0 * spectrum[lag].real();
        const double d = (std::max)(0.0, squared) / (count - lag);
        sum += d;
        difference[lag] = sum > 1e-20 ? d * lag / sum : 1.0;
    }
    int lag = -1;
    for (int i = minLag; i < maxLag; ++i)
    {
        if (difference[i] < 0.15)
        {
            while (i + 1 <= maxLag && difference[i + 1] < difference[i]) ++i;
            lag = i;
            break;
        }
    }
    if (lag < 0)
    {
        lag = minLag;
        for (int i = minLag + 1; i < maxLag; ++i)
            if (difference[i] < difference[lag]) lag = i;
    }
    if (lag <= minLag || lag >= maxLag || difference[lag] > 0.4)
    {
        fail("no confident periodicity in configured pitch range");
        return;
    }
    const double curvature = difference[lag - 1] - 2.0 * difference[lag] + difference[lag + 1];
    const double shift = curvature > 1e-12
        ? (std::clamp)(0.5 * (difference[lag - 1] - difference[lag + 1]) / curvature, -0.5, 0.5)
        : 0.0;
    const double pitch = SampleRate / (lag + shift);
    if (pitch < MinPitchHz || pitch > MaxPitchHz)
    {
        fail("detected pitch outside configured range");
        return;
    }

    std::fill(spectrum.begin(), spectrum.end(), std::complex<double>{});
    for (int i = 0; i < count; ++i)
        spectrum[i] = samples[i] * (0.5 - 0.5 * std::cos(2.0 * Pi * i / count));
    FFT(spectrum, false);
    std::vector<double> power(n / 2 + 1);
    double peakPower = 0.0;
    for (int i = 1; i < n / 2; ++i)
    {
        power[i] = std::norm(spectrum[i]);
        peakPower = (std::max)(peakPower, power[i]);
    }
    const double binHz = SampleRate / n;
    const auto findPeak = [&](double center, double radius) -> double
        {
            const int first = (std::max)(2, static_cast<int>(std::ceil((center - radius) / binHz)));
            const int last = (std::min)(n / 2 - 2, static_cast<int>(std::floor((center + radius) / binHz)));
            int best = -1;
            for (int i = first; i <= last; ++i)
                if (power[i] > peakPower * 1e-6 && power[i] >= power[i - 1] && power[i] > power[i + 1]
                    && (best < 0 || power[i] > power[best])) best = i;
            if (best < 0) return 0.0;
            const double a = std::log((std::max)(power[best - 1], 1e-300));
            const double b = std::log((std::max)(power[best], 1e-300));
            const double c = std::log((std::max)(power[best + 1], 1e-300));
            const double denominator = a - 2.0 * b + c;
            const double delta = denominator < -1e-12
                ? (std::clamp)(0.5 * (a - c) / denominator, -0.5, 0.5) : 0.0;
            return (best + delta) * binHz;
        };
    const auto peakConfidence = [&](double frequency, double fundamental) -> double
        {
            const int bin = static_cast<int>(std::floor(frequency / binHz + 0.5));
            if (bin < 1 || bin >= n / 2) return 0.0;
            // Compare with local background, not the strongest low-frequency
            // partial. Exclude the Hann main lobe from the noise estimate.
            const int exclusion = static_cast<int>(std::ceil(2.0 * n / count));
            const int radius = (std::max)(exclusion + 4,
                static_cast<int>(0.15 * fundamental / binHz));
            std::vector<double> background;
            for (int k = (std::max)(1, bin - radius); k <= (std::min)(n / 2 - 1, bin + radius); ++k)
                if (std::abs(k - bin) > exclusion) background.push_back(power[k]);
            if (background.empty()) return 0.0;
            const double noise = (std::max)(Median(background), peakPower * 1e-12);
            return power[bin] / (power[bin] + 16.0 * noise);
        };
    const double fundamental = findPeak(pitch, 0.2 * pitch);
    if (fundamental <= 0.0 || peakConfidence(fundamental, fundamental) < MinPeakConfidence)
    {
        fail("fundamental peak missing; octave/partial numbering is ambiguous");
        return;
    }

    // Low-partial pairs predict stretched high-partial locations directly:
    // (f_m/m)^2=A+C*m^2, B=C/A. Algebra + medians, no fitted objective.
    std::vector<Partial> partials{ {1, fundamental, peakConfidence(fundamental, fundamental)} };
    for (int m = 2; m <= 8 && m * fundamental < MaxPartialHz; ++m)
    {
        const double f = findPeak(m * fundamental, 0.38 * fundamental);
        if (f <= 0.0) continue;
        const double confidence = peakConfidence(f, fundamental);
        if (confidence >= MinPeakConfidence) partials.push_back({ m, f, confidence });
    }
    std::vector<double> stiffness;
    for (std::size_t i = 0; i < partials.size(); ++i)
        for (std::size_t j = i + 1; j < partials.size(); ++j)
        {
            const double a = partials[i].frequency / partials[i].number;
            const double b = partials[j].frequency / partials[j].number;
            const double x = partials[i].number * partials[i].number;
            const double y = partials[j].number * partials[j].number;
            const double denominator = y * a * a - x * b * b;
            if (denominator <= 0.0) continue;
            const double B = (b * b - a * a) / denominator;
            if (B >= 0.0 && B <= 0.02) stiffness.push_back(B);
        }
    const double B = stiffness.empty() ? 0.0 : Median(stiffness);
    partials.resize(1);
    for (int m = 2; m <= MaxPartials; ++m)
    {
        const double center = m * fundamental * std::sqrt((1.0 + B * m * m) / (1.0 + B));
        const double previousCenter = (m - 1) * fundamental
            * std::sqrt((1.0 + B * (m - 1) * (m - 1)) / (1.0 + B));
        const double nextCenter = (m + 1) * fundamental
            * std::sqrt((1.0 + B * (m + 1) * (m + 1)) / (1.0 + B));
        // Wider spacing at high orders needs wider search bands. Neighboring
        // predicted bands stay disjoint, avoiding arbitrary fixed-Hz clipping.
        const double radius = 0.4 * (std::min)(center - previousCenter, nextCenter - center);
        if (center + radius >= MaxPartialHz) break;
        const double f = findPeak(center, radius);
        if (f <= partials.back().frequency) continue;
        const double confidence = peakConfidence(f, fundamental);
        if (confidence >= MinPeakConfidence) partials.push_back({ m, f, confidence });
    }
    if (partials.size() < 6)
    {
        fail("fewer than six reliable partials for four independent allpass coefficients");
        return;
    }

    // Enumerate every physically feasible integer D, not just the neighboring
    // roundings of a median estimate. Each D gets four independent k's and
    // asymmetric multi-start analytic Levenberg-Marquardt fits.
    const double lowFundamental = fundamental / std::exp2(FundamentalToleranceCents / 1200.0);
    // At Nyquist the available phase is pi*(D+OtherLoopDelay+Sections).
    const int minDelay = (std::max)(1, static_cast<int>(std::floor(
        2.0 * partials.back().number - OtherLoopDelay - Sections)) + 1);
    // Both signs of k are permitted. The smallest stable section delay gives
    // the largest feasible D; do not retain the old equal-negative-k bound.
    const double lowOmega = 2.0 * Pi * lowFundamental / SampleRate;
    const double minSectionPhase = 2.0 * std::atan(std::tan(lowOmega * 0.5) / MaxDelayRatio);
    const int maxDelay = static_cast<int>(std::floor(SampleRate / lowFundamental
        - OtherLoopDelay - Sections * minSectionPhase / lowOmega));
    if (maxDelay < minDelay)
    {
        fail("partial numbering incompatible with single-string cascade x4");
        return;
    }
    std::vector<IntegerFit> candidates;
    candidates.reserve(maxDelay - minDelay + 1);
    for (int candidateDelay = minDelay; candidateDelay <= maxDelay; ++candidateDelay)
    {
        const IntegerFit candidate = FitIntegerDelay(partials, candidateDelay);
        if (candidate.peaks.valid) candidates.push_back(candidate);
    }
    if (candidates.empty())
    {
        fail("no stable integer-delay/four-k combination satisfies the fundamental tolerance");
        return;
    }
    std::sort(candidates.begin(), candidates.end(), [](const IntegerFit& a, const IntegerFit& b)
        { return a.peaks.cost < b.peaks.cost; });
    const IntegerFit& best = candidates.front();
    const int delay = best.delay;
    const FourValues& r = best.r;
    const FourValues k = SortedK(r);
    const double fittedFundamental = PartialFrequency(1, delay, r, fundamental);
    const double fundamentalCents = 1200.0 * std::log2(fittedFundamental / fundamental);
    for (int mode = 1; numDispFreqs < capacity; ++mode)
    {
        const double frequency = PartialFrequency(mode, delay, r);
        if (!std::isfinite(frequency) || frequency <= 0.0 || frequency >= SampleRate * 0.5) break;
        const float exported = static_cast<float>(frequency);
        if (exported >= static_cast<float>(SampleRate * 0.5)) break;
        dispFreqs[numDispFreqs++] = exported;
    }
    std::printf("target waveguide delayline delay(sample):%d, disperse k[4]:{%.9f, %.9f, %.9f, %.9f}\n",
        delay, k[0], k[1], k[2], k[3]);
    std::printf("Single-string analytic fit: target f1=%.6f Hz, fitted f1=%.6f Hz (%+.3f cents).\n",
        fundamental, fittedFundamental, fundamentalCents);
    std::printf("Weighted peak error: MAE=%.3f cents, RMS=%.3f cents; %d reliable partials, B hint=%.8g.\n",
        best.peaks.meanAbsCents, best.peaks.rmsCents, static_cast<int>(partials.size()), B);
    std::printf("CascadeAPF VecToParams values atanh(k):{%.9f, %.9f, %.9f, %.9f}\n",
        std::atanh(k[0]), std::atanh(k[1]), std::atanh(k[2]), std::atanh(k[3]));
    std::printf("Model: 1/(1-g*z^-(D+extra)*A0(z)*A1(z)*A2(z)*A3(z)), Fs=48000, extra=%.1f.\n",
        OtherLoopDelay);
    std::printf("Enumerated D=%d..%d; %d feasible fits; f1 tolerance=+/-%.3f cents.\n",
        minDelay, maxDelay, static_cast<int>(candidates.size()), FundamentalToleranceCents);
    for (std::size_t i = 0; i < candidates.size() && i < 5; ++i)
    {
        const auto& candidate = candidates[i];
        const FourValues candidateK = SortedK(candidate.r);
        std::printf("Candidate D=%d, k={%.6f, %.6f, %.6f, %.6f}: RMS=%.3f cents%s\n", candidate.delay,
            candidateK[0], candidateK[1], candidateK[2], candidateK[3], candidate.peaks.rmsCents,
            i == 0 ? " [selected]" : "");
    }

    // Actual spectral maxima of the simplified loop, not a phase-error proxy.
    // A trained multi-string bridge can shift peaks relative to these predictions.
    std::printf("Partial   target(Hz)   predicted(Hz)   error(cents)   confidence\n");
    double highError = 0.0, highSquared = 0.0, highWeight = 0.0;
    int highPartials = 0;
    for (const auto& p : partials)
    {
        const double predicted = PartialFrequency(p.number, delay, r, p.frequency);
        const double cents = 1200.0 * std::log2(predicted / p.frequency);
        std::printf("%7d %12.5f %15.5f %+14.3f %12.3f\n",
            p.number, p.frequency, predicted, cents, p.confidence);
        if (p.number >= HighPartialStart)
        {
            const double weight = PartialWeight(p);
            highError += weight * cents;
            highSquared += weight * cents * cents;
            highWeight += weight;
            ++highPartials;
        }
    }
    if (highWeight > 0.0)
        std::printf("High partials >=%d: mean=%+.3f cents, RMS=%.3f cents (%d peaks).\n",
            HighPartialStart, highError / highWeight, std::sqrt(highSquared / highWeight), highPartials);
    else
        std::printf("Warning: no reliable high partials; dispersion is constrained only by low modes.\n");
    if (std::abs(fundamentalCents) > 0.95 * FundamentalToleranceCents)
        std::printf("Note: f1 tolerance is active; changing FundamentalToleranceCents changes the pitch/dispersion tradeoff.\n");
    if (best.peaks.rmsCents > 5.0)
        std::printf("Warning: peaks remain misaligned; inspect partial numbering/bridge phase or use more sections.\n");
    std::printf("This is a simplified-loop fit, not validation of the full trained piano/bridge.\n");
    std::printf("Exported %d estimated partial frequencies (Hz).\n", numDispFreqs);
}

// Preserve existing callers that only need printed delay/k estimates.
inline void EstimationDelayK(float* target, int numSamples)
{
    int count = 0;
    EstimationDelayK(target, numSamples, nullptr, count);
}
