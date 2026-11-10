#pragma once

// Include the standard types used by D2DCanvas before its inline implementation.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <condition_variable>
#include <cwchar>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>
#include "PianoDWG2.h"
#include "estimationDelayK.h"
#include "D2DCanvas.h"

class RenderHelper
{
public:
	static constexpr float SampleRate = PianoDWG2::SampleRate;

	bool Render(const float* values, int count, int numSamples,
		std::array<std::vector<float>, 2>& audio)
	{
		if (!values || count != PianoDWG2::PianoDWG2Voice<float>::NumParams || numSamples <= 0)
			return false;

		params_.assign(values, values + count);
		for (auto& channel : audio)
			channel.resize(numSamples);
		voice_.VecToParams(params_.data());
		voice_.Reset();
		voice_.NoteOn(1.0f);
		voice_.ProcessBlock(audio[0].data(), audio[1].data(), numSamples);
		return true;
	}

private:
	PianoDWG2::PianoDWG2Voice<float> voice_;
	std::vector<float> params_;
};

class TrainStateUI : public D2DCanvas
{
public:
	// Adjustable plot ranges (Hz and amplitude dB).
	static constexpr float FrequencyMinHz = 0.0f;
	static constexpr float FrequencyMaxHz = 11000.0f;
	static constexpr float MagnitudeMinDb = -200.0f;
	static constexpr float MagnitudeMaxDb = -15.0f;

	static_assert(FrequencyMinHz >= 0.0f && FrequencyMaxHz > FrequencyMinHz &&
		FrequencyMaxHz <= RenderHelper::SampleRate / 2.0f,
		"Frequency range must be increasing and within [0, sample rate / 2]");
	static_assert(MagnitudeMaxDb > MagnitudeMinDb,
		"Maximum dB must exceed minimum dB");

	TrainStateUI()
	{
		Microsoft::WRL::ComPtr<IDWriteFactory> factory;
		if (SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
			__uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(factory.GetAddressOf()))))
		{
			factory->CreateTextFormat(L"Microsoft YaHei", nullptr,
				DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
				DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"zh-cn",
				textFormat_.GetAddressOf());
		}
	}

	~TrainStateUI() override { Stop(); }

	// Copy estimated frequencies; the caller's buffer need not outlive this call.
	void UpdateTarget(const float* samples, int count, const float* dispFreqs = nullptr, int numDispFreqs = 0)
	{
		UpdateTarget(samples, nullptr, count, dispFreqs, numDispFreqs);
	}

	void UpdateTarget(const float* left, const float* right, int count,
		const float* dispFreqs = nullptr, int numDispFreqs = 0)
	{
		{
			std::lock_guard<std::mutex> lock(updateMutex_);
			if (!left || count <= 0)
			{
				targetSpectrum_ = {};
				dispersionFrequencies_.reset();
				numSamples_ = fftSize_ = 0;
				std::atomic_store(&display_, std::shared_ptr<const SpectrumFrame>{});
			}
			else
			{
				// Strictly greater than count, even for a power-of-two count.
				int n = 1;
				while (n <= count)
				{
					// Keep the padded size representable as an int.
					if (n > (std::numeric_limits<int>::max)() / 2)
						throw std::length_error("Spectrum FFT size exceeds safe int range");
					n *= 2;
				}
				auto spectrum = std::make_shared<StereoSpectrum>();
				(*spectrum)[0] = ComputeSpectrum(left, count, n);
				if (right)
					(*spectrum)[1] = ComputeSpectrum(right, count, n);
				auto frequencies = std::make_shared<std::vector<float>>();
				if (dispFreqs && numDispFreqs > 0)
				{
					frequencies->reserve(numDispFreqs);
					for (int i = 0; i < numDispFreqs; ++i)
					{
						const float hz = dispFreqs[i];
						if (std::isfinite(hz) && hz > 0.0f &&
							hz < RenderHelper::SampleRate * 0.5f)
							frequencies->push_back(hz);
					}
				}
				targetSpectrum_ = spectrum;
				dispersionFrequencies_ = frequencies;
				numSamples_ = count;
				fftSize_ = n;
				auto frame = std::make_shared<SpectrumFrame>();
				frame->target = targetSpectrum_;
				frame->dispersionFrequencies = dispersionFrequencies_;
				frame->fftSize = fftSize_;
				frame->numSamples = numSamples_;
				Publish(frame);
			}
			lastRepaintTime_ = {};
		}
		RequestRepaint();
	}

	void UpdateAndRepaint(float* values, int count)
	{
		{
			std::lock_guard<std::mutex> lock(updateMutex_);
			if (!targetSpectrum_)
				return;
			const auto now = std::chrono::steady_clock::now();
			if (now - lastRepaintTime_ < std::chrono::duration<double>(1.0 / 30.0))
				return;

			if (!renderer_.Render(values, count, numSamples_, audio_))
				return;

			auto frame = std::make_shared<SpectrumFrame>();
			frame->target = targetSpectrum_;
			frame->dispersionFrequencies = dispersionFrequencies_;
			for (std::size_t channel = 0; channel < audio_.size(); ++channel)
				frame->piano[channel] = ComputeSpectrum(audio_[channel].data(), numSamples_, fftSize_);
			frame->fftSize = fftSize_;
			frame->numSamples = numSamples_;
			Publish(frame);
			lastRepaintTime_ = std::chrono::steady_clock::now();
		}
		RequestRepaint();
	}

