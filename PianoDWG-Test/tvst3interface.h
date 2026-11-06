#pragma once

#include "tvst3info.h"

#include <stddef.h>
#include <stdint.h>
#if !defined(_WIN32)
#include <stdlib.h>
#endif

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #define TVST3_CALL __stdcall
    #define TVST3_EXPORT __declspec(dllexport)
#else
    using HWND = void*;
    #define TVST3_CALL
    #define TVST3_EXPORT __attribute__((visibility("default")))
#endif

// MIDI 1.0 channel-voice message. status includes the MIDI channel nibble.
// sampleOffset is relative to the beginning of the current audio block.
struct MidiMsg {
    int32_t sampleOffset = 0;
    uint8_t status = 0;
    uint8_t data1 = 0;
    uint8_t data2 = 0;
};

// Implement these callbacks in the .cpp including this header.
// This header emits the VST3 entry points by default, so compile it in exactly
// one translation unit. Define TVST3_NO_IMPLEMENTATION in auxiliary .cpp files.
void* CreateProcessInstance();
void DestroyProcessInstance(void* processInstance);
void Prepare(void* processInstance, double sampleRate);
// bufs is an array of planar channel pointers, valid only during ProcessBlock.
// MIDI events use sample offsets within the current block.
void ProcessBlock(void* processInstance, float** bufs, int numChannels,
                  int numSamples, const MidiMsg* midis, int numMidiMsgs);
// Return required byte count when data == nullptr, or the bytes actually written.
// Caller allocates exactly the number of bytes reported by the first call.
int SaveData(void* processInstance, char* data, int capacity);
void LoadData(void* processInstance, const char* data, int size);

void* CreateEditorInstance(void* processInstance);
void ShowEditor(void* editorInstance, HWND hostHwnd);
void CloseEditor(void* editorInstance);


