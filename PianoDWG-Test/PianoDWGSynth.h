#pragma once
#include "PianoDWG.h"

class PianoDWGSynth {
public:
	constexpr static float DwgSampleRate = 22050.;//由于训练数据就是22050，所以定死22050，输出时再转换采样率即可
	float hostSampleRate = 48000.;//宿主采样率
	constexpr static int NumPolys = 16;//复音
	PianoDWG::PianoWaveguide<float>* dwgs;
	PianoDWGSynth() {
		dwgs = new PianoDWG::PianoWaveguide<float>[NumPolys];
	}

};

class PianoDWGEditor
{
public:
	PianoDWGSynth& proc;
	PianoDWGEditor(PianoDWGSynth& p) : proc(p) {}
};