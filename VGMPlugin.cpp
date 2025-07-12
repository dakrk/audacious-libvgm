#include <cstring>
#include <libaudcore/audstrings.h>
#include <libaudcore/threads.h> // cannot be included after i18n!
#include <libaudcore/i18n.h>
#include <libaudcore/plugin.h>
#include <libaudcore/preferences.h>
#include <libaudcore/runtime.h>
#include <player/playera.hpp> // libvgm, please put yourself in a directory
#include <player/vgmplayer.hpp>
#include <player/s98player.hpp>
#include <player/droplayer.hpp>
#include <player/gymplayer.hpp>
#include "VGMPlugin.hpp"
#include "VFSLoader.hpp"

#define NUM_SAMPLES 2048
#define NUM_CHANNELS 2
#define CFG_SECTION "libvgm"

EXPORT VGMPlugin aud_plugin_instance;

const char VGMPlugin::about[] =
	"libvgm is by Valley Bell and contributors: https://github.com/ValleyBell/libvgm\n"
	"Audacious plugin by DarkOK: https://github.com/dakrk/audacious-libvgm";

const char *const VGMPlugin::exts[] = {
	"vgm", "vgz", "s98", "dro", "gym", nullptr
};

const char *const VGMPlugin::defaults[] = {
	// Audio
	"sample_rate",       "48000",
	"bit_depth",         "16",

	// Duration
	"loop_count",        "2",
	"fade_time",         "5000",
	"end_silence",       "1000",
	"loop_end_silence",  "0",

	// Tagging
	"untranslated_tags", "FALSE",
	nullptr
};

const ComboItem VGMPlugin::bit_depth_widgets[] = {
	{ "8", 8 },
	{ "16", 16 },
	{ "24", 24 },
	{ "32", 32 },
};

/**
 * TODO: I believe this table can be better, it appears you can pass a
 * reference to a config value instead of repeating the same WidgetInt stuff
 */
const PreferencesWidget VGMPlugin::widgets[] = {
	WidgetLabel(N_("<b>Audio</b>")),

	WidgetSpin(
		N_("Sample rate:"),
		WidgetInt(CFG_SECTION, "sample_rate"),
		{ 0, 96000, 1, "Hz" }
	),

	WidgetCombo(
		N_("Bit depth:"),
		WidgetInt(CFG_SECTION, "bit_depth"),
		{ bit_depth_widgets }
	),

	/**
	 * TODO (to match vgmplay):
	 * - chip_rate
	 * - resampling_mode
	 * - chip_sampling_mode
	 * - playback_rate
	 * - volume
	 * Channel muting and other such chip configuration might not be possible
	 * (atleast in a sane manner) due to how limited Audacious' preferences
	 * API is.
	 */

	WidgetLabel(N_("<b>Duration</b>")),

	WidgetSpin(
		N_("Loop count:"),
		WidgetInt(CFG_SECTION, "loop_count"),
		{ 0, 100, 1, N_("times (0 = infinite)") }
	),

	WidgetSpin(
		N_("Fade time:"),
		WidgetInt(CFG_SECTION, "fade_time"),
		{ 0, 30000, 500, N_("ms") }
	),

	WidgetSpin(
		N_("End silence:"),
		WidgetInt(CFG_SECTION, "end_silence"),
		{ 0, 10000, 500, N_("ms") }
	),

	WidgetSpin(
		N_("Loop end silence:"),
		WidgetInt(CFG_SECTION, "loop_end_silence"),
		{ 0, 10000, 500, N_("ms") }
	),

	WidgetLabel(N_("<b>Tagging</b>")),

	WidgetCheck(
		N_("Prefer untranslated text"),
		WidgetBool(CFG_SECTION, "untranslated_tags")
	)
};

/**
 * I don't think putting load_settings in cleanup is ideal?
 * There's an apply function, but I must need to rework settings to use
 * whatever reference stuff, because at the moment that just adds an apply
 * button on a dialog that will apply regardless.
 */
const PluginPreferences VGMPlugin::prefs = {
	{ widgets },
	nullptr,
	nullptr,
	load_settings
};

bool VGMPlugin::init()
{
	aud_config_set_defaults(CFG_SECTION, defaults);
	load_settings();

	/**
	 * It seems to take quite a lot for a Player with its engines to init, so
	 * we keep one available at all times that we can use for tag reading.
	 * Wastes space, but not much that can be done about that.
	 */
	probe_player = new PlayerA;
	probe_player->RegisterPlayerEngine(new VGMPlayer);
	probe_player->RegisterPlayerEngine(new S98Player);
	probe_player->RegisterPlayerEngine(new DROPlayer);
	probe_player->RegisterPlayerEngine(new GYMPlayer);

	main_player = new PlayerA;
	main_player->RegisterPlayerEngine(new VGMPlayer);
	main_player->RegisterPlayerEngine(new S98Player);
	main_player->RegisterPlayerEngine(new DROPlayer);
	main_player->RegisterPlayerEngine(new GYMPlayer);
	main_player->SetEventCallback(event_callback, this);
	main_player->SetFileReqCallback(file_callback, this);
	main_player->SetLogCallback(log_callback, this);
	return true;
}

