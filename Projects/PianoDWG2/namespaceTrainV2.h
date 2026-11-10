#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>
#include "PianoDWG2.h"

namespace TrainPianoDWG2
{
	class MonoLossState
	{
	public:
		void Init(float* target, int numSamples, float* overtoneFreqs, int numOvertones)
		{
			numSamples_ = 0;
			if (!target || !overtoneFreqs || numOvertones <= 0 || numSamples < 7168)
				return;

			re_.resize(4096);
			im_.resize(4096);
			prepareTarget(target, numSamples, overtoneFreqs, numOvertones);
			// The initialized state is copied to seed Enzyme's shadow.
			// Discard the final target FFT so its scratch starts with zero adjoints.
			std::fill(re_.begin(), re_.end(), 0.0);
			std::fill(im_.begin(), im_.end(), 0.0);
			env_.resize(refEnv_.size());
			residual_.resize(refRipple_.size());
			mod_.resize(refMod_.size());
			rms22_.resize(refRms22_.size());
			rms88_.resize(refRms88_.size());
			for (int i = 0; i < 3; ++i)
			{
				yMain_[i].mag.resize(targetMain_[i].mag.size());
				yShort_[i].mag.resize(targetShort_[i].mag.size());
			}
			yFlux_.mag.resize(targetFlux_.mag.size());
			numSamples_ = numSamples;
		}

