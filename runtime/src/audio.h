// Audio output device.
#pragma once
#include <cstdint>

namespace psx::hw {

void audio_init();
// Queue interleaved 44.1 kHz stereo frames.
void audio_push(const int16_t *frames, uint32_t count);
// Seconds of audio waiting to be played, or a negative value when there's no audio device.
double audio_queued();
// Change the output volume by `delta` percent (0..200).
void audio_adjust_volume(int delta);
// While fast-forwarding, output is discarded instead of piling up.
void audio_set_fast_forward(bool on);

}  // namespace psx::hw
