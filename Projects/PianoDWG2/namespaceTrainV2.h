#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>
#include <vector>

#include "PianoDWG2.h"

namespace TrainPianoDWG2
{
	class LossV2PoweredByAI
	{
	private:
		static constexpr int SR = 22050;
		static constexpr double PI = 3.141592653589793238462643383279502884;
		static constexpr int FFT_WORK_SIZE = 4096;

		struct Spectrum
		{
			int n = 0;
			int hop = 0;
			int frames = 0;
			int bins = 0;
			double* mag = nullptr;
			double largest = 0.0;
			double norm = 0.0;

			double at(int k, int t) const
			{
				return mag[std::size_t(k) * std::size_t(frames) + std::size_t(t)];
			}
		};

		struct Spec
		{
			int n = 0;
			int hop = 0;
			int frames = 0;
			int bins = 0;
			std::vector<double> mag;
			double largest = 0.0;
			double norm = 0.0;

			double at(int k, int t) const
			{
				return mag[std::size_t(k) * std::size_t(frames) + std::size_t(t)];
			}
		};

		struct AttackBand
		{
			int lo = 0;
			int hi = 0;
			double denominator = 1.0;
		};

		static bool checkedAdd(std::size_t& total, std::size_t value)
		{
			if (value > (std::numeric_limits<std::size_t>::max)() - total)
				return false;
			total += value;
			return true;
		}

		static bool checkedMul(std::size_t a, std::size_t b, std::size_t& result)
		{
			if (a != 0 && b > (std::numeric_limits<std::size_t>::max)() / a)
				return false;
			result = a * b;
			return true;
		}

		static int reflect(int i, int length)
		{
			while (i < 0 || i >= length)
			{
				if (i < 0)
					i = -i;
				if (i >= length)
					i = 2 * length - 2 - i;
			}
			return i;
		}

		static void fft(int n, double* re, double* im)
		{
			for (int i = 1, j = 0; i < n; ++i)
			{
				int bit = n >> 1;
				for (; j & bit; bit >>= 1)
					j ^= bit;
				j ^= bit;
				if (i < j)
				{
					std::swap(re[i], re[j]);
					std::swap(im[i], im[j]);
				}
			}

			for (int step = 2; step <= n; step <<= 1)
			{
				const int half = step >> 1;
				const double a = -2.0 * PI / step;
				const double baseR = std::cos(a);
				const double baseI = std::sin(a);
				for (int k = 0; k < n; k += step)
				{
					double wr = 1.0;
					double wi = 0.0;
					for (int j = 0; j < half; ++j)
					{
						const double tr = wr * re[k + j + half] - wi * im[k + j + half];
						const double ti = wr * im[k + j + half] + wi * re[k + j + half];
						re[k + j + half] = re[k + j] - tr;
						im[k + j + half] = im[k + j] - ti;
						re[k + j] += tr;
						im[k + j] += ti;

						const double nextR = wr * baseR - wi * baseI;
						wi = wr * baseI + wi * baseR;
						wr = nextR;
					}
				}
			}
		}

		template<class SpectrumType>
		static void resizeSpec(int length, int n, int hop, SpectrumType& out)
		{
			const std::size_t frames = std::size_t(length / hop + 1);
			const std::size_t bins = std::size_t(n / 2 + 1);
			out.mag.resize(frames * bins);
		}

		template<class SpectrumType>
		static void makeSpec(const float* x, int length, int n, int hop,
			const double* window, double* fftRe, double* fftIm,
			SpectrumType& out)
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
					fftRe[j] = double(x[reflect(offset + j, length)]) * window[j];
					fftIm[j] = 0.0;
				}

