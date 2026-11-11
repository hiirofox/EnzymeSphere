#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include "PianoDWG2.h"
#include "PianoDWGParams.h"

namespace PianoDWGParams = PianoDWGParams_Piano1;

class PianoDWGSynth {
public:
    static constexpr float DwgSampleRate = PianoDWG2::SampleRate;
    static constexpr int NumPolys = 16;
    float hostSampleRate = 48000.f;
private:
    using Model = PianoDWG2::PianoDWG2Voice<float>;
    static_assert(Model::NumParams == PianoDWGParams::NumParams, "Checkpoint layout mismatch");
    struct Stereo {
        float left = 0, right = 0;
    };
    struct Voice {
        Model model;
        int note = -1, channel = 0;
        bool ui = false, held = false, released = false;
        uint64_t age = 0;
        unsigned quiet = 0, samples = 0;
    };
    std::unique_ptr<Voice[]> voices{new Voice[NumPolys]};
    bool sustain[16]{};
    uint64_t clock = 0;
    double phase = 0;
    Stereo previous, current;
    std::atomic<unsigned> uiStrikes[PianoDWGParams::NumNotes]{};
    std::atomic<int> uiVelocity[PianoDWGParams::NumNotes]{}, keyLights[PianoDWGParams::NumNotes]{};
    unsigned seenStrikes[PianoDWGParams::NumNotes]{};
    bool uiHeld[PianoDWGParams::NumNotes]{};
    std::atomic<bool> uiPedal{false}, pedalLight{false};
    bool lastUiPedal = false;
    void Release(Voice& v) {
        if (!v.released) { v.model.NoteOff(); v.released = true; }
    }
    void NoteOn(int note, int velocity, int channel, bool ui) {
        const int row = PianoDWGParams::RowForNote(note);
        if (row < 0 || velocity <= 0) return;
        Voice* chosen = nullptr;
        for (int i = 0; i < NumPolys; ++i)
            if (voices[i].note < 0) { chosen = &voices[i]; break; }
        if (!chosen) {
            chosen = &voices[0];
            for (int i = 1; i < NumPolys; ++i) {
                auto& v = voices[i];
                if ((v.released && !chosen->released) ||
                    (v.released == chosen->released && v.age < chosen->age)) chosen = &v;
            }
        }
        auto& v = *chosen;
        // VecToParams accepts mutable memory; preserve the embedded parameter table.
        float params[Model::NumParams];
        std::memcpy(params, PianoDWGParams::Params[row], sizeof(params));
        v.model.VecToParams(params);
        v.model.Reset(); v.model.NoteOn(float(velocity) / 127.f);
        v.note = note; v.channel = channel; v.ui = ui;
        v.held = true; v.released = false; v.age = ++clock;
        v.quiet = v.samples = 0;
    }
    void NoteOff(int note, int channel, bool ui) {
        for (int i = 0; i < NumPolys; ++i) {
            auto& v = voices[i];
            if (v.note == note && v.channel == channel && v.ui == ui && v.held) {
                v.held = false;
                if (!(ui ? lastUiPedal : sustain[channel])) Release(v);
            }
        }
    }
    void Pedal(bool down, int channel, bool ui) {
        if (ui) lastUiPedal = down; else sustain[channel] = down;
        if (!down) for (int i = 0; i < NumPolys; ++i) {
            auto& v = voices[i];
            if (v.note >= 0 && v.ui == ui && v.channel == channel && !v.held) Release(v);
        }
    }
    Stereo Render() {
        Stereo sum;
        for (int i = 0; i < NumPolys; ++i) {
            auto& v = voices[i];
            if (v.note < 0) continue;
            const auto [left, right] = v.model.ProcessSample();
            if (!std::isfinite(left) || !std::isfinite(right)) { v.note = -1; continue; }
            ++v.samples;
            v.quiet = std::max(std::fabs(left), std::fabs(right)) < 1.e-7f ? v.quiet + 1 : 0;
            if (v.released && (v.quiet > 4096 || v.samples > unsigned(DwgSampleRate * 60))) {
                v.note = -1; continue;
            }
            sum.left += left;
            sum.right += right;
        }
        return {sum.left * 0.2f, sum.right * 0.2f};
    }
    void PollUI() {
        const bool pedal = uiPedal.load(std::memory_order_acquire);
        if (pedal != lastUiPedal) Pedal(pedal, 0, true);
        for (int i = 0; i < PianoDWGParams::NumNotes; ++i) {
            const int note = PianoDWGParams::MidiNotes[i];
            const unsigned strikes = uiStrikes[i].load(std::memory_order_acquire);
            const int velocity = uiVelocity[i].load(std::memory_order_acquire);
            if (strikes != seenStrikes[i]) {
                NoteOff(note, 0, true);
                NoteOn(note, velocity > 0 ? velocity : 100, 0, true);
                uiHeld[i] = true; seenStrikes[i] = strikes;
            }
            if (uiHeld[i] && !velocity) { NoteOff(note, 0, true); uiHeld[i] = false; }
        }
    }
public:
    PianoDWGSynth() = default;
    PianoDWGSynth(const PianoDWGSynth&) = delete;
    PianoDWGSynth& operator=(const PianoDWGSynth&) = delete;
    void Prepare(double rate) {
        hostSampleRate = std::isfinite(rate) && rate > 0 ? float(rate) : 48000.f;
        for (int i = 0; i < NumPolys; ++i) { voices[i].note = -1; voices[i].model.Reset(); }
        phase = 0; previous = current = Stereo{};
        for (bool& down : sustain) down = false;
        for (int i = 0; i < PianoDWGParams::NumNotes; ++i) {
            uiHeld[i] = false;
            seenStrikes[i] = uiStrikes[i].load(std::memory_order_acquire);
            if (uiVelocity[i].load(std::memory_order_acquire) > 0) --seenStrikes[i];
        }
    }
    void UIKey(int note, int velocity) {
        const int row = PianoDWGParams::RowForNote(note);
        if (row < 0) return;
        uiVelocity[row].store(velocity, std::memory_order_release);
        if (velocity > 0) uiStrikes[row].fetch_add(1, std::memory_order_release);
    }
    void UIPedal(bool down) { uiPedal.store(down, std::memory_order_release); }
    bool KeyLit(int note) const {
        const int row = PianoDWGParams::RowForNote(note);
        return row >= 0 && keyLights[row].load(std::memory_order_relaxed) != 0;
    }
    bool PedalLit() const { return pedalLight.load(std::memory_order_relaxed); }
    // Independent of tvst3: a message needs only sampleOffset/status/data1/data2.
    template<class MidiMessage>
    void ProcessBlock(float** buffers, int channels, int samples, const MidiMessage* events, int count) {
        PollUI();
        int event = 0;
        for (int s = 0; s < samples; ++s) {
            while (events && event < count && events[event].sampleOffset <= s) {
                const auto& m = events[event++];
                const int type = m.status & 0xf0, channel = m.status & 15;
                if (type == 0x90 && m.data2) NoteOn(m.data1, m.data2, channel, false);
                else if (type == 0x80 || type == 0x90) NoteOff(m.data1, channel, false);
                else if (type == 0xb0 && m.data1 == 64) Pedal(m.data2 >= 64, channel, false);
            }
            // Continuous linear resampling preserves tuning across host block boundaries.
            phase += double(DwgSampleRate) / hostSampleRate;
            while (phase >= 1) { previous = current; current = Render(); phase -= 1; }
            const float left = previous.left + float(phase) * (current.left - previous.left);
            const float right = previous.right + float(phase) * (current.right - previous.right);
            if (channels == 1) buffers[0][s] = std::clamp(0.5f * left + 0.5f * right, -1.f, 1.f);
            if (channels >= 2) {
                buffers[0][s] = std::clamp(left, -1.f, 1.f);
                buffers[1][s] = std::clamp(right, -1.f, 1.f);
            }
            for (int ch = 2; ch < channels; ++ch) buffers[ch][s] = 0;
        }
        bool lights[PianoDWGParams::NumNotes]{};
        for (int i = 0; i < NumPolys; ++i) {
            const auto& v = voices[i];
            if (!v.released) {
                const int row = PianoDWGParams::RowForNote(v.note);
                if (row >= 0) lights[row] = true;
            }
        }
        for (int i = 0; i < PianoDWGParams::NumNotes; ++i)
            keyLights[i].store(lights[i], std::memory_order_relaxed);
        bool pedal = lastUiPedal;
        for (bool down : sustain) pedal = pedal || down;
        pedalLight.store(pedal, std::memory_order_relaxed);
    }
    int SaveData(char*, int) const { return 0; }
    void LoadData(const char*, int) {} // Fixed preset, no persistent controls.
};
