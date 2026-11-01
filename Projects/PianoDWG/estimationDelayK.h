#ifndef ESTIMATION_DELAY_K_H
#define ESTIMATION_DELAY_K_H

// Standalone C++11 implementation.
// Default sample rate for the four-argument API. Override before including
// this header, e.g. #define ESTIMATION_DELAY_K_SAMPLE_RATE 48000.0
#ifndef ESTIMATION_DELAY_K_SAMPLE_RATE
#define ESTIMATION_DELAY_K_SAMPLE_RATE 48000.
#endif

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <vector>

#ifndef ESTIMATION_DELAY_K_PI
#define ESTIMATION_DELAY_K_PI 3.141592653589793238462643383279502884
#endif

// -----------------------------------------------------------------------------
// Four-stage all-pass lattice, equivalent to the recursive ProcessLattice
// implementation supplied by the user. The helper avoids requiring C++17
// if constexpr while preserving the exact state-update order.
// Reflection coefficients satisfy |k[i]| < 1.
// -----------------------------------------------------------------------------
template <int Layer, int NumLayers, typename Sample>
struct LatticeAPFProcess
{
    static inline Sample Run(Sample x, Sample* z, const Sample* k)
    {
        const Sample z1 = z[Layer];
        const Sample a = x - z1 * k[Layer];
        z[Layer] = LatticeAPFProcess<Layer + 1, NumLayers, Sample>::Run(a, z, k);
        return z1 + a * k[Layer];
    }
};

template <int NumLayers, typename Sample>
struct LatticeAPFProcess<NumLayers, NumLayers, Sample>
{
    static inline Sample Run(Sample x, Sample*, const Sample*) { return x; }
};

template <typename Sample, int NumLayers>
class LatticeAPF
{
private:
    Sample z[NumLayers];
    Sample k[NumLayers];

public:
    constexpr static int NumParams = NumLayers;

    LatticeAPF() { InitParams(0); }

    void InitParams(Sample* out) // k=0: pure NumLayers-sample delay
    {
        for (int i = 0; i < NumLayers; ++i) k[i] = static_cast<Sample>(0);
        Reset();
        if (out) ParamsToVec(out);
    }

    // Input/output vectors are unconstrained optimizer coordinates:
    // physical reflection coefficients are k[i] = tanh(in[i]).
    void VecToParams(Sample* in)
    {
        if (!in) return;
        for (int i = 0; i < NumLayers; ++i)
            k[i] = static_cast<Sample>(std::tanh(static_cast<double>(in[i])));
    }

    void ParamsToVec(Sample* out)
    {
        if (!out) return;
        for (int i = 0; i < NumLayers; ++i)
        {
            const double v = (std::max)(-0.999999, (std::min)(0.999999,
                static_cast<double>(k[i])));
            out[i] = static_cast<Sample>(std::atanh(v));
        }
    }

    void Reset()
    {
        for (int i = 0; i < NumLayers; ++i) z[i] = static_cast<Sample>(0);
    }

    inline Sample ProcessSample(Sample x)
    {
        return LatticeAPFProcess<0, NumLayers, Sample>::Run(x, z, k);
    }

    // Direct physical-coefficient setter, useful for deterministic model setup.
    void SetReflectionCoefficients(const Sample* values)
    {
        if (!values) return;
        for (int i = 0; i < NumLayers; ++i)
        {
            double v = static_cast<double>(values[i]);
            v = (std::max)(-0.999999, (std::min)(0.999999, v));
            k[i] = static_cast<Sample>(v);
        }
        Reset();
    }
};

