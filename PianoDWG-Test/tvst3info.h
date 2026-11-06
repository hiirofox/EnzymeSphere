#pragma once

#include <stdint.h>

namespace TVST3Info {

	enum class PluginType : uint8_t {
		Effect,
		Instrument
	};

	// Edit these values for each plug-in. Keep both UIDs unchanged after release.
	inline constexpr char PluginName[] = "PianoDWG test";
	inline constexpr char PluginVersion[] = "1.0.0";
	inline constexpr char PluginVendor[] = "our Company";
	inline constexpr char PluginURL[] = "pornhub.com";
	inline constexpr char PluginEmail[] = "";

	inline constexpr PluginType Type = PluginType::Instrument;
	inline constexpr bool AcceptsMIDI = true;
	inline constexpr bool HasEditor = true;
	inline constexpr char PluginSubCategory[] = "";

	inline constexpr uint32_t EditorWidth = 640;
	inline constexpr uint32_t EditorHeight = 480;
	inline constexpr uint32_t MaxMidiEventsPerBlock = 4096;

	// These are example IDs. Generate two globally unique 128-bit IDs before shipping.
	// The component and controller IDs must be different from each other.
	inline constexpr uint32_t PluginUID[4] = {
		0x12345679, 0x7B6C4D11, 0xA9623C74, 0x5E200101
	};

	inline constexpr uint32_t ControllerUID[4] = {
		0xA38E4129, 0x7B6C4D11, 0xA9623C74, 0x5E200102
	};

} // namespace TVST3Info
