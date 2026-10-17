// GTE (COP2) — placeholder until the real implementation lands.
#include "runtime.h"

using namespace psx;

extern "C" {

static uint32_t gte_data[32], gte_ctrl[32];

uint32_t gte_mfc2(Cpu *, int reg) { return gte_data[reg]; }
uint32_t gte_cfc2(Cpu *, int reg) { return gte_ctrl[reg]; }
void gte_mtc2(Cpu *, int reg, uint32_t v) { gte_data[reg] = v; }
void gte_ctc2(Cpu *, int reg, uint32_t v) { gte_ctrl[reg] = v; }
void gte_cmd(Cpu *c, uint32_t op) { fatal("GTE command %07X not implemented (ra=%08X)", op, c->r[31]); }

}  // extern "C"
