// Controller in port 1: digital pad / DualShock protocol, including analog mode, the
// configuration commands (0x43-0x4D) and rumble.
#include <cstring>

#include "input.h"
#include "pad.h"
#include "runtime.h"

namespace psx::hw {

namespace {

bool analog;          // analog mode (sticks reported, ID 0x73)
bool config_mode;     // inside 0x43 configuration mode (ID 0xF3)
uint8_t motor_map[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // 0x4D: which poll bytes drive motors
uint8_t small_motor, large_motor;

// Current exchange
uint8_t command;
uint8_t out[8];  // bytes after the 0x5A header
int out_len;
uint8_t args[8];

uint8_t id() { return config_mode ? 0xF3 : analog ? 0x73 : 0x41; }

void build_poll() {
    uint16_t buttons = pad_buttons(0);  // active low
    out[0] = buttons & 0xFF;
    out[1] = buttons >> 8;
    out_len = 2;
    if (analog || config_mode) {
        PadSticks s = pad_sticks(0);
        out[2] = s.rx; out[3] = s.ry; out[4] = s.lx; out[5] = s.ly;
        out_len = 6;
    }
}

void build_response() {
    memset(out, 0, sizeof out);
    out_len = 6;
    switch (command) {
    case 0x42: build_poll(); break;
    case 0x43:  // enter/exit config: answers like a poll
        build_poll();
        break;
    case 0x45:  // status: DualShock, current mode
        out[0] = 0x03; out[1] = 0x02; out[2] = analog ? 0x01 : 0x00; out[3] = 0x02; out[4] = 0x01; out[5] = 0x00;
        break;
    case 0x47: out[2] = 0x02; out[4] = 0x01; break;
    case 0x4D: memcpy(out, motor_map, 6); break;
    default: break;  // 0x44, 0x46, 0x4C: filled when their argument arrives
    }
}

// Commands whose reply depends on the first argument byte.
void argument_dependent(int i, uint8_t tx) {
    if (i != 0) return;
    static const uint8_t r46[2][6] = {{0, 0, 1, 2, 0, 0x0A}, {0, 0, 1, 1, 1, 0x14}};
    static const uint8_t r4c[2][6] = {{0, 0, 0, 4, 0, 0}, {0, 0, 0, 7, 0, 0}};
    if (command == 0x46 && tx < 2) memcpy(out, r46[tx], 6);
    if (command == 0x4C && tx < 2) memcpy(out, r4c[tx], 6);
}

void finish_command() {
    switch (command) {
    case 0x42:
        if (analog || config_mode) {
            // Motor bytes are the poll's arguments, routed by the 0x4D mapping.
            for (int i = 0; i < 6; i++) {
                if (motor_map[i] == 0x00) small_motor = args[i] & 1;
                else if (motor_map[i] == 0x01) large_motor = args[i];
            }
            pad_rumble(0, small_motor, large_motor);
        }
        break;
    case 0x43:
        config_mode = args[0] == 1;
        break;
    case 0x44:
        if (config_mode) {
            analog = args[0] == 1;
            log("input: game set the pad to %s mode", analog ? "analog" : "digital");
        }
        break;
    case 0x4D:
        if (config_mode) memcpy(motor_map, args, 6);
        break;
    }
}

}  // namespace

void pad_toggle_analog() {
    analog = !analog;
    log("input: pad in %s mode (F7 toggles)", analog ? "analog" : "digital");
}

// Byte `step` of an exchange (step 0 is the 0x01 select byte).
uint8_t pad_exchange(int step, uint8_t tx, bool &ack) {
    ack = true;
    if (step == 0) return 0xFF;
    if (step == 1) {
        command = tx;
        bool known = tx == 0x42 || tx == 0x43 || (config_mode && (tx == 0x44 || tx == 0x45 || tx == 0x46 ||
                                                                  tx == 0x47 || tx == 0x4C || tx == 0x4D));
        if (!known) { ack = false; return 0xFF; }
        memset(args, 0, sizeof args);
        build_response();
        return id();
    }
    if (step == 2) return 0x5A;
    int i = step - 3;
    if (i < 8) args[i] = tx;
    argument_dependent(i, tx);
    if (i >= out_len) { ack = false; return 0xFF; }
    uint8_t r = out[i];
    if (i == out_len - 1) {
        ack = false;  // last byte
        finish_command();
    }
    return r;
}

}  // namespace psx::hw
