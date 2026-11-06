// tvst3 callbacks only: DSP and editor implementations live in their own headers.
#include "tvst3info.h"
#include "tvst3interface.h"
#include "PianoDWGSynth.h"
#include "PianoDWGEditor.h"

void* CreateProcessInstance() { return new PianoDWGSynth; }
void DestroyProcessInstance(void* p) { delete static_cast<PianoDWGSynth*>(p); }
void Prepare(void* p, double rate) { static_cast<PianoDWGSynth*>(p)->Prepare(rate); }
void ProcessBlock(void* p, float** buffers, int channels, int samples,
                  const MidiMsg* events, int count) {
    static_cast<PianoDWGSynth*>(p)->ProcessBlock(buffers, channels, samples, events, count);
}
int SaveData(void* p, char* data, int capacity) {
    return static_cast<PianoDWGSynth*>(p)->SaveData(data, capacity);
}
void LoadData(void* p, const char* data, int size) {
    static_cast<PianoDWGSynth*>(p)->LoadData(data, size);
}
void* CreateEditorInstance(void* p) { return new PianoDWGEditor(*static_cast<PianoDWGSynth*>(p)); }
void ShowEditor(void* p, HWND parent) { static_cast<PianoDWGEditor*>(p)->Show(parent); }
void CloseEditor(void* p) { delete static_cast<PianoDWGEditor*>(p); }