void VGMPlugin::cleanup()
{
	delete probe_player;
	delete main_player;
	delete sample_buffer;
}

bool VGMPlugin::is_our_file(const char *filename, VFSFile &file)
{
	DATA_LOADER *loader = VFSLoader_Init(file);
	DataLoader_SetPreloadBytes(loader, 0x40);

	/**
	 * HACK: libvgm's data loader abstraction requires you retrieve the total
	 * file length at the time of loading the file.
	 * 
	 * This works fine with Audacious for VGM files as a simple VFS fsize call
	 * should work, but this doesn't work with VGZ files, as to get a gzip
	 * file's uncompressed size you need to seek to the end.
	 * 
	 * Seeking to the end when probing doesn't work, as fseek is bound by the
	 * VFS ProbeBuffer's current buffer size. Thus, we try and disable that.
	 * 
	 * As disabling set_limit_to_buffer impacts the behaviour of subsequent
	 * plugins in the probe cycle, potentially causing issues for those
	 * developing plugins (works on one install, not on another), we reset it
	 * back to true if this isn't our file.
	 */
	file.set_limit_to_buffer(false);

	if (DataLoader_Load(loader))
	{
		DataLoader_Deinit(loader);
		file.set_limit_to_buffer(true);
		return false;
	}

	for (PlayerBase *p : probe_player->GetRegisteredPlayers())
	{
		if (!p->CanLoadFile(loader))
		{
			DataLoader_Deinit(loader);
			return true;
		}
	}

	DataLoader_Deinit(loader);
	file.set_limit_to_buffer(true);
	return false;
}

bool VGMPlugin::read_tag(const char *filename, VFSFile &file, Tuple &tuple, Index<char> *image)
{
	DATA_LOADER *loader;
	PlayerBase *engine;
	StringBuf comment;
	bool success;

	loader = VFSLoader_Init(file);
	DataLoader_SetPreloadBytes(loader, 0x100);
	success = false;

	if (DataLoader_Load(loader))
		goto cleanup;

	if (probe_player->LoadFile(loader))
		goto cleanup;

	apply_player_settings(probe_player);
	engine = probe_player->GetPlayer();

	for (const char *const *tag = engine->GetTags(); *tag; tag += 2)
	{
		if (!config.untranslated_tags)
		{
			if (!strcmp(tag[0], "TITLE"))
			{
				tuple.set_str(Tuple::Title, tag[1]);
			}
			else if (!strcmp(tag[0], "GAME"))
			{
				tuple.set_str(Tuple::Album, tag[1]);
			}
			else if (!strcmp(tag[0], "SYSTEM"))
			{
				str_append_printf(comment, "System: %s\n", tag[1]);
			}
			else if (!strcmp(tag[0], "ARTIST"))
			{
				tuple.set_str(Tuple::Artist, tag[1]);
			}
		}
		else
		{
			if (!strcmp(tag[0], "TITLE-JPN"))
			{
				tuple.set_str(Tuple::Title, tag[1]);
			}
			else if (!strcmp(tag[0], "GAME-JPN"))
			{
				tuple.set_str(Tuple::Album, tag[1]);
			}
			else if (!strcmp(tag[0], "SYSTEM-JPN"))
			{
				str_append_printf(comment, "System: %s\n", tag[1]);
			}
			else if (!strcmp(tag[0], "ARTIST-JPN"))
			{
				tuple.set_str(Tuple::Artist, tag[1]);
			}
		}

		if (!strcmp(tag[0], "DATE"))
		{
			tuple.set_str(Tuple::Date, tag[1]);
		}
		else if (!strcmp(tag[0], "ENCODED_BY"))
		{
			str_append_printf(comment, "Encoded by: %s\n", tag[1]);
		}
		else if (!strcmp(tag[0], "COMMENT"))
		{
			comment.insert(-1, tag[1], -1);
		}
	}

	tuple.set_str(Tuple::Comment, comment);
	tuple.set_int(Tuple::Length, probe_player->GetTotalTime(PLAYTIME_LOOP_INCL | PLAYTIME_TIME_PBK | PLAYTIME_WITH_FADE | PLAYTIME_WITH_SLNC) * 1000);
	// Tuple::Codec = "libvgm {PlayerName} ({chips})"?
	success = true;

cleanup:
	probe_player->UnloadFile();
	DataLoader_Deinit(loader);
	return success;
}