		float loss(const float* y)
		{
			if (!y || numSamples_ == 0)
				return 0.0f;

			constexpr int mainN[3] = { 256, 1024, 4096 };
			constexpr int shortN[3] = { 64, 128, 512 };
			const int numSamples = numSamples_;
			const int end = std::min(numSamples, int(0.12 * SR));
			const int parts = static_cast<int>(partialBins_.size());
			const int frames = targetMain_[2].frames;
			const int T = frames - 6;
			const int modN = std::min(19, T / 2);
			for (int i = 0; i < 3; ++i)
				makeSpec(y, numSamples, mainN[i], mainN[i] / 4, yMain_[i]);
			double sc = 0.0, logMagnitude = 0.0;
			for (int ix = 0; ix < 3; ++ix)
			{
				const Spec& a = yMain_[ix];
				const Spec& r = targetMain_[ix];
				double delta = 0.0, logError = 0.0;
				const double eps = r.largest * 1.0e-4;
				for (std::size_t i = 0; i < a.mag.size(); ++i)
				{
					const double d = a.mag[i] - r.mag[i];
					delta += d * d;
					logError += std::fabs(std::log(a.mag[i] + eps) - std::log(r.mag[i] + eps));
				}
				sc += std::sqrt(delta) / (r.norm + 1.0e-8) / 3.0;
				logMagnitude += logError / double(a.mag.size()) / 3.0;
			}
			double harmonicEnvelope = 0.0, ripple = 0.0, modulation = 0.0;
			for (int k = 0; k < parts; ++k)
			{
				double envError = 0.0;
				double* e = env_.data() + std::size_t(k) * frames;
				for (int t = 0; t < frames; ++t)
				{
					e[t] = harmonicAt(yMain_[2], partialBins_[k], t);
					envError += std::fabs(std::log(e[t] + floors_[k]) -
						std::log(refEnv_[std::size_t(k) * frames + t] + floors_[k]));
				}
				harmonicEnvelope += weights_[k] * envError / frames;
				double* res = residual_.data() + std::size_t(k) * T;
				detrend(e, frames, res);
				double rippleError = 0.0;
				for (int t = 0; t < T; ++t)
					rippleError += std::fabs(res[t] - refRipple_[std::size_t(k) * T + t]);
				ripple += weights_[k] * rippleError / T;
				double* m = mod_.data() + std::size_t(k) * modN;
				modulations(res, T, modN, m);
				double modulationError = 0.0;
				for (int h = 0; h < modN; ++h)
					modulationError += std::fabs(m[h] - refMod_[std::size_t(k) * modN + h]);
				modulation += weights_[k] * modulationError / modN;
			}
			for (int i = 0; i < 3; ++i)
				makeSpec(y, end, shortN[i], shortN[i] / 4, yShort_[i]);
			double attackBandSC = 0.0;
			int attackBandCount = 0;
			for (int ix = 0; ix < 3; ++ix)
			{
				const Spec& a = yShort_[ix];
				const Spec& r = targetShort_[ix];
				for (int band = 0; band < 3; ++band)
				{
					const int fmin = band == 0 ? 0 : (band == 1 ? 600 : 2500);
					const int fmax = band == 0 ? 600 : (band == 1 ? 2500 : 8000);
					const int lo = int(std::ceil(double(fmin) * a.n / SR));
					const int hi = std::min(a.bins, int(std::ceil(double(fmax) * a.n / SR)));
					if (lo >= hi) continue;
					double ea = 0.0, eb = 0.0, et = 0.0;
					for (int k = lo; k < hi; ++k)
					{
						for (int t = 0; t < a.frames; ++t)
						{
							const double av = a.at(k, t);
							const double rv = r.at(k, t);
							ea += (av - rv) * (av - rv);
							eb += rv * rv;
						}
					}
					for (double v : r.mag) et += v * v;
					attackBandSC += std::sqrt(ea) /
						(std::sqrt(eb) + 0.03 * std::sqrt(et) + 1.0e-8);
					++attackBandCount;
				}
			}
			attackBandSC /= attackBandCount;
			makeRms(y, end, 22, rms22_);
			makeRms(y, end, 88, rms88_);
			double shortRms = 0.0, positiveSlope = 0.0;
			for (int wi = 0; wi < 2; ++wi)
			{
				const std::vector<double>& ref = wi == 0 ? refRms22_ : refRms88_;
				const std::vector<double>& cur = wi == 0 ? rms22_ : rms88_;
				const int count = int(cur.size());
				double err = 0.0, den = 0.0, up = 0.0, down = 0.0, prev = 0.0;
				for (int t = 0; t < count; ++t)
				{
					const double x = cur[t];
					err += std::fabs(x - ref[t]);
					den += ref[t];
					if (t)
					{
						up += std::fabs(std::max(0.0, x - prev) -
							std::max(0.0, ref[t] - ref[t - 1]));
						down += std::max(0.0, ref[t] - ref[t - 1]);
					}
					prev = x;
				}
				shortRms += 0.5 * (err / count) / (den / count + 1.0e-8);
				positiveSlope += 0.5 * up / (down + 1.0e-8);
			}
			makeSpec(y, end, 256, 32, yFlux_);
			double fluxDiff = 0.0, fluxSum = 0.0;
			std::size_t fluxIndex = 0;
			for (int k = 7; k < 94; ++k)
			{
				for (int t = 1; t < yFlux_.frames; ++t)
				{
					const double x = std::max(0.0, yFlux_.at(k, t) - yFlux_.at(k, t - 1));
					fluxDiff += std::fabs(x - refFlux_[fluxIndex]);
					fluxSum += std::fabs(refFlux_[fluxIndex]);
					++fluxIndex;
				}
			}
			const double highBandFlux = fluxDiff / (fluxSum + 1.0e-8);
			double totalEnergy = 0.0;
			for (int i = 0; i < end; ++i)
				totalEnergy += double(y[i]) * y[i];
			double cumulative = 0.0, energyTiming = 0.0;
			for (int i = 0; i < end; ++i)
			{
				cumulative += double(y[i]) * y[i];
				energyTiming += std::fabs(cumulative / (totalEnergy + 1.0e-8) - refEnergy_[i]);
			}
			energyTiming /= end;
			const double total = sc + 0.1 * logMagnitude + 0.2 * harmonicEnvelope +
				0.25 * ripple + 0.3 * modulation + 0.6 * attackBandSC +
				0.4 * shortRms + 0.25 * highBandFlux +
				0.2 * positiveSlope + 0.1 * energyTiming;
			return float(total);
		}
	private:
		static constexpr int SR = 22050;
		static constexpr double PI = 3.141592653589793238462643383279502884;
		struct Spec
		{
			int n = 0, hop = 0, frames = 0, bins = 0;
			std::vector<double> mag;
			double largest = 0.0, norm = 0.0;
			double at(int k, int t) const
			{
				return mag[std::size_t(k) * frames + t];
			}
		};
		std::array<Spec, 3> targetMain_, targetShort_;
		std::array<Spec, 3> yMain_, yShort_;
		Spec targetFlux_, yFlux_;
		std::vector<double> re_, im_;
		std::vector<int> partialBins_;
		std::vector<double> refEnv_, env_, floors_, weights_;
		std::vector<double> refRipple_, residual_, refMod_, mod_;
		std::vector<double> refRms22_, refRms88_, rms22_, rms88_;
		std::vector<double> refFlux_, refEnergy_;
		int numSamples_ = 0;
		void prepareTarget(float* target, int numSamples, float* overtoneFreqs, int numOvertones)
		{
			constexpr int mainN[3] = { 256, 1024, 4096 };
			constexpr int shortN[3] = { 64, 128, 512 };
			const int end = std::min(numSamples, int(0.12 * SR));
			const int parts = numOvertones;
			for (int i = 0; i < 3; ++i)
			{
				resizeSpec(numSamples, mainN[i], mainN[i] / 4, targetMain_[i]);
				makeSpec(target, numSamples, mainN[i], mainN[i] / 4, targetMain_[i]);
			}
			for (int i = 0; i < 3; ++i)
			{
				resizeSpec(end, shortN[i], shortN[i] / 4, targetShort_[i]);
				makeSpec(target, end, shortN[i], shortN[i] / 4, targetShort_[i]);
			}
			resizeSpec(end, 256, 32, targetFlux_);
			makeSpec(target, end, 256, 32, targetFlux_);
			const int frames = targetMain_[2].frames;
			const int T = frames - 6;
			const int modN = std::min(19, T / 2);
			partialBins_.resize(parts);
			for (int k = 0; k < parts; ++k)
				partialBins_[k] = std::clamp(int(std::nearbyint(
					double(overtoneFreqs[k]) * 4096.0 / SR)), 0, 2048);
			refEnv_.resize(std::size_t(parts) * frames);
			floors_.resize(parts);
			weights_.resize(parts);
			for (int k = 0; k < parts; ++k)
			{
				double maxv = 0.0, power = 0.0;
				for (int t = 0; t < frames; ++t)
				{
					const double v = harmonicAt(targetMain_[2], partialBins_[k], t);
					refEnv_[std::size_t(k) * frames + t] = v;
					maxv = std::max(maxv, v);
					power += v * v / frames;
				}
				floors_[k] = maxv * 0.01 + 1.0e-8;
				weights_[k] = std::sqrt(std::sqrt(power));
			}
			double weightSum = 0.0;
			for (double w : weights_) weightSum += w;
			for (double& w : weights_) w /= weightSum;
			refRipple_.resize(std::size_t(parts) * T);
			for (int k = 0; k < parts; ++k)
				detrend(refEnv_.data() + std::size_t(k) * frames,
					frames, refRipple_.data() + std::size_t(k) * T);
			refMod_.resize(std::size_t(parts) * modN);
			for (int k = 0; k < parts; ++k)
				modulations(refRipple_.data() + std::size_t(k) * T,
					T, modN, refMod_.data() + std::size_t(k) * modN);
			refRms22_.resize(rmsFrames(end, 22));
			refRms88_.resize(rmsFrames(end, 88));
			makeRms(target, end, 22, refRms22_);
			makeRms(target, end, 88, refRms88_);
			refFlux_.clear();
			for (int k = 7; k < 94; ++k)
			{
				for (int t = 1; t < targetFlux_.frames; ++t)
					refFlux_.push_back(std::max(0.0,
						targetFlux_.at(k, t) - targetFlux_.at(k, t - 1)));
			}
			refEnergy_.resize(end);
			double targetEnergy = 0.0;
			for (int i = 0; i < end; ++i)
			{
				targetEnergy += double(target[i]) * target[i];
				refEnergy_[i] = targetEnergy;
			}
			for (double& v : refEnergy_)
				v /= targetEnergy + 1.0e-8;
		}
		static int reflect(int i, int length)
		{
			while (i < 0 || i >= length)
			{
				if (i < 0) i = -i;
				if (i >= length) i = 2 * length - 2 - i;
			}
			return i;
		}
		void fft(int n)
		{
			for (int i = 1, j = 0; i < n; ++i)
			{
				int bit = n >> 1;
				for (; j & bit; bit >>= 1) j ^= bit;
				j ^= bit;
				if (i < j)
				{
					std::swap(re_[i], re_[j]);
					std::swap(im_[i], im_[j]);
				}
			}
			for (int step = 2; step <= n; step <<= 1)
			{
				const int half = step >> 1;
				const double a = -2.0 * PI / step;
				const double baseR = std::cos(a), baseI = std::sin(a);
				for (int k = 0; k < n; k += step)
				{
					double wr = 1.0, wi = 0.0;
					for (int j = 0; j < half; ++j)
					{
						const double tr = wr * re_[k + j + half] - wi * im_[k + j + half];
						const double ti = wr * im_[k + j + half] + wi * re_[k + j + half];
						re_[k + j + half] = re_[k + j] - tr;
						im_[k + j + half] = im_[k + j] - ti;
						re_[k + j] += tr;
						im_[k + j] += ti;
						const double nextR = wr * baseR - wi * baseI;
						wi = wr * baseI + wi * baseR;
						wr = nextR;
					}
				}
			}
		}
		static void resizeSpec(int length, int n, int hop, Spec& out)
		{
			out.mag.resize(std::size_t(length / hop + 1) * (n / 2 + 1));
		}

