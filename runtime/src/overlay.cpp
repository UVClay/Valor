// Debug overlay (Dear ImGui): F3 cycles off -> basic (FPS) -> extended (frame times, pacing,
// emulation load, renderer stats).
#include <string>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "audio.h"
#include "game.h"
#include "gpu.h"
#include "tape.h"
#include "gpu_gl.h"
#include "hw.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"
#include "kbm.h"
#include "overlay.h"
#include "runtime.h"
#include "settings.h"

namespace psx::hw {

OverlayCounters overlay_counters;

namespace {

bool initialized;
bool menu_open;

constexpr int HISTORY = 240;
float frame_ms[HISTORY];  // host time between presents
float emu_ms[HISTORY];    // time spent emulating each vblank (excluding pacing and presenting)
int history_pos, emu_pos;
uint64_t last_present_ns, frame_start_ns;

// Game frame rate: the game flips its display buffer once per rendered frame.
uint64_t flips_window_start_ns;
int flips_in_window;
float game_fps, display_hz;
int presents_in_window;

// GPU counters summed over the measurement window, reported per game frame.
OverlayCounters window_sum, per_game_frame;
uint64_t last_cycles;
double instructions_per_frame;

// The rectangle the game image was last shown in, and whether it was a 3D view.
struct { int x, y, w, h; bool world; } view;

}  // namespace

void overlay_init(SDL_Window *window, void *gl_context) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;  // don't write imgui.ini
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NavEnableKeyboard |
                      ImGuiConfigFlags_NavEnableGamepad;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    initialized = true;
}

void overlay_event(const SDL_Event &e) {
    if (initialized) ImGui_ImplSDL3_ProcessEvent(&e);
}

void overlay_cycle() {
    config.overlay = (config.overlay + 1) % 3;
    static const char *names[] = {"off", "basic", "extended"};
    log("overlay: %s", names[config.overlay]);
    settings_save();
}

bool menu_is_open() { return menu_open; }

void menu_toggle() {
    menu_open = !menu_open;
    if (!menu_open) settings_save();
}

