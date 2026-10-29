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

protected:
	void OnPaint(ID2D1RenderTarget& g) override
	{
		ID2D1SolidColorBrush* brush1 = nullptr;
		ID2D1SolidColorBrush* brush2 = nullptr;
		g.CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f), &brush1);
		g.CreateSolidColorBrush(D2D1::ColorF(1.0f, 0.0f, 0.0f, 1.0f), &brush2);
		D2D1_SIZE_F size = g.GetSize();
	}
};
