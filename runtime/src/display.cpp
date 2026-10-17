// Window, frame pacing, controller/keyboard input and screenshots (SDL3).
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <vector>

#include "gpu.h"
#include "hw.h"
#include "input.h"

namespace psx::hw {

namespace {

constexpr double FRAME_SECONDS = double(CYCLES_PER_FRAME) / double(CPU_HZ);

SDL_Window *window;
SDL_Renderer *renderer;
SDL_Texture *texture;
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
    default: return;
    }
    keyboard_buttons = down ? (keyboard_buttons | bit) : (keyboard_buttons & ~bit);
}

void pump_events() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
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

}  // namespace

void display_init(bool want_window) {
    if (!want_window) return;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        log("display: SDL init failed (%s), running headless", SDL_GetError());
        return;
    }
    if (!SDL_CreateWindowAndRenderer("Valor", 960, 720, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
        log("display: no window (%s), running headless", SDL_GetError());
        return;
    }
    SDL_SetRenderVSync(renderer, 0);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 640, 480);
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    headless = false;
    next_frame_ns = SDL_GetTicksNS();
    log("display: %s renderer", SDL_GetRendererName(renderer));
}

void gpu_present() {
    if (!config.screenshots.empty() && gpu.frames % config.screenshot_every == 0) {
        capture_display();
        char name[32];
        snprintf(name, sizeof name, "frame%06llu.png", (unsigned long long)gpu.frames);
        std::filesystem::create_directories(config.screenshots);
        write_png(config.screenshots / name, frame, frame_w, frame_h);
    }
    if (headless) return;

    capture_display();
    pump_events();
    SDL_Rect src = {0, 0, frame_w, frame_h};
    SDL_UpdateTexture(texture, &src, frame.data(), frame_w * 4);
    int ww, wh;
    SDL_GetRenderOutputSize(renderer, &ww, &wh);
    float scale = std::min(ww / 4.0f, wh / 3.0f);
    SDL_FRect dst = {(ww - 4 * scale) / 2, (wh - 3 * scale) / 2, 4 * scale, 3 * scale};
    SDL_FRect fsrc = {0, 0, float(frame_w), float(frame_h)};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, texture, &fsrc, &dst);
    SDL_RenderPresent(renderer);

    // Pace to the console's refresh rate.
    next_frame_ns += uint64_t(FRAME_SECONDS * 1e9);
    uint64_t now_ns = SDL_GetTicksNS();
    if (fast_forward || now_ns > next_frame_ns + 100000000) next_frame_ns = now_ns;  // don't try to catch up
    else if (next_frame_ns > now_ns) SDL_DelayPrecise(next_frame_ns - now_ns);
}

bool pad_connected(int port) { return port == 0; }

uint16_t pad_buttons(int port) {
    if (port != 0) return 0xFFFF;
    uint16_t pressed = keyboard_buttons;
    for (const auto &p : config.presses)
        if (gpu.frames >= p.frame && gpu.frames < p.frame + p.duration) pressed |= p.buttons;
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

}  // namespace psx::hw