		void makeSpec(const float* x, int length, int n, int hop, Spec& out)
		{
			out.n = n;
			out.hop = hop;
			out.frames = length / hop + 1;
			out.bins = n / 2 + 1;
			out.largest = 0.0;
			out.norm = 0.0;
			for (int t = 0; t < out.frames; ++t)
			{
				const int offset = t * hop - n / 2;
				for (int j = 0; j < n; ++j)
				{
					re_[j] = double(x[reflect(offset + j, length)]) *
						(0.5 - 0.5 * std::cos(2.0 * PI * j / n));
					im_[j] = 0.0;
				}
				fft(n);
				for (int k = 0; k < out.bins; ++k)
				{
					const double v = std::sqrt(
						re_[k] * re_[k] + im_[k] * im_[k] + 1.0e-30);
					out.mag[std::size_t(k) * out.frames + t] = v;
					out.largest = std::max(out.largest, v);
					out.norm += v * v;
				}
			}
			out.norm = std::sqrt(out.norm);
		}
		static double harmonicAt(const Spec& s, int center, int t)
		{
			double v = 1.0e-12;
			for (int j = -2; j <= 2; ++j)
			{
				const int ix = std::clamp(center + j, 0, s.bins - 1);
				const double a = s.at(ix, t);
				v += a * a;
			}
			return std::sqrt(v);
		}
		static void detrend(const double* env, int frames, double* out)
		{
			const int len = frames - 6;
			double maxv = 0.0;
			for (int t = 3; t < frames - 3; ++t)
				maxv = std::max(maxv, env[t]);
			double mean = 0.0, slope = 0.0, denom = 0.0;
			for (int t = 0; t < len; ++t)
			{
				const double x = len > 1 ? (-1.0 + 2.0 * t / (len - 1)) : 0.0;
				const double v = std::log(env[t + 3] + maxv * 0.005 + 1.0e-8);
				out[t] = v;
				mean += v;
				slope += x * v;
				denom += x * x;
			}
			mean /= len;
			slope /= denom;
			for (int t = 0; t < len; ++t)
			{
				const double x = -1.0 + 2.0 * t / (len - 1);
				out[t] -= mean + slope * x;
			}
		}
		static void modulations(const double* residual, int len, int count, double* out)
		{
			for (int h = 1; h <= count; ++h)
			{
				double re = 0.0, im = 0.0;
				for (int t = 0; t < len; ++t)
				{
					const double x = residual[t] *
						(0.5 - 0.5 * std::cos(2.0 * PI * t / len));
					const double a = 2.0 * PI * t * h / len;
					re += x * std::cos(a);
					im -= x * std::sin(a);
				}
				out[h - 1] = std::sqrt(re * re + im * im + 1.0e-30) / len;
			}
		}
		static int rmsFrames(int end, int window)
		{
			return 1 + (end - window) / std::max(1, window / 4);
		}

