/* Module registries emitted by the recompiler (<project>_modules.c). */
#pragma once
#include "psx/recomp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GameOverlay {
    const RecompModule *module;
    const char *file;     /* file name on disc */
    uint32_t base;        /* load address */
    uint32_t size;
    uint32_t id;          /* word at `base` that identifies this overlay */
} GameOverlay;

typedef struct RecompProject {
    const char *name;
    uint32_t entry;
    const RecompModule *const *resident;  /* always-loaded modules */
    uint32_t resident_count;
    const GameOverlay *overlays;          /* share one load address; one resident at a time */
    uint32_t overlay_count;
} RecompProject;

extern const RecompProject bios_project;
extern const RecompProject game_project;

#ifdef __cplusplus
}
#endif
