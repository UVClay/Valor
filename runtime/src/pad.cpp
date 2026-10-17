// Controllers in ports 1 and 2: digital pad / DualShock protocol, including analog mode, the
// configuration commands (0x43-0x4D) and rumble.
#include <cstring>

#include "input.h"
#include "pad.h"
#include "runtime.h"

namespace psx::hw {

namespace {

struct Pad {
    bool analog;          // analog mode (sticks reported, ID 0x73)
    bool config_mode;     // inside 0x43 configuration mode (ID 0xF3)
    uint8_t motor_map[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // 0x4D: poll bytes driving motors
    uint8_t small_motor, large_motor;
    // Current exchange
    uint8_t command;
    uint8_t out[8];  // bytes after the 0x5A header
    int out_len;
    uint8_t args[8];
};

Pad pads[2];

uint8_t id(const Pad &p) { return p.config_mode ? 0xF3 : p.analog ? 0x73 : 0x41; }

void build_poll(Pad &p, int port) {
    uint16_t buttons = pad_buttons(port);  // active low
    p.out[0] = buttons & 0xFF;
    p.out[1] = buttons >> 8;
    p.out_len = 2;
    if (p.analog || p.config_mode) {
        PadSticks s = pad_sticks(port);
        p.out[2] = s.rx; p.out[3] = s.ry; p.out[4] = s.lx; p.out[5] = s.ly;
        p.out_len = 6;
    }
}

void build_response(Pad &p, int port) {
    memset(p.out, 0, sizeof p.out);
    p.out_len = 6;
    switch (p.command) {
    case 0x42: build_poll(p, port); break;
    case 0x43: build_poll(p, port); break;  // enter/exit config: answers like a poll
    case 0x45:  // status: DualShock, current mode
        p.out[0] = 0x03; p.out[1] = 0x02; p.out[2] = p.analog ? 0x01 : 0x00;
        p.out[3] = 0x02; p.out[4] = 0x01; p.out[5] = 0x00;
        break;
    case 0x47: p.out[2] = 0x02; p.out[4] = 0x01; break;
    case 0x4D: memcpy(p.out, p.motor_map, 6); break;
    default: break;  // 0x44, 0x46, 0x4C: filled when their argument arrives
    }
}

// Commands whose reply depends on the first argument byte.
void argument_dependent(Pad &p, int i, uint8_t tx) {
    if (i != 0) return;
    static const uint8_t r46[2][6] = {{0, 0, 1, 2, 0, 0x0A}, {0, 0, 1, 1, 1, 0x14}};
    static const uint8_t r4c[2][6] = {{0, 0, 0, 4, 0, 0}, {0, 0, 0, 7, 0, 0}};
    if (p.command == 0x46 && tx < 2) memcpy(p.out, r46[tx], 6);
    if (p.command == 0x4C && tx < 2) memcpy(p.out, r4c[tx], 6);
}

void finish_command(Pad &p, int port) {
    switch (p.command) {
    case 0x42:
        if (p.analog || p.config_mode) {
            // Motor bytes are the poll's arguments, routed by the 0x4D mapping.
            for (int i = 0; i < 6; i++) {
                if (p.motor_map[i] == 0x00) p.small_motor = p.args[i] & 1;
                else if (p.motor_map[i] == 0x01) p.large_motor = p.args[i];
            }
            pad_rumble(port, p.small_motor, p.large_motor);
        }
        break;
    case 0x43:
        p.config_mode = p.args[0] == 1;
        break;
    case 0x44:
        if (p.config_mode) {
            p.analog = p.args[0] == 1;
            log("input: game set pad %d to %s mode", port + 1, p.analog ? "analog" : "digital");
        }
        break;
    case 0x4D:
        if (p.config_mode) memcpy(p.motor_map, p.args, 6);
        break;
    }
}

}  // namespace

void pad_toggle_analog() {
    pads[0].analog = !pads[0].analog;
    log("input: pad 1 in %s mode (F7 toggles)", pads[0].analog ? "analog" : "digital");
}

// Byte `step` of an exchange with the pad in `port` (step 0 is the 0x01 select byte).
uint8_t pad_exchange(int port, int step, uint8_t tx, bool &ack) {
    Pad &p = pads[port];
    ack = true;
    if (step == 0) return 0xFF;
    if (step == 1) {
        p.command = tx;
        bool known = tx == 0x42 || tx == 0x43 || (p.config_mode && (tx == 0x44 || tx == 0x45 || tx == 0x46 ||
                                                                    tx == 0x47 || tx == 0x4C || tx == 0x4D));
        if (!known) { ack = false; return 0xFF; }
        memset(p.args, 0, sizeof p.args);
        build_response(p, port);
        return id(p);
    }
    if (step == 2) return 0x5A;
    int i = step - 3;
    if (i < 8) p.args[i] = tx;
    argument_dependent(p, i, tx);
    if (i >= p.out_len) { ack = false; return 0xFF; }
    uint8_t r = p.out[i];
    if (i == p.out_len - 1) {
        ack = false;  // last byte
        finish_command(p, port);
    }
    return r;
}

}  // namespace psx::hw
