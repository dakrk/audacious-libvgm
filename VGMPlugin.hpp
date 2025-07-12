#pragma once
#include <libaudcore/i18n.h>
#include <libaudcore/plugin.h>
#include <libaudcore/preferences.h>
#include <player/playera.hpp>
#include <utils/DataLoader.h>

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT __attribute__((visibility("default")))
#endif

class VGMPlugin : public InputPlugin
{
public:
	static const char about[];
	static const char *const exts[];
	static const char *const defaults[];
	static const ComboItem bit_depth_widgets[];
	static const PreferencesWidget widgets[];
	static const PluginPreferences prefs;

	static constexpr PluginInfo info = {
		N_("libvgm Decoder"),
		N_("libvgm"),
		about,
		&prefs,
		0
	};

	static constexpr InputInfo iinfo = InputInfo()
		.with_priority(_AUD_PLUGIN_DEFAULT_PRIO - 1) // go before GME
		.with_exts(exts);

	constexpr VGMPlugin() :
		InputPlugin(info, iinfo),
		probe_player(nullptr),
		main_player(nullptr) {}

	bool init();
	void cleanup();

	bool is_our_file(const char *filename, VFSFile &file);
	bool read_tag(const char *filename, VFSFile &file, Tuple &tuple, Index<char> *image);
	bool play(const char *filename, VFSFile &file);

private:
	static void load_settings();
	void apply_player_settings(PlayerA *player);
	void allocate_sample_buffer();

	static int get_aud_fmt(UINT8 bit_depth);

	static UINT8 event_callback(PlayerBase *player, void *user_param, UINT8 event_type, void *event_param);
	static DATA_LOADER *file_callback(void *user_param, PlayerBase *player, const char *filename);
	static void log_callback(void *user_param, PlayerBase *player, UINT8 level, UINT8 src_type, const char *src_tag, const char *message);

	struct Config {
		// Audio
		UINT32 sample_rate;
		UINT8 bit_depth;

		// Duration
		UINT8 loop_count;
		UINT32 fade_time;
		UINT32 end_silence;
		UINT32 loop_end_silence;

		// Tagging
		bool untranslated_tags;
	} config;

	PlayerA *probe_player;
	PlayerA *main_player;
	UINT32 sample_buffer_size;
	UINT8 *sample_buffer;
	bool song_ended;
};