protected:
	void OnPaint(ID2D1RenderTarget& g) override
	{
		const auto frame = std::atomic_load(&display_);
		const auto size = g.GetSize();
		if (size.width < 200.0f || size.height < 290.0f) return;

		Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> black, red, grid, lightBlue;
		if (FAILED(g.CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f), black.GetAddressOf())) ||
			FAILED(g.CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.0f, 0.0f), red.GetAddressOf())) ||
			FAILED(g.CreateSolidColorBrush(D2D1::ColorF(0.86f, 0.86f, 0.86f), grid.GetAddressOf())) ||
			FAILED(g.CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.78f, 1.0f, 0.8f), lightBlue.GetAddressOf())))
			return;

		constexpr float legendLeft = 62.0f;
		g.DrawLine(D2D1::Point2F(legendLeft, 21.0f), D2D1::Point2F(legendLeft + 22.0f, 21.0f), black.Get(), 2.0f);
		DrawLabel(g, L"Target", D2D1::RectF(legendLeft + 28.0f, 10.0f, legendLeft + 95.0f, 34.0f), black.Get());
		g.DrawLine(D2D1::Point2F(legendLeft + 110.0f, 21.0f), D2D1::Point2F(legendLeft + 132.0f, 21.0f), red.Get(), 2.0f);
		DrawLabel(g, L"Piano waveguide", D2D1::RectF(legendLeft + 138.0f, 10.0f, legendLeft + 300.0f, 34.0f), red.Get());
		g.DrawLine(D2D1::Point2F(legendLeft + 320.0f, 13.0f), D2D1::Point2F(legendLeft + 320.0f, 29.0f), lightBlue.Get(), 1.5f);
		DrawLabel(g, L"Estimated partials", D2D1::RectF(legendLeft + 330.0f, 10.0f, legendLeft + 490.0f, 34.0f), black.Get());
		if (frame)
		{
			wchar_t info[128];
			std::swprintf(info, 128, L"Samples: %d   FFT N: %d   One-sided amplitude (0 dB = 1)",
				frame->numSamples, frame->fftSize);
			DrawLabel(g, info, D2D1::RectF(legendLeft, 35.0f, size.width, 60.0f), black.Get());
		}

		const float panelHeight = (size.height - 62.0f) * 0.5f;
		for (std::size_t channel = 0; channel < 2; ++channel)
		{
			const float top = 62.0f + static_cast<float>(channel) * panelHeight;
			const auto plot = D2D1::RectF(legendLeft, top + 22.0f,
				size.width - 24.0f, top + panelHeight - 48.0f);
			DrawPlot(g, frame.get(), channel, plot, black.Get(), red.Get(), grid.Get(), lightBlue.Get());
		}
	}