namespace estimation_delay_k_detail
{
    static const double kPi = ESTIMATION_DELAY_K_PI;
    static const double kTwoPi = 2.0 * kPi;
    static const double kDefaultInitialB = 1.2e-4;
    // Number of measured peaks used to fit the piano F0/B guide model.
    static const int kPianoModelFitPartials = 20;
    // Maximum number of directly measured target partials used by the KS fit.
    // Increase this if the recording has sufficient SNR and bandwidth.
    static const int kMaxMeasuredPartials = 80;
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
            if (len > n / 2) break; // Avoid unsigned overflow in len <<= 1.
        }
    }

    inline double InterpolatedPeakFrequency(const std::vector<double>& mag,
        int bin,
        double sampleRate,
        int nfft)
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

    inline bool FindPeakInRange(const std::vector<double>& mag,
        double lowHz,
        double highHz,
        double sampleRate,
        int nfft,
        double& peakHz)
    {
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
        return IsFinite(peakHz);
    }

    inline double PianoFitCost(double a, double b,
        const std::vector<double>& observed,
        double f1)
    {
        double cost = 0.0;
        for (int n = 1; n <= kPianoModelFitPartials; ++n)
        {
            const double nn = static_cast<double>(n);
            const double s = std::sqrt(1.0 + b * 1.0e-4 * nn * nn);
            const double r = f1 * a * s - observed[static_cast<std::size_t>(n - 1)] / nn;
            const double x = r / kSoftL1Scale;
            cost += 2.0 * kSoftL1Scale * kSoftL1Scale * (std::sqrt(1.0 + x * x) - 1.0);
        }
        return cost;
    }

    // Robust two-parameter fit equivalent in objective to SciPy soft_l1,
    // with F0 = f1*a and B = b*1e-4.
    inline bool FitPianoF0B(const std::vector<double>& observed,
        double f1,
        double& F0,
        double& B)
    {
        if (observed.size() < static_cast<std::size_t>(kPianoModelFitPartials)) return false;
        double a = 1.0;
        double b = 1.2;
        double cost = PianoFitCost(a, b, observed, f1);

        for (int iter = 0; iter < 100; ++iter)
        {
            double h00 = 0.0, h01 = 0.0, h11 = 0.0;
            double g0 = 0.0, g1 = 0.0;
            for (int n = 1; n <= kPianoModelFitPartials; ++n)
            {
                const double nn = static_cast<double>(n);
                const double nn2 = nn * nn;
                const double s = std::sqrt(1.0 + b * 1.0e-4 * nn2);
                const double r = f1 * a * s - observed[static_cast<std::size_t>(n - 1)] / nn;
                const double x = r / kSoftL1Scale;
                const double weight = 1.0 / std::sqrt(1.0 + x * x); // rho'(r^2)
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
                    const double change = cost - trialCost;
                    a = trialA;
                    b = trialB;
                    cost = trialCost;
                    accepted = true;
                    if (std::fabs(scale * da) < 1.0e-11 && std::fabs(scale * db) < 1.0e-9)
                        iter = 100;
                    (void)change;
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

    // The user's recursive lattice has the all-pass transfer
    // H(z) = z^4 D(z^-1) / D(z), where D is generated by the Schur step-up
    // recursion D_n(z) = z D_(n-1)(z) + k_(4-n) D_(n-1)^*(z).
    // Coefficients below are stored in ascending powers of z.
    struct LatticePolynomial
    {
        double c[5];       // D(z) coefficients
        double dc[4][5];   // partial derivatives dD/dk[j]
    };

    inline void BuildLatticePolynomial(const double k[4], LatticePolynomial& p)
    {
        for (int i = 0; i < 5; ++i)
        {
            p.c[i] = 0.0;
            for (int j = 0; j < 4; ++j) p.dc[j][i] = 0.0;
        }
        p.c[0] = 1.0;
        int degree = 0;
        for (int step = 0; step < 4; ++step)
        {
            const int parameter = 3 - step;
            double next[5] = { 0.0, 0.0, 0.0, 0.0, 0.0 };
            double nextD[4][5] = { {0.0} };
            for (int i = 0; i <= degree + 1; ++i)
            {
                const double shifted = (i >= 1) ? p.c[i - 1] : 0.0;
                const double reversed = (i <= degree) ? p.c[degree - i] : 0.0;
                next[i] = shifted + k[parameter] * reversed;
                for (int j = 0; j < 4; ++j)
                {
                    const double shiftedD = (i >= 1) ? p.dc[j][i - 1] : 0.0;
                    const double reversedD = (i <= degree) ? p.dc[j][degree - i] : 0.0;
                    nextD[j][i] = shiftedD + k[parameter] * reversedD +
                        ((j == parameter) ? reversed : 0.0);
                }
            }
            for (int i = 0; i < 5; ++i)
            {
                p.c[i] = next[i];
                for (int j = 0; j < 4; ++j) p.dc[j][i] = nextD[j][i];
            }
            ++degree;
        }
    }

    // Continuous (unwrapped) phase lag of the lattice all-pass.
    // This recursion is algebraically equivalent to 2*arg(D(e^jw))-4*w,
    // but avoids phase wrapping from atan2(D).
    inline double LatticePhase(double omega, const double k[4])
    {
        double theta = 0.0; // continuous arg(D_n(e^jw))
        for (int n = 1; n <= 4; ++n)
        {
            const int parameter = 4 - n; // k4, k3, k2, k1 in the step-up recursion
            const double alpha = theta + (2.0 - n) * 0.5 * omega;
            const double kk = k[parameter];
            const double delta = std::atan2(-kk * std::sin(2.0 * alpha),
                1.0 + kk * std::cos(2.0 * alpha));
            theta += omega + delta;
        }
        return 2.0 * theta - 4.0 * omega;
    }

    inline void LatticePhaseDerivatives(double omega,
        const LatticePolynomial& p,
        double& dPhaseDw,
        double dPhaseDk[4])
    {
        const std::complex<double> z(std::cos(omega), std::sin(omega));
        std::complex<double> D(0.0, 0.0);
        std::complex<double> Dz(0.0, 0.0);
        std::complex<double> power(1.0, 0.0);
        for (int i = 0; i <= 4; ++i)
        {
            D += p.c[i] * power;
            if (i >= 1) Dz += static_cast<double>(i) * p.c[i] * power / z;
            power *= z;
        }
        if (std::abs(D) < 1.0e-24)
        {
            dPhaseDw = 0.0;
            for (int j = 0; j < 4; ++j) dPhaseDk[j] = 0.0;
            return;
        }
        dPhaseDw = 2.0 * std::real(z * Dz / D) - 4.0;
        for (int j = 0; j < 4; ++j)
        {
            std::complex<double> Dk(0.0, 0.0);
            power = std::complex<double>(1.0, 0.0);
            for (int i = 0; i <= 4; ++i)
            {
                Dk += p.dc[j][i] * power;
                power *= z;
            }
            dPhaseDk[j] = 2.0 * std::imag(Dk / D);
        }
    }

    // Solve m*w + Phi_lattice(w) = 2*pi*partial.
    inline bool SolvePartialOmega(int m,
        const double k[4],
        const LatticePolynomial& polynomial,
        int partial,
        double& omega)
    {
        double lo = 0.0;
        double hi = kPi - 1.0e-12;
        const double target = kTwoPi * static_cast<double>(partial);
        if (m * hi + LatticePhase(hi, k) < target) return false;

        double x = (std::min)(hi, target / (static_cast<double>(m) + 4.0));
        if (!(x > lo && x < hi)) x = 0.5 * (lo + hi);
        for (int iter = 0; iter < 48; ++iter)
        {
            const double fx = m * x + LatticePhase(x, k) - target;
            if (fx > 0.0) hi = x;
            else lo = x;
            if (std::fabs(fx) < 1.0e-13 || hi - lo < 1.0e-13)
            {
                omega = x;
                return true;
            }
            double dPhaseDw = 0.0;
            double dPhaseDk[4];
            LatticePhaseDerivatives(x, polynomial, dPhaseDw, dPhaseDk);
            const double derivative = static_cast<double>(m) + dPhaseDw;
            double next = x - fx / derivative;
            if (!(next > lo && next < hi) || !IsFinite(next) || !(derivative > 0.0))
                next = 0.5 * (lo + hi);
            x = next;
        }
        omega = 0.5 * (lo + hi);
        return true;
    }

    inline bool EvaluateLoopModel(int m,
        const double k[4],
        const std::vector<double>& targetHz,
        double sampleRate,
        std::vector<double>& residual,
        std::vector<double>* jacobian,
        double& cost)
    {
        const int count = static_cast<int>(targetHz.size());
        residual.resize(static_cast<std::size_t>(count));
        if (jacobian) jacobian->assign(static_cast<std::size_t>(count) * 4u, 0.0);
        cost = 0.0;
        const double hzScale = sampleRate / kTwoPi;
        LatticePolynomial polynomial;
        BuildLatticePolynomial(k, polynomial);

        for (int i = 0; i < count; ++i)
        {
            double omega = 0.0;
            if (!SolvePartialOmega(m, k, polynomial, i + 1, omega)) return false;
            const double predictedHz = hzScale * omega;
            const double r = predictedHz - targetHz[static_cast<std::size_t>(i)];
            if (!IsFinite(r)) return false;
            residual[static_cast<std::size_t>(i)] = r;
            cost += r * r;

            if (jacobian)
            {
                double dPhaseDw = 0.0;
                double dPhaseDk[4];
                LatticePhaseDerivatives(omega, polynomial, dPhaseDw, dPhaseDk);
                const double denominator = static_cast<double>(m) + dPhaseDw;
                if (!(denominator > 0.0)) return false;
                for (int j = 0; j < 4; ++j)
                {
                    const double dOmegaDk = -dPhaseDk[j] / denominator;
                    (*jacobian)[static_cast<std::size_t>(i) * 4u + static_cast<std::size_t>(j)] =
                        hzScale * dOmegaDk;
                }
            }
        }
        return IsFinite(cost);
    }

    inline bool SolveLinear4(double a[4][4], double b[4], double x[4])
    {
        double m[4][5];
        for (int r = 0; r < 4; ++r)
        {
            for (int c = 0; c < 4; ++c) m[r][c] = a[r][c];
            m[r][4] = b[r];
        }
        for (int c = 0; c < 4; ++c)
        {
            int pivot = c;
            for (int r = c + 1; r < 4; ++r)
                if (std::fabs(m[r][c]) > std::fabs(m[pivot][c])) pivot = r;
            if (std::fabs(m[pivot][c]) < 1.0e-24) return false;
            if (pivot != c)
                for (int j = c; j < 5; ++j) std::swap(m[c][j], m[pivot][j]);
            const double d = m[c][c];
            for (int j = c; j < 5; ++j) m[c][j] /= d;
            for (int r = 0; r < 4; ++r)
            {
                if (r == c) continue;
                const double factor = m[r][c];
                for (int j = c; j < 5; ++j) m[r][j] -= factor * m[c][j];
            }
        }
        for (int i = 0; i < 4; ++i) x[i] = m[i][4];
        return true;
    }

    inline bool OptimizeForM(int m,
        const std::vector<double>& targetHz,
        double sampleRate,
        const double initialK[4],
        double resultK[4],
        double& resultCost)
    {
        const double kMin = -0.9995;
        const double kMax = 0.9995;
        double k[4];
        for (int j = 0; j < 4; ++j)
            k[j] = (std::max)(kMin, (std::min)(kMax, initialK[j]));

        std::vector<double> residual;
        std::vector<double> jacobian;
        double cost = 0.0;
        if (!EvaluateLoopModel(m, k, targetHz, sampleRate, residual, &jacobian, cost))
            return false;

        double lambda = 1.0e-3;
        for (int iter = 0; iter < 120; ++iter)
        {
            double h[4][4] = { {0.0} };
            double g[4] = { 0.0, 0.0, 0.0, 0.0 };
            const int count = static_cast<int>(residual.size());
            for (int i = 0; i < count; ++i)
            {
                const double r = residual[static_cast<std::size_t>(i)];
                for (int a = 0; a < 4; ++a)
                {
                    const double ja = jacobian[static_cast<std::size_t>(i) * 4u + static_cast<std::size_t>(a)];
                    g[a] += ja * r;
                    for (int b = 0; b < 4; ++b)
                    {
                        const double jb = jacobian[static_cast<std::size_t>(i) * 4u + static_cast<std::size_t>(b)];
                        h[a][b] += ja * jb;
                    }
                }
            }

            double damped[4][4];
            double rhs[4];
            for (int a = 0; a < 4; ++a)
            {
                for (int b = 0; b < 4; ++b) damped[a][b] = h[a][b];
                damped[a][a] += lambda * ((std::max)(h[a][a], 1.0e-8));
                rhs[a] = -g[a];
            }
            double step[4];
            if (!SolveLinear4(damped, rhs, step))
            {
                lambda *= 10.0;
                if (lambda > 1.0e12) break;
                continue;
            }

            double trialK[4];
            double stepNorm = 0.0;
            for (int j = 0; j < 4; ++j)
            {
                trialK[j] = (std::max)(kMin, (std::min)(kMax, k[j] + step[j]));
                stepNorm += (trialK[j] - k[j]) * (trialK[j] - k[j]);
            }

            std::vector<double> trialResidual;
            double trialCost = 0.0;
            if (EvaluateLoopModel(m, trialK, targetHz, sampleRate, trialResidual, 0, trialCost) && trialCost < cost)
            {
                const double improvement = cost - trialCost;
                for (int j = 0; j < 4; ++j) k[j] = trialK[j];
                cost = trialCost;
                residual.swap(trialResidual);
                if (!EvaluateLoopModel(m, k, targetHz, sampleRate, residual, &jacobian, cost)) return false;
                lambda = (std::max)(1.0e-12, lambda * 0.3);
                if (stepNorm < 1.0e-18 || improvement < 1.0e-14) break;
            }
            else
            {
                lambda *= 10.0;
                if (lambda > 1.0e12) break;
            }
        }

        // Lattice reflection coefficients are order-dependent; do NOT sort them.
        if (!EvaluateLoopModel(m, k, targetHz, sampleRate, residual, 0, cost)) return false;
        for (int j = 0; j < 4; ++j) resultK[j] = k[j];
        resultCost = cost;
        return IsFinite(cost);
    }

    inline bool OptimizeDispersiveLoop(const std::vector<double>& frequencies,
        double sampleRate,
        int& bestM,
        double bestK[4],
        double& bestCost)
    {
        if (frequencies.size() < 4 || !(sampleRate > 0.0)) return false;
        std::vector<double> targetHz;
        targetHz.reserve(frequencies.size());
        for (std::size_t i = 0; i < frequencies.size(); ++i)
        {
            if (!IsFinite(frequencies[i]) || frequencies[i] <= 0.0 || frequencies[i] >= 0.5 * sampleRate)
                break;
            targetHz.push_back(frequencies[i]);
        }
        const int count = static_cast<int>(targetHz.size());
        if (count < 4) return false;

        const double omega1 = kTwoPi * targetHz.front() / sampleRate;
        const double omegaN = kTwoPi * targetHz.back() / sampleRate;
        if (!(omega1 > 0.0 && omegaN > 0.0 && omegaN < kPi)) return false;

        // A stable 4th-order all-pass has total unwrapped phase lag 4*pi
        // from DC to Nyquist. These bounds cover every possible integer m.
        int mMin = static_cast<int>(std::ceil((kTwoPi * count - 4.0 * kPi) / omegaN));
        int mMax = static_cast<int>(std::floor(kTwoPi / omega1));
        mMin = (std::max)(1, mMin);
        if (mMin > mMax) return false;

        // First seeds are direct Schur step-down conversions of four equal
        // cascade poles. This is a useful basin for piano-like dispersion and
        // avoids starting far from the highly nonuniform lattice coordinates.
        const double starts[24][4] = {
            { 0.06250000, -0.37647059,  0.77793904, -0.94693878}, // cascade k=-0.50
            { 0.12960000, -0.56240627,  0.88255019, -0.97248955}, // cascade k=-0.60
            { 0.24010000, -0.74252509,  0.94523066, -0.98695866}, // cascade k=-0.70
            { 0.31640625, -0.82041543,  0.96513074, -0.99159167}, // cascade k=-0.75
            { 0.40960000, -0.88591124,  0.97940972, -0.99497373}, // cascade k=-0.80
            { 0.52200625, -0.93700326,  0.98923860, -0.99734574}, // cascade k=-0.85
            { 0.65610000, -0.97279737,  0.99552413, -0.99888764}, // cascade k=-0.90
            {-0.90, -0.80, -0.70, -0.60},
            {-0.95, -0.85, -0.70, -0.45},
            {-0.85, -0.65, -0.45, -0.25},
            {-0.95, -0.90, -0.85, -0.80},
            {-0.60, -0.75, -0.90, -0.95},
            {-0.95, -0.60, -0.85, -0.40},
            {-0.40, -0.85, -0.60, -0.95},
            {-0.75, -0.25, -0.80, -0.35},
            { 0.00, -0.50, -0.80, -0.90},
            {-0.90,  0.00, -0.70, -0.30},
            { 0.50, -0.50, -0.80, -0.90},
            {-0.90, -0.80,  0.00, -0.50},
            { 0.20,  0.20,  0.20,  0.20},
            {-0.20, -0.20, -0.20, -0.20},
            { 0.70, -0.70,  0.70, -0.70},
            {-0.70,  0.70, -0.70,  0.70},
            { 0.40, -0.85,  0.97, -0.995}
        };

        bestCost = (std::numeric_limits<double>::infinity)();
        bestM = 0;
        for (int m = mMin; m <= mMax; ++m)
        {
            // Ensure the highest requested partial has a root below Nyquist.
            if (m + 4 < 2 * count) continue;
            for (int s = 0; s < 24; ++s)
            {
                double candidateK[4];
                double candidateCost = 0.0;
                if (OptimizeForM(m, targetHz, sampleRate, starts[s], candidateK, candidateCost) &&
                    candidateCost < bestCost)
                {
                    bestCost = candidateCost;
                    bestM = m;
                    for (int j = 0; j < 4; ++j) bestK[j] = candidateK[j];
                }
            }
        }
        return bestM > 0 && IsFinite(bestCost);
    }

    inline bool EstimatePianoPartials(const float* target,
        int numSamples,
        double sampleRate,
        std::vector<double>& measuredPartials,
        std::vector<double>& modelPartials,
        double& f1,
        double& F0,
        double& B)
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

        const int start = static_cast<int>((onsetTime + 0.150) * sampleRate);
        const int requestedLength = static_cast<int>(1.8 * sampleRate);
        const int end = (std::min)(numSamples, start + requestedLength);
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
        std::vector<std::complex<double> > spectrum(static_cast<std::size_t>(nfft),
            std::complex<double>(0.0, 0.0));
        for (int i = 0; i < segmentLength; ++i)
        {
            const double hann = (segmentLength <= 1) ? 1.0 :
                0.5 - 0.5 * std::cos(kTwoPi * static_cast<double>(i) / (segmentLength - 1));
            spectrum[static_cast<std::size_t>(i)] =
                std::complex<double>((segment[static_cast<std::size_t>(i)] - mean) * hann, 0.0);
        }
        FFT(spectrum);

        std::vector<double> magnitude(static_cast<std::size_t>(nfft / 2 + 1));
        for (int i = 0; i <= nfft / 2; ++i)
            magnitude[static_cast<std::size_t>(i)] = std::abs(spectrum[static_cast<std::size_t>(i)]);

        if (!FindPeakInRange(magnitude, 105.0, 165.0, sampleRate, nfft, f1)) return false;

        std::vector<double> observed;
        observed.reserve(kMaxMeasuredPartials);
        for (int i = 1; i <= kMaxMeasuredPartials; ++i)
        {
            const double ii = static_cast<double>(i);
            const double predicted = ii * f1 *
                std::sqrt((1.0 + kDefaultInitialB * ii * ii) / (1.0 + kDefaultInitialB));
            const double halfWidth = (std::max)(12.0, 0.01 * predicted);
            double peak = 0.0;
            if (predicted + halfWidth >= 0.5 * sampleRate) break;
            if (!FindPeakInRange(magnitude, predicted - halfWidth, predicted + halfWidth,
                sampleRate, nfft, peak))
                break;
            observed.push_back(peak);
        }

        if (observed.size() < static_cast<std::size_t>(kPianoModelFitPartials)) return false;
        std::vector<double> firstFit(observed.begin(),
            observed.begin() + kPianoModelFitPartials);
        if (!FitPianoF0B(firstFit, f1, F0, B)) return false;
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
} // namespace estimation_delay_k_detail

// -----------------------------------------------------------------------------
// Dispersive Karplus-Strong loop: z^-m followed by a 4th-order all-pass lattice in the feedback path.
// The excitation is added at the delay-line write point. Pluck() fills the
// delay line with a short noise-like excitation; ProcessSample() then generates
// the decaying string signal.
// -----------------------------------------------------------------------------
class DispersiveKarplusStrong
{
private:
    std::vector<float> delayLine_;
    std::size_t writeIndex_;
    LatticeAPF<float, 4> apf_;
    float feedback_;

public:
    DispersiveKarplusStrong() : writeIndex_(0), feedback_(0.996f) {}

    bool Init(int m, const float k[4], float feedback = 0.996f)
    {
        if (m <= 0 || !k) return false;
        delayLine_.assign(static_cast<std::size_t>(m), 0.0f);
        // Estimation returns physical reflection coefficients k. LatticeAPF::VecToParams
        // expects unconstrained coordinates v=atanh(k), then applies tanh(v).
        float params[4];
        for (int i = 0; i < 4; ++i)
        {
            const double physicalK = (std::max)(-0.999999, (std::min)(0.999999,
                static_cast<double>(k[i])));
            params[i] = static_cast<float>(std::atanh(physicalK));
        }
        apf_.InitParams(0);
        apf_.VecToParams(params);
        apf_.Reset();
        writeIndex_ = 0;
        if (feedback > 0.99999f) feedback = 0.99999f;
        if (feedback < -0.99999f) feedback = -0.99999f;
        feedback_ = feedback;
        return true;
    }

    void Reset()
    {
        std::fill(delayLine_.begin(), delayLine_.end(), 0.0f);
        writeIndex_ = 0;
        apf_.Reset();
    }

    void Pluck(float amplitude, unsigned int seed = 1u)
    {
        if (delayLine_.empty()) return;
        for (std::size_t i = 0; i < delayLine_.size(); ++i)
        {
            seed = 1664525u * seed + 1013904223u;
            const double unit = static_cast<double>(seed) / 4294967295.0;
            delayLine_[i] = amplitude * static_cast<float>(2.0 * unit - 1.0);
        }
        writeIndex_ = 0;
        apf_.Reset();
    }

    inline float ProcessSample(float excitation = 0.0f)
    {
        if (delayLine_.empty()) return excitation;
        const float delayed = delayLine_[writeIndex_];
        const float dispersed = apf_.ProcessSample(delayed);
        delayLine_[writeIndex_] = excitation + feedback_ * dispersed;
        ++writeIndex_;
        if (writeIndex_ >= delayLine_.size()) writeIndex_ = 0;
        return delayed;
    }

    int GetDelayLength() const { return static_cast<int>(delayLine_.size()); }
};

// Explicit-sample-rate implementation. dispFreqs is an output array with
// capacity numDispFreqs. Frequencies are F0/B model partial positions up to 5000 Hz;
// unused entries are filled with NaN. The function prints the optimized m,k1..k4.
inline void EstimationDelayK(float* target,
    int numSamples,
    float* dispFreqs,
    int numDispFreqs,
    float sampleRate,
    int* optimizedM,
    float* optimizedK)
{
    using namespace estimation_delay_k_detail;
    const float nanValue = (std::numeric_limits<float>::quiet_NaN)();
    if (optimizedM) *optimizedM = 0;
    if (optimizedK)
        for (int j = 0; j < 4; ++j) optimizedK[j] = nanValue;
    if (dispFreqs && numDispFreqs > 0)
        for (int i = 0; i < numDispFreqs; ++i) dispFreqs[i] = nanValue;

    if (!target || !dispFreqs || numSamples <= 0 || numDispFreqs <= 0 || !(sampleRate > 0.0f))
    {
        std::printf("[EstimationDelayK] invalid input.\n");
        return;
    }

    std::vector<double> measuredPartials;
    std::vector<double> estimatedPartials;
    double f1 = 0.0, F0 = 0.0, B = 0.0;
    if (!EstimatePianoPartials(target, numSamples, sampleRate,
        measuredPartials, estimatedPartials, f1, F0, B))
    {
        std::printf("[EstimationDelayK] piano partial estimation failed. Check sample rate, note range (C3), and audio length.\n");
        return;
    }

    const int outputCount = (std::min)(numDispFreqs, static_cast<int>(estimatedPartials.size()));
    for (int i = 0; i < outputCount; ++i)
        dispFreqs[i] = static_cast<float>(estimatedPartials[static_cast<std::size_t>(i)]);

    // Crucial: optimize against the directly measured target peaks, not the
    // F0/B extrapolated display curve. Up to kMaxMeasuredPartials are extracted
    // from the actual target spectrum; the F0/B model is used only for display
    // and for the initial search guide.
    const std::vector<double>& fitFrequencies = measuredPartials;

    int bestM = 0;
    double bestK[4] = { 0.0, 0.0, 0.0, 0.0 };
    double bestCost = 0.0;
    const bool optimized = OptimizeDispersiveLoop(fitFrequencies, sampleRate,
        bestM, bestK, bestCost);

    std::printf("[EstimationDelayK] sampleRate=%.3f Hz, firstPeak=%.6f Hz, F0=%.9f Hz, B=%.12g\n",
        static_cast<double>(sampleRate), f1, F0, B);
    if (optimized)
    {
        std::printf("[EstimationDelayK] m=%d, k1=%.12f, k2=%.12f, k3=%.12f, k4=%.12f\n",
            bestM, bestK[0], bestK[1], bestK[2], bestK[3]);
        if (optimizedM) *optimizedM = bestM;
        if (optimizedK)
            for (int j = 0; j < 4; ++j) optimizedK[j] = static_cast<float>(bestK[j]);
        std::vector<double> fitResidual;
        double checkedCost = 0.0;
        EvaluateLoopModel(bestM, bestK, fitFrequencies, sampleRate, fitResidual, 0, checkedCost);
        double maxAbsError = 0.0;
        std::printf("[EstimationDelayK] lattice APF4 absolute-frequency RMSE on %d directly measured partials = %.9f Hz\n",
            static_cast<int>(fitFrequencies.size()),
            std::sqrt(checkedCost / fitFrequencies.size()));
        for (std::size_t i = 0; i < fitResidual.size(); ++i)
            maxAbsError = (std::max)(maxAbsError, std::fabs(fitResidual[i]));
        std::printf("[EstimationDelayK] max absolute partial error = %.9f Hz\n", maxAbsError);
        for (std::size_t i = 0; i < fitResidual.size(); ++i)
            std::printf("  partial %02u: target=%10.5f Hz, KS-lattice=%10.5f Hz, error=%+9.5f Hz\n",
                static_cast<unsigned int>(i + 1), fitFrequencies[i],
                fitFrequencies[i] + fitResidual[i], fitResidual[i]);
    }
    else
    {
        std::printf("[EstimationDelayK] lattice APF4 optimization failed (need at least four valid measured partials).\n");
    }
}

// Five-argument convenience overload: explicit sample rate, print-only result.
inline void EstimationDelayK(float* target,
    int numSamples,
    float* dispFreqs,
    int numDispFreqs,
    float sampleRate)
{
    EstimationDelayK(target, numSamples, dispFreqs, numDispFreqs,
        sampleRate, 0, 0);
}

// Seven-argument overload: also returns optimized m and k[4] to the caller.
// This is useful when immediately initializing DispersiveKarplusStrong.
// Requested four-argument API. Uses ESTIMATION_DELAY_K_SAMPLE_RATE (44100 Hz
// unless overridden before including this header).
inline void EstimationDelayK(float* target,
    int numSamples,
    float* dispFreqs,
    int numDispFreqs)
{
    EstimationDelayK(target, numSamples, dispFreqs, numDispFreqs,
        static_cast<float>(ESTIMATION_DELAY_K_SAMPLE_RATE), 0, 0);
}

#endif // ESTIMATION_DELAY_K_H
