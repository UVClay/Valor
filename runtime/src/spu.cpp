// SPU: Mednafen's SPU (third_party/beetle-psx) driven from our clock, interrupts and DMA.
#include "audio.h"
#include "hw.h"

extern "C" {
#include "psx/spu.h"

// Configuration the vendored SPU expects from its host.
uint8_t spu_samples = 1;                // samples per update step
bool psx_spu_silent_voice_opt = false;  // keep exact behavior for silent voices

void IRQ_Assert(int which, bool asserted) {
    static bool level;
    (void)which;
    if (asserted && !level) psx::hw::irq_raise(psx::hw::IRQ_SPU);
    level = asserted;
}
}

namespace psx::hw {

namespace {

constexpr int64_t UPDATE_PERIOD = 768 * 64;  // run the SPU every 64 samples
int64_t last_update;

// Bring the SPU up to the current time and hand its output to the audio device.
void catch_up() {
    int64_t t = now();
    int64_t clocks = t - last_update;
    if (clocks <= 0) return;
    last_update = t;
    while (clocks > 0) {
        int32_t step = int32_t(std::min<int64_t>(clocks, 1 << 20));
        SPU_UpdateFromCDC(step);
        clocks -= step;
        if (IntermediateBufferPos >= 2048) {
            audio_push(&IntermediateBuffer[0][0], IntermediateBufferPos);
            IntermediateBufferPos = 0;
        }
    }
    audio_push(&IntermediateBuffer[0][0], IntermediateBufferPos);
    IntermediateBufferPos = 0;
}

}  // namespace

void spu_init() {
    SPU_Init();
    SPU_Power();
    last_update = now();
    schedule(Event::Spu, UPDATE_PERIOD);
}

void spu_sync() { catch_up(); }

void spu_event() {
    catch_up();
    schedule(Event::Spu, UPDATE_PERIOD);
}

uint16_t spu_read(uint32_t phys) {
    catch_up();
    return SPU_Read(0, phys & 0x3FF);
}

void spu_write(uint32_t phys, uint16_t v) {
    catch_up();
    SPU_Write(0, phys & 0x3FF, v);
}

void spu_dma_write(uint32_t addr, uint32_t words) {
    catch_up();
    for (uint32_t i = 0; i < words; i++) {
        uint32_t w;
        memcpy(&w, psx_ram + ((addr + 4 * i) & 0x1FFFFC), 4);
        SPU_WriteDMA(w);
    }
}

void spu_dma_read(uint32_t addr, uint32_t words) {
    catch_up();
    for (uint32_t i = 0; i < words; i++) {
        uint32_t w = SPU_ReadDMA();
        memcpy(psx_ram + ((addr + 4 * i) & 0x1FFFFC), &w, 4);
    }
}

}  // namespace psx::hw
