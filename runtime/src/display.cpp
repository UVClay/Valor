// Window, frame pacing, controller/keyboard input and screenshots (SDL3).
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <fstream>
#include <vector>

#include "audio.h"
#include "gl.h"
#include "gpu.h"
#include "gpu_gl.h"
#include "hw.h"
#include "input.h"
#include "interp.h"
#include "game.h"
#include "kbm.h"
#include "memcard.h"
#include "overlay.h"
#include "pad.h"
#include "settings.h"
#include "shadow.h"
#include "tape.h"

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
bool screenshot_requested;  // F12

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

// Runtime keys (the game's controls are in kbm.cpp). True when the key was one of them.
bool handle_key(SDL_Keycode key, bool down) {
    switch (key) {
    case SDLK_F10: fast_forward = down; return true;
    case SDLK_F7:
        if (down) {
            pad_toggle_analog();
            tape_analog_toggled();
        }
        return true;
    case SDLK_F3: if (down) overlay_cycle(); return true;
    case SDLK_F1: if (down) menu_toggle(); return true;
    case SDLK_ESCAPE:
        if (!menu_is_open()) return false;
        if (down) menu_toggle();
        return true;
    case SDLK_F11:
        if (down) {
            config.fullscreen = !config.fullscreen;
            display_apply_settings();
            settings_save();
        }
        return true;
    case SDLK_F12: if (down) screenshot_requested = true; return true;
    case SDLK_TAB:
    case SDLK_F9: if (down) tape_mark(); return true;
    case SDLK_MINUS: if (down) audio_adjust_volume(-10); return true;
    case SDLK_EQUALS: if (down) audio_adjust_volume(10); return true;
    default: return false;
    }
}

void pump_events() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (kbm_rebinding() >= 0 && kbm_event(e)) continue;  // the settings menu is rebinding
        overlay_event(e);
        bool release = e.type == SDL_EVENT_KEY_UP || e.type == SDL_EVENT_MOUSE_BUTTON_UP;
        switch (e.type) {
        case SDL_EVENT_QUIT: throw Halt("window closed");
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            if (e.key.repeat) break;
            if (!handle_key(e.key.key, e.type == SDL_EVENT_KEY_DOWN) && (!menu_is_open() || release)) kbm_event(e);
            break;
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
            if (!menu_is_open() || release) kbm_event(e);
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!gamepad) {
                gamepad = SDL_OpenGamepad(e.gdevice.which);
                if (gamepad) {
                    bool gyro = SDL_GamepadHasSensor(gamepad, SDL_SENSOR_GYRO) &&
                                SDL_SetGamepadSensorEnabled(gamepad, SDL_SENSOR_GYRO, true);
                    log("input: %s connected%s", SDL_GetGamepadName(gamepad), gyro ? " (gyro)" : "");
                }
            }
            break;
        case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
            if (e.gsensor.sensor == SDL_SENSOR_GYRO) kbm_gyro(e.gsensor.data, e.gsensor.sensor_timestamp);
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
    kbm_update(window);
}

