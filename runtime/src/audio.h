// Audio output device.
#pragma once
#include <cstdint>

namespace psx::hw {

void audio_init();
// Queue interleaved 44.1 kHz stereo frames.
void audio_push(const int16_t *frames, uint32_t count);

}  // namespace psx::hw