namespace {

// The settings window. Changes apply immediately.
// Testing (--menu-screenshot): VALOR_MENU_TAB picks the tab shown.
ImGuiTabItemFlags tab_flags(const char *name) {
    static const char *tab = getenv("VALOR_MENU_TAB");
    return tab && !strcmp(tab, name) ? ImGuiTabItemFlags_SetSelected : 0;
}

void draw_menu() {
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(io.DisplaySize.x * 0.95f, io.DisplaySize.y * 0.9f));
    ImGui::SetNextWindowBgAlpha(0.92f);
    bool open = true;
    ImGui::Begin("Settings", &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Video", nullptr, tab_flags("Video"))) {
            static const char *scales[] = {"Fit display", "1x (original, software)", "2x", "3x", "4x", "5x", "6x", "7x", "8x"};
            int scale = std::clamp(config.scale, 0, 8);
            if (ImGui::Combo("Resolution", &scale, scales, IM_ARRAYSIZE(scales))) {
                config.scale = scale;
                display_apply_settings();
            }
            static const struct { const char *name; double aspect; } aspects[] = {
                {"4:3 (original)", 4.0 / 3.0}, {"16:9", 16.0 / 9.0}, {"16:10", 16.0 / 10.0},
                {"21:9", 21.0 / 9.0},           {"32:9", 32.0 / 9.0}, {"Match window", 0}};
            int current = 0;
            for (int i = 0; i < IM_ARRAYSIZE(aspects); i++)
                if (std::abs(aspects[i].aspect - config.aspect) < 1e-6) current = i;
            if (ImGui::BeginCombo("Aspect ratio", aspects[current].name)) {
                for (int i = 0; i < IM_ARRAYSIZE(aspects); i++)
                    if (ImGui::Selectable(aspects[i].name, i == current)) config.aspect = aspects[i].aspect;
                ImGui::EndCombo();
            }
            bool original_fov = config.fov == 0;
            if (ImGui::Checkbox("Original field of view", &original_fov)) config.fov = original_fov ? 0 : 75;
            if (!original_fov) {
                float fov = float(config.fov);
                if (ImGui::SliderFloat("Horizontal FOV", &fov, 40.0f, 130.0f, "%.0f deg")) config.fov = fov;
            }
            ImGui::Checkbox("Frame interpolation", &config.interpolate);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("The game runs at 30 Hz; draw frames in between by moving the camera.\nNeeds a resolution above 1x.");
            if (config.interpolate) {
                static const int rates[] = {0, 60, 120, 144, 165, 240, 360};
                static const char *names[] = {"Display refresh rate", "60", "120", "144", "165", "240", "360"};
                int current = 0;
                for (int i = 0; i < IM_ARRAYSIZE(rates); i++)
                    if (rates[i] == config.frame_rate) current = i;
                if (ImGui::Combo("Frame rate", &current, names, IM_ARRAYSIZE(names))) config.frame_rate = rates[current];
            }
            ImGui::Checkbox("PS1 dithering and 15-bit color", &config.dither);
            ImGui::Checkbox("Sub-pixel precision", &config.subpixel);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Draw 3D vertices at their exact positions instead of whole pixels:\n"
                                  "removes the PS1's vertex wobble. Needs a resolution above 1x.");
            ImGui::Checkbox("Texture filtering", &config.texture_filter);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Smooth textures (bilinear) instead of the PS1's sharp texels.");
            ImGui::Checkbox("Depth buffer", &config.depth_buffer);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Hide what's behind walls by real depth instead of the game's polygon sorting,\n"
                                  "which can show things through nearby walls. Needs a resolution above 1x.");
            ImGui::Checkbox("Smooth scaling", &config.smooth_scaling);
            if (ImGui::Checkbox("Fullscreen (F11)", &config.fullscreen)) display_apply_settings();
            ImGui::Checkbox("Pause in the background", &config.pause_unfocused);
            ImGui::Checkbox("Fast loading", &config.fast_loading);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Read game data from the disc 8x faster (music and movies stream as usual).");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Audio", nullptr, tab_flags("Audio"))) {
            int volume = config.volume;
            if (ImGui::SliderInt("Volume", &volume, 0, 200, "%d%%")) audio_adjust_volume(volume - config.volume);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Controls", nullptr, tab_flags("Controls"))) {
            ImGui::Checkbox("Square stick range (DualShock-like)", &config.square_sticks);
            ImGui::TextDisabled("Full deflection reaches both axes on diagonals, as on a DualShock.\n"
                                "Without it, angling a round-gated stick off-axis loses most of the speed.");
            ImGui::Checkbox("Modern stick aiming", &config.stick_aim);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Look with a small deadzone and a smooth curve instead of the game's\n"
                                  "large deadzone and steep response (whichever stick your controller\n"
                                  "configuration uses for looking).");
            if (config.stick_aim) {
                float yaw = float(config.stick_yaw_speed), pitch = float(config.stick_pitch_speed);
                if (ImGui::SliderFloat("Turn speed", &yaw, 30, 720, "%.0f deg/s")) config.stick_yaw_speed = yaw;
                if (ImGui::SliderFloat("Look up/down speed", &pitch, 30, 720, "%.0f deg/s")) config.stick_pitch_speed = pitch;
            }
            static const char *gyro_modes[] = {"Off", "Always", "In aim mode"};
            ImGui::Combo("Gyro aiming", &config.gyro, gyro_modes, IM_ARRAYSIZE(gyro_modes));
            if (config.gyro) {
                float g = float(config.gyro_sensitivity);
                if (ImGui::SliderFloat("Gyro sensitivity", &g, 0.1f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic))
                    config.gyro_sensitivity = g;
            }
            ImGui::Separator();
            ImGui::Checkbox("Mouse look", &config.mouse_look);
            ImGui::SameLine();
            ImGui::TextDisabled("(captured while playing a mission)");
            float sens = float(config.mouse_sensitivity);
            if (ImGui::SliderFloat("Mouse sensitivity", &sens, 0.1f, 20.0f, "%.2f", ImGuiSliderFlags_Logarithmic))
                config.mouse_sensitivity = sens;
            ImGui::Checkbox("Invert mouse", &config.invert_mouse);
            ImGui::TextUnformatted("Keys (missions; menus use arrows/WASD, Enter, Backspace)");
            if (ImGui::BeginTable("binds", 4, ImGuiTableFlags_SizingFixedFit)) {
                for (int c = 0; c < KC_COUNT; c++) {
                    if (c % 2 == 0) ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(kbm_control_label(c));
                    ImGui::TableNextColumn();
                    std::string label = kbm_rebinding() == c ? "Press a key... (Esc cancels)" : kbm_input_name(kbm_bindings[c]);
                    label += "##bind" + std::to_string(c);
                    if (ImGui::Button(label.c_str(), ImVec2(ImGui::GetFontSize() * 14, 0))) kbm_begin_rebind(c);
                }
                ImGui::EndTable();
            }
            if (ImGui::Button("Default keys")) kbm_reset_bindings();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Interface", nullptr, tab_flags("Interface"))) {
            ImGui::Checkbox("Widescreen HUD (keep its proportions)", &config.widescreen_hud);
            if (config.widescreen_hud) ImGui::Checkbox("HUD at the screen's edges", &config.hud_edges);
            ImGui::Checkbox("Crosshair", &config.crosshair);
            if (config.crosshair) ImGui::SliderInt("Crosshair size", &config.crosshair_size, 1, 20);
            ImGui::Separator();
            ImGui::TextUnformatted("Debug overlay (F3)");
            ImGui::RadioButton("Off", &config.overlay, 0);
            ImGui::SameLine();
            ImGui::RadioButton("FPS", &config.overlay, 1);
            ImGui::SameLine();
            ImGui::RadioButton("Extended", &config.overlay, 2);
            ImGui::Checkbox("Debug mode: record every session (from the next start)", &config.debug);
            if (config.debug) ImGui::TextDisabled("Sessions go to recordings/; F9 marks a moment to look at.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::Separator();
    ImGui::TextDisabled("F1 or Esc closes. Saved to saves/settings.ini");
    ImGui::End();
    if (!open) menu_toggle();
}

}  // namespace