// The aspect the image is shown at; 3D projection is adjusted to match (see gte_set_projection).
double output_aspect() {
    static double applied = -1;
    double a = config.aspect;
    if (a == 0) {
        int ww = 0, wh = 0;
        if (window) SDL_GetWindowSizeInPixels(window, &ww, &wh);
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
// vblank_time >= 0: the moment shown, in emulated vblanks, for frame interpolation.
void present_frame(double vblank_time = -1) {
    static const int widths[] = {256, 320, 512, 640};
    int w = (gpu.display_mode & 0x40) ? 368 : widths[gpu.display_mode & 3];
    int y1 = (gpu.vrange & 0x3FF), y2 = (gpu.vrange >> 10) & 0x3FF;
    int h = std::clamp(y2 - y1, 1, 240);
    bool rgb24 = gpu.display_mode & 0x10;

    int ww, wh;
    SDL_GetWindowSizeInPixels(window, &ww, &wh);
    if (vblank_time >= 0) {
        static bool sized;
        if (!sized) tape_window(ww, wh);
        sized = true;
        tape_presented(vblank_time);
    }
    // 2D screens (FMVs, title cards) stay 4:3; 3D ones (the GTE is projecting) use the output
    // aspect. Switch back to 4:3 only after a moment without 3D to avoid flicker.
    static uint64_t last_projections, frames_without_3d = 1000;
    frames_without_3d = gte_projections != last_projections ? 0 : frames_without_3d + 1;
    last_projections = gte_projections;
    double aspect = output_aspect();
    // Widescreen only in a mission: the menus are 2D pictures with a few 3D pieces, made for 4:3.
    if (rgb24 || frames_without_3d > 30 || !game_in_level()) aspect = 4.0 / 3.0;
    int dw = ww, dh = int(ww / aspect);
    if (dh > wh) { dh = wh; dw = int(wh * aspect); }
    int dx = (ww - dw) / 2, dy = (wh - dh) / 2;
    overlay_set_view(dx, dy, dw, dh, !rgb24 && frames_without_3d <= 30 && !gpu.display_disabled && game_in_level());

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, ww, wh);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (gpu.display_disabled) return;
    if (use_gl_renderer && !rgb24) {
        int sx, sy;
        if (config.decoupled && vblank_time >= 0 && shadow_render(vblank_time, sx, sy)) {
            gl_renderer_present(dx, dy, dw, dh, sx, sy, w, h);
            return;
        }
        if (vblank_time >= 0) gl_renderer_interpolate(vblank_time);
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

// Replays: the window's image as a frame of the video stream (YUV4MPEG2, 4:4:4, BT.601).
void video_frame() {
    if (config.video.empty() || gpu.frames < config.video_from || gpu.frames > config.video_to) return;
    static FILE *f;
    static int vw, vh;
    int ww, wh;
    SDL_GetWindowSizeInPixels(window, &ww, &wh);
    if (!f) {
        f = fopen(config.video.c_str(), "wb");
        if (!f) fatal("can't write %s", config.video.c_str());
        vw = ww & ~1;
        vh = wh & ~1;
        fprintf(f, "YUV4MPEG2 W%d H%d F%d:1 Ip A1:1 C444\n", vw, vh, std::max(present_rate() > 0 ? int(present_rate()) : 60, 1));
    }
    if (ww < vw || wh < vh) return;
    static std::vector<uint8_t> px, plane;
    px.resize(size_t(ww) * wh * 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, ww, wh, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    plane.resize(size_t(vw) * vh * 3);
    uint8_t *y = plane.data(), *u = y + size_t(vw) * vh, *v = u + size_t(vw) * vh;
    for (int r = 0; r < vh; r++) {
        const uint8_t *s = px.data() + size_t(wh - 1 - r) * ww * 4;  // OpenGL rows are bottom-up
        for (int c = 0; c < vw; c++, s += 4) {
            int R = s[0], G = s[1], B = s[2];
            size_t i = size_t(r) * vw + c;
            y[i] = uint8_t((66 * R + 129 * G + 25 * B + 128) / 256 + 16);
            u[i] = uint8_t((-38 * R - 74 * G + 112 * B + 128) / 256 + 128);
            v[i] = uint8_t((112 * R - 94 * G - 18 * B + 128) / 256 + 128);
        }
    }
    fputs("FRAME\n", f);
    fwrite(plane.data(), 1, plane.size(), f);
}

// Frames per second to present with interpolation: the setting, or the display's refresh rate.
double present_rate() {
    if (config.frame_rate > 0) return config.frame_rate;
    const SDL_DisplayMode *dm = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
    return dm && dm->refresh_rate > 0 ? dm->refresh_rate : 60.0;
}

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

static void open_window();

void display_init(bool want_window) {
    if (want_window) open_window();
    // The projection changes what the game culls and draws, and so its timing: set it before the
    // game runs, window or not, or a replay without a display runs a different game.
    output_aspect();
}

static void open_window() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        log("display: SDL init failed (%s), running headless", SDL_GetError());
        return;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    window = SDL_CreateWindow("Valor", config.window_w, config.window_h,
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
    overlay_frame_emulated();
    memcard_flush();
    log_chain();
    gte_trace_frame();
    if (config.dump_ram_frame && gpu.frames == config.dump_ram_frame) {
        std::ofstream(config.dump_ram_path, std::ios::binary).write(reinterpret_cast<const char *>(psx_ram), PSX_RAM_SIZE);
        log("dumped RAM to %s at frame %llu", config.dump_ram_path.c_str(), (unsigned long long)gpu.frames);
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
    tape_vblank();
    if (headless) {
        // Nothing to show, but a replay still ends where the recording does.
        if (tape_replaying() && !tape_replay_presents([](double) {})) throw Halt("replay finished");
        return;
    }

    auto save_window = [](const char *name) {
        int ww, wh;
        SDL_GetWindowSizeInPixels(window, &ww, &wh);
        std::vector<uint32_t> px(size_t(ww) * wh), flipped(px.size());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glReadPixels(0, 0, ww, wh, GL_BGRA, GL_UNSIGNED_BYTE, px.data());
        for (int y = 0; y < wh; y++)
            for (int x = 0; x < ww; x++) flipped[size_t(y) * ww + x] = px[size_t(wh - 1 - y) * ww + x] | 0xFF000000u;
        std::filesystem::create_directories(config.screenshots);
        write_png(config.screenshots / name, flipped, ww, wh);
    };

    pump_events();
    if (tape_replaying()) {
        // The recorded frames, at their moments, as fast as they can be drawn.
        if (!tape_replay_presents([](double t) {
                present_frame(t);
                overlay_render();
                video_frame();
            }))
            throw Halt("replay finished");
        if (!config.video.empty() && gpu.frames > config.video_to) throw Halt("replay finished (end of --video-range)");
        overlay_frame_begin();
        return;
    }
    double vblank = double(gpu.frames);
    // With frame interpolation, frames are presented on their own clock (below).
    bool own_clock = interp_presenting() && use_gl_renderer && !fast_forward && present_rate() > 0;
    if (!own_clock || shot) {
        present_frame(vblank);
        overlay_render();
    }
    if (shot && config.screenshot_window) {
        char name[40];
        snprintf(name, sizeof name, "window%06llu.png", (unsigned long long)gpu.frames);
        save_window(name);
        if (interp_presenting()) {
            // Testing: an in-between frame halfway to the next vblank as well.
            present_frame(vblank + 0.5);
            overlay_render();
            snprintf(name, sizeof name, "window%06llu_5.png", (unsigned long long)gpu.frames);
            save_window(name);
        }
    }
    if (screenshot_requested) {
        // F12: the window as shown, into screenshots/.
        screenshot_requested = false;
        present_frame(vblank);
        overlay_render();
        char name[64];
        time_t now = time(nullptr);
        strftime(name, sizeof name, "valor-%Y%m%d-%H%M%S.png", localtime(&now));
        auto saved = config.screenshots;
        config.screenshots = "screenshots";
        save_window(name);
        config.screenshots = saved;
        log("screenshot: screenshots/%s", name);
    }
    if (!own_clock) SDL_GL_SwapWindow(window);

    if (config.menu_screenshot && gpu.frames == config.menu_screenshot) {
        // Testing: draw the menu once over this frame and save the window.
        menu_toggle();
        present_frame();
        for (int i = 0; i < 4; i++) overlay_render();  // ImGui lays windows out over a few frames
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

    // The game pauses while the settings menu is open, and (config.pause_unfocused) while the
    // window is in the background.
    auto paused = [] {
        return menu_is_open() || (config.pause_unfocused && !config.hidden &&
                                  !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS));
    };
    if (paused()) {
        audio_pause(true);
        while (paused()) {
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
    uint64_t vblank_ns = next_frame_ns;
    next_frame_ns += uint64_t(period * 1e9);
    uint64_t now_ns = SDL_GetTicksNS();
    if (fast_forward || now_ns > next_frame_ns + 100000000) {
        next_frame_ns = now_ns;  // don't try to catch up
    } else if (own_clock) {
        // Frame interpolation: present on our own clock until the next vblank is due, then
        // emulate it (possibly a little early; frames are placed by their own time).
        uint64_t interval = uint64_t(1e9 / present_rate());
        static uint64_t next_present_ns;
        if (next_present_ns + 2 * interval < now_ns || next_present_ns > now_ns + 2 * interval)
            next_present_ns = now_ns + interval;
        // Not before this vblank (after a stall the clock restarts at it).
        next_present_ns = std::max(next_present_ns, vblank_ns);
        while (next_present_ns < next_frame_ns) {
            now_ns = SDL_GetTicksNS();
            if (next_present_ns > now_ns) SDL_DelayPrecise(next_present_ns - now_ns);
            pump_events();
            if (menu_is_open()) break;
            present_frame(vblank + double(next_present_ns - vblank_ns) / (period * 1e9));
            overlay_render();
            SDL_GL_SwapWindow(window);
            next_present_ns += interval;
            now_ns = SDL_GetTicksNS();
            if (next_present_ns + interval < now_ns) next_present_ns = now_ns;  // fell behind
        }
    } else if (next_frame_ns > now_ns) {
        SDL_DelayPrecise(next_frame_ns - now_ns);
    }
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
    uint16_t pressed = kbm_buttons();
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
    return tape_pad_buttons(uint16_t(~pressed));
}

PadSticks pad_sticks(int port) {
    PadSticks s = {0x80, 0x80, 0x80, 0x80};
    if (port == 0 && config.monkey_seed && gpu.frames >= config.monkey_from) return monkey_state().sticks;
    bool scripted = false;
    if (port == 0)
        for (const auto &st : config.sticks)
            if (gpu.frames >= st.frame && gpu.frames < st.frame + st.duration) {
                uint8_t *axes[] = {&s.lx, &s.ly, &s.rx, &s.ry};
                *axes[st.axis] = st.value;
                scripted = true;
            }
    if (port != 0) return s;
    if (!gamepad || scripted) return tape_pad_sticks(s);
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
    return tape_pad_sticks(s);
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
