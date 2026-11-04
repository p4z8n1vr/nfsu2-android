#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>

#include "app/app.h"
#include "core/input.h"

namespace {

std::mutex g_input_lock;
std::array<input::Pad, 4> g_pads{};
std::array<uint32_t, 4> g_rumble{};

}

namespace input {

void Init() {
    std::lock_guard lock(g_input_lock);
    g_pads = {};
    g_rumble = {};
}

Pad Poll(int port) {
    Pad result{};

    if (port < 0 || port >= 4) return result;

    {
        std::lock_guard lock(g_input_lock);
        result = g_pads[port];
    }

    if (app::kGame.pad) {
        app::kGame.pad(port, result);
    }

    return result;
}

void Rumble(int port, uint16_t left, uint16_t right) {
    if (port < 0 || port >= 4) return;

    if (app::kGame.rumble) {
        app::kGame.rumble(port, left, right);
    }

    std::lock_guard lock(g_input_lock);

    g_rumble[port] =
        uint32_t(left) |
        (uint32_t(right) << 16);
}

}

extern "C" __attribute__((visibility("default")))
bool nfsu2_android_update_pad(
    int32_t port,
    int32_t connected,
    uint32_t buttons,
    uint64_t analog,
    int32_t lx,
    int32_t ly,
    int32_t rx,
    int32_t ry
) {
    if (port < 0 || port >= 4) return false;

    input::Pad pad{};

    if (connected) {
        pad.connected = true;
        pad.buttons = uint16_t(buttons);

        for (int i = 0; i < 8; ++i) {
            pad.analog[i] = uint8_t(
                analog >> (i * 8)
            );
        }

        auto stick = [](int32_t value) {
            return int16_t(std::clamp(
                value, -32768, 32767
            ));
        };

        pad.lx = stick(lx);
        pad.ly = stick(ly);
        pad.rx = stick(rx);
        pad.ry = stick(ry);
    }

    std::lock_guard lock(g_input_lock);
    g_pads[port] = pad;

    if (!connected) g_rumble[port] = 0;

    return true;
}

extern "C" __attribute__((visibility("default")))
uint32_t nfsu2_android_get_rumble(int32_t port) {
    if (port < 0 || port >= 4) return 0;

    std::lock_guard lock(g_input_lock);
    return g_rumble[port];
}
