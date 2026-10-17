// SIO0 (controller and memory card ports) at 0x1F801040.
#include "hw.h"
#include "input.h"

namespace psx::hw {

namespace {

constexpr int64_t BYTE_TIME = 1088;  // one byte at the usual 250 kHz rate

uint16_t ctrl, mode, baud;
uint8_t rx;
bool rx_ready, irq_flag, ack_low;

// The device currently selected and how far into its exchange we are.
enum class Device { None, Pad, Card } device;
int step;
uint8_t reply;
bool reply_acks;

void respond(uint8_t tx) {
    int port = (ctrl >> 13) & 1;
    if (step == 0) {
        if (tx == 0x01) device = Device::Pad;
        else if (tx == 0x81) device = Device::Card;
        else device = Device::None;
    }
    reply = 0xFF;
    reply_acks = false;
    if (device == Device::Pad && pad_connected(port)) {
        uint16_t buttons = pad_buttons(port);  // active low
        static const uint8_t header[] = {0xFF, 0x41, 0x5A};
        if (step < 3) {
            reply = header[step];
            reply_acks = !(step == 1 && tx != 0x42);
        } else if (step == 3) {
            reply = buttons & 0xFF;
            reply_acks = true;
        } else if (step == 4) {
            reply = buttons >> 8;
            reply_acks = false;  // last byte: no ack
        }
    }
    // No memory card inserted: nothing answers.
    step++;
}

}  // namespace

void sio_init() {
    ctrl = mode = baud = 0;
    rx_ready = irq_flag = ack_low = false;
    device = Device::None;
    step = 0;
}

void sio_event_ack() {
    rx = reply;
    rx_ready = true;
    if (reply_acks) {
        ack_low = true;
        if (ctrl & 0x1000) {
            irq_flag = true;
            irq_raise(IRQ_SIO0);
        }
    }
}

uint32_t sio_read(uint32_t phys, int size) {
    (void)size;
    switch (phys) {
    case 0x1F801040: {
        uint8_t v = rx_ready ? rx : 0xFF;
        rx_ready = false;
        return v;
    }
    case 0x1F801044: {
        uint32_t s = 0x05 | (rx_ready ? 0x02 : 0) | (ack_low ? 0x80 : 0) | (irq_flag ? 0x200 : 0);
        ack_low = false;  // /ACK is a short pulse
        return s;
    }
    case 0x1F801048: return mode;
    case 0x1F80104A: return ctrl;
    case 0x1F80104E: return baud;
    }
    return 0;
}

void sio_write(uint32_t phys, uint32_t v, int size) {
    (void)size;
    switch (phys) {
    case 0x1F801040:
        if (!(ctrl & 0x0002)) return;  // nothing selected
        respond(v & 0xFF);
        schedule(Event::SioAck, BYTE_TIME);
        break;
    case 0x1F801048: mode = v; break;
    case 0x1F80104A:
        if (v & 0x0040) { sio_init(); return; }  // reset
        if (v & 0x0010) irq_flag = false;        // acknowledge
        if (!(v & 0x0002)) { step = 0; device = Device::None; }  // deselect ends the exchange
        ctrl = v & ~0x0050;
        break;
    case 0x1F80104E: baud = v; break;
    }
}

}  // namespace psx::hw
