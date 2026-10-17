/* Shim for Mednafen's interrupt controller (Valor). */
#pragma once
#include <stdbool.h>
enum { IRQ_SPU = 9 };
void IRQ_Assert(int which, bool asserted);
