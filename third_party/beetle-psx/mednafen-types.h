/* Shim for Mednafen's type header (Valor). */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#define INLINE inline
#define MDFN_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define MDFN_LIKELY(x) __builtin_expect(!!(x), 1)
