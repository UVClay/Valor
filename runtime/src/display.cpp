// Window, frame pacing, controller/keyboard input and screenshots (SDL3).
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <fstream>
#include <vector>

#include "audio.h"
#include "gl.h"
#include "gpu.h"
#include "gpu_gl.h"
#include "hw.h"
#include "input.h"
#include "overlay.h"
#include "pad.h"
#include "settings.h"

using namespace psx::gl;

namespace psx::hw {

namespace {

constexpr double FRAME_SECONDS = double(CYCLES_PER_FRAME) / double(CPU_HZ);

SDL_Window *window;
SDL_GLContext gl_context;
bool use_gl_renderer;
GLuint native_tex, native_fbo;  // the software frame, for --scale 1 and 24-bit (FMV) display
bool headless = true;
bool fast_forward;
uint64_t next_frame_ns;

SDL_Gamepad *gamepad;
uint16_t keyboard_buttons;  // pressed (active high)

std::vector<uint32_t> frame;  // ARGB8888 display area
int frame_w, frame_h;

// The displayed part of VRAM, converted to ARGB.
void capture_display() {
    static const int widths[] = {256, 320, 512, 640};
    int w = (gpu.display_mode & 0x40) ? 368 : widths[gpu.display_mode & 3];
    bool interlaced = (gpu.display_mode & 0x24) == 0x24;
    int y1 = (gpu.vrange & 0x3FF), y2 = (gpu.vrange >> 10) & 0x3FF;
    int h = std::clamp(y2 - y1, 1, 240);
    if (interlaced) h *= 2;
    bool rgb24 = gpu.display_mode & 0x10;
    frame_w = w;
    frame_h = h;
    frame.resize(size_t(w) * h);
    const uint16_t *vram = gpu.vram.data();
    for (int y = 0; y < h; y++) {
        int vy = (gpu.display_y + y) & 511;
        const uint16_t *row = vram + vy * 1024;
        for (int x = 0; x < w; x++) {
            uint32_t r, g, b;
            if (rgb24) {
                // Three bytes per pixel packed across halfwords.
                int byte = gpu.display_x * 2 + x * 3;
                auto at = [&](int i) { uint16_t hw = row[((i >> 1)) & 1023]; return (i & 1) ? hw >> 8 : hw & 0xFF; };
                r = at(byte); g = at(byte + 1); b = at(byte + 2);
            } else {
                uint16_t px = row[(gpu.display_x + x) & 1023];
                r = (px & 0x1F) << 3; g = ((px >> 5) & 0x1F) << 3; b = ((px >> 10) & 0x1F) << 3;
                r |= r >> 5; g |= g >> 5; b |= b >> 5;
            }
            frame[size_t(y) * w + x] = 0xFF000000u | r << 16 | g << 8 | b;
        }
    }
    if (gpu.display_disabled) std::fill(frame.begin(), frame.end(), 0xFF000000u);
}

// Minimal PNG writer (uncompressed deflate), enough for debug screenshots.
void write_png(const std::filesystem::path &path, const std::vector<uint32_t> &px, int w, int h) {
    auto crc32 = [](const uint8_t *d, size_t n, uint32_t c = 0xFFFFFFFFu) {
        for (size_t i = 0; i < n; i++) {
            c ^= d[i];
            for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
        }
        return c;
    };
    std::vector<uint8_t> raw;
    for (int y = 0; y < h; y++) {
        raw.push_back(0);
        for (int x = 0; x < w; x++) {
            uint32_t p = px[size_t(y) * w + x];
            raw.push_back(uint8_t(p >> 16)); raw.push_back(uint8_t(p >> 8)); raw.push_back(uint8_t(p));
        }
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
    for (size_t off = 0; off < raw.size(); off += 65535) {
        size_t n = std::min<size_t>(65535, raw.size() - off);
        z.push_back(off + n == raw.size());
        z.push_back(uint8_t(n)); z.push_back(uint8_t(n >> 8));
        z.push_back(uint8_t(~n)); z.push_back(uint8_t(~n >> 8));
        z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
    }
    uint32_t adler = b << 16 | a;
    for (int i = 3; i >= 0; i--) z.push_back(uint8_t(adler >> (8 * i)));
    std::ofstream f(path, std::ios::binary);
    auto be32 = [&](uint32_t v) { for (int i = 3; i >= 0; i--) f.put(char(v >> (8 * i))); };
    auto chunk = [&](const char *type, const std::vector<uint8_t> &data) {
        be32(uint32_t(data.size()));
        std::vector<uint8_t> td(type, type + 4);
        td.insert(td.end(), data.begin(), data.end());
        f.write(reinterpret_cast<const char *>(td.data()), td.size());
        be32(~crc32(td.data(), td.size()));
    };
    f.write("\x89PNG\r\n\x1a\n", 8);
    std::vector<uint8_t> ihdr = {uint8_t(w >> 24), uint8_t(w >> 16), uint8_t(w >> 8), uint8_t(w),
                                 uint8_t(h >> 24), uint8_t(h >> 16), uint8_t(h >> 8), uint8_t(h), 8, 2, 0, 0, 0};
    chunk("IHDR", ihdr);
    chunk("IDAT", z);
    chunk("IEND", {});
}

void handle_key(SDL_Keycode key, bool down) {
    uint16_t bit = 0;
    switch (key) {
    case SDLK_UP: bit = BTN_UP; break;
    case SDLK_DOWN: bit = BTN_DOWN; break;
    case SDLK_LEFT: bit = BTN_LEFT; break;
    case SDLK_RIGHT: bit = BTN_RIGHT; break;
    case SDLK_X: bit = BTN_CROSS; break;
    case SDLK_C: bit = BTN_CIRCLE; break;
    case SDLK_Z: bit = BTN_SQUARE; break;
    case SDLK_S: bit = BTN_TRIANGLE; break;
    case SDLK_Q: bit = BTN_L1; break;
    case SDLK_E: bit = BTN_R1; break;
    case SDLK_1: bit = BTN_L2; break;
    case SDLK_3: bit = BTN_R2; break;
    case SDLK_RETURN: bit = BTN_START; break;
    case SDLK_BACKSPACE: bit = BTN_SELECT; break;
    case SDLK_TAB: fast_forward = down; return;
    case SDLK_F7: if (down) pad_toggle_analog(); return;
    case SDLK_F3: if (down) overlay_cycle(); return;
    case SDLK_F1: if (down) menu_toggle(); return;
    case SDLK_ESCAPE: if (down && menu_is_open()) menu_toggle(); return;
    case SDLK_F11:
        if (down) {
            config.fullscreen = !config.fullscreen;
            display_apply_settings();
            settings_save();
        }
        return;
    case SDLK_MINUS: if (down) audio_adjust_volume(-10); return;
    case SDLK_EQUALS: if (down) audio_adjust_volume(10); return;
    default: return;
    }
    keyboard_buttons = down ? (keyboard_buttons | bit) : (keyboard_buttons & ~bit);
}

void pump_events() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        overlay_event(e);
        switch (e.type) {
        case SDL_EVENT_QUIT: throw Halt("window closed");
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            if (!e.key.repeat) handle_key(e.key.key, e.type == SDL_EVENT_KEY_DOWN);
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!gamepad) {
                gamepad = SDL_OpenGamepad(e.gdevice.which);
                if (gamepad) log("input: %s connected", SDL_GetGamepadName(gamepad));
            }
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (gamepad && SDL_GetGamepadID(gamepad) == e.gdevice.which) {
                SDL_CloseGamepad(gamepad);
                gamepad = nullptr;
                log("input: controller disconnected");
            }
            break;
        }
    }
}

