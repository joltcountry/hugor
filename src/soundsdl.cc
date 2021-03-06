/* Copyright 2015 Nikos Chantziaras
 *
 * This file is part of Hugor.
 *
 * Hugor is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * Hugor is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * Hugor.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Additional permission under GNU GPL version 3 section 7
 *
 * If you modify this Program, or any covered work, by linking or combining it
 * with the Hugo Engine (or a modified version of the Hugo Engine), containing
 * parts covered by the terms of the Hugo License, the licensors of this
 * Program grant you additional permission to convey the resulting work.
 * Corresponding Source for a non-source form of such a combination shall
 * include the source code for the parts of the Hugo Engine used as well as
 * that of the covered work.
 */
#include <SDL.h>
#include <SDL_mixer.h>
#include <QDebug>
#include <QFile>
#include <cstdio>
#include <cmath>

extern "C" {
#include "heheader.h"
}
#include "happlication.h"
#include "settings.h"
#include "rwopsbundle.h"
#include "hugohandlers.h"
#include "hugodefs.h"
#include "hugorfile.h"


// Current music and sample volumes. Needed to restore the volumes
// after muting them.
static int currentMusicVol = 100;
static int currentSampleVol = 100;
static bool isMuted = false;


void
initSoundEngine()
{
    // Initialize only the audio part of SDL.
    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        qWarning("Unable to initialize sound system: %s", SDL_GetError());
        exit(1);
    }

    // This will preload the needed codecs now instead of constantly loading
    // and unloading them each time a sound is played/stopped.
    int sdlFormats = MIX_INIT_MP3 | MIX_INIT_MOD;
    if (Mix_Init((sdlFormats & sdlFormats) != sdlFormats)) {
        qWarning("Unable to load MP3 and/or MOD audio formats: %s", Mix_GetError());
        exit(1);
    }

    // Initialize the mixer. 44.1kHz, default sample format,
    // 2 channels (stereo) and a 4k chunk size.
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 4096) != 0) {
        qWarning("Unable to initialize audio mixer: %s", Mix_GetError());
        exit(1);
    }
    Mix_AllocateChannels(8);
}


void
closeSoundEngine()
{
    // Shut down SDL and SDL_mixer.
    Mix_ChannelFinished(0);
    Mix_HookMusicFinished(0);
    // Close the audio device as many times as it was opened.
    int opened = Mix_QuerySpec(0, 0, 0);
    for (int i = 0; i < opened; ++i) {
        Mix_CloseAudio();
    }
    SDL_Quit();
}


void
muteSound( bool mute )
{
    if (mute and not isMuted) {
        isMuted = true;
        Mix_VolumeMusic(0);
        Mix_Volume(-1, 0);
    } else if (not mute and isMuted) {
        isMuted = false;
        updateMusicVolume();
        updateSoundVolume();
    }
}


void
updateMusicVolume()
{
    hHandlers->musicvolume(currentMusicVol);
}


void
updateSoundVolume()
{
    hHandlers->samplevolume(currentSampleVol);
}


bool
isMusicPlaying()
{
    return Mix_PlayingMusic();
}


bool
isSamplePlaying()
{
    return Mix_Playing(-1) > 0;
}


void
HugoHandlers::playmusic(HUGO_FILE infile, long reslength, char loop_flag, int* result)
{
    if (not hApp->settings()->enableMusic) {
        hugo_fclose(infile);
        *result = false;
        return;
    }

    // We only play one music track at a time, so it's enough
    // to make this static.
    static Mix_Music* music = 0;

    // Any currently playing music should be stopped before playing
    // a new one.
    Mix_HaltMusic();

    // Clean up any active data from a previous call.
    if (music != 0) {
        Mix_FreeMusic(music);
        music = 0;
    }

    // Create an RWops for the embedded media resource.
    SDL_RWops* rwops = RWFromMediaBundle(infile->get(), reslength);
    if (rwops == 0) {
        qWarning() << "ERROR:" << SDL_GetError();
        delete infile;
        *result = false;
        return;
    }

    // SDL_mixer's auto-detection doesn't always work reliably. It's very
    // common for example to have broken headers in MP3s that otherwise play
    // just fine. So we use Mix_LoadMUSType_RW() without auto-detection.
    Mix_MusicType musType;
    switch (resource_type) {
    case MIDI_R:
        musType = MUS_MID;
        break;
    case XM_R:
    case S3M_R:
    case MOD_R:
        musType = MUS_MOD;
        break;
    case MP3_R:
        musType = MUS_MP3;
        break;
    default:
        qWarning() << "ERROR: Unknown music resource type";
        *result = false;
        return;
    }

    // Create a Mix_Music* from the RWops. Let SDL_mixer take ownership of
    // the rwops and free it automatically as needed.
    music = Mix_LoadMUSType_RW(rwops, musType, true);
    if (music == 0) {
        qWarning() << "ERROR:" << Mix_GetError();
        *result = false;
        return;
    }

    // Start playing the music. Loop forever if 'loop_flag' is true.
    // Otherwise, just play it once.
    updateMusicVolume();
    if (Mix_PlayMusic(music, loop_flag ? -1 : 1) != 0) {
        qWarning() << "ERROR:" << Mix_GetError();
        Mix_FreeMusic(music);
        *result = false;
        return;
    }
    *result = true;
}