private:
	using StereoSpectrum = std::array<std::vector<float>, 2>;

	struct SpectrumFrame
	{
		std::shared_ptr<const StereoSpectrum> target;
		std::shared_ptr<const std::vector<float>> dispersionFrequencies;
		StereoSpectrum piano;
		int fftSize = 0;
		int numSamples = 0;
	};

	void DrawPlot(ID2D1RenderTarget& g, const SpectrumFrame* frame, std::size_t channel,
		const D2D1_RECT_F& plot, ID2D1SolidColorBrush* black, ID2D1SolidColorBrush* red,
		ID2D1SolidColorBrush* grid, ID2D1SolidColorBrush* lightBlue)
	{
		const float width = plot.right - plot.left;
		const float height = plot.bottom - plot.top;
		constexpr int tickIntervals = 6;
		for (int tick = 0; tick <= tickIntervals; ++tick)
		{
			const float fraction = static_cast<float>(tick) / tickIntervals;
			const float db = MagnitudeMaxDb - fraction * (MagnitudeMaxDb - MagnitudeMinDb);
			const float y = plot.top + fraction * height;
			g.DrawLine(D2D1::Point2F(plot.left, y), D2D1::Point2F(plot.right, y), grid);
			wchar_t label[32];
			std::swprintf(label, 32, L"%.4g", static_cast<double>(db));
			DrawLabel(g, label, D2D1::RectF(8.0f, y - 10.0f, 56.0f, y + 12.0f), black);
		}
		for (int tick = 0; tick <= tickIntervals; ++tick)
		{
			const float fraction = static_cast<float>(tick) / tickIntervals;
			const float hz = FrequencyMinHz + fraction * (FrequencyMaxHz - FrequencyMinHz);
			const float x = plot.left + fraction * width;
			g.DrawLine(D2D1::Point2F(x, plot.top), D2D1::Point2F(x, plot.bottom), grid);
			wchar_t label[32];
			std::swprintf(label, 32, L"%.6g", static_cast<double>(hz));
			DrawLabel(g, label, D2D1::RectF(x - 20.0f, plot.bottom + 7.0f, x + 40.0f, plot.bottom + 28.0f), black);
		}
		g.DrawRectangle(plot, black);
		DrawLabel(g, L"dB", D2D1::RectF(10.0f, plot.top - 22.0f, 55.0f, plot.top), black);
		DrawLabel(g, L"Hz", D2D1::RectF(plot.right - 18.0f, plot.bottom + 26.0f, plot.right + 24.0f, plot.bottom + 48.0f), black);
		const wchar_t* title = channel == 0 ? L"Left" : L"Right";
		if (frame && channel == 1 && (*frame->target)[channel].empty())
			title = L"Right (no target supplied)";
		DrawLabel(g, title,
			D2D1::RectF(plot.left, plot.top - 22.0f, plot.right, plot.top), black);
		if (!frame)
		{
			DrawLabel(g, L"Waiting for target...", plot, black);
			return;
		}

		g.PushAxisAlignedClip(plot, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
		// Model-estimated resonances behind both measured spectrum curves.
		if (frame->dispersionFrequencies)
		{
			for (float hz : *frame->dispersionFrequencies)
			{
				if (hz < FrequencyMinHz || hz > FrequencyMaxHz) continue;
				const float x = plot.left + (hz - FrequencyMinHz) /
					(FrequencyMaxHz - FrequencyMinHz) * width;
				g.DrawLine(D2D1::Point2F(x, plot.top), D2D1::Point2F(x, plot.bottom), lightBlue, 1.0f);
			}
		}
		// Complete target first, then overlay the complete model spectrum.
		DrawSpectrum(g, (*frame->target)[channel], frame->fftSize, plot, black);
		DrawSpectrum(g, frame->piano[channel], frame->fftSize, plot, red);
		g.PopAxisAlignedClip();
	}

	static std::vector<float> ComputeSpectrum(const float* samples, int count, int n)
	{
		std::vector<std::complex<double>> bins(n);
		std::copy(samples, samples + count, bins.begin());
		estimation_delay_k_detail::FFT(bins);
		std::vector<float> spectrum(n / 2 + 1);
		for (int k = 0; k <= n / 2; ++k)
		{
			// Original sample count: zero padding must not change amplitude.
			const double scale = (k == 0 || k == n / 2) ? 1.0 / count : 2.0 / count;
			const double amplitude = std::abs(bins[k]) * scale;
			spectrum[k] = std::isfinite(amplitude)
				? static_cast<float>(20.0 * std::log10((std::max)(amplitude, 1e-12)))
				: std::numeric_limits<float>::quiet_NaN();
		}
		return spectrum;
	}

	static void DrawSpectrum(ID2D1RenderTarget& g, const std::vector<float>& spectrum,
		int n, const D2D1_RECT_F& plot, ID2D1SolidColorBrush* brush)
	{
		if (spectrum.empty() || n <= 0) return;
		const float width = plot.right - plot.left;
		const float height = plot.bottom - plot.top;
		const int columns = (std::max)(2, static_cast<int>(std::ceil(width)));
		const double firstBin = static_cast<double>(FrequencyMinHz) * n /
			RenderHelper::SampleRate;
		const double lastBin = (std::min)(static_cast<double>(spectrum.size() - 1),
			static_cast<double>(FrequencyMaxHz) * n / RenderHelper::SampleRate);
		const double binSpan = lastBin - firstBin;
		D2D1_POINT_2F previous{};
		bool havePrevious = false;
		for (int column = 0; column < columns; ++column)
		{
			const int first = static_cast<int>(std::floor(firstBin + column * binSpan / columns));
			const int end = (std::min)(static_cast<int>(spectrum.size()) - 1,
				static_cast<int>(std::ceil(firstBin + (column + 1) * binSpan / columns)));
			float peak = -std::numeric_limits<float>::infinity();
			for (int k = first; k <= end; ++k)
				if (std::isfinite(spectrum[k])) peak = (std::max)(peak, spectrum[k]);
			if (!std::isfinite(peak))
			{
				havePrevious = false;
				continue;
			}
			peak = (std::clamp)(peak, MagnitudeMinDb, MagnitudeMaxDb);
			const auto point = D2D1::Point2F(plot.left + column * width / (columns - 1),
				plot.top + (MagnitudeMaxDb - peak) / (MagnitudeMaxDb - MagnitudeMinDb) * height);
			if (havePrevious) g.DrawLine(previous, point, brush, 1.3f);
			previous = point;
			havePrevious = true;
		}
	}

	void DrawLabel(ID2D1RenderTarget& g, const wchar_t* text, const D2D1_RECT_F& rect,
		ID2D1SolidColorBrush* brush)
	{
		if (textFormat_)
			g.DrawText(text, static_cast<UINT32>(std::wcslen(text)), textFormat_.Get(), rect, brush,
				D2D1_DRAW_TEXT_OPTIONS_CLIP);
	}

	void Publish(const std::shared_ptr<SpectrumFrame>& frame)
	{
		std::shared_ptr<const SpectrumFrame> immutable = frame;
		std::atomic_store(&display_, immutable);
	}

	void RequestRepaint()
	{
		if (const HWND window = GetHWND()) InvalidateRect(window, nullptr, FALSE);
	}

	Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat_;
	RenderHelper renderer_;
	std::mutex updateMutex_;
	std::array<std::vector<float>, 2> audio_;
	std::shared_ptr<const StereoSpectrum> targetSpectrum_;
	std::shared_ptr<const std::vector<float>> dispersionFrequencies_;
	std::shared_ptr<const SpectrumFrame> display_;
	int numSamples_ = 0;
	int fftSize_ = 0;
	std::chrono::steady_clock::time_point lastRepaintTime_{};
};