// The aspect the image is shown at; 3D projection is adjusted to match (see gte_set_projection).
double output_aspect() {
    static double applied = -1;
    double a = config.aspect;
    if (a == 0) {
        int ww, wh;
        SDL_GetWindowSizeInPixels(window, &ww, &wh);
        a = wh > 0 ? double(ww) / wh : 4.0 / 3.0;
    }
    static double applied_fov = -1;
    if (a != applied || config.fov != applied_fov) {
        applied = a;
        applied_fov = config.fov;
        gte_set_projection(a, config.fov, config.world_h, config.world_base_tan);
    }
    return a;
}

// Draw the PS1's displayed image into the window, letterboxed to the output aspect.
void present_frame() {
    static const int widths[] = {256, 320, 512, 640};
    int w = (gpu.display_mode & 0x40) ? 368 : widths[gpu.display_mode & 3];
    int y1 = (gpu.vrange & 0x3FF), y2 = (gpu.vrange >> 10) & 0x3FF;
    int h = std::clamp(y2 - y1, 1, 240);
    bool rgb24 = gpu.display_mode & 0x10;

    int ww, wh;
    SDL_GetWindowSizeInPixels(window, &ww, &wh);
    // 2D screens (FMVs, title cards) stay 4:3; 3D ones (the GTE is projecting) use the output
    // aspect. Switch back to 4:3 only after a moment without 3D to avoid flicker.
    static uint64_t last_projections, frames_without_3d = 1000;
    frames_without_3d = gte_projections != last_projections ? 0 : frames_without_3d + 1;
    last_projections = gte_projections;
    double aspect = output_aspect();
    if (rgb24 || frames_without_3d > 30) aspect = 4.0 / 3.0;
    int dw = ww, dh = int(ww / aspect);
    if (dh > wh) { dh = wh; dw = int(wh * aspect); }
    int dx = (ww - dw) / 2, dy = (wh - dh) / 2;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, ww, wh);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (gpu.display_disabled) return;
    if (use_gl_renderer && !rgb24) {
        gl_renderer_present(dx, dy, dw, dh, gpu.display_x, gpu.display_y, w, h);
        return;
    }
    capture_display();
    glBindTexture(GL_TEXTURE_2D, native_tex);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame_w, frame_h, GL_BGRA, GL_UNSIGNED_BYTE, frame.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, native_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBlitFramebuffer(0, 0, frame_w, frame_h, dx, dy + dh, dx + dw, dy, GL_COLOR_BUFFER_BIT, GL_NEAREST);
}

}  // namespace

