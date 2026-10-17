/* Shim for Mednafen's CD controller (Valor): CD audio (XA-ADPCM) comes from Valor's CD-ROM. */
#pragma once
#include <stdint.h>
/* Next 44.1 kHz stereo CD audio sample after volume, each in -32768..32767. */
void CDC_GetCDAudioSample(int32_t samples[2]);
