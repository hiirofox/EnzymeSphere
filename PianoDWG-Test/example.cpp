// Single-translation-unit example. User DSP/editor may use the normal C++ runtime.
#include "tvst3info.h"
#include "tvst3interface.h"
#include "PianoDWGSynth.h"
#include "D2DCanvas.h"

class mydsp
{
public:
	float sampleRate = 48000.;
	float gain = 0.5f;
};

void* CreateProcessInstance() { return new mydsp{}; }
void DestroyProcessInstance(void* p) { delete static_cast<mydsp*>(p); }
void Prepare(void* p, double sampleRate) { static_cast<mydsp*>(p)->sampleRate = sampleRate; }
void ProcessBlock(void* p, float** bufs, int channels, int samples,
	const MidiMsg*, int) {
	const float gain = static_cast<mydsp*>(p)->gain;
	for (int ch = 0; ch < channels; ++ch)
		for (int i = 0; i < samples; ++i) bufs[ch][i] *= gain;
}
int SaveData(void* p, char* data, int capacity) {
	if (!data) return 4;
	if (capacity < 4) return -1;
	const char* src = reinterpret_cast<const char*>(&static_cast<mydsp*>(p)->gain);
	for (int i = 0; i < 4; ++i) data[i] = src[i];
	return 4;
}
void LoadData(void* p, const char* data, int size) {
	if (size != 4) return;
	float gain;
	char* dst = reinterpret_cast<char*>(&gain);
	for (int i = 0; i < 4; ++i) dst[i] = data[i];
	if (gain >= 0.0f && gain <= 1.0f) static_cast<mydsp*>(p)->gain = gain;
}
class myeditor {
public:
	mydsp* process;
	HWND window;
};
void* CreateEditorInstance(void* processInstance) {
	myeditor* editor = new myeditor{};
	if (editor) editor->process = static_cast<mydsp*>(processInstance);
	return editor;
}
void ShowEditor(void* p, HWND parent) {
#if defined(_WIN32)
	myeditor* editor = static_cast<myeditor*>(p);
	editor->window = CreateWindowExW(0, L"STATIC", L"TVST3 minimal example",
		WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0,
		(int)TVST3Info::EditorWidth, (int)TVST3Info::EditorHeight,
		parent, nullptr, nullptr, nullptr);
#else
	(void)p; (void)parent;
#endif
}
void CloseEditor(void* p) {
	myeditor* editor = static_cast<myeditor*>(p);
#if defined(_WIN32)
	if (editor && editor->window) DestroyWindow(editor->window);
#endif
	delete editor;
}