void display_apply_settings() {
    if (headless) return;
    int scale = config.scale;
    if (scale == 0) {
        // Fit the display: enough resolution that the 240-line image is never upscaled.
        const SDL_DisplayMode *dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
        scale = dm ? std::clamp((dm->h + 239) / 240, 2, 8) : 4;
    }
    gl_renderer_set_scale(scale);
    use_gl_renderer = gl_renderer_scale() > 1;
    SDL_SetWindowFullscreen(window, config.fullscreen);
}

void display_init(bool want_window) {
    if (!want_window) return;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        log("display: SDL init failed (%s), running headless", SDL_GetError());
        return;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    window = SDL_CreateWindow("Valor", 960, 720,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY |
                                  (config.hidden ? SDL_WINDOW_HIDDEN : 0));
    if (!window || !(gl_context = SDL_GL_CreateContext(window))) {
        log("display: no OpenGL 3.3 window (%s), running headless", SDL_GetError());
        return;
    }
    if (!gl::load()) fatal("display: OpenGL functions missing");
    SDL_GL_SetSwapInterval(0);  // we pace frames ourselves

    glGenTextures(1, &native_tex);
    glBindTexture(GL_TEXTURE_2D, native_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 640, 480, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &native_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, native_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, native_tex, 0);

    headless = false;
    display_apply_settings();
    overlay_init(window, gl_context);
    next_frame_ns = SDL_GetTicksNS();
    log("display: OpenGL %s, %s", reinterpret_cast<const char *>(glGetString(GL_VERSION)),
        use_gl_renderer ? "hardware renderer" : "software renderer");
}