namespace tvst3_detail {

// The bridge itself uses no STL or operator new/delete. It allocates on the OS heap.
inline void* allocate(size_t n) {
#if defined(_WIN32)
    return HeapAlloc(GetProcessHeap(), 0, n ? n : 1);
#else
    return ::malloc(n ? n : 1);
#endif
}
inline void deallocate(void* p) {
#if defined(_WIN32)
    if (p) HeapFree(GetProcessHeap(), 0, p);
#else
    ::free(p);
#endif
}
inline void copyBytes(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
}
inline void zeroBytes(void* dst, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    for (size_t i = 0; i < n; ++i) d[i] = 0;
}
inline int compareText(const char* a, const char* b) {
    while (*a && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}
inline uint32_t increment(volatile long* p) {
#if defined(_WIN32)
    return (uint32_t)InterlockedIncrement(p);
#else
    return (uint32_t)__atomic_add_fetch(p, 1, __ATOMIC_RELAXED);
#endif
}
inline uint32_t decrement(volatile long* p) {
#if defined(_WIN32)
    return (uint32_t)InterlockedDecrement(p);
#else
    return (uint32_t)__atomic_sub_fetch(p, 1, __ATOMIC_ACQ_REL);
#endif
}
template <typename T> inline T minimum(T a, T b) { return a < b ? a : b; }
template <typename T> inline T maximum(T a, T b) { return a > b ? a : b; }

using int8 = int8_t;
using int16 = int16_t;
using int32 = int32_t;
using int64 = int64_t;
using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using uint64 = uint64_t;
using tresult = int32;
using TBool = uint8;
using TUID = char[16];
using FIDString = const char*;
using TChar = char16_t;
using String128 = TChar[128];
using SpeakerArrangement = uint64;

constexpr tresult kNoInterface = static_cast<tresult>(0x80004002u);
constexpr tresult kResultOk = 0;
constexpr tresult kResultFalse = 1;
constexpr tresult kInvalidArgument = static_cast<tresult>(0x80070057u);
constexpr tresult kNotImplemented = static_cast<tresult>(0x80004001u);
constexpr tresult kInternalError = static_cast<tresult>(0x80004005u);
constexpr tresult kOutOfMemory = static_cast<tresult>(0x8007000Eu);
constexpr uint32 kManyInstances = 0x7FFFFFFFu;

struct Uid {
    uint8 bytes[16];
};

constexpr Uid makeUid(uint32 a, uint32 b, uint32 c, uint32 d) {
    return {{
        static_cast<uint8>(a), static_cast<uint8>(a >> 8),
        static_cast<uint8>(a >> 16), static_cast<uint8>(a >> 24),
        static_cast<uint8>(b >> 16), static_cast<uint8>(b >> 24),
        static_cast<uint8>(b), static_cast<uint8>(b >> 8),
        static_cast<uint8>(c >> 24), static_cast<uint8>(c >> 16),
        static_cast<uint8>(c >> 8), static_cast<uint8>(c),
        static_cast<uint8>(d >> 24), static_cast<uint8>(d >> 16),
        static_cast<uint8>(d >> 8), static_cast<uint8>(d)
    }};
}

constexpr Uid kFUnknownUID = makeUid(0x00000000, 0x00000000, 0xC0000000, 0x00000046);
static_assert(kFUnknownUID.bytes[8] == 0xC0 && kFUnknownUID.bytes[15] == 0x46, "Invalid VST3 UID byte order");
constexpr Uid kPluginFactoryUID = makeUid(0x7A4D811C, 0x52114A1F, 0xAED9D2EE, 0x0B43BF9F);
constexpr Uid kPluginFactory2UID = makeUid(0x0007B650, 0xF24B4C0B, 0xA464EDB9, 0xF00B2ABB);
constexpr Uid kPluginBaseUID = makeUid(0x22888DDB, 0x156E45AE, 0x8358B348, 0x08190625);
constexpr Uid kComponentUID = makeUid(0xE831FF31, 0xF2D54301, 0x928EBBEE, 0x25697802);
constexpr Uid kAudioProcessorUID = makeUid(0x42043F99, 0xB7DA453C, 0xA569E79D, 0x9AAEC33D);
constexpr Uid kEditControllerUID = makeUid(0xDCD7BBE3, 0x7742448D, 0xA874AACC, 0x979C759E);
constexpr Uid kPlugViewUID = makeUid(0x5BC32507, 0xD06049EA, 0xA6151B52, 0x2B755B29);
constexpr Uid kConnectionPointUID = makeUid(0x70A4156F, 0x6E6E4026, 0x989148BF, 0xAA60D8D1);
constexpr Uid kMessageUID = makeUid(0x936F033B, 0xC6C047DB, 0xBB0882F8, 0x13C1E613);
constexpr Uid kAttributeListUID = makeUid(0x1E5F0AEB, 0xCC7F4533, 0xA2544011, 0x38AD5EE4);
constexpr Uid kStreamUID = makeUid(0xC3BF6EA2, 0x30994752, 0x9B6BF990, 0x1EE33E9B);
constexpr Uid kEventListUID = makeUid(0x3A2C4214, 0x346349FE, 0xB2C4F397, 0xB9695A44);

constexpr Uid kComponentClassUID = makeUid(
    TVST3Info::PluginUID[0], TVST3Info::PluginUID[1],
    TVST3Info::PluginUID[2], TVST3Info::PluginUID[3]);
constexpr Uid kControllerClassUID = makeUid(
    TVST3Info::ControllerUID[0], TVST3Info::ControllerUID[1],
    TVST3Info::ControllerUID[2], TVST3Info::ControllerUID[3]);

inline bool uidBytesEqual(const void* p, const uint8* b) {
    const uint8* a = (const uint8*)p;
    for (int i = 0; i < 16; ++i) if (a[i] != b[i]) return false;
    return true;
}
inline bool uidEqual(const void* a, const Uid& b) {
    return a != nullptr && uidBytesEqual(a, b.bytes);
}

inline void copyAscii(char* dst, size_t capacity, const char* src) {
    if (!dst || capacity == 0) {
        return;
    }
    zeroBytes(dst, capacity);
    if (src) {
        size_t i = 0;
        for (; i + 1 < capacity && src[i]; ++i) dst[i] = src[i];
    }
}

inline void copyAsciiToUtf16(TChar* dst, size_t capacity, const char* src) {
    if (!dst || capacity == 0) {
        return;
    }
    for (size_t j = 0; j < capacity; ++j) dst[j] = 0;
    if (!src) {
        return;
    }
    for (size_t i = 0; i + 1 < capacity && src[i] != '\0'; ++i) {
        dst[i] = static_cast<unsigned char>(src[i]);
    }
}

inline uint32 countBits(uint64 value) {
    uint32 count = 0;
    while (value != 0) {
        value &= value - 1;
        ++count;
    }
    return count;
}

class FUnknown {
public:
    virtual tresult TVST3_CALL queryInterface(const TUID iid, void** obj) = 0;
    virtual uint32 TVST3_CALL addRef() = 0;
    virtual uint32 TVST3_CALL release() = 0;
};

struct PFactoryInfo {
    char vendor[64];
    char url[256];
    char email[128];
    int32 flags;
};

struct PClassInfo {
    TUID cid;
    int32 cardinality;
    char category[32];
    char name[64];
};

struct PClassInfo2 {
    TUID cid;
    int32 cardinality;
    char category[32];
    char name[64];
    uint32 classFlags;
    char subCategories[128];
    char vendor[64];
    char version[64];
    char sdkVersion[64];
};

class IPluginFactory : public FUnknown {
public:
    virtual tresult TVST3_CALL getFactoryInfo(PFactoryInfo* info) = 0;
    virtual int32 TVST3_CALL countClasses() = 0;
    virtual tresult TVST3_CALL getClassInfo(int32 index, PClassInfo* info) = 0;
    virtual tresult TVST3_CALL createInstance(FIDString cid, FIDString iid, void** obj) = 0;
};

class IPluginFactory2 : public IPluginFactory {
public:
    virtual tresult TVST3_CALL getClassInfo2(int32 index, PClassInfo2* info) = 0;
};

class IPluginBase : public FUnknown {
public:
    virtual tresult TVST3_CALL initialize(FUnknown* context) = 0;
    virtual tresult TVST3_CALL terminate() = 0;
};

struct BusInfo {
    int32 mediaType;
    int32 direction;
    int32 channelCount;
    String128 name;
    int32 busType;
    uint32 flags;
};

struct RoutingInfo {
    int32 mediaType;
    int32 busIndex;
    int32 channel;
};

class IBStream : public FUnknown {
public:
    virtual tresult TVST3_CALL read(void* buffer, int32 numBytes, int32* numBytesRead) = 0;
    virtual tresult TVST3_CALL write(void* buffer, int32 numBytes, int32* numBytesWritten) = 0;
    virtual tresult TVST3_CALL seek(int64 pos, int32 mode, int64* result) = 0;
    virtual tresult TVST3_CALL tell(int64* pos) = 0;
};

class IComponent : public IPluginBase {
public:
    virtual tresult TVST3_CALL getControllerClassId(TUID classId) = 0;
    virtual tresult TVST3_CALL setIoMode(int32 mode) = 0;
    virtual int32 TVST3_CALL getBusCount(int32 mediaType, int32 direction) = 0;
    virtual tresult TVST3_CALL getBusInfo(int32 mediaType, int32 direction, int32 index, BusInfo& bus) = 0;
    virtual tresult TVST3_CALL getRoutingInfo(RoutingInfo& inInfo, RoutingInfo& outInfo) = 0;
    virtual tresult TVST3_CALL activateBus(int32 mediaType, int32 direction, int32 index, TBool state) = 0;
    virtual tresult TVST3_CALL setActive(TBool state) = 0;
    virtual tresult TVST3_CALL setState(IBStream* state) = 0;
    virtual tresult TVST3_CALL getState(IBStream* state) = 0;
};

struct ProcessSetup {
    int32 processMode;
    int32 symbolicSampleSize;
    int32 maxSamplesPerBlock;
    double sampleRate;
};

struct AudioBusBuffers {
    int32 numChannels;
    uint64 silenceFlags;
    union {
        float** channelBuffers32;
        double** channelBuffers64;
    };
};

class IEventList;
class IParameterChanges;

struct ProcessData {
    int32 processMode;
    int32 symbolicSampleSize;
    int32 numSamples;
    int32 numInputs;
    int32 numOutputs;
    AudioBusBuffers* inputs;
    AudioBusBuffers* outputs;
    IParameterChanges* inputParameterChanges;
    IParameterChanges* outputParameterChanges;
    IEventList* inputEvents;
    IEventList* outputEvents;
    void* processContext;
};

class IAudioProcessor : public FUnknown {
public:
    virtual tresult TVST3_CALL setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                                   SpeakerArrangement* outputs, int32 numOuts) = 0;
    virtual tresult TVST3_CALL getBusArrangement(int32 direction, int32 index,
                                                  SpeakerArrangement& arr) = 0;
    virtual tresult TVST3_CALL canProcessSampleSize(int32 symbolicSampleSize) = 0;
    virtual uint32 TVST3_CALL getLatencySamples() = 0;
    virtual tresult TVST3_CALL setupProcessing(ProcessSetup& setup) = 0;
    virtual tresult TVST3_CALL setProcessing(TBool state) = 0;
    virtual tresult TVST3_CALL process(ProcessData& data) = 0;
    virtual uint32 TVST3_CALL getTailSamples() = 0;
};

struct ParameterInfo {
    uint32 id;
    TChar title[128];
    TChar shortTitle[128];
    TChar units[128];
    int32 stepCount;
    double defaultNormalizedValue;
    int32 unitId;
    int32 flags;
};

class IComponentHandler : public FUnknown {};
class IPlugView;

class IEditController : public IPluginBase {
public:
    virtual tresult TVST3_CALL setComponentState(IBStream* state) = 0;
    virtual tresult TVST3_CALL setState(IBStream* state) = 0;
    virtual tresult TVST3_CALL getState(IBStream* state) = 0;
    virtual int32 TVST3_CALL getParameterCount() = 0;
    virtual tresult TVST3_CALL getParameterInfo(int32 paramIndex, ParameterInfo& info) = 0;
    virtual tresult TVST3_CALL getParamStringByValue(uint32 id, double valueNormalized, String128 string) = 0;
    virtual tresult TVST3_CALL getParamValueByString(uint32 id, TChar* string, double& valueNormalized) = 0;
    virtual double TVST3_CALL normalizedParamToPlain(uint32 id, double valueNormalized) = 0;
    virtual double TVST3_CALL plainParamToNormalized(uint32 id, double plainValue) = 0;
    virtual double TVST3_CALL getParamNormalized(uint32 id) = 0;
    virtual tresult TVST3_CALL setParamNormalized(uint32 id, double value) = 0;
    virtual tresult TVST3_CALL setComponentHandler(IComponentHandler* handler) = 0;
    virtual IPlugView* TVST3_CALL createView(FIDString name) = 0;
};

struct ViewRect {
    int32 left;
    int32 top;
    int32 right;
    int32 bottom;
};

class IPlugView : public FUnknown {
public:
    virtual tresult TVST3_CALL isPlatformTypeSupported(FIDString type) = 0;
    virtual tresult TVST3_CALL attached(void* parent, FIDString type) = 0;
    virtual tresult TVST3_CALL removed() = 0;
    virtual tresult TVST3_CALL onWheel(float distance) = 0;
    virtual tresult TVST3_CALL onKeyDown(TChar key, int16 keyCode, int16 modifiers) = 0;
    virtual tresult TVST3_CALL onKeyUp(TChar key, int16 keyCode, int16 modifiers) = 0;
    virtual tresult TVST3_CALL getSize(ViewRect* size) = 0;
    virtual tresult TVST3_CALL onSize(ViewRect* newSize) = 0;
    virtual tresult TVST3_CALL onFocus(TBool state) = 0;
    virtual tresult TVST3_CALL setFrame(FUnknown* frame) = 0;
    virtual tresult TVST3_CALL canResize() = 0;
    virtual tresult TVST3_CALL checkSizeConstraint(ViewRect* rect) = 0;
};

class IAttributeList : public FUnknown {
public:
    virtual tresult TVST3_CALL setInt(FIDString id, int64 value) = 0;
    virtual tresult TVST3_CALL getInt(FIDString id, int64& value) = 0;
    virtual tresult TVST3_CALL setFloat(FIDString id, double value) = 0;
    virtual tresult TVST3_CALL getFloat(FIDString id, double& value) = 0;
    virtual tresult TVST3_CALL setString(FIDString id, const TChar* string) = 0;
    virtual tresult TVST3_CALL getString(FIDString id, TChar* string, uint32 sizeInBytes) = 0;
    virtual tresult TVST3_CALL setBinary(FIDString id, const void* data, uint32 sizeInBytes) = 0;
    virtual tresult TVST3_CALL getBinary(FIDString id, const void*& data, uint32& sizeInBytes) = 0;
};

class IMessage : public FUnknown {
public:
    virtual FIDString TVST3_CALL getMessageID() = 0;
    virtual void TVST3_CALL setMessageID(FIDString id) = 0;
    virtual IAttributeList* TVST3_CALL getAttributes() = 0;
};

class IConnectionPoint : public FUnknown {
public:
    virtual tresult TVST3_CALL connect(IConnectionPoint* other) = 0;
    virtual tresult TVST3_CALL disconnect(IConnectionPoint* other) = 0;
    virtual tresult TVST3_CALL notify(IMessage* message) = 0;
};

struct NoteOnEvent {
    int16 channel;
    int16 pitch;
    float tuning;
    float velocity;
    int32 length;
    int32 noteId;
};

struct NoteOffEvent {
    int16 channel;
    int16 pitch;
    float velocity;
    int32 noteId;
    float tuning;
};

struct PolyPressureEvent {
    int16 channel;
    int16 pitch;
    float pressure;
    int32 noteId;
};

struct DataEvent {
    uint32 size;
    uint32 type;
    const uint8* bytes;
};

struct LegacyMIDICCOutEvent {
    uint8 controlNumber;
    int8 channel;
    int8 value;
    int8 value2;
};

struct Event {
    int32 busIndex;
    int32 sampleOffset;
    double ppqPosition;
    uint16 flags;
    uint16 type;
    union {
        NoteOnEvent noteOn;
        NoteOffEvent noteOff;
        DataEvent data;
        PolyPressureEvent polyPressure;
        LegacyMIDICCOutEvent midiCCOut;
    };
};

static_assert(sizeof(PClassInfo2) == 440, "Unexpected VST3 PClassInfo2 layout");
static_assert(sizeof(ProcessSetup) == 24, "Unexpected VST3 ProcessSetup layout");
static_assert(sizeof(AudioBusBuffers) == 24, "Unexpected VST3 AudioBusBuffers layout");
static_assert(sizeof(ProcessData) == 80, "Unexpected VST3 ProcessData layout");
static_assert(sizeof(Event) == 48, "Unexpected VST3 Event layout");

class IEventList : public FUnknown {
public:
    virtual int32 TVST3_CALL getEventCount() = 0;
    virtual tresult TVST3_CALL getEvent(int32 index, Event& e) = 0;
    virtual tresult TVST3_CALL addEvent(Event& e) = 0;
};

// The objects below are plain C-compatible vtable objects. No bridge class has
// a C++ virtual method, constructor, destructor, RTTI or exception dependency.
// The user's DSP is free to use C++ and the normal C++ runtime.

} // namespace tvst3_detail