void draw_stats();

void overlay_set_view(int x, int y, int w, int h, bool world) { view = {x, y, w, h, world}; }

namespace {

// A plain white + at the center of the 3D view, scaled with the image.
void draw_crosshair() {
    if (!view.world || view.h <= 0) return;
    if (game_aiming() || game_paused()) return;  // aim mode draws the game's own reticle
    float px = float(view.h) / 240.0f;
    float cx = float(view.x) + float(view.w) * 0.5f, cy = float(view.y) + float(view.h) * 0.5f;
    float arm = float(config.crosshair_size) * px, thick = std::max(1.0f, std::round(px));
    ImDrawList *dl = ImGui::GetBackgroundDrawList();
    ImU32 white = IM_COL32(255, 255, 255, 255);
    dl->AddRectFilled(ImVec2(cx - arm, cy - thick * 0.5f), ImVec2(cx + arm, cy + thick * 0.5f), white);
    dl->AddRectFilled(ImVec2(cx - thick * 0.5f, cy - arm), ImVec2(cx + thick * 0.5f, cy + arm), white);
}

}  // namespace

void overlay_frame_begin() { frame_start_ns = SDL_GetTicksNS(); }

void overlay_frame_emulated() {
    if (!frame_start_ns) return;
    emu_ms[emu_pos] = float(SDL_GetTicksNS() - frame_start_ns) / 1e6f;
    emu_pos = (emu_pos + 1) % HISTORY;
}

void overlay_note_flip() { flips_in_window++; }