				fft(n, fftRe, fftIm);
				for (int k = 0; k < out.bins; ++k)
				{
					const double v = std::sqrt(
						fftRe[k] * fftRe[k] + fftIm[k] * fftIm[k] + 1.0e-30);
					out.mag[std::size_t(k) * std::size_t(out.frames) + std::size_t(t)] = v;
					out.largest = std::max(out.largest, v);
					out.norm += v * v;
				}
			}
			out.norm = std::sqrt(out.norm);
		}

		template<class SpectrumType>
		static double harmonicAt(const SpectrumType& s, int center, int t)
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

		static int rmsFrames(int end, int window)
		{
			return 1 + (end - window) / std::max(1, window / 4);
		}

		static void makeRms(const float* x, int end, int window, double* out)
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

		static void detrend(const double* env, int frames, double* out,
			const double* x, double denominator)
		{
			const int len = frames - 6;
			double maxv = 0.0;
			for (int t = 3; t < frames - 3; ++t)
				maxv = std::max(maxv, env[t]);

			double mean = 0.0;
			double slope = 0.0;
			for (int t = 0; t < len; ++t)
			{
				const double v = std::log(env[t + 3] + maxv * 0.005 + 1.0e-8);
				out[t] = v;
				mean += v;
				slope += x[t] * v;
			}
			mean /= len;
			slope /= denominator;
			for (int t = 0; t < len; ++t)
				out[t] -= mean + slope * x[t];
		}

		static void modulations(const double* residual, int len, int count,
			const double* modCos, const double* modSin, double* out)
		{
			for (int h = 0; h < count; ++h)
			{
				const std::size_t base = std::size_t(h) * std::size_t(len);
				double re = 0.0;
				double im = 0.0;
				for (int t = 0; t < len; ++t)
				{
					const double v = residual[t];
					re += v * modCos[base + std::size_t(t)];
					im += v * modSin[base + std::size_t(t)];
				}
				out[h] = std::sqrt(re * re + im * im + 1.0e-30) / len;
			}
		}

		class MonoLossState
		{
		public:
			void Init(const float* target, int numSamples, const float* overtoneFreqs,
				int numOvertones, const std::array<std::vector<double>, 7>& windows,
				double* fftRe, double* fftIm)
			{
				Reset();
				if (!target || !overtoneFreqs || numOvertones <= 0 || numSamples < 7168)
					return;

				numSamples_ = numSamples;
				end_ = std::min(numSamples_, int(0.12 * SR));
				parts_ = numOvertones;
				frames_ = numSamples_ / 1024 + 1;
				T_ = frames_ - 6;
				modN_ = std::min(19, T_ / 2);

				prepareTarget(target, overtoneFreqs, windows, fftRe, fftIm);
				if (!buildScratchSize())
					Reset();
			}

			float loss(const float* y, const std::array<std::vector<double>, 7>& windows,
				double* memory) const
			{
				if (!y || numSamples_ == 0 || !memory)
					return 0.0f;

				// All parameter-dependent work buffers come from this heap workspace.
				// No vector resize/allocation and no large automatic arrays are used here.
				double* next = memory;
				double* main4096Memory = next;
				next += targetMain_[2].mag.size();
				double* tempMemory = next;
				next += tempCapacity_;
				double* env = next;
				next += std::size_t(parts_) * std::size_t(frames_);
				double* residual = next;
				next += std::size_t(parts_) * std::size_t(T_);
				double* mod = next;
				next += std::size_t(parts_) * std::size_t(modN_);
				double* rms22 = next;
				next += refRms22_.size();
				double* rms88 = next;
				next += refRms88_.size();
				double* fftRe = next;
				next += FFT_WORK_SIZE;
				double* fftIm = next;

				Spectrum main4096;
				main4096.mag = main4096Memory;
				Spectrum temp;
				temp.mag = tempMemory;

				makeSpec(y, numSamples_, 4096, 1024, windows[6].data(),
					fftRe, fftIm, main4096);

				const int mainN[3] = { 256, 1024, 4096 };
				double sc = 0.0;
				double logMagnitude = 0.0;
				for (int ix = 0; ix < 3; ++ix)
				{
					Spectrum* current = &main4096;
					if (ix < 2)
					{
						current = &temp;
					makeSpec(y, numSamples_, mainN[ix], mainN[ix] / 4,
						windows[ix == 0 ? 2 : 4].data(), fftRe, fftIm, *current);
					}

					const Spec& target = targetMain_[ix];
					const std::vector<double>& targetLog = targetMainLog_[ix];
					double delta = 0.0;
					double logError = 0.0;
					const double eps = mainLogEps_[ix];
					for (std::size_t i = 0; i < target.mag.size(); ++i)
					{
						const double a = current->mag[i];
						const double d = a - target.mag[i];
						delta += d * d;
						logError += std::fabs(std::log(a + eps) - targetLog[i]);
					}
					sc += std::sqrt(delta) / (target.norm + 1.0e-8) / 3.0;
					logMagnitude += logError / double(target.mag.size()) / 3.0;
				}

				double harmonicEnvelope = 0.0;
				double ripple = 0.0;
				double modulation = 0.0;
				for (int k = 0; k < parts_; ++k)
				{
					double envError = 0.0;
					double* e = env + std::size_t(k) * std::size_t(frames_);
					const std::size_t envBase = std::size_t(k) * std::size_t(frames_);
					for (int t = 0; t < frames_; ++t)
					{
						e[t] = harmonicAt(main4096, partialBins_[k], t);
						envError += std::fabs(std::log(e[t] + floors_[k]) - refEnvLog_[envBase + std::size_t(t)]);
					}
					harmonicEnvelope += weights_[k] * envError / frames_;

					double* res = residual + std::size_t(k) * std::size_t(T_);
					detrend(e, frames_, res, detrendX_.data(), detrendDenominator_);
					double rippleError = 0.0;
					const std::size_t rippleBase = std::size_t(k) * std::size_t(T_);
					for (int t = 0; t < T_; ++t)
						rippleError += std::fabs(res[t] - refRipple_[rippleBase + std::size_t(t)]);
					ripple += weights_[k] * rippleError / T_;

					double* m = mod + std::size_t(k) * std::size_t(modN_);
					modulations(res, T_, modN_, modCos_.data(), modSin_.data(), m);
					double modulationError = 0.0;
					const std::size_t modBase = std::size_t(k) * std::size_t(modN_);
					for (int h = 0; h < modN_; ++h)
						modulationError += std::fabs(m[h] - refMod_[modBase + std::size_t(h)]);
					modulation += weights_[k] * modulationError / modN_;
				}

				const int shortN[3] = { 64, 128, 512 };
				double attackBandSC = 0.0;
				for (int ix = 0; ix < 3; ++ix)
				{
					makeSpec(y, end_, shortN[ix], shortN[ix] / 4,
						windows[ix == 0 ? 0 : (ix == 1 ? 1 : 3)].data(),
						fftRe, fftIm, temp);

					const Spec& target = targetShort_[ix];
					for (int band = 0; band < 3; ++band)
					{
						const AttackBand& info = attackBands_[ix][band];
						if (info.lo >= info.hi)
							continue;

						double errorEnergy = 0.0;
						for (int k = info.lo; k < info.hi; ++k)
						{
							for (int t = 0; t < temp.frames; ++t)
							{
								const double d = temp.at(k, t) - target.at(k, t);
								errorEnergy += d * d;
							}
						}
						attackBandSC += std::sqrt(errorEnergy) / info.denominator;
					}
				}
				if (attackBandCount_ > 0)
					attackBandSC /= attackBandCount_;

				makeRms(y, end_, 22, rms22);
				makeRms(y, end_, 88, rms88);
				double shortRms = 0.0;
				double positiveSlope = 0.0;
				for (int wi = 0; wi < 2; ++wi)
				{
					const std::vector<double>& ref = wi == 0 ? refRms22_ : refRms88_;
					const std::vector<double>& refUp = wi == 0 ? refRms22Up_ : refRms88Up_;
					const double* cur = wi == 0 ? rms22 : rms88;
					const int count = int(ref.size());
					double error = 0.0;
					double up = 0.0;
					double prev = 0.0;
					for (int t = 0; t < count; ++t)
					{
						const double x = cur[t];
						error += std::fabs(x - ref[std::size_t(t)]);
						if (t)
						{
							up += std::fabs(std::max(0.0, x - prev) - refUp[std::size_t(t - 1)]);
						}
						prev = x;
					}
					shortRms += 0.5 * (error / count) / refRmsMean_[wi];
					positiveSlope += 0.5 * up / refRmsDown_[wi];
				}

				makeSpec(y, end_, 256, 32, windows[2].data(), fftRe, fftIm, temp);
				double fluxDiff = 0.0;
				double fluxSum = 0.0;
				std::size_t fluxIndex = 0;
				for (int k = 7; k < 94; ++k)
				{
					for (int t = 1; t < temp.frames; ++t)
					{
						const double x = std::max(0.0, temp.at(k, t) - temp.at(k, t - 1));
						fluxDiff += std::fabs(x - refFlux_[fluxIndex]);
						fluxSum += std::fabs(refFlux_[fluxIndex]);
						++fluxIndex;
					}
				}
				const double highBandFlux = fluxDiff / (fluxSum + 1.0e-8);

				double totalEnergy = 0.0;
				for (int i = 0; i < end_; ++i)
					totalEnergy += double(y[i]) * y[i];
				double cumulative = 0.0;
				double energyTiming = 0.0;
				for (int i = 0; i < end_; ++i)
				{
					cumulative += double(y[i]) * y[i];
					energyTiming += std::fabs(cumulative / (totalEnergy + 1.0e-8) - refEnergy_[std::size_t(i)]);
				}
				energyTiming /= end_;

				const double total = sc + 0.1 * logMagnitude + 0.2 * harmonicEnvelope +
					0.25 * ripple + 0.3 * modulation + 0.6 * attackBandSC +
					0.4 * shortRms + 0.25 * highBandFlux +
					0.2 * positiveSlope + 0.1 * energyTiming;
				return float(total);
			}

			std::size_t scratchDoubles() const
			{
				return scratchDoubles_;
			}

		private:
			std::array<Spec, 3> targetMain_;
			std::array<Spec, 3> targetShort_;
			std::array<std::vector<double>, 3> targetMainLog_;
			std::array<double, 3> mainLogEps_{};
			std::array<std::array<AttackBand, 3>, 3> attackBands_{};

			std::vector<int> partialBins_;
			std::vector<double> refEnvLog_;
			std::vector<double> floors_;
			std::vector<double> weights_;
			std::vector<double> refRipple_;
			std::vector<double> refMod_;
			std::vector<double> refRms22_;
			std::vector<double> refRms88_;
			std::vector<double> refRms22Up_;
			std::vector<double> refRms88Up_;
			std::vector<double> refFlux_;
			std::vector<double> refEnergy_;

			std::vector<double> detrendX_;
			std::vector<double> modCos_;
			std::vector<double> modSin_;

			int numSamples_ = 0;
			int end_ = 0;
			int parts_ = 0;
			int frames_ = 0;
			int T_ = 0;
			int modN_ = 0;
			int attackBandCount_ = 0;
			double detrendDenominator_ = 1.0;
			std::array<double, 2> refRmsMean_{};
			std::array<double, 2> refRmsDown_{};
			std::size_t tempCapacity_ = 0;
			std::size_t scratchDoubles_ = 0;

			void Reset()
			{
				numSamples_ = 0;
				end_ = 0;
				parts_ = 0;
				frames_ = 0;
				T_ = 0;
				modN_ = 0;
				attackBandCount_ = 0;
				detrendDenominator_ = 1.0;
				refRmsMean_ = {};
				refRmsDown_ = {};
				tempCapacity_ = 0;
				scratchDoubles_ = 0;

				for (Spec& s : targetMain_)
				{
					s.mag.clear();
					s.n = s.hop = s.frames = s.bins = 0;
					s.largest = s.norm = 0.0;
				}
				for (Spec& s : targetShort_)
				{
					s.mag.clear();
					s.n = s.hop = s.frames = s.bins = 0;
					s.largest = s.norm = 0.0;
				}
				for (auto& v : targetMainLog_) v.clear();
				partialBins_.clear();
				refEnvLog_.clear();
				floors_.clear();
				weights_.clear();
				refRipple_.clear();
				refMod_.clear();
				refRms22_.clear();
				refRms88_.clear();
				refRms22Up_.clear();
				refRms88Up_.clear();
				refFlux_.clear();
				refEnergy_.clear();
				detrendX_.clear();
				modCos_.clear();
				modSin_.clear();
				mainLogEps_ = {};
				attackBands_ = {};
			}

			void prepareTarget(const float* target, const float* overtoneFreqs,
				const std::array<std::vector<double>, 7>& windows,
				double* fftRe, double* fftIm)
			{
				const int mainN[3] = { 256, 1024, 4096 };
				const int shortN[3] = { 64, 128, 512 };

				for (int i = 0; i < 3; ++i)
				{
					resizeSpec(numSamples_, mainN[i], mainN[i] / 4, targetMain_[i]);
					const int windowIndex = i == 0 ? 2 : (i == 1 ? 4 : 6);
					makeSpec(target, numSamples_, mainN[i], mainN[i] / 4,
						windows[windowIndex].data(), fftRe, fftIm, targetMain_[i]);

					mainLogEps_[i] = targetMain_[i].largest * 1.0e-4;
					targetMainLog_[i].resize(targetMain_[i].mag.size());
					for (std::size_t j = 0; j < targetMain_[i].mag.size(); ++j)
						targetMainLog_[i][j] = std::log(targetMain_[i].mag[j] + mainLogEps_[i]);
				}

				for (int i = 0; i < 3; ++i)
				{
					resizeSpec(end_, shortN[i], shortN[i] / 4, targetShort_[i]);
					const int windowIndex = i == 0 ? 0 : (i == 1 ? 1 : 3);
					makeSpec(target, end_, shortN[i], shortN[i] / 4,
						windows[windowIndex].data(), fftRe, fftIm, targetShort_[i]);
				}

				prepareAttackBands();
				prepareDetrendAndModulation();
				prepareHarmonicReferences(overtoneFreqs);
				prepareRmsReferences(target);
				prepareFluxReference(target, windows, fftRe, fftIm);
				prepareEnergyReference(target);

				// The one reusable temporary spectrum must fit every non-4096 spectrum.
				tempCapacity_ = 0;
				for (int i = 0; i < 2; ++i)
					tempCapacity_ = std::max(tempCapacity_, targetMain_[i].mag.size());
				for (int i = 0; i < 3; ++i)
					tempCapacity_ = std::max(tempCapacity_, targetShort_[i].mag.size());
				const std::size_t fluxSize = std::size_t(end_ / 32 + 1) * std::size_t(129);
				tempCapacity_ = std::max(tempCapacity_, fluxSize);
			}

			void prepareAttackBands()
			{
				const int fmin[3] = { 0, 600, 2500 };
				const int fmax[3] = { 600, 2500, 8000 };
				attackBandCount_ = 0;

				for (int ix = 0; ix < 3; ++ix)
				{
					const Spec& target = targetShort_[ix];
					double totalEnergy = 0.0;
					for (double v : target.mag)
						totalEnergy += v * v;

					for (int band = 0; band < 3; ++band)
					{
						AttackBand& info = attackBands_[ix][band];
						info.lo = int(std::ceil(double(fmin[band]) * target.n / SR));
						info.hi = std::min(target.bins,
							int(std::ceil(double(fmax[band]) * target.n / SR)));
						if (info.lo >= info.hi)
							continue;

						double bandEnergy = 0.0;
						for (int k = info.lo; k < info.hi; ++k)
						{
							for (int t = 0; t < target.frames; ++t)
							{
								const double v = target.at(k, t);
								bandEnergy += v * v;
							}
						}
						info.denominator = std::sqrt(bandEnergy) +
							0.03 * std::sqrt(totalEnergy) + 1.0e-8;
						++attackBandCount_;
					}
				}
			}

			void prepareDetrendAndModulation()
			{
				detrendX_.resize(std::size_t(T_));
				detrendDenominator_ = 0.0;
				for (int t = 0; t < T_; ++t)
				{
					const double x = -1.0 + 2.0 * double(t) / double(T_ - 1);
					detrendX_[std::size_t(t)] = x;
					detrendDenominator_ += x * x;
				}

				const std::size_t modulationSize = std::size_t(modN_) * std::size_t(T_);
				modCos_.resize(modulationSize);
				modSin_.resize(modulationSize);
				for (int h = 0; h < modN_; ++h)
				{
					const std::size_t base = std::size_t(h) * std::size_t(T_);
					for (int t = 0; t < T_; ++t)
					{
						const double window = 0.5 - 0.5 * std::cos(2.0 * PI * t / T_);
						const double a = 2.0 * PI * t * (h + 1) / T_;
						modCos_[base + std::size_t(t)] = window * std::cos(a);
						modSin_[base + std::size_t(t)] = -window * std::sin(a);
					}
				}
			}

			void prepareHarmonicReferences(const float* overtoneFreqs)
			{
				const std::size_t envSize = std::size_t(parts_) * std::size_t(frames_);
				std::vector<double> targetEnv(envSize);
				partialBins_.resize(std::size_t(parts_));
				refEnvLog_.resize(envSize);
				floors_.resize(std::size_t(parts_));
				weights_.resize(std::size_t(parts_));

				for (int k = 0; k < parts_; ++k)
				{
					partialBins_[std::size_t(k)] = std::clamp(int(std::nearbyint(
						double(overtoneFreqs[k]) * 4096.0 / SR)), 0, 2048);

					double maxv = 0.0;
					double power = 0.0;
					for (int t = 0; t < frames_; ++t)
					{
						const double v = harmonicAt(targetMain_[2], partialBins_[std::size_t(k)], t);
						targetEnv[std::size_t(k) * std::size_t(frames_) + std::size_t(t)] = v;
						maxv = std::max(maxv, v);
						power += v * v / frames_;
					}
					floors_[std::size_t(k)] = maxv * 0.01 + 1.0e-8;
					weights_[std::size_t(k)] = std::sqrt(std::sqrt(power));
				}

				double weightSum = 0.0;
				for (double w : weights_)
					weightSum += w;
				if (weightSum > 0.0)
				{
					for (double& w : weights_)
						w /= weightSum;
				}
				else
				{
					const double uniformWeight = 1.0 / double(parts_);
					for (double& w : weights_)
						w = uniformWeight;
				}

				for (int k = 0; k < parts_; ++k)
				{
					const std::size_t base = std::size_t(k) * std::size_t(frames_);
					for (int t = 0; t < frames_; ++t)
					{
						refEnvLog_[base + std::size_t(t)] = std::log(
							targetEnv[base + std::size_t(t)] + floors_[std::size_t(k)]);
					}
				}

				refRipple_.resize(std::size_t(parts_) * std::size_t(T_));
				for (int k = 0; k < parts_; ++k)
				{
					detrend(targetEnv.data() + std::size_t(k) * std::size_t(frames_),
						frames_, refRipple_.data() + std::size_t(k) * std::size_t(T_),
						detrendX_.data(), detrendDenominator_);
				}

				refMod_.resize(std::size_t(parts_) * std::size_t(modN_));
				for (int k = 0; k < parts_; ++k)
				{
					modulations(refRipple_.data() + std::size_t(k) * std::size_t(T_),
						T_, modN_, modCos_.data(), modSin_.data(),
						refMod_.data() + std::size_t(k) * std::size_t(modN_));
				}
			}

			void prepareRmsReferences(const float* target)
			{
				refRms22_.resize(std::size_t(rmsFrames(end_, 22)));
				refRms88_.resize(std::size_t(rmsFrames(end_, 88)));
				makeRms(target, end_, 22, refRms22_.data());
				makeRms(target, end_, 88, refRms88_.data());
				prepareOneRmsReference(refRms22_, refRms22Up_, 0);
				prepareOneRmsReference(refRms88_, refRms88Up_, 1);
			}

			void prepareOneRmsReference(const std::vector<double>& ref,
				std::vector<double>& refUp, int index)
			{
				const int count = int(ref.size());
				refUp.resize(count > 0 ? std::size_t(count - 1) : 0);
				double sum = 0.0;
				double down = 0.0;
				for (int t = 0; t < count; ++t)
				{
					sum += ref[std::size_t(t)];
					if (t)
					{
						const double up = std::max(0.0, ref[std::size_t(t)] - ref[std::size_t(t - 1)]);
						refUp[std::size_t(t - 1)] = up;
						down += up;
					}
				}
				refRmsMean_[index] = sum / count + 1.0e-8;
				refRmsDown_[index] = down + 1.0e-8;
			}

			void prepareFluxReference(const float* target,
				const std::array<std::vector<double>, 7>& windows,
				double* fftRe, double* fftIm)
			{
				Spec targetFlux;
				resizeSpec(end_, 256, 32, targetFlux);
				makeSpec(target, end_, 256, 32, windows[2].data(), fftRe, fftIm, targetFlux);

				const std::size_t count = std::size_t(94 - 7) * std::size_t(targetFlux.frames - 1);
				refFlux_.resize(count);
				std::size_t index = 0;
				for (int k = 7; k < 94; ++k)
				{
					for (int t = 1; t < targetFlux.frames; ++t)
					{
						refFlux_[index++] = std::max(0.0,
							targetFlux.at(k, t) - targetFlux.at(k, t - 1));
					}
				}
			}

			void prepareEnergyReference(const float* target)
			{
				refEnergy_.resize(std::size_t(end_));
				double targetEnergy = 0.0;
				for (int i = 0; i < end_; ++i)
				{
					targetEnergy += double(target[i]) * target[i];
					refEnergy_[std::size_t(i)] = targetEnergy;
				}
				for (double& v : refEnergy_)
					v /= targetEnergy + 1.0e-8;
			}

			bool buildScratchSize()
			{
				std::size_t total = 0;
				std::size_t envCount = 0;
				std::size_t residualCount = 0;
				std::size_t modCount = 0;
				if (!checkedMul(std::size_t(parts_), std::size_t(frames_), envCount) ||
					!checkedMul(std::size_t(parts_), std::size_t(T_), residualCount) ||
					!checkedMul(std::size_t(parts_), std::size_t(modN_), modCount))
					return false;

				if (!checkedAdd(total, targetMain_[2].mag.size()) ||
					!checkedAdd(total, tempCapacity_) ||
					!checkedAdd(total, envCount) ||
					!checkedAdd(total, residualCount) ||
					!checkedAdd(total, modCount) ||
					!checkedAdd(total, refRms22_.size()) ||
					!checkedAdd(total, refRms88_.size()) ||
					!checkedAdd(total, std::size_t(2) * FFT_WORK_SIZE))
					return false;

				if (total > (std::numeric_limits<std::size_t>::max)() / sizeof(double))
					return false;
				scratchDoubles_ = total;
				return true;
			}
		};

	public:
		void InitTrainState(float* targetL, float* targetR, int numSamples,
			float* overtoneFreqs, int numOvertones)
		{
			InitWindows();

			// Setup-only FFT workspace. It is heap-backed and is not part of autodiff.
			std::vector<double> fftWork(std::size_t(2) * FFT_WORK_SIZE);
			double* fftRe = fftWork.data();
			double* fftIm = fftRe + FFT_WORK_SIZE;

			left_.Init(targetL, numSamples, overtoneFreqs, numOvertones,
				windows_, fftRe, fftIm);
			right_.Init(targetR, numSamples, overtoneFreqs, numOvertones,
				windows_, fftRe, fftIm);
		}

		float loss(const float* renderL, const float* renderR) const
		{
			const std::size_t scratchCount = std::max(left_.scratchDoubles(), right_.scratchDoubles());
			if (scratchCount == 0)
				return 0.0f;
			if (scratchCount > (std::numeric_limits<std::size_t>::max)() / sizeof(double))
				return std::numeric_limits<float>::infinity();

			// One heap workspace is reused sequentially for both channels.
			double* workspace = static_cast<double*>(std::malloc(scratchCount * sizeof(double)));
			if (!workspace)
				return std::numeric_limits<float>::infinity();

			const float left = left_.loss(renderL, windows_, workspace);
			const float right = right_.loss(renderR, windows_, workspace);
			std::free(workspace);
			return (left + right) * 0.5f;
		}

	private:
		void InitWindows()
		{
			for (int power = 6; power <= 12; ++power)
			{
				const int n = 1 << power;
				auto& window = windows_[std::size_t(power - 6)];
				window.resize(std::size_t(n));
				for (int i = 0; i < n; ++i)
					window[std::size_t(i)] = 0.5 - 0.5 * std::cos(2.0 * PI * i / n);
			}
		}

		std::array<std::vector<double>, 7> windows_;
		MonoLossState left_;
		MonoLossState right_;
	};

	class TrainSetup
	{
	public:
		void Setup(float* targetL, float* targetR, int numSamples,
			float* overtoneFreqs, int numOvertones)
		{
			losseval_.InitTrainState(targetL, targetR, numSamples, overtoneFreqs, numOvertones);
			numSamples_ = std::max(0, numSamples);
		}

	private:
		friend class TrainPianoDWG2;
		LossV2PoweredByAI losseval_;
		int numSamples_ = 0;
	};

	class TrainPianoDWG2
	{
	public:
		explicit TrainPianoDWG2(const TrainSetup& setup) : setup_(setup) {}

		float loss(float* params, int numParams)
		{
			if (!params || numParams != Voice::NumParams || setup_.numSamples_ == 0)
				return 0.0f;

			void* storage = std::malloc(sizeof(Voice));
			auto* audio = static_cast<float*>(
				std::malloc(2 * std::size_t(setup_.numSamples_) * sizeof(float)));
			if (!storage || !audio)
			{
				std::free(storage);
				std::free(audio);
				return std::numeric_limits<float>::infinity();
			}

			auto* voice = ::new (storage) Voice;
			voice->Reset();
			voice->VecToParams(params);
			voice->NoteOn(1.0f);
			float* right = audio + setup_.numSamples_;
			voice->ProcessBlock(audio, right, setup_.numSamples_);
			const float value = setup_.losseval_.loss(audio, right);
			voice->~Voice();
			std::free(audio);
			std::free(storage);
			return value;
		}

	private:
		using Voice = PianoDWG2::PianoDWG2Voice<float>;
		const TrainSetup& setup_;
	};
}