bool VGMPlugin::play(const char *filename, VFSFile &file)
{
	DATA_LOADER *loader;
	bool success;
	UINT8 ret;

	loader = VFSLoader_Init(file);
	DataLoader_SetPreloadBytes(loader, 0x100);

	success = false;

	if ((ret = DataLoader_Load(loader)))
	{
		AUDERR("Failed to open file (error %x)\n", ret);
		goto cleanup;
	}

	if ((ret = main_player->SetOutputSettings(config.sample_rate, NUM_CHANNELS, config.bit_depth, NUM_SAMPLES)))
	{
		AUDERR("Failed to set output settings (error %x)\n", ret);
		goto cleanup;
	}

	if ((ret = main_player->LoadFile(loader)))
	{
		AUDERR("Failed to load file (error %x)\n", ret);
		goto cleanup;
	}

	apply_player_settings(main_player);

	if ((ret = main_player->Start()))
	{
		AUDERR("Failed to start playback (error %x)\n", ret);
		goto cleanup;
	}

	open_audio(get_aud_fmt(config.bit_depth), config.sample_rate, NUM_CHANNELS);

	while (!check_stop() && !song_ended)
	{
		int seek_value = check_seek();
		if (seek_value >= 0)
		{
			UINT32 seek_sample = main_player->GetSampleRate() * (seek_value / 1000.f);
			if (main_player->Seek(PLAYPOS_SAMPLE, seek_sample))
			{
				goto cleanup;
			}
		}

		UINT32 rendered_bytes = main_player->Render(sample_buffer_size, sample_buffer);
		write_audio(sample_buffer, rendered_bytes);
	}

	success = true;

cleanup:
	main_player->UnloadFile();
	DataLoader_Deinit(loader);
	return success;
}

void VGMPlugin::load_settings()
{
	VGMPlugin &self = aud_plugin_instance;

	// Audio
	self.config.sample_rate = aud_get_int(CFG_SECTION, "sample_rate");
	self.config.bit_depth = aud_get_int(CFG_SECTION, "bit_depth");

	// Duration
	self.config.loop_count = aud_get_int(CFG_SECTION, "loop_count");
	self.config.fade_time = aud_get_int(CFG_SECTION, "fade_time");
	self.config.end_silence = aud_get_int(CFG_SECTION, "end_silence");
	self.config.loop_end_silence = aud_get_int(CFG_SECTION, "loop_end_silence");

	// Tagging
	self.config.untranslated_tags = aud_get_bool(CFG_SECTION, "untranslated_tags");

	self.allocate_sample_buffer();
}

void VGMPlugin::apply_player_settings(PlayerA *player)
{
	player->SetLoopCount(config.loop_count);
	player->SetFadeSamples(probe_player->GetSampleRate() * (config.fade_time / 1000.f));
	UINT32 end_silence = (main_player->GetLoopTime() > 0) ? config.loop_end_silence : config.end_silence;
	player->SetEndSilenceSamples(main_player->GetSampleRate() * (end_silence / 1000.f));
}

void VGMPlugin::allocate_sample_buffer()
{
	aud::mutex mutex; // to account for configuring during playback
	mutex.lock();
	sample_buffer_size = (config.bit_depth / 8) * NUM_SAMPLES * NUM_CHANNELS;
	delete sample_buffer;
	sample_buffer = new UINT8[sample_buffer_size];
	mutex.unlock();
}

int VGMPlugin::get_aud_fmt(UINT8 bit_depth)
{
	switch (bit_depth)
	{
	case 8:
		return FMT_U8;
	case 16:
		return FMT_S16_NE;
	case 24:
		return FMT_S24_NE;
	case 32:
		return FMT_S32_NE;
	default:
		return -1;
	}
}

UINT8 VGMPlugin::event_callback(PlayerBase *player, void *user_param, UINT8 event_type, void *event_param)
{
	VGMPlugin *self = (VGMPlugin *)user_param;

	switch (event_type)
	{
	case PLREVT_START:
		self->song_ended = false;
		break;

	case PLREVT_END:
		self->song_ended = true;
		break;
	}

	return 0;
}

DATA_LOADER *VGMPlugin::file_callback(void *user_param, PlayerBase *player, const char *filename)
{
	AUDERR("Failed to load %s; file requests are unimplemented!", filename);
	return nullptr; // TODO (fortunately very uncommon)
}

void VGMPlugin::log_callback(void *user_param, PlayerBase *player, UINT8 level, UINT8 src_type, const char *src_tag, const char *message)
{
	audlog::Level aud_level;

	switch (level)
	{
	case PLRLOG_ERROR:
		aud_level = audlog::Error;
		break;

	case PLRLOG_WARN:
		aud_level = audlog::Warning;
		break;

	case PLRLOG_INFO:
		aud_level = audlog::Info;
		break;

	default:
		aud_level = audlog::Debug;
		break;
	}

	if (src_tag)
	{
		audlog::log(aud_level, __FILE__, __LINE__, __FUNCTION__, "[%s]: %s", src_tag, message);
	}
	else
	{
		audlog::log(aud_level, __FILE__, __LINE__, __FUNCTION__, "%s", message);
	}
}