void
HugoHandlers::musicvolume(int vol)
{
    if (vol < 0)
        vol = 0;
    else if (vol > 100)
        vol = 100;

    // Convert the Hugo volume range [0..100] to the SDL volume
    // range [0..MIX_MAX_VOLUME].
    vol = (vol * MIX_MAX_VOLUME) / 100;
    currentMusicVol = vol;

    // Attenuate the result by the global volume setting. Use an exponential
    // volume scale (we actually want the third power or higher, but
    // SDL_mixer's shitty range of 0..128 doesn't really allow for that.)
    vol = vol * std::pow((float)hApp->settings()->soundVolume / 100.f, 2);

    if (not isMuted) {
        Mix_VolumeMusic(vol);
    }
}


void
HugoHandlers::stopmusic()
{
    Mix_HaltMusic();
}


void
HugoHandlers::playsample(HUGO_FILE infile, long reslength, char loop_flag, int* result)
{
    if (not hApp->settings()->enableSoundEffects) {
        delete infile;
        *result = false;
        return;
    }

    // We only play one sample at a time, so it's enough to make these
    // static.
    static QFile* file = 0;
    static Mix_Chunk* chunk = 0;

    // Any currently playing sample should be stopped before playing
    // a new one.
    Mix_HaltChannel(-1);

    // If a file already exists from a previous call, delete it first.
    if (file != 0) {
        delete file;
        file = 0;
    }

    // Open 'infile' as a QFile.
    file = new QFile;
    if (not file->open(infile->get(), QIODevice::ReadOnly)) {
        qWarning() << "ERROR: Can't open sample sound file";
        file->close();
        delete infile;
        *result = false;
        return;
    }

    // Map the data into memory and create an RWops from that data.
    SDL_RWops* rwops = SDL_RWFromConstMem(file->map(ftell(infile->get()), reslength), reslength);
    // Done with the file.
    file->close();
    delete infile;
    if (rwops == 0) {
        qWarning() << "ERROR:" << SDL_GetError();
        *result = false;
        return;
    }

    // If a Mix_Chunk* already exists from a previous call, delete it first.
    if (chunk != 0) {
        Mix_FreeChunk(chunk);
        chunk = 0;
    }

    // Create a Mix_Chunk* from the RWops. Tell Mix_LoadWAV_RW() to take
    // ownership of the RWops so it will free it as necessary.
    chunk = Mix_LoadWAV_RW(rwops, true);
    if (chunk == 0) {
        qWarning() << "ERROR:" << Mix_GetError();
        *result = false;
        return;
    }

    // Start playing the sample. Loop forever if 'loop_flag' is true.
    // Otherwise, just play it once.
    updateSoundVolume();
    if (Mix_PlayChannel(-1, chunk, loop_flag ? -1 : 0) < 0) {
        qWarning() << "ERROR:" << Mix_GetError();
        Mix_FreeChunk(chunk);
        *result = false;
        return;
    }
    *result = true;
}


void
HugoHandlers::samplevolume(int vol)
{
    if (vol < 0)
        vol = 0;
    else if (vol > 100)
        vol = 100;

    // Convert the Hugo volume range [0..100] to the SDL volume
    // range [0..MIX_MAX_VOLUME].
    vol = (vol * MIX_MAX_VOLUME) / 100;
    currentSampleVol = vol;

    // Attenuate the result by the global volume setting. Use an exponential
    // volume scale (we actually want the third power or higher, but
    // SDL_mixer's shitty range of 0..128 doesn't really allow for that.)
    vol = vol * std::pow((float)hApp->settings()->soundVolume / 100.f, 2);

    if (not isMuted) {
        Mix_Volume(-1, vol);
    }
}


void
HugoHandlers::stopsample()
{
    Mix_HaltChannel(-1);
}
