#include <atomic>

#include "app/app.h"

namespace {

std::atomic<bool> g_first_swap_seen{false};

}

namespace app {

void SplashFirstFrame() {
    // Android manages its own splash screen and Activity window.
    // Notify the host that the Xbox submitted its first Swap.
    g_first_swap_seen.store(true, std::memory_order_release);
}

bool HandingOver() {
    // Android currently uses one Activity and one game process.
    // Linux cross-process window handover is not applicable.
    return false;
}

void FinishHandover() {
    // No previous desktop process or window to signal.
}

}

extern "C" __attribute__((visibility("default")))
bool nfsu2_android_first_swap_seen() {
    return g_first_swap_seen.load(std::memory_order_acquire);
}

extern "C" __attribute__((visibility("default")))
void nfsu2_android_reset_first_swap() {
    g_first_swap_seen.store(false, std::memory_order_release);
}
