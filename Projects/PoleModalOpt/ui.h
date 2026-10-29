#pragma once
#include "D2DCanvas.h"
#include <complex>
#include <vector>

class TrainStateUI : public D2DCanvas
{
public:
	IDWriteTextFormat* textFormat = nullptr;
	TrainStateUI()
	{
		IDWriteFactory* pDWriteFactory = nullptr;
		DWriteCreateFactory(
			DWRITE_FACTORY_TYPE_SHARED,
			__uuidof(IDWriteFactory),
			reinterpret_cast<IUnknown**>(&pDWriteFactory)
		);
		pDWriteFactory->CreateTextFormat(
			L"Microsoft YaHei",              // 字体：微软雅黑
			nullptr,                          // 使用系统字体集合
			DWRITE_FONT_WEIGHT_NORMAL,        // 正常字重
			DWRITE_FONT_STYLE_NORMAL,         // 正常样式
			DWRITE_FONT_STRETCH_NORMAL,       // 正常宽度
			20.0f,                            // 字号：20 DIP
			L"zh-cn",                         // 中文区域设置
			&textFormat                       // 输出创建的对象
		);
	}
	~TrainStateUI() override { Stop(); }

	std::vector<double>ps;
	int numParams = 0;

	std::chrono::steady_clock::time_point lastRepaintTime{};
	void UpdateAndRepaint(double* params, int numParams)
	{
		if (ps.size() < numParams)
			ps.resize(numParams);
		this->numParams = numParams;
		for (int i = 0; i < numParams; ++i)
			ps[i] = params[i];

		auto now = std::chrono::steady_clock::now();
		if (now - lastRepaintTime < std::chrono::duration<double>(1.0 / 30.0)) return;
		lastRepaintTime = now;

		InvalidateRect(GetHWND(), nullptr, FALSE);//reapint
	}

protected:
	std::complex<double> DampOscHw(double omega, double a, double k, double w, double phi)
	{
		//Aexp(-kt)cos(wt+phi)
		std::complex<double> s{ 0., omega };
		return ((s + k) * cos(phi) - w * sin(phi)) * a / ((s + k) * (s + k) + w * w);
	}

	void OnPaint(ID2D1RenderTarget& g) override
	{
		ID2D1SolidColorBrush* brush1 = nullptr;
		ID2D1SolidColorBrush* brush2 = nullptr;
		g.CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f), &brush1);
		g.CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.0f, 0.0f, 1.0f), &brush2);
		D2D1_SIZE_F size = g.GetSize();
		double lasty = 0, lastw;

		constexpr static double nyqx = 4;
		for (int i = 1; i < size.width; ++i)
		{
			const double lw = log(8.3333e-4), hw = log(3.14159265 * nyqx);
			const double x = (double)i / size.width;
			double omega = exp(x * (hw - lw) + lw);//nyq->pi

			double t = (double)i / size.width * 3.14159265358979 * 20.0;
			std::complex<double>hs{ 0.,0. };
			double ht = 0.;
			for (int j = 0; j < numParams; j += 4)
			{
				double a = ps[j + 0];
				double k = ps[j + 1];
				double w = ps[j + 2];
				double phi = ps[j + 3];
				hs += DampOscHw(omega, a, k, w, phi);
				ht += a * exp(-k * t) * cos(w * t + phi);
			}
			double magdb = 20. * std::log10(std::abs(hs));
			const double highdb = 20.;
			const double lowdb = -60.;
			double y = size.height - (magdb - lowdb) / (highdb - lowdb) * size.height;

			double w = .5 * size.height - ht * size.height * .125;

			g.DrawLine(D2D1::Point2(i - 1, (int)lasty), D2D1::Point2(i, (int)y), brush1);
			g.DrawLine(D2D1::Point2(i - 1, (int)lastw), D2D1::Point2(i, (int)w), brush2);
			lasty = y;
			lastw = w;
		}
	}
};