		static void makeRms(const float* x, int end, int window, std::vector<double>& out)
		{
			const int stride = std::max(1, window / 4);
			const int count = rmsFrames(end, window);
			for (int t = 0; t < count; ++t)
			{
				double v = 1.0e-12;
				for (int j = 0; j < window; ++j)
				{
					const double z = x[t * stride + j];
					v += z * z / window;
				}
				out[t] = std::sqrt(v);
			}
		}
	};

	class LossPoweredByAI
	{
	public:
		void InitTrainState(float* targetL, float* targetR, int numSamples,
			float* overtoneFreqs, int numOvertones)
		{
			left_.Init(targetL, numSamples, overtoneFreqs, numOvertones);
			right_.Init(targetR, numSamples, overtoneFreqs, numOvertones);
		}

		float loss(const float* renderL, const float* renderR)
		{
			const float left = left_.loss(renderL);
			const float right = right_.loss(renderR);
			return (left + right) * 0.5f;
		}

	private:
		MonoLossState left_, right_;
	};

	class TrainPianoDWG2
	{
	private:
		std::vector<float> renderL, renderR;
		PianoDWG2::PianoDWG2Voice<float> voice;
		LossPoweredByAI losseval;
		int numSamples = 0;
	public:
		void InitTrainState(float* targetL, float* targetR, int numSamples, float* overtoneFreqs, int numOvertones)
		{
			losseval.InitTrainState(targetL, targetR, numSamples, overtoneFreqs, numOvertones);
			renderL.resize(numSamples);
			renderR.resize(numSamples);
			this->numSamples = numSamples;
		}
		void Reset() { voice.Reset(); }
		float loss(float* params, int numParams)
		{
			voice.VecToParams(params);
			voice.NoteOn(1.);
			voice.ProcessBlock(renderL.data(), renderR.data(), numSamples);
			return losseval.loss(renderL.data(), renderR.data());
		}
	};
}