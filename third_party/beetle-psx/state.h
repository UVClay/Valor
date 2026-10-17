/* Shim for Mednafen's save-state header (Valor). Save states are not wired up yet, so the
 * SPU's state table is accepted and ignored. */
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef struct StateMem StateMem;
typedef struct { void *v; size_t size; uint32_t flags; const char *name; } SFORMAT;
#define SFVAR(x) { &(x), sizeof(x), 0, #x }
#define SFARRAY16(x, n) { (x), (n) * 2, 0, #x }
#define SFARRAY32(x, n) { (x), (n) * 4, 0, #x }
#define SFARRAY(x, n) { (x), (n), 0, #x }
#define SFEND { 0, 0, 0, 0 }
#define MDFNSS_StateAction(...) 1