// Debugging: VALOR_LOG_CHAIN="PTR,OFF,...,OFF:COUNT" logs COUNT words each frame at the end of a
// pointer chain: start at PTR, dereference, add each OFF (dereferencing between offsets).
static void log_chain() {
    static std::vector<uint32_t> chain;
    static int count = -1;
    if (count == -1) {
        count = 0;
        const char *e = getenv("VALOR_LOG_CHAIN");
        if (!e) return;
        std::string spec(e);
        auto colon = spec.find(':');
        count = colon == std::string::npos ? 1 : atoi(spec.c_str() + colon + 1);
        for (size_t pos = 0; pos < colon && pos != std::string::npos;) {
            chain.push_back(uint32_t(strtoul(spec.c_str() + pos, nullptr, 16)));
            pos = spec.find(',', pos);
            if (pos != std::string::npos && pos < colon) pos++;
            else break;
        }
    }
    if (!count || chain.empty()) return;
    uint32_t a = chain[0];
    for (size_t i = 1; i < chain.size(); i++) {
        a = psx_r32(cpu, a);
        if (!a) return;
        a += chain[i];
    }
    std::string line;
    char b[16];
    for (int i = 0; i < count; i++) {
        snprintf(b, sizeof b, " %d", int32_t(psx_r32(cpu, a + 4 * i)));
        line += b;
    }
    log("chain f%llu:%s", (unsigned long long)gpu.frames, line.c_str());
}

void gpu_present() {
    log_chain();
    if (config.dump_ram_frame && gpu.frames == config.dump_ram_frame) {
        std::ofstream("build/ram.bin", std::ios::binary).write(reinterpret_cast<const char *>(psx_ram), PSX_RAM_SIZE);
        log("dumped RAM to build/ram.bin at frame %llu", (unsigned long long)gpu.frames);
    }
    bool shot = !config.screenshots.empty() && gpu.frames >= config.screenshot_from &&
                gpu.frames % config.screenshot_every == 0;
    if (shot && !config.screenshot_window) {
        capture_display();
        char name[32];
        snprintf(name, sizeof name, "frame%06llu.png", (unsigned long long)gpu.frames);
        std::filesystem::create_directories(config.screenshots);
        if (use_gl_renderer && !(gpu.display_mode & 0x10)) {
            int s = gl_renderer_scale();
            write_png(config.screenshots / name, gl_renderer_read(gpu.display_x, gpu.display_y, frame_w, frame_h),
                      frame_w * s, frame_h * s);
        } else {
            write_png(config.screenshots / name, frame, frame_w, frame_h);
        }
    }
    if (headless) return;

    pump_events();
    present_frame();
    overlay_render();
    if (shot && config.screenshot_window) {
        int ww, wh;
        SDL_GetWindowSizeInPixels(window, &ww, &wh);
        std::vector<uint32_t> px(size_t(ww) * wh), flipped(px.size());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glReadPixels(0, 0, ww, wh, GL_BGRA, GL_UNSIGNED_BYTE, px.data());
        for (int y = 0; y < wh; y++)
            for (int x = 0; x < ww; x++) flipped[size_t(y) * ww + x] = px[size_t(wh - 1 - y) * ww + x] | 0xFF000000u;
        char name[40];
        snprintf(name, sizeof name, "window%06llu.png", (unsigned long long)gpu.frames);
        std::filesystem::create_directories(config.screenshots);
        write_png(config.screenshots / name, flipped, ww, wh);
    }
    SDL_GL_SwapWindow(window);

    if (config.menu_screenshot && gpu.frames == config.menu_screenshot) {
        // Testing: draw the menu once over this frame and save the window.
        menu_toggle();
        present_frame();
        overlay_render();
        overlay_render();  // ImGui lays windows out on the first frame they appear
        int ww, wh;
        SDL_GetWindowSizeInPixels(window, &ww, &wh);
        std::vector<uint32_t> px(size_t(ww) * wh), flipped(px.size());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glReadPixels(0, 0, ww, wh, GL_BGRA, GL_UNSIGNED_BYTE, px.data());
        for (int y = 0; y < wh; y++)
            for (int x = 0; x < ww; x++) flipped[size_t(y) * ww + x] = px[size_t(wh - 1 - y) * ww + x] | 0xFF000000u;
        std::filesystem::create_directories(config.screenshots.empty() ? "." : config.screenshots);
        write_png((config.screenshots.empty() ? std::filesystem::path(".") : config.screenshots) / "menu.png", flipped, ww, wh);
        menu_toggle();
    }

    // The game pauses while the settings menu is open.
    if (menu_is_open()) {
        audio_pause(true);
        while (menu_is_open()) {
            pump_events();
            present_frame();
            overlay_render();
            SDL_GL_SwapWindow(window);
            SDL_Delay(16);
        }
        audio_pause(false);
        next_frame_ns = SDL_GetTicksNS();
    }

    // Pace to the console's refresh rate. With audio, nudge the period (at most 0.5%) to keep
    // the output queue near its target, so the host audio clock never under- or overruns.
    double period = FRAME_SECONDS;
    double queued = audio_queued();
    if (queued >= 0) {
        constexpr double TARGET = 0.060;
        period *= 1.0 + std::clamp(0.005 * (queued - TARGET) / TARGET, -0.005, 0.005);
    }
    audio_set_fast_forward(fast_forward);
    next_frame_ns += uint64_t(period * 1e9);
    uint64_t now_ns = SDL_GetTicksNS();
    if (fast_forward || now_ns > next_frame_ns + 100000000) next_frame_ns = now_ns;  // don't try to catch up
    else if (next_frame_ns > now_ns) SDL_DelayPrecise(next_frame_ns - now_ns);
    overlay_frame_begin();
}

