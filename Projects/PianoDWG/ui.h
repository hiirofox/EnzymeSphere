#pragma once

// Include the standard types used by D2DCanvas before its inline implementation.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cwchar>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>
#include "D2DCanvas.h"
#include "PianoDWG.h"

class TrainStateUI : public D2DCanvas
{
public:
	// Adjustable plot ranges (Hz and amplitude dB).
	static constexpr float FrequencyMinHz = 0.0f;
	static constexpr float FrequencyMaxHz = 16000.0f;
	static constexpr float MagnitudeMinDb = -160.0f;
	static constexpr float MagnitudeMaxDb = -15.0f;

	static_assert(FrequencyMinHz >= 0.0f && FrequencyMaxHz > FrequencyMinHz &&
		FrequencyMaxHz <= PianoDWG::PianoWaveguide<float>::SampleRate / 2.0f,
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

	void UpdateTarget(float* samples, int count)
	{
		{
			std::lock_guard<std::mutex> lock(updateMutex_);
			if (!samples || count <= 0)
			{
				targetSpectrum_.reset();
				numSamples_ = fftSize_ = 0;
				std::atomic_store(&display_, std::shared_ptr<const SpectrumFrame>{});
			}
			else
			{
				// Strictly greater than count, even for a power-of-two count.
				int n = 1;
				while (n <= count)
				{
					// Train::FFT uses signed int indices and len <<= 1.
					if (n > (std::numeric_limits<int>::max)() / 4)
						throw std::length_error("Spectrum FFT size exceeds safe int range");
					n *= 2;
				}
				auto spectrum = std::make_shared<std::vector<float>>(ComputeSpectrum(samples, count, n));
				targetSpectrum_ = spectrum;
				numSamples_ = count;
				fftSize_ = n;
				auto frame = std::make_shared<SpectrumFrame>();
				frame->target = targetSpectrum_;
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
			if (!values || count != PianoDWG::PianoWaveguide<float>::NumParams || !targetSpectrum_)
				return;
			const auto now = std::chrono::steady_clock::now();
			if (now - lastRepaintTime_ < std::chrono::duration<double>(1.0 / 30.0))
				return;

			// Own the parameter snapshot and model state independently of training.
			params_.assign(values, values + count);
			pdwg_.VecToParams(params_.data());
			pdwg_.Reset();
			pdwg_.NoteOn(1.0f);
			audio_.resize(numSamples_);
			pdwg_.ProcessBlock(audio_.data(), numSamples_);

			auto frame = std::make_shared<SpectrumFrame>();
			frame->target = targetSpectrum_;
			frame->piano = ComputeSpectrum(audio_.data(), numSamples_, fftSize_);
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
		if (size.width < 200.0f || size.height < 150.0f) return;

		Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> black, red, grid;
		if (FAILED(g.CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f), black.GetAddressOf())) ||
			FAILED(g.CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.0f, 0.0f), red.GetAddressOf())) ||
			FAILED(g.CreateSolidColorBrush(D2D1::ColorF(0.86f, 0.86f, 0.86f), grid.GetAddressOf())))
			return;

