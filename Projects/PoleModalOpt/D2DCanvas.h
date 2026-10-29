#pragma once
#include <windows.h>
#include <windowsx.h>
#include <wrl/client.h>
#include <d2d1.h>
#include <dwrite.h>
#include <thread>
#include <mutex>
#include <vector>

class D2DCanvas {
public:
	virtual ~D2DCanvas() { Stop(); }
	bool Create(int width = 800, int height = 600) { return Start(nullptr, width, height, SW_SHOW); }
	bool CreateFromHWND(HWND parent) {
		if (!IsWindow(parent)) return false;
		DWORD processId = 0;
		GetWindowThreadProcessId(parent, &processId);
		if (processId != GetCurrentProcessId()) return false;
		return Start(parent, 0, 0, SW_SHOW);
	}
	HWND GetHWND() const { return hwnd_.load(); }
	void Stop() {
		if (!thread_.joinable()) return;
		PostThreadMessageW(threadId_, WM_QUIT, 0, 0);
		if (thread_.get_id() == std::this_thread::get_id()) return;
		thread_.join();
		parent_ = nullptr;
		threadId_ = 0;
	}
protected:
	virtual void OnPaint(ID2D1RenderTarget& target) = 0;
	virtual void MouseMsg(int x, int y, UINT message) {}
	virtual void KeyboardMsg(int key, UINT message) {}
private:
	bool Start(HWND parent, int width, int height, int showCmd) {
		if (thread_.joinable()) return false;
		parent_ = parent;
		width_ = width;
		height_ = height;
		showCmd_ = showCmd;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			ready_ = false;
			startOk_ = false;
		}
		try {
			thread_ = std::thread(&D2DCanvas::ThreadMain, this);
		}
		catch (...) {
			return false;
		}
		std::unique_lock<std::mutex> lock(mutex_);
		cv_.wait(lock, [this] { return ready_; });
		bool result = startOk_;
		lock.unlock();
		if (!result) thread_.join();
		return result;
	}
	static bool RegisterWindowClass() {
		static std::once_flag flag;
		static bool result = false;
		std::call_once(flag, [] {
			WNDCLASSEXW wc{};
			wc.cbSize = sizeof(wc);
			wc.style = CS_HREDRAW | CS_VREDRAW;
			wc.lpfnWndProc = WindowProc;
			wc.hInstance = GetModuleHandleW(nullptr);
			wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
			wc.lpszClassName = L"D2DCanvas.Internal.Window";
			result = RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
			});
		return result;
	}
	void ThreadMain() {
		threadId_ = GetCurrentThreadId();
		MSG msg{};
		PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
		HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf());
		bool canCreate = SUCCEEDED(hr) && RegisterWindowClass();
		int width = width_;
		int height = height_;
		if (canCreate && parent_) {
			RECT rc{};
			if (!IsWindow(parent_) || !GetClientRect(parent_, &rc)) {
				canCreate = false;
			}
			else {
				width = rc.right - rc.left;
				height = rc.bottom - rc.top;
			}
		}
		if (canCreate) {
			DWORD style = parent_ ? WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS : WS_OVERLAPPEDWINDOW;
			DWORD exStyle = parent_ ? WS_EX_NOPARENTNOTIFY : 0;
			hwnd_.store(CreateWindowExW(exStyle, L"D2DCanvas.Internal.Window",
				parent_ ? L"" : L"Direct2D Canvas",
				style,
				parent_ ? 0 : CW_USEDEFAULT,
				parent_ ? 0 : CW_USEDEFAULT,
				width, height, parent_, nullptr, GetModuleHandleW(nullptr), this));
		}
		{
			std::lock_guard<std::mutex> lock(mutex_);
			startOk_ = hwnd_.load() != nullptr;
			ready_ = true;
		}
		cv_.notify_one();
		HWND hwnd = hwnd_.load();
		if (!hwnd) {
			factory_.Reset();
			return;
		}
		if (parent_) {
			SetTimer(hwnd, 1, 50, nullptr);
		}
		else {
			ShowWindow(hwnd, showCmd_);
			UpdateWindow(hwnd);
		}
		while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
		hwnd = hwnd_.load();
		if (hwnd && IsWindow(hwnd)) DestroyWindow(hwnd);
		renderTarget_.Reset();
		factory_.Reset();
	}
	HRESULT CreateDeviceResources() {
		if (renderTarget_) return S_OK;
		HWND hwnd = hwnd_.load();
		if (!hwnd || !factory_) return E_FAIL;
		RECT rc{};
		if (!GetClientRect(hwnd, &rc)) return E_FAIL;
		D2D1_SIZE_U size = D2D1::SizeU(static_cast<UINT32>(std::max<LONG>(1, rc.right - rc.left)), static_cast<UINT32>(std::max<LONG>(1, rc.bottom - rc.top)));
		return factory_->CreateHwndRenderTarget(D2D1::RenderTargetProperties(), D2D1::HwndRenderTargetProperties(hwnd, size), renderTarget_.GetAddressOf());
	}
	LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {
		case WM_PAINT: {
			PAINTSTRUCT ps{};
			BeginPaint(hwnd, &ps);
			if (SUCCEEDED(CreateDeviceResources())) {
				renderTarget_->BeginDraw();
				renderTarget_->Clear(D2D1::ColorF(D2D1::ColorF::White));
				OnPaint(*renderTarget_.Get());
				HRESULT hr = renderTarget_->EndDraw();
				if (hr == D2DERR_RECREATE_TARGET) {
					renderTarget_.Reset();
					InvalidateRect(hwnd, nullptr, FALSE);
				}
			}
			EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_SIZE:
			if (renderTarget_ && LOWORD(lParam) > 0 && HIWORD(lParam) > 0) {
				HRESULT hr = renderTarget_->Resize(D2D1::SizeU(LOWORD(lParam), HIWORD(lParam)));
				if (FAILED(hr)) renderTarget_.Reset();
			}
			InvalidateRect(hwnd, nullptr, FALSE);
			return 0;
		case WM_TIMER: {
			if (!parent_) return 0;
			if (!IsWindow(parent_)) {
				DestroyWindow(hwnd);
				return 0;
			}
			RECT parentRect{};
			RECT childRect{};
			if (!GetClientRect(parent_, &parentRect) || !GetClientRect(hwnd, &childRect)) return 0;
			int width = parentRect.right - parentRect.left;
			int height = parentRect.bottom - parentRect.top;
			if (width != childRect.right - childRect.left || height != childRect.bottom - childRect.top) SetWindowPos(hwnd, nullptr, 0, 0, width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
			return 0;
		}
		case WM_ERASEBKGND:
			return 1;
		case WM_CLOSE:
			DestroyWindow(hwnd);
			return 0;
		case WM_DESTROY:
			KillTimer(hwnd, 1);
			PostQuitMessage(0);
			return 0;
		case WM_NCDESTROY: {
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
			HWND expected = hwnd;
			hwnd_.compare_exchange_strong(expected, nullptr);
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		}
		case WM_MOUSEMOVE:
		case WM_LBUTTONDOWN:
		case WM_LBUTTONUP:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONUP:
		case WM_MBUTTONDOWN:
		case WM_MBUTTONUP:
			if (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN) SetFocus(hwnd);
			MouseMsg(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), msg);
			return 0;
		case WM_KEYDOWN:
		case WM_KEYUP:
			KeyboardMsg(static_cast<int>(wParam), msg);
			return 0;
		case WM_SYSKEYDOWN:
		case WM_SYSKEYUP:
			KeyboardMsg(static_cast<int>(wParam), msg);
			return DefWindowProcW(hwnd, msg, wParam, lParam);
		}
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		D2DCanvas* self = nullptr;
		if (msg == WM_NCCREATE) {
			auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
			self = static_cast<D2DCanvas*>(cs->lpCreateParams);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
			self->hwnd_.store(hwnd);
		}
		else {
			self = reinterpret_cast<D2DCanvas*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		}
		if (!self) return DefWindowProcW(hwnd, msg, wParam, lParam);
		return self->HandleMessage(hwnd, msg, wParam, lParam);
	}
	std::atomic<HWND> hwnd_{ nullptr };
	HWND parent_ = nullptr;
	int width_ = 800;
	int height_ = 600;
	int showCmd_ = SW_SHOW;
	DWORD threadId_ = 0;
	std::thread thread_;
	std::mutex mutex_;
	std::condition_variable cv_;
	bool ready_ = false;
	bool startOk_ = false;
	Microsoft::WRL::ComPtr<ID2D1Factory> factory_;
	Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> renderTarget_;
};