// Monkey testing: deterministic random input from a seed. A new "intent" (buttons and stick
// positions) is picked every 10-90 frames, biased toward moving and shooting.
struct MonkeyInput {
    uint16_t pressed = 0;
    PadSticks sticks = {0x80, 0x80, 0x80, 0x80};
};

static MonkeyInput monkey_state() {
    static MonkeyInput cur;
    static uint64_t next_change, rng;
    if (!config.monkey_seed || gpu.frames < config.monkey_from) return {};
    if (!rng) rng = config.monkey_seed * 0x9E3779B97F4A7C15ull;
    auto rnd = [&]() {
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
        return rng;
    };
    if (gpu.frames >= next_change) {
        next_change = gpu.frames + 10 + rnd() % 80;
        cur = {};
        static const uint16_t weighted[] = {BTN_CROSS, BTN_CROSS, BTN_SQUARE, BTN_CIRCLE, BTN_TRIANGLE,
                                            BTN_L1, BTN_R1, BTN_L2, BTN_R2, BTN_UP, BTN_UP, BTN_DOWN,
                                            BTN_LEFT, BTN_RIGHT, BTN_START, BTN_SELECT};
        int n = int(rnd() % 3);
        for (int i = 0; i < n; i++) cur.pressed |= weighted[rnd() % std::size(weighted)];
        if (rnd() % 40 != 0) cur.pressed &= ~(BTN_START | BTN_SELECT);  // pause only now and then
        cur.sticks.ly = uint8_t(rnd() % 3 == 0 ? 0x80 : rnd() % 256);
        cur.sticks.lx = uint8_t(rnd() % 2 ? 0x80 : rnd() % 256);
        cur.sticks.rx = uint8_t(rnd() % 2 ? 0x80 : rnd() % 256);
        cur.sticks.ry = uint8_t(rnd() % 4 ? 0x80 : rnd() % 256);
    }
    return cur;
}

bool pad_connected(int port) { return port == 0; }

