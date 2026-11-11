#pragma once
#include <atomic>
#include <condition_variable>
#include <algorithm>
#include <cwchar>
#include "D2DCanvas.h"
#include "PianoDWGSynth.h"

class PianoDWGEditor : public D2DCanvas {
    PianoDWGSynth& proc;
    Microsoft::WRL::ComPtr<IDWriteFactory> textFactory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> titleFont, bodyFont, keyFont;
    int mouseNote = -1;
    bool spaceDown = false, mouseDown = false;
    static constexpr UINT_PTR RefreshTimer = 71;
    static constexpr const wchar_t* Property = L"PianoDWG.Editor";
    static bool Black(int note) {
        const int p = note % 12;
        return p == 1 || p == 3 || p == 6 || p == 8 || p == 10;
    }
    D2D1_RECT_F KeyRect(int note, float width, float height) const {
        int whites = 0;
        for (int n = PianoDWGParams::FirstNote; n < note; ++n) if (!Black(n)) ++whites;
        const float w = (width - 32.f) / 52.f;
        const float x = 16.f + whites * w;
        return Black(note) ? D2D1::RectF(x - w * .32f, height - 240.f, x + w * .32f, height - 105.f)
                           : D2D1::RectF(x, height - 240.f, x + w, height - 24.f);
    }
    int HitKey(float x, float y) const {
        RECT rc{}; GetClientRect(GetHWND(), &rc);
        // Map physical mouse coordinates to the same logical layout used for painting.
        if (rc.right <= 0 || rc.bottom <= 0) return -1;
        x *= 640.f / rc.right; y *= 480.f / rc.bottom;
        for (int pass = 0; pass < 2; ++pass) for (int n = PianoDWGParams::FirstNote; n <= PianoDWGParams::LastNote; ++n) {
            if (Black(n) != (pass == 0)) continue;
            const auto r = KeyRect(n, 640.f, 480.f);
            if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
                return PianoDWGParams::RowForNote(n) >= 0 ? n : -1;
        }
        return -1;
    }
    void MouseKey(int note) {
        if (note == mouseNote) return;
        if (mouseNote >= 0) proc.UIKey(mouseNote, 0);
        mouseNote = note;
        if (note >= 0) proc.UIKey(note, 100);
    }
    void ReleaseInput() {
        MouseKey(-1); mouseDown = false;
        if (spaceDown) { proc.UIPedal(false); spaceDown = false; }
        if (GetCapture() == GetHWND()) ReleaseCapture();
    }
    static void CALLBACK Tick(HWND hwnd, UINT, UINT_PTR, DWORD) {
        auto* self = static_cast<PianoDWGEditor*>(GetPropW(hwnd, Property));
        if (!self) return;
        if (GetFocus() != hwnd) self->ReleaseInput();
        else {
            if (self->mouseDown && (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000) || GetCapture() != hwnd)) {
                self->MouseKey(-1); self->mouseDown = false;
                if (GetCapture() == hwnd) ReleaseCapture();
            }
            if (self->spaceDown && !(GetAsyncKeyState(VK_SPACE) & 0x8000)) {
                self->proc.UIPedal(false); self->spaceDown = false;
            }
        }
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    void Text(ID2D1RenderTarget& t, ID2D1SolidColorBrush* b, const wchar_t* s,
              IDWriteTextFormat* font, D2D1_RECT_F r) {
        if (font) t.DrawText(s, UINT32(wcslen(s)), font, r, b);
    }
protected:
    void OnPaint(ID2D1RenderTarget& t) override {
        const auto actualSize = t.GetSize();
        const auto size = D2D1::SizeF(640.f, 480.f);
        t.SetTransform(D2D1::Matrix3x2F::Scale(actualSize.width / size.width, actualSize.height / size.height));
        t.Clear(D2D1::ColorF(0x171c26));
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
        if (FAILED(t.CreateSolidColorBrush(D2D1::ColorF(0xe8edf5), brush.GetAddressOf()))) return;
        Text(t, brush.Get(), L"PianoDWG2", titleFont.Get(), D2D1::RectF(24, 24, size.width - 24, 70));
        brush->SetColor(D2D1::ColorF(0x9daec4));
        wchar_t modelInfo[96]{};
        swprintf_s(modelInfo, L"%d trained keys  |  %d voices  |  Model: %.0f Hz",
                   PianoDWGParams::NumNotes, PianoDWGSynth::NumPolys, PianoDWGSynth::DwgSampleRate);
        Text(t, brush.Get(), modelInfo, bodyFont.Get(), D2D1::RectF(26, 81, size.width - 24, 107));
        Text(t, brush.Get(), L"Click or drag to play. Hold SPACE for sustain.", bodyFont.Get(), D2D1::RectF(26, 153, size.width - 24, 179));
        Text(t, brush.Get(), L"Host MIDI: Note On / Off + CC64", bodyFont.Get(), D2D1::RectF(26, 181, size.width - 24, 207));
        brush->SetColor(D2D1::ColorF(proc.PedalLit() ? 0x62d9b1 : 0x65758a));
        Text(t, brush.Get(), proc.PedalLit() ? L"SUSTAIN  ON" : L"SUSTAIN  OFF", bodyFont.Get(),
             D2D1::RectF(size.width - 155, 36, size.width - 20, 65));
        for (int pass = 0; pass < 2; ++pass) for (int n = PianoDWGParams::FirstNote; n <= PianoDWGParams::LastNote; ++n) {
            const bool black = Black(n);
            if (black != (pass == 1)) continue;
            const auto r = KeyRect(n, size.width, size.height);
            const bool lit = proc.KeyLit(n) || n == mouseNote;
            const bool playable = PianoDWGParams::RowForNote(n) >= 0;
            const auto keyColor = playable ? (black ? 0x222936 : 0xe8edf5)
                                           : (black ? 0x191e28 : 0x788395);
            brush->SetColor(D2D1::ColorF(lit ? 0x62d9b1 : keyColor));
            t.FillRectangle(r, brush.Get());
            brush->SetColor(D2D1::ColorF(0x171c26));
            t.DrawRectangle(r, brush.Get(), .8f);
            if (n % 12 == 0 || n == 21) {
                wchar_t label[8]{};
                swprintf_s(label, L"%c%d", n == 21 ? L'A' : L'C', n / 12 - 1);
                Text(t, brush.Get(), label, keyFont.Get(), D2D1::RectF(r.left + 1, r.bottom - 22, r.right, r.bottom));
            }
        }
    }
    void MouseMsg(int x, int y, UINT message) override {
        if (message == WM_LBUTTONDOWN) {
            mouseDown = true; SetCapture(GetHWND()); MouseKey(HitKey(float(x), float(y)));
        } else if (message == WM_MOUSEMOVE && mouseDown) MouseKey(HitKey(float(x), float(y)));
        else if (message == WM_LBUTTONUP) {
            MouseKey(-1); mouseDown = false;
            if (GetCapture() == GetHWND()) ReleaseCapture();
        }
        InvalidateRect(GetHWND(), nullptr, FALSE);
    }
    void KeyboardMsg(int key, UINT message) override {
        if (key != VK_SPACE) return;
        const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        spaceDown = down; proc.UIPedal(down);
    }
public:
    explicit PianoDWGEditor(PianoDWGSynth& p) : proc(p) {
        if (SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                      reinterpret_cast<IUnknown**>(textFactory.GetAddressOf())))) {
            textFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 32.f, L"en-us", titleFont.GetAddressOf());
            textFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.f, L"en-us", bodyFont.GetAddressOf());
            textFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 8.f, L"en-us", keyFont.GetAddressOf());
        }
    }
    void Show(HWND parent) {
        if (CreateFromHWND(parent)) {
            SetPropW(GetHWND(), Property, this);
            SetTimer(GetHWND(), RefreshTimer, 33, Tick);
        }
    }
    ~PianoDWGEditor() override {
        // Join the canvas thread before any derived members are destroyed.
        Stop();
        if (mouseNote >= 0) proc.UIKey(mouseNote, 0);
        proc.UIPedal(false);
    }
};