// Called right before the frame is shown; `work_ns` is when emulating this frame started.
void overlay_render() {
    uint64_t now = SDL_GetTicksNS();
    if (last_present_ns) {
        frame_ms[history_pos] = float(now - last_present_ns) / 1e6f;
        history_pos = (history_pos + 1) % HISTORY;
    }
    last_present_ns = now;
    presents_in_window++;
    if (!flips_window_start_ns) flips_window_start_ns = now;
    window_sum.primitives += overlay_counters.primitives;
    window_sum.draw_calls += overlay_counters.draw_calls;
    window_sum.vram_uploads += overlay_counters.vram_uploads;
    overlay_counters = {};
    if (now - flips_window_start_ns >= 500000000) {
        double secs = double(now - flips_window_start_ns) / 1e9;
        game_fps = float(flips_in_window / secs);
        display_hz = float(presents_in_window / secs);
        uint32_t frames = std::max(1, flips_in_window);
        per_game_frame = {window_sum.primitives / frames, window_sum.draw_calls / frames, window_sum.vram_uploads / frames};
        window_sum = {};
        if (static bool log_fps = getenv("VALOR_LOG_FPS"); log_fps) {
            float worst = 0;
            for (float v : frame_ms) worst = std::max(worst, v);
            log("fps: display %.1f game %.1f, worst frame %.1f ms at vblank %llu", display_hz, game_fps, worst,
                (unsigned long long)hw::gpu.frames);
        }
        flips_in_window = presents_in_window = 0;
        flips_window_start_ns = now;
    }
    instructions_per_frame = double(cpu->cycles - last_cycles);
    last_cycles = cpu->cycles;

    // Debug mode always shows the overlay, so a screenshot or video says it was on.
    if (!initialized || (config.overlay == 0 && !config.debug && !menu_open && !config.crosshair)) return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    // Keep the UI readable at high resolutions: scale with the window height.
    ImGui::GetStyle().FontScaleMain = std::max(1.0f, ImGui::GetIO().DisplaySize.y / 720.0f);
    ImGui::NewFrame();
    if (config.crosshair) draw_crosshair();
    if (config.overlay > 0 || config.debug) draw_stats();
    if (menu_open) draw_menu();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void draw_stats() {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.6f);
    ImGui::Begin("stats", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
    if (config.debug) {
        bool rec = tape_recording();
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "DEBUGGING%s", rec ? "  REC" : "");
        ImGui::Text("vblank %llu%s", (unsigned long long)hw::gpu.frames,
                    rec && tape_marks() ? ("  marks " + std::to_string(tape_marks())).c_str() : "");
    }
    if (config.interpolate && display_hz > game_fps * 1.5f) ImGui::Text("%.0f FPS (game %.1f)", display_hz, game_fps);
    else ImGui::Text("%.1f FPS", game_fps);
    if (config.overlay >= 2) {
        // Frame pacing over the history window.
        std::vector<float> ft(frame_ms, frame_ms + HISTORY);
        std::vector<float> sorted = ft;
        std::sort(sorted.begin(), sorted.end());
        float sum = 0, sq = 0;
        for (float v : ft) { sum += v; sq += v * v; }
        float avg = sum / HISTORY, sd = std::sqrt(std::max(0.0f, sq / HISTORY - avg * avg));
        float p99 = sorted[HISTORY * 99 / 100];
        float target = 1000.0f * float(CYCLES_PER_FRAME) / float(CPU_HZ);
        if (config.interpolate && gl_renderer_scale() > 1) target = float(1000.0 / present_rate());
        int late = int(std::count_if(ft.begin(), ft.end(), [&](float v) { return v > target * 1.5f; }));
        float emu_avg = 0, emu_max = 0;
        for (float v : emu_ms) { emu_avg += v / HISTORY; emu_max = std::max(emu_max, v); }

        ImGui::Separator();
        ImGui::Text("display  %.2f Hz (target %.2f)", display_hz, 1000.0f / target);
        ImGui::PlotLines("##frametime", frame_ms, HISTORY, history_pos, nullptr, 0.0f, target * 2,
                         ImVec2(260, 50));
        ImGui::Text("frame    avg %.2f  min %.2f  max %.2f ms", avg, sorted.front(), sorted.back());
        ImGui::Text("pacing   sd %.2f ms  99th %.2f ms  late %d/%d", sd, p99, late, HISTORY);
        float vblank_ms = 1000.0f * float(CYCLES_PER_FRAME) / float(CPU_HZ);
        ImGui::Text("emulate  avg %.2f  max %.2f ms per vblank (%.0f%% of %.1f ms)", emu_avg, emu_max,
                    100.0f * emu_avg / vblank_ms, vblank_ms);
        ImGui::Text("cpu      %.2f M instructions/frame", instructions_per_frame / 1e6);
        double q = audio_queued();
        if (q >= 0) ImGui::Text("audio    %.1f ms queued", q * 1000);
        else ImGui::Text("audio    off");
        ImGui::Separator();
        int scale = gl_renderer_scale();
        ImGui::Text("render   %s, %dx", scale > 1 ? "OpenGL" : "software", scale);
        ImGui::Text("gpu      %u prims  %u draws  %u VRAM uploads per frame", per_game_frame.primitives,
                    per_game_frame.draw_calls, per_game_frame.vram_uploads);
    }
    ImGui::End();
}

}  // namespace psx::hw
