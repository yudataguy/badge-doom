//
// Copyright(C) 2026
//
// Silent Pico sound backend for badge scaffolding.
//

#include "config.h"

#include "i_sound.h"
#include "pico/i_picosound.h"

static snddevice_t null_sound_devices[] = { SNDDEVICE_SB };

static boolean null_sound_init(boolean use_sfx_prefix)
{
    (void) use_sfx_prefix;
    return true;
}

static void null_sound_shutdown(void)
{
}

static int null_sound_get_sfx_lump_num(should_be_const sfxinfo_t *sfxinfo)
{
    (void) sfxinfo;
    return 0;
}

static void null_sound_update(void)
{
}

static void null_sound_update_params(int channel, int vol, int sep)
{
    (void) channel;
    (void) vol;
    (void) sep;
}

static int null_sound_start(should_be_const sfxinfo_t *sfxinfo,
                            int channel,
                            int vol,
                            int sep,
                            int pitch)
{
    (void) sfxinfo;
    (void) channel;
    (void) vol;
    (void) sep;
    (void) pitch;
    return channel;
}

static void null_sound_stop(int channel)
{
    (void) channel;
}

static boolean null_sound_is_playing(int channel)
{
    (void) channel;
    return false;
}

static void null_sound_cache(should_be_const sfxinfo_t *sounds, int num_sounds)
{
    (void) sounds;
    (void) num_sounds;
}

sound_module_t sound_pico_module =
{
    null_sound_devices,
    arrlen(null_sound_devices),
    null_sound_init,
    null_sound_shutdown,
    null_sound_get_sfx_lump_num,
    null_sound_update,
    null_sound_update_params,
    null_sound_start,
    null_sound_stop,
    null_sound_is_playing,
    null_sound_cache,
};

void I_PicoSoundFade(bool in)
{
    (void) in;
}

bool I_PicoSoundFading(void)
{
    return false;
}
