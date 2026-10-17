// SDL3 audio output for the SPU's 44.1 kHz stereo stream.
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>

#include "audio.h"
#include "runtime.h"

namespace psx::hw {

namespace {

SDL_AudioStream *stream;
FILE *wav;
uint32_t wav_frames;

// Rewrite the WAV header with the current length (cheap; keeps the file valid if we halt).
void wav_header() {
    uint32_t data = wav_frames * 4;
    uint8_t h[44] = {'R', 'I', 'F', 'F'};
    auto le32 = [&](int at, uint32_t v) { for (int i = 0; i < 4; i++) h[at + i] = uint8_t(v >> (8 * i)); };
    auto le16 = [&](int at, uint16_t v) { h[at] = uint8_t(v); h[at + 1] = uint8_t(v >> 8); };
    le32(4, 36 + data);
    memcpy(h + 8, "WAVEfmt ", 8);
    le32(16, 16); le16(20, 1); le16(22, 2); le32(24, 44100); le32(28, 44100 * 4); le16(32, 4); le16(34, 16);
    memcpy(h + 36, "data", 4);
    le32(40, data);
    long pos = ftell(wav);
    fseek(wav, 0, SEEK_SET);
    fwrite(h, 1, sizeof h, wav);
    fseek(wav, pos, SEEK_SET);
}
// Keep latency bounded: video pacing and the host audio clock drift slightly apart.
constexpr int MAX_QUEUED_BYTES = 44100 * 4 / 8;  // 125 ms

}  // namespace

void audio_init() {
    if (!config.wav.empty()) {
        wav = fopen(config.wav.c_str(), "wb");
        if (!wav) fatal("cannot write %s", config.wav.c_str());
        fseek(wav, 44, SEEK_SET);
    }
    if (config.headless) return;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        log("audio: unavailable (%s)", SDL_GetError());
        return;
    }
    SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, 44100};
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!stream) {
        log("audio: no output device (%s)", SDL_GetError());
        return;
    }
    SDL_ResumeAudioStreamDevice(stream);
    log("audio: %s", SDL_GetCurrentAudioDriver());
}

void audio_push(const int16_t *frames, uint32_t count) {
    if (wav && count) {
        fwrite(frames, 4, count, wav);
        wav_frames += count;
        wav_header();
    }
    if (!stream || !count) return;
    if (SDL_GetAudioStreamQueued(stream) > MAX_QUEUED_BYTES) return;  // drop rather than lag
    SDL_PutAudioStreamData(stream, frames, int(count * 4));
}

}  // namespace psx::hw