uint16_t pad_buttons(int port) {
    if (port != 0) return 0xFFFF;
    uint16_t pressed = keyboard_buttons;
    for (const auto &p : config.presses)
        if (gpu.frames >= p.frame && gpu.frames < p.frame + p.duration) pressed |= p.buttons;
    pressed |= monkey_state().pressed;
    if (gamepad) {
        static const struct { SDL_GamepadButton b; uint16_t bit; } map[] = {
            {SDL_GAMEPAD_BUTTON_SOUTH, BTN_CROSS},        {SDL_GAMEPAD_BUTTON_EAST, BTN_CIRCLE},
            {SDL_GAMEPAD_BUTTON_WEST, BTN_SQUARE},        {SDL_GAMEPAD_BUTTON_NORTH, BTN_TRIANGLE},
            {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, BTN_L1},   {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, BTN_R1},
            {SDL_GAMEPAD_BUTTON_BACK, BTN_SELECT},        {SDL_GAMEPAD_BUTTON_START, BTN_START},
            {SDL_GAMEPAD_BUTTON_LEFT_STICK, BTN_L3},      {SDL_GAMEPAD_BUTTON_RIGHT_STICK, BTN_R3},
            {SDL_GAMEPAD_BUTTON_DPAD_UP, BTN_UP},         {SDL_GAMEPAD_BUTTON_DPAD_DOWN, BTN_DOWN},
            {SDL_GAMEPAD_BUTTON_DPAD_LEFT, BTN_LEFT},     {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, BTN_RIGHT},
        };
        for (auto &m : map)
            if (SDL_GetGamepadButton(gamepad, m.b)) pressed |= m.bit;
        if (SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 8000) pressed |= BTN_L2;
        if (SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 8000) pressed |= BTN_R2;
    }
    return uint16_t(~pressed);
}

PadSticks pad_sticks(int port) {
    PadSticks s = {0x80, 0x80, 0x80, 0x80};
    if (port == 0 && config.monkey_seed && gpu.frames >= config.monkey_from) return monkey_state().sticks;
    if (port == 0)
        for (const auto &st : config.sticks)
            if (gpu.frames >= st.frame && gpu.frames < st.frame + st.duration) {
                uint8_t *axes[] = {&s.lx, &s.ly, &s.rx, &s.ry};
                *axes[st.axis] = st.value;
            }
    if (port != 0 || !gamepad) return s;
    auto stick = [](SDL_GamepadAxis ax, SDL_GamepadAxis ay, uint8_t &out_x, uint8_t &out_y) {
        double x = std::max(-1.0, SDL_GetGamepadAxis(gamepad, ax) / 32767.0);
        double y = std::max(-1.0, SDL_GetGamepadAxis(gamepad, ay) / 32767.0);
        if (double m = std::max(std::abs(x), std::abs(y)); config.square_sticks && m > 0) {
            double k = std::min(1.0, std::hypot(x, y)) / m;
            x = std::clamp(x * k, -1.0, 1.0);
            y = std::clamp(y * k, -1.0, 1.0);
        }
        auto raw = [](double v) { return uint8_t(std::clamp(int(std::lround(128 + v * 128)), 0, 255)); };
        out_x = raw(x);
        out_y = raw(y);
    };
    stick(SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, s.lx, s.ly);
    stick(SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY, s.rx, s.ry);
    return s;
}

void pad_rumble(int port, uint8_t small, uint8_t large) {
    static uint8_t last_small, last_large;
    if (port != 0 || !gamepad) return;
    if (small == last_small && large == last_large) return;
    last_small = small;
    last_large = large;
    // Large motor -> low-frequency rumble, small motor -> high-frequency. Refreshed on change;
    // the long duration keeps it running until the game changes it.
    SDL_RumbleGamepad(gamepad, uint16_t(large * 257), small ? 0xFFFF : 0, 10000);
}

}  // namespace psx::hw