		const auto plot = D2D1::RectF(62.0f, 62.0f, size.width - 24.0f, size.height - 50.0f);
		const float width = plot.right - plot.left;
		const float height = plot.bottom - plot.top;
		constexpr int tickIntervals = 6;
		for (int tick = 0; tick <= tickIntervals; ++tick)
		{
			const float fraction = static_cast<float>(tick) / tickIntervals;
			const float db = MagnitudeMaxDb - fraction * (MagnitudeMaxDb - MagnitudeMinDb);
			const float y = plot.top + fraction * height;
			g.DrawLine(D2D1::Point2F(plot.left, y), D2D1::Point2F(plot.right, y), grid.Get());
			wchar_t label[32];
			std::swprintf(label, 32, L"%.4g", static_cast<double>(db));
			DrawLabel(g, label, D2D1::RectF(8.0f, y - 10.0f, 56.0f, y + 12.0f), black.Get());
		}
		for (int tick = 0; tick <= tickIntervals; ++tick)
		{
			const float fraction = static_cast<float>(tick) / tickIntervals;
			const float hz = FrequencyMinHz + fraction * (FrequencyMaxHz - FrequencyMinHz);
			const float x = plot.left + fraction * width;
			g.DrawLine(D2D1::Point2F(x, plot.top), D2D1::Point2F(x, plot.bottom), grid.Get());
			wchar_t label[32];
			std::swprintf(label, 32, L"%.6g", static_cast<double>(hz));
			DrawLabel(g, label, D2D1::RectF(x - 20.0f, plot.bottom + 7.0f, x + 40.0f, plot.bottom + 28.0f), black.Get());
		}
		g.DrawRectangle(plot, black.Get());
		DrawLabel(g, L"dB", D2D1::RectF(10.0f, 35.0f, 55.0f, 58.0f), black.Get());
		DrawLabel(g, L"Hz", D2D1::RectF(plot.right - 18.0f, size.height - 23.0f, size.width, size.height), black.Get());
		g.DrawLine(D2D1::Point2F(plot.left, 21.0f), D2D1::Point2F(plot.left + 22.0f, 21.0f), black.Get(), 2.0f);
		DrawLabel(g, L"Target", D2D1::RectF(plot.left + 28.0f, 10.0f, plot.left + 95.0f, 34.0f), black.Get());
		g.DrawLine(D2D1::Point2F(plot.left + 110.0f, 21.0f), D2D1::Point2F(plot.left + 132.0f, 21.0f), red.Get(), 2.0f);
		DrawLabel(g, L"Piano waveguide", D2D1::RectF(plot.left + 138.0f, 10.0f, size.width, 34.0f), red.Get());
		if (!frame)
		{
			DrawLabel(g, L"Waiting for target...", plot, black.Get());
			return;
		}
		wchar_t info[128];
		std::swprintf(info, 128, L"Samples: %d   FFT N: %d   One-sided amplitude (0 dB = 1)",
			frame->numSamples, frame->fftSize);
		DrawLabel(g, info, D2D1::RectF(plot.left, 35.0f, size.width, 60.0f), black.Get());

		g.PushAxisAlignedClip(plot, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
		// Complete target first, then overlay the complete model spectrum.
		DrawSpectrum(g, *frame->target, frame->fftSize, plot, black.Get());
		DrawSpectrum(g, frame->piano, frame->fftSize, plot, red.Get());
		g.PopAxisAlignedClip();
	}

private:
	struct SpectrumFrame
	{
		std::shared_ptr<const std::vector<float>> target;
		std::vector<float> piano;
		int fftSize = 0;
		int numSamples = 0;
	};

	static std::vector<float> ComputeSpectrum(const float* samples, int count, int n)
	{
		std::vector<float> re(n, 0.0f), im(n, 0.0f);
		std::copy(samples, samples + count, re.begin());
		PianoDWG::Train::FFT(re.data(), im.data(), n, 0);
		std::vector<float> spectrum(n / 2 + 1);
		for (int k = 0; k <= n / 2; ++k)
		{
			// Original sample count: zero padding must not change amplitude.
			const double scale = (k == 0 || k == n / 2) ? 1.0 / count : 2.0 / count;
			const double amplitude = std::hypot(static_cast<double>(re[k]), static_cast<double>(im[k])) * scale;
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
			PianoDWG::PianoWaveguide<float>::SampleRate;
		const double lastBin = (std::min)(static_cast<double>(spectrum.size() - 1),
			static_cast<double>(FrequencyMaxHz) * n / PianoDWG::PianoWaveguide<float>::SampleRate);
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
	PianoDWG::PianoWaveguide<float> pdwg_;
	std::mutex updateMutex_;
	std::vector<float> params_, audio_;
	std::shared_ptr<const std::vector<float>> targetSpectrum_;
	std::shared_ptr<const SpectrumFrame> display_;
	int numSamples_ = 0;
	int fftSize_ = 0;
	std::chrono::steady_clock::time_point lastRepaintTime_{};
};
