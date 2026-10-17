// Debug overlay (Dear ImGui): F3 cycles off -> basic (FPS) -> extended (frame times, pacing,
// emulation load, renderer stats).
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "audio.h"
#include "gpu.h"
#include "gpu_gl.h"
#include "hw.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl3.h"
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
float emu_ms[HISTORY];    // time spent emulating each frame (excluding pacing sleeps)
int history_pos;
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
void draw_menu() {
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.92f);
    bool open = true;
    ImGui::Begin("Settings", &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Video")) {
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
            ImGui::Checkbox("PS1 dithering and 15-bit color", &config.dither);
            ImGui::Checkbox("Smooth scaling", &config.smooth_scaling);
            if (ImGui::Checkbox("Fullscreen (F11)", &config.fullscreen)) display_apply_settings();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Audio")) {
            int volume = config.volume;
            if (ImGui::SliderInt("Volume", &volume, 0, 200, "%d%%")) audio_adjust_volume(volume - config.volume);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Controls")) {
            ImGui::Checkbox("Square stick range (DualShock-like)", &config.square_sticks);
            ImGui::TextDisabled("Full deflection reaches both axes on diagonals, as on a DualShock.\n"
                                "Without it, angling a round-gated stick off-axis loses most of the speed.");
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Interface")) {
            ImGui::TextUnformatted("Debug overlay (F3)");
            ImGui::RadioButton("Off", &config.overlay, 0);
            ImGui::SameLine();
            ImGui::RadioButton("FPS", &config.overlay, 1);
            ImGui::SameLine();
            ImGui::RadioButton("Extended", &config.overlay, 2);
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

void overlay_frame_begin() { frame_start_ns = SDL_GetTicksNS(); }

void overlay_note_flip() { flips_in_window++; }

// Called right before the frame is shown; `work_ns` is when emulating this frame started.
void overlay_render() {
    uint64_t now = SDL_GetTicksNS();
    if (last_present_ns) {
        frame_ms[history_pos] = float(now - last_present_ns) / 1e6f;
        emu_ms[history_pos] = frame_start_ns ? float(now - frame_start_ns) / 1e6f : 0;
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
        flips_in_window = presents_in_window = 0;
        flips_window_start_ns = now;
    }
    instructions_per_frame = double(cpu->cycles - last_cycles);
    last_cycles = cpu->cycles;

    if (!initialized || (config.overlay == 0 && !menu_open)) return;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    // Keep the UI readable at high resolutions: scale with the window height.
    ImGui::GetStyle().FontScaleMain = std::max(1.0f, ImGui::GetIO().DisplaySize.y / 720.0f);
    ImGui::NewFrame();
    if (config.overlay > 0) draw_stats();
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
    ImGui::Text("%.1f FPS", game_fps);
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
        int late = int(std::count_if(ft.begin(), ft.end(), [&](float v) { return v > target * 1.5f; }));
        float emu_avg = 0, emu_max = 0;
        for (float v : emu_ms) { emu_avg += v / HISTORY; emu_max = std::max(emu_max, v); }

        ImGui::Separator();
        ImGui::Text("display  %.2f Hz (target %.2f)", display_hz, 1000.0f / target);
        ImGui::PlotLines("##frametime", frame_ms, HISTORY, history_pos, nullptr, 0.0f, target * 2,
                         ImVec2(260, 50));
        ImGui::Text("frame    avg %.2f  min %.2f  max %.2f ms", avg, sorted.front(), sorted.back());
        ImGui::Text("pacing   sd %.2f ms  99th %.2f ms  late %d/%d", sd, p99, late, HISTORY);
        ImGui::Text("emulate  avg %.2f  max %.2f ms (%.0f%% of budget)", emu_avg, emu_max, 100.0f * emu_avg / target);
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