#if !defined(TVST3_NO_IMPLEMENTATION)
namespace tvst3_detail {

struct Handle { const void* vtable; void* owner; };
struct Core;
struct Control;
struct View;

struct Context {
    volatile long refs;
    void* dsp;
    MidiMsg* midi;
    double rate;
    int32 maxSamples;
};
inline Context* acquire(Context* c) { if (c) increment(&c->refs); return c; }
inline void release(Context* c) {
    if (c && !decrement(&c->refs)) {
        if (c->dsp) DestroyProcessInstance(c->dsp);
        deallocate(c->midi);
        deallocate(c);
    }
}
struct Core {
    Handle component, processor, connection;
    volatile long refs;
    Context* context;
    uint64 inputArrangement, outputArrangement;
    uint8 inputActive, outputActive, midiActive;
};
struct Control {
    Handle controller, connection;
    volatile long refs;
    Context* context;
    IComponentHandler* handler;
};
struct View {
    Handle view;
    volatile long refs;
    Context* context;
    void* editor;
};
struct Factory { Handle factory; volatile long refs; };

enum { KIND_FACTORY, KIND_CORE, KIND_CONTROL, KIND_VIEW };

inline bool isUid(const void* input, const Uid& id) { return uidEqual(input, id); }
inline tresult TVST3_CALL query(void* self, const TUID iid, void** out);
inline uint32 TVST3_CALL addref(void* self);
inline uint32 TVST3_CALL unref(void* self);

// All interface pointers are different subobjects; they share an owner but
// never use a global current-instance pointer or an instance-order lookup.
// Minimal state stream helpers. The size prefix is little-endian on Win64.
inline bool transfer(IBStream* stream, void* bytes, int32 size, bool writing) {
    if (!stream || size < 0) return false;
    uint8* p = (uint8*)bytes;
    while (size > 0) {
        int32 got = 0;
        tresult r = writing ? stream->write(p, size, &got) : stream->read(p, size, &got);
        if (r != kResultOk || got <= 0 || got > size) return false;
        size -= got;
        p += got;
    }
    return true;
}
inline tresult saveState(Context* ctx, IBStream* stream) {
    if (!ctx || !stream) return kInvalidArgument;
    int32 size = SaveData(ctx->dsp, nullptr, 0);
    if (size < 0 || size > 64 * 1024 * 1024) return kInternalError;
    char* data = size ? (char*)allocate(size) : nullptr;
    if (size && !data) return kOutOfMemory;
    bool ok = (!size || SaveData(ctx->dsp, data, size) == size) &&
              transfer(stream, &size, 4, true) && transfer(stream, data, size, true);
    deallocate(data);
    return ok ? kResultOk : kInternalError;
}
inline tresult loadState(Context* ctx, IBStream* stream) {
    if (!ctx || !stream) return kInvalidArgument;
    int32 size = 0;
    if (!transfer(stream, &size, 4, false) || size < 0 || size > 64 * 1024 * 1024) return kInternalError;
    char* data = size ? (char*)allocate(size) : nullptr;
    if (size && !data) return kOutOfMemory;
    bool ok = transfer(stream, data, size, false);
    if (ok) LoadData(ctx->dsp, data, size);
    deallocate(data);
    return ok ? kResultOk : kInternalError;
}

// The link message and its attribute object are embedded in a stack-allocated
// envelope. The receiver consumes it synchronously. No message heap allocation.
struct LinkMessage {
    Handle message, attributes;
    Context* ctx;
};
inline tresult TVST3_CALL linkQuery(void* self, const TUID iid, void** out) {
    if (!out) return kInvalidArgument;
    *out = nullptr;
    LinkMessage* m = (LinkMessage*)((Handle*)self)->owner;
    if (isUid(iid, kFUnknownUID) || isUid(iid, kMessageUID)) *out = &m->message;
    else if (isUid(iid, kAttributeListUID)) *out = &m->attributes;
    return *out ? kResultOk : kNoInterface;
}
inline uint32 TVST3_CALL linkRef(void*) { return 2; }
inline uint32 TVST3_CALL linkUnref(void*) { return 1; }
inline FIDString TVST3_CALL linkId(void*) { return "tvst3.link"; }
inline void TVST3_CALL linkSetId(void*, FIDString) {}
inline IAttributeList* TVST3_CALL linkAttrs(void* self) {
    return (IAttributeList*)&((LinkMessage*)((Handle*)self)->owner)->attributes;
}
inline tresult TVST3_CALL linkSetInt(void*, FIDString, int64) { return kNotImplemented; }
inline tresult TVST3_CALL linkGetInt(void* self, FIDString key, int64& value) {
    if (compareText(key, "ctx") != 0) return kResultFalse;
    value = (int64)(intptr_t)((LinkMessage*)((Handle*)self)->owner)->ctx;
    return kResultOk;
}
inline tresult TVST3_CALL linkSetFloat(void*, FIDString, double) { return kNotImplemented; }
inline tresult TVST3_CALL linkGetFloat(void*, FIDString, double&) { return kNotImplemented; }
inline tresult TVST3_CALL linkSetString(void*, FIDString, const TChar*) { return kNotImplemented; }
inline tresult TVST3_CALL linkGetString(void*, FIDString, TChar*, uint32) { return kNotImplemented; }
inline tresult TVST3_CALL linkSetBinary(void*, FIDString, const void*, uint32) { return kNotImplemented; }
inline tresult TVST3_CALL linkGetBinary(void*, FIDString, const void*&, uint32&) { return kNotImplemented; }

// Each vtable uses the exact SDK ordering, including the initial FUnknown trio.
struct FactoryVT {
    tresult (TVST3_CALL* qi)(void*, const TUID, void**);
    uint32 (TVST3_CALL* retain)(void*);
    uint32 (TVST3_CALL* drop)(void*);
    tresult (TVST3_CALL* info)(void*, PFactoryInfo*);
    int32 (TVST3_CALL* count)(void*);
    tresult (TVST3_CALL* classInfo)(void*, int32, PClassInfo*);
    tresult (TVST3_CALL* make)(void*, FIDString, FIDString, void**);
    tresult (TVST3_CALL* classInfo2)(void*, int32, PClassInfo2*);
};
struct ComponentVT {
    tresult (TVST3_CALL* qi)(void*, const TUID, void**);
    uint32 (TVST3_CALL* retain)(void*); uint32 (TVST3_CALL* drop)(void*);
    tresult (TVST3_CALL* init)(void*, FUnknown*);
    tresult (TVST3_CALL* term)(void*);
    tresult (TVST3_CALL* controllerId)(void*, TUID);
    tresult (TVST3_CALL* ioMode)(void*, int32);
    int32 (TVST3_CALL* busCount)(void*, int32, int32);
    tresult (TVST3_CALL* busInfo)(void*, int32, int32, int32, BusInfo&);
    tresult (TVST3_CALL* routing)(void*, RoutingInfo&, RoutingInfo&);
    tresult (TVST3_CALL* activateBus)(void*, int32, int32, int32, TBool);
    tresult (TVST3_CALL* active)(void*, TBool);
    tresult (TVST3_CALL* setState)(void*, IBStream*);
    tresult (TVST3_CALL* getState)(void*, IBStream*);
};
struct ProcessorVT {
    tresult (TVST3_CALL* qi)(void*, const TUID, void**);
    uint32 (TVST3_CALL* retain)(void*); uint32 (TVST3_CALL* drop)(void*);
    tresult (TVST3_CALL* setArr)(void*, SpeakerArrangement*, int32, SpeakerArrangement*, int32);
    tresult (TVST3_CALL* getArr)(void*, int32, int32, SpeakerArrangement&);
    tresult (TVST3_CALL* sampleSize)(void*, int32);
    uint32 (TVST3_CALL* latency)(void*);
    tresult (TVST3_CALL* setup)(void*, ProcessSetup&);
    tresult (TVST3_CALL* processing)(void*, TBool);
    tresult (TVST3_CALL* process)(void*, ProcessData&);
    uint32 (TVST3_CALL* tail)(void*);
};
struct ConnectionVT {
    tresult (TVST3_CALL* qi)(void*, const TUID, void**);
    uint32 (TVST3_CALL* retain)(void*); uint32 (TVST3_CALL* drop)(void*);
    tresult (TVST3_CALL* connect)(void*, IConnectionPoint*);
    tresult (TVST3_CALL* disconnect)(void*, IConnectionPoint*);
    tresult (TVST3_CALL* notify)(void*, IMessage*);
};
struct ControllerVT {
    tresult (TVST3_CALL* qi)(void*, const TUID, void**);
    uint32 (TVST3_CALL* retain)(void*); uint32 (TVST3_CALL* drop)(void*);
    tresult (TVST3_CALL* init)(void*, FUnknown*);
    tresult (TVST3_CALL* term)(void*);
    tresult (TVST3_CALL* componentState)(void*, IBStream*);
    tresult (TVST3_CALL* setState)(void*, IBStream*);
    tresult (TVST3_CALL* getState)(void*, IBStream*);
    int32 (TVST3_CALL* paramCount)(void*);
    tresult (TVST3_CALL* paramInfo)(void*, int32, ParameterInfo&);
    tresult (TVST3_CALL* valueString)(void*, uint32, double, String128);
    tresult (TVST3_CALL* stringValue)(void*, uint32, TChar*, double&);
    double (TVST3_CALL* normalizedPlain)(void*, uint32, double);
    double (TVST3_CALL* plainNormalized)(void*, uint32, double);
    double (TVST3_CALL* normalized)(void*, uint32);
    tresult (TVST3_CALL* setNormalized)(void*, uint32, double);
    tresult (TVST3_CALL* handler)(void*, IComponentHandler*);
    IPlugView* (TVST3_CALL* view)(void*, FIDString);
};
struct ViewVT {
    tresult (TVST3_CALL* qi)(void*, const TUID, void**);
    uint32 (TVST3_CALL* retain)(void*); uint32 (TVST3_CALL* drop)(void*);
    tresult (TVST3_CALL* supported)(void*, FIDString);
    tresult (TVST3_CALL* attached)(void*, void*, FIDString);
    tresult (TVST3_CALL* removed)(void*);
    tresult (TVST3_CALL* wheel)(void*, float);
    tresult (TVST3_CALL* key)(void*, TChar, int16, int16);
    tresult (TVST3_CALL* keyUp)(void*, TChar, int16, int16);
    tresult (TVST3_CALL* size)(void*, ViewRect*);
    tresult (TVST3_CALL* onSize)(void*, ViewRect*);
    tresult (TVST3_CALL* focus)(void*, TBool);
    tresult (TVST3_CALL* frame)(void*, FUnknown*);
    tresult (TVST3_CALL* resize)(void*);
    tresult (TVST3_CALL* constraints)(void*, ViewRect*);
};
struct MessageVT {
    tresult (TVST3_CALL* qi)(void*, const TUID, void**);
    uint32 (TVST3_CALL* retain)(void*); uint32 (TVST3_CALL* drop)(void*);
    FIDString (TVST3_CALL* id)(void*);
    void (TVST3_CALL* setId)(void*, FIDString);
    IAttributeList* (TVST3_CALL* attrs)(void*);
};
struct AttributesVT {
    tresult (TVST3_CALL* qi)(void*, const TUID, void**);
    uint32 (TVST3_CALL* retain)(void*); uint32 (TVST3_CALL* drop)(void*);
    tresult (TVST3_CALL* setInt)(void*, FIDString, int64);
    tresult (TVST3_CALL* getInt)(void*, FIDString, int64&);
    tresult (TVST3_CALL* setFloat)(void*, FIDString, double);
    tresult (TVST3_CALL* getFloat)(void*, FIDString, double&);
    tresult (TVST3_CALL* setString)(void*, FIDString, const TChar*);
    tresult (TVST3_CALL* getString)(void*, FIDString, TChar*, uint32);
    tresult (TVST3_CALL* setBinary)(void*, FIDString, const void*, uint32);
    tresult (TVST3_CALL* getBinary)(void*, FIDString, const void*&, uint32&);
};
extern const FactoryVT factoryVT;


inline tresult TVST3_CALL factoryInfo(void*, PFactoryInfo* p) {
    if (!p) return kInvalidArgument;
    zeroBytes(p, sizeof(*p));
    copyAscii(p->vendor, sizeof(p->vendor), TVST3Info::PluginVendor);
    copyAscii(p->url, sizeof(p->url), TVST3Info::PluginURL);
    copyAscii(p->email, sizeof(p->email), TVST3Info::PluginEmail);
    p->flags = 1 << 4;
    return kResultOk;
}
inline int32 TVST3_CALL factoryCount(void*) { return 2; }
inline tresult TVST3_CALL factoryClass(void*, int32 index, PClassInfo* info) {
    if (!info || (uint32)index >= 2) return kInvalidArgument;
    zeroBytes(info, sizeof(*info));
    copyBytes(info->cid, (index ? kControllerClassUID : kComponentClassUID).bytes, 16);
    info->cardinality = kManyInstances;
    copyAscii(info->category, sizeof(info->category), index ? "Component Controller Class" : "Audio Module Class");
    copyAscii(info->name, sizeof(info->name), TVST3Info::PluginName);
    return kResultOk;
}
inline tresult TVST3_CALL factoryClass2(void* s, int32 index, PClassInfo2* info) {
    if (!info || (uint32)index >= 2) return kInvalidArgument;
    zeroBytes(info, sizeof(*info));
    factoryClass(s, index, (PClassInfo*)info);
    copyAscii(info->subCategories, sizeof(info->subCategories), index ? "" :
        (TVST3Info::PluginSubCategory[0] ? TVST3Info::PluginSubCategory :
        (TVST3Info::Type == TVST3Info::PluginType::Instrument ? "Instrument|Synth" : "Fx")));
    copyAscii(info->vendor, sizeof(info->vendor), TVST3Info::PluginVendor);
    copyAscii(info->version, sizeof(info->version), TVST3Info::PluginVersion);
    copyAscii(info->sdkVersion, sizeof(info->sdkVersion), "VST 3.0");
    return kResultOk;
}

inline tresult TVST3_CALL coreInit(void* s, FUnknown*) {
    Core* c = (Core*)((Handle*)s)->owner;
    if (c->context) return kResultOk;
    void* dsp = CreateProcessInstance();
    if (!dsp) return kOutOfMemory;
    Context* ctx = (Context*)allocate(sizeof(Context));
    if (!ctx) { DestroyProcessInstance(dsp); return kOutOfMemory; }
    zeroBytes(ctx, sizeof(*ctx));
    ctx->refs = 1;
    ctx->dsp = dsp;
    c->context = ctx;
    return kResultOk;
}
inline tresult TVST3_CALL coreTerm(void* s) {
    Core* c = (Core*)((Handle*)s)->owner;
    release(c->context);
    c->context = nullptr;
    return kResultOk;
}
inline tresult TVST3_CALL coreId(void*, TUID id) {
    if (!id) return kInvalidArgument;
    copyBytes(id, kControllerClassUID.bytes, 16);
    return kResultOk;
}
inline tresult TVST3_CALL okInt(void*, int32) { return kResultOk; }
inline tresult TVST3_CALL okBool(void*, TBool) { return kResultOk; }
inline int32 TVST3_CALL coreBusCount(void*, int32 media, int32 direction) {
    if (media == 0 && direction == 1) return 1;
    if (media == 0 && direction == 0 && TVST3Info::Type == TVST3Info::PluginType::Effect) return 1;
    return media == 1 && direction == 0 && TVST3Info::AcceptsMIDI ? 1 : 0;
}
inline tresult TVST3_CALL coreBusInfo(void* s, int32 media, int32 dir, int32 index, BusInfo& b) {
    if (index || !coreBusCount(s, media, dir)) return kInvalidArgument;
    Core* c = (Core*)((Handle*)s)->owner;
    zeroBytes(&b, sizeof(b));
    b.mediaType = media;
    b.direction = dir;
    b.flags = 1;
    if (media == 1) {
        b.channelCount = 16;
        copyAsciiToUtf16(b.name, 128, "MIDI Input");
    } else {
        b.channelCount = countBits(dir ? c->outputArrangement : c->inputArrangement);
        copyAsciiToUtf16(b.name, 128, dir ? "Audio Output" : "Audio Input");
    }
    return kResultOk;
}
inline tresult TVST3_CALL coreRouting(void*, RoutingInfo&, RoutingInfo&) { return kNotImplemented; }
inline tresult TVST3_CALL coreActivate(void* s, int32 media, int32 dir, int32 index, TBool active) {
    if (index || !coreBusCount(s, media, dir)) return kInvalidArgument;
    Core* c = (Core*)((Handle*)s)->owner;
    if (media) c->midiActive = active;
    else if (dir) c->outputActive = active;
    else c->inputActive = active;
    return kResultOk;
}
inline tresult TVST3_CALL coreSave(void* s, IBStream* stream) {
    return saveState(((Core*)((Handle*)s)->owner)->context, stream);
}
inline tresult TVST3_CALL coreLoad(void* s, IBStream* stream) {
    return loadState(((Core*)((Handle*)s)->owner)->context, stream);
}
inline tresult TVST3_CALL procSetArr(void* s, SpeakerArrangement* in, int32 ni,
                                     SpeakerArrangement* out, int32 no) {
    bool synth = TVST3Info::Type == TVST3Info::PluginType::Instrument;
    if (!out || no != 1 || (synth ? ni != 0 : (!in || ni != 1 || in[0] != out[0])) ||
        !out[0] || countBits(out[0]) > 64) return kResultFalse;
    Core* c = (Core*)((Handle*)s)->owner;
    c->outputArrangement = out[0];
    if (!synth) c->inputArrangement = in[0];
    return kResultOk;
}
inline tresult TVST3_CALL procGetArr(void* s, int32 dir, int32 index, SpeakerArrangement& out) {
    if (index || (dir != 0 && dir != 1) || (dir == 0 && TVST3Info::Type == TVST3Info::PluginType::Instrument))
        return kInvalidArgument;
    Core* c = (Core*)((Handle*)s)->owner;
    out = dir ? c->outputArrangement : c->inputArrangement;
    return kResultOk;
}
inline tresult TVST3_CALL procSampleSize(void*, int32 x) { return x == 0 ? kResultOk : kResultFalse; }
inline uint32 TVST3_CALL zeroUnsigned(void*) { return 0; }
inline tresult TVST3_CALL procSetup(void* s, ProcessSetup& p) {
    Core* c = (Core*)((Handle*)s)->owner;
    if (!c->context || p.maxSamplesPerBlock <= 0 || p.sampleRate <= 0 || p.symbolicSampleSize != 0)
        return kInvalidArgument;
    Context* ctx = c->context;
    if (TVST3Info::AcceptsMIDI && !ctx->midi) {
        ctx->midi = (MidiMsg*)allocate(TVST3Info::MaxMidiEventsPerBlock * sizeof(MidiMsg));
        if (!ctx->midi) return kOutOfMemory;
    }
    ctx->maxSamples = p.maxSamplesPerBlock;
    if (ctx->rate != p.sampleRate) {
        ctx->rate = p.sampleRate;
        Prepare(ctx->dsp, p.sampleRate);
    }
    return kResultOk;
}
inline uint8 midiVelocity(float v) { return v <= 0 ? 0 : v >= 1 ? 127 : (uint8)(v * 127.f + .5f); }
inline tresult TVST3_CALL procProcess(void* s, ProcessData& p) {
    Core* c = (Core*)((Handle*)s)->owner;
    if (!c->context || p.symbolicSampleSize || p.numSamples < 0 ||
        p.numSamples > c->context->maxSamples || p.numOutputs < 1 || !p.outputs ||
        !p.outputs[0].channelBuffers32) return kResultFalse;
    AudioBusBuffers& o = p.outputs[0];
    if (o.numChannels < 1 || o.numChannels > 64) return kResultFalse;
    float** bufs = o.channelBuffers32;
    for (int ch = 0; ch < o.numChannels; ++ch) {
        float* dst = bufs[ch];
        if (!dst) return kResultFalse;
        const float* src = nullptr;
        if (TVST3Info::Type == TVST3Info::PluginType::Effect && p.numInputs && p.inputs &&
            p.inputs[0].channelBuffers32 && p.inputs[0].numChannels > ch &&
            !(p.inputs[0].silenceFlags & (uint64(1) << ch))) src = p.inputs[0].channelBuffers32[ch];
        if (src && src != dst) copyBytes(dst, src, (size_t)p.numSamples * 4);
        if (!src) zeroBytes(dst, (size_t)p.numSamples * 4);
    }
    int n = 0;
    if (TVST3Info::AcceptsMIDI && c->midiActive && c->context->midi && p.inputEvents) {
        int count = p.inputEvents->getEventCount();
        for (int i = 0; i < count && n < (int)TVST3Info::MaxMidiEventsPerBlock; ++i) {
            Event e{};
            if (p.inputEvents->getEvent(i, e) != kResultOk) continue;
            MidiMsg m{};
            m.sampleOffset = maximum(0, minimum(e.sampleOffset, maximum(0, p.numSamples - 1)));
            if (e.type == 0 || e.type == 1) {
                int chan = e.type == 0 ? e.noteOn.channel : e.noteOff.channel;
                int pitch = e.type == 0 ? e.noteOn.pitch : e.noteOff.pitch;
                float vel = e.type == 0 ? e.noteOn.velocity : e.noteOff.velocity;
                m.status = (uint8)((e.type == 0 ? 0x90 : 0x80) | (chan & 15));
                m.data1 = (uint8)maximum(0, minimum(pitch, 127));
                m.data2 = midiVelocity(vel);
            } else if (e.type == 3) {
                m.status = (uint8)(0xA0 | (e.polyPressure.channel & 15));
                m.data1 = (uint8)maximum(0, minimum((int)e.polyPressure.pitch, 127));
                m.data2 = midiVelocity(e.polyPressure.pressure);
            } else if (e.type == 65535) { // kLegacyMIDICCOutEvent (VST3)
                m.status = (uint8)(0xB0 | (e.midiCCOut.channel & 15));
                m.data1 = e.midiCCOut.controlNumber;
                m.data2 = (uint8)e.midiCCOut.value;
            } else continue;
            c->context->midi[n++] = m;
        }
    }
    ProcessBlock(c->context->dsp, bufs, o.numChannels, p.numSamples, c->context->midi, n);
    o.silenceFlags = 0;
    return kResultOk;
}

inline tresult TVST3_CALL cpConnect(void* s, IConnectionPoint* other);
inline tresult TVST3_CALL cpDisconnect(void* s, IConnectionPoint* other);
inline tresult TVST3_CALL cpNotify(void* s, IMessage* message);

inline tresult TVST3_CALL ctlInit(void*, FUnknown*) { return kResultOk; }
inline tresult TVST3_CALL ctlTerm(void* s) {
    Control* c = (Control*)((Handle*)s)->owner;
    release(c->context); c->context = nullptr;
    if (c->handler) c->handler->release();
    c->handler = nullptr;
    return kResultOk;
}
inline tresult TVST3_CALL ctlState(void*, IBStream*) { return kResultOk; }
inline int32 TVST3_CALL ctlParamCount(void*) { return 0; }
inline tresult TVST3_CALL ctlParamInfo(void*, int32, ParameterInfo&) { return kInvalidArgument; }
inline tresult TVST3_CALL ctlValueString(void*, uint32, double, String128) { return kNotImplemented; }
inline tresult TVST3_CALL ctlStringValue(void*, uint32, TChar*, double&) { return kNotImplemented; }
inline double TVST3_CALL ctlDouble(void*, uint32, double) { return 0.0; }
inline double TVST3_CALL ctlGetDouble(void*, uint32) { return 0.0; }
inline tresult TVST3_CALL ctlSetDouble(void*, uint32, double) { return kResultFalse; }
inline tresult TVST3_CALL ctlHandler(void* s, IComponentHandler* handler) {
    Control* c = (Control*)((Handle*)s)->owner;
    if (handler) handler->addRef();
    if (c->handler) c->handler->release();
    c->handler = handler;
    return kResultOk;
}
inline IPlugView* TVST3_CALL ctlView(void* s, FIDString name);

inline tresult TVST3_CALL viewSupported(void*, FIDString name) {
    return name && compareText(name, "HWND") == 0 ? kResultOk : kResultFalse;
}
inline tresult TVST3_CALL viewRemoved(void* s) {
    View* v = (View*)((Handle*)s)->owner;
    if (v->editor) { CloseEditor(v->editor); v->editor = nullptr; }
    return kResultOk;
}
inline tresult TVST3_CALL viewAttached(void* s, void* hwnd, FIDString name) {
    View* v = (View*)((Handle*)s)->owner;
    if (!hwnd || !v->context || viewSupported(s, name) != kResultOk) return kInvalidArgument;
    viewRemoved(s);
    v->editor = CreateEditorInstance(v->context->dsp);
    if (!v->editor) return kOutOfMemory;
    ShowEditor(v->editor, (HWND)hwnd);
    return kResultOk;
}
inline tresult TVST3_CALL viewWheel(void*, float) { return kResultFalse; }
inline tresult TVST3_CALL viewKey(void*, TChar, int16, int16) { return kResultFalse; }
inline tresult TVST3_CALL viewSize(void*, ViewRect* rect) {
    if (!rect) return kInvalidArgument;
    rect->left = rect->top = 0;
    rect->right = (int32)TVST3Info::EditorWidth;
    rect->bottom = (int32)TVST3Info::EditorHeight;
    return kResultOk;
}
inline tresult TVST3_CALL viewOnSize(void*, ViewRect* r) {
    if (!r) return kInvalidArgument;
    return r->right - r->left == (int32)TVST3Info::EditorWidth &&
           r->bottom - r->top == (int32)TVST3Info::EditorHeight ? kResultOk : kResultFalse;
}
inline tresult TVST3_CALL viewFrame(void*, FUnknown*) { return kResultOk; }
inline tresult TVST3_CALL viewConstraints(void*, ViewRect* r) {
    if (!r) return kInvalidArgument;
    r->right = r->left + (int32)TVST3Info::EditorWidth;
    r->bottom = r->top + (int32)TVST3Info::EditorHeight;
    return kResultOk;
}
inline tresult TVST3_CALL resultFalse(void*) { return kResultFalse; }

// Immutable vtables have no dynamic initialization and can be folded by the linker.

inline const ComponentVT componentVT {query, addref, unref, coreInit, coreTerm, coreId,
    okInt, coreBusCount, coreBusInfo, coreRouting, coreActivate, okBool, coreLoad, coreSave};
inline const ProcessorVT processorVT {query, addref, unref, procSetArr, procGetArr,
    procSampleSize, zeroUnsigned, procSetup, okBool, procProcess, zeroUnsigned};
inline const ConnectionVT connectionVT {query, addref, unref, cpConnect, cpDisconnect, cpNotify};
inline const ControllerVT controllerVT {query, addref, unref, ctlInit, ctlTerm,
    ctlState, ctlState, ctlState, ctlParamCount, ctlParamInfo, ctlValueString,
    ctlStringValue, ctlDouble, ctlDouble, ctlGetDouble, ctlSetDouble, ctlHandler, ctlView};
inline const ViewVT viewVT {query, addref, unref, viewSupported, viewAttached, viewRemoved,
    viewWheel, viewKey, viewKey, viewSize, viewOnSize, okBool, viewFrame,
    resultFalse, viewConstraints};
inline const MessageVT messageVT {linkQuery, linkRef, linkUnref, linkId, linkSetId, linkAttrs};
inline const AttributesVT attrVT {linkQuery, linkRef, linkUnref, linkSetInt, linkGetInt,
    linkSetFloat, linkGetFloat, linkSetString, linkGetString, linkSetBinary, linkGetBinary};

inline tresult TVST3_CALL query(void* self, const TUID iid, void** out) {
    if (!out) return kInvalidArgument;
    *out = nullptr;
    Handle* h = (Handle*)self;
    if (h->vtable == &factoryVT) {
        if (isUid(iid, kFUnknownUID) || isUid(iid, kPluginFactoryUID) || isUid(iid, kPluginFactory2UID))
            *out = self;
    } else if (h->vtable == &componentVT || h->vtable == &processorVT ||
               (h->vtable == &connectionVT && ((Core*)h->owner)->component.vtable == &componentVT)) {
        Core* c = (Core*)h->owner;
        if (isUid(iid, kComponentUID) || isUid(iid, kPluginBaseUID)) *out = &c->component;
        else if (isUid(iid, kAudioProcessorUID)) *out = &c->processor;
        else if (isUid(iid, kConnectionPointUID)) *out = &c->connection;
        else if (isUid(iid, kFUnknownUID)) *out = &c->component;
    } else if (h->vtable == &controllerVT || h->vtable == &connectionVT) {
        Control* c = (Control*)h->owner;
        if (isUid(iid, kEditControllerUID) || isUid(iid, kPluginBaseUID)) *out = &c->controller;
        else if (isUid(iid, kConnectionPointUID)) *out = &c->connection;
        else if (isUid(iid, kFUnknownUID)) *out = &c->controller;
    } else if (h->vtable == &viewVT) {
        if (isUid(iid, kFUnknownUID) || isUid(iid, kPlugViewUID)) *out = self;
    }
    if (!*out) return kNoInterface;
    addref(self);
    return kResultOk;
}
inline uint32 TVST3_CALL addref(void* self) {
    Handle* h = (Handle*)self;
    if (h->vtable == &factoryVT) return increment(&((Factory*)h->owner)->refs);
    if (h->vtable == &viewVT) return increment(&((View*)h->owner)->refs);
    if (h->vtable == &controllerVT || (h->vtable == &connectionVT &&
        ((Core*)h->owner)->component.vtable != &componentVT))
        return increment(&((Control*)h->owner)->refs);
    return increment(&((Core*)h->owner)->refs);
}
inline uint32 TVST3_CALL unref(void* self) {
    Handle* h = (Handle*)self;
    if (h->vtable == &factoryVT) return decrement(&((Factory*)h->owner)->refs);
    if (h->vtable == &viewVT) {
        View* v = (View*)h->owner;
        uint32 n = decrement(&v->refs);
        if (!n) { viewRemoved(self); release(v->context); deallocate(v); }
        return n;
    }
    if (h->vtable == &controllerVT || (h->vtable == &connectionVT &&
        ((Core*)h->owner)->component.vtable != &componentVT)) {
        Control* c = (Control*)h->owner;
        uint32 n = decrement(&c->refs);
        if (!n) { ctlTerm(self); deallocate(c); }
        return n;
    }
    Core* c = (Core*)h->owner;
    uint32 n = decrement(&c->refs);
    if (!n) { coreTerm(self); deallocate(c); }
    return n;
}
inline tresult TVST3_CALL cpConnect(void* self, IConnectionPoint* other) {
    Handle* h = (Handle*)self;
    if (!other) return kInvalidArgument;
    if (((Core*)h->owner)->component.vtable != &componentVT) return kResultOk;
    Core* c = (Core*)h->owner;
    if (!c->context) return kResultFalse;
    LinkMessage m{};
    m.message = {&messageVT, &m};
    m.attributes = {&attrVT, &m};
    m.ctx = c->context;
    return other->notify((IMessage*)&m.message);
}
inline tresult TVST3_CALL cpDisconnect(void* self, IConnectionPoint*) {
    Handle* h = (Handle*)self;
    if (((Core*)h->owner)->component.vtable == &componentVT) return kResultOk;
    Control* c = (Control*)h->owner;
    release(c->context); c->context = nullptr;
    return kResultOk;
}
inline tresult TVST3_CALL cpNotify(void* self, IMessage* msg) {
    Handle* h = (Handle*)self;
    if (((Core*)h->owner)->component.vtable == &componentVT) return kResultOk;
    if (!msg || !msg->getMessageID() || compareText(msg->getMessageID(), "tvst3.link")) return kResultFalse;
    IAttributeList* a = msg->getAttributes();
    if (!a) return kResultFalse;
    int64 raw = 0;
    tresult r = a->getInt("ctx", raw);
    a->release();
    if (r != kResultOk) return r;
    Control* c = (Control*)h->owner;
    Context* ctx = (Context*)(intptr_t)raw;
    if (ctx != c->context) { release(c->context); c->context = acquire(ctx); }
    return kResultOk;
}
inline IPlugView* TVST3_CALL ctlView(void* self, FIDString name) {
    Control* c = (Control*)((Handle*)self)->owner;
    if (!TVST3Info::HasEditor || !c->context || !name || compareText(name, "editor")) return nullptr;
    View* v = (View*)allocate(sizeof(View));
    if (!v) return nullptr;
    zeroBytes(v, sizeof(*v));
    v->view = {&viewVT, v};
    v->refs = 1;
    v->context = acquire(c->context);
    return (IPlugView*)&v->view;
}
inline tresult TVST3_CALL factoryMake(void*, FIDString cid, FIDString iid, void** out) {
    if (!out || !cid || !iid) return kInvalidArgument;
    *out = nullptr;
    if (isUid(cid, kComponentClassUID)) {
        Core* c = (Core*)allocate(sizeof(Core));
        if (!c) return kOutOfMemory;
        zeroBytes(c, sizeof(*c));
        c->component = {&componentVT, c};
        c->processor = {&processorVT, c};
        c->connection = {&connectionVT, c};
        c->refs = 1;
        c->inputArrangement = c->outputArrangement = 3; // stereo
        c->inputActive = c->outputActive = c->midiActive = 1;
        tresult r = query(&c->component, iid, out);
        unref(&c->component);
        return r;
    }
    if (isUid(cid, kControllerClassUID)) {
        Control* c = (Control*)allocate(sizeof(Control));
        if (!c) return kOutOfMemory;
        zeroBytes(c, sizeof(*c));
        c->controller = {&controllerVT, c};
        c->connection = {&connectionVT, c};
        c->refs = 1;
        tresult r = query(&c->controller, iid, out);
        unref(&c->controller);
        return r;
    }
    return kInvalidArgument;
}
// This definition is compile-time constant: no global C++ constructors.
inline const FactoryVT factoryVT {query, addref, unref, factoryInfo, factoryCount,
    factoryClass, factoryMake, factoryClass2};
inline Factory factoryObject {{&factoryVT, &factoryObject}, 0};

} // namespace tvst3_detail

// Do not override operator new/delete. The user's .cpp retains normal C++
// runtime and can link the STL, exceptions, etc. when desired.
extern "C" {
TVST3_EXPORT void* TVST3_CALL GetPluginFactory() {
    // factoryVT and factoryVT have the same interface identity.
    tvst3_detail::addref(&tvst3_detail::factoryObject.factory);
    return &tvst3_detail::factoryObject.factory;
}
TVST3_EXPORT bool TVST3_CALL InitDll() { return true; }
TVST3_EXPORT bool TVST3_CALL ExitDll() { return true; }
}
#endif // !TVST3_NO_IMPLEMENTATION
