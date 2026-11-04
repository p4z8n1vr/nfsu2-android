#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

#include <aaudio/AAudio.h>
#include <android/log.h>

#include "audio/audio.h"

namespace {

constexpr uint32_t kChannels = 2;
constexpr uint32_t kChunk = 480;
constexpr uint32_t kCapacity = 4096;
constexpr uint32_t kTargetBuffered = 1920;

struct Output {
    std::array<float, kCapacity * kChannels> samples{};

    std::atomic<uint64_t> read{0};
    std::atomic<uint64_t> write{0};
    std::atomic<bool> running{false};

    std::function<void(float*, uint32_t)> render;
    std::thread producer;
    AAudioStream* stream = nullptr;

    ~Output() {
        running.store(false, std::memory_order_release);

        if (stream) {
            AAudioStream_requestStop(stream);
            AAudioStream_close(stream);
        }

        if (producer.joinable()) producer.join();
    }
};

Output g_audio;
std::mutex g_control;

void CopyToRing(const float* data, uint64_t offset,
                uint32_t frames) {
    uint32_t start = uint32_t(offset % kCapacity);
    uint32_t first = std::min(frames, kCapacity - start);

    std::memcpy(
        g_audio.samples.data() + start * kChannels,
        data,
        size_t(first) * kChannels * sizeof(float)
    );

    if (first < frames) {
        std::memcpy(
            g_audio.samples.data(),
            data + first * kChannels,
            size_t(frames - first) * kChannels * sizeof(float)
        );
    }
}

void Produce() {
    std::array<float, kChunk * kChannels> chunk{};

    while (g_audio.running.load(std::memory_order_acquire)) {
        uint64_t read =
            g_audio.read.load(std::memory_order_acquire);

        uint64_t write =
            g_audio.write.load(std::memory_order_relaxed);

        uint64_t buffered = write - read;

        if (buffered >= kTargetBuffered ||
            buffered + kChunk > kCapacity) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1)
            );
            continue;
        }

        g_audio.render(chunk.data(), kChunk);

        CopyToRing(chunk.data(), write, kChunk);

        g_audio.write.store(
            write + kChunk,
            std::memory_order_release
        );
    }
}

aaudio_data_callback_result_t AudioCallback(
    AAudioStream*,
    void* user,
    void* audio_data,
    int32_t num_frames
) {
    auto* output = static_cast<Output*>(user);

    if (num_frames <= 0) {
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

    auto* dst = static_cast<float*>(audio_data);
    uint32_t requested = uint32_t(num_frames);

    uint64_t read =
        output->read.load(std::memory_order_relaxed);

    uint64_t write =
        output->write.load(std::memory_order_acquire);

    uint32_t available = uint32_t(
        std::min<uint64_t>(write - read, requested)
    );

    uint32_t start = uint32_t(read % kCapacity);
    uint32_t first = std::min(
        available, kCapacity - start
    );

    std::memcpy(
        dst,
        output->samples.data() + start * kChannels,
        size_t(first) * kChannels * sizeof(float)
    );

    if (first < available) {
        std::memcpy(
            dst + first * kChannels,
            output->samples.data(),
            size_t(available - first) *
                kChannels * sizeof(float)
        );
    }

    if (available < requested) {
        std::memset(
            dst + available * kChannels,
            0,
            size_t(requested - available) *
                kChannels * sizeof(float)
        );
    }

    output->read.store(
        read + available,
        std::memory_order_release
    );

    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void StopLocked() {
    g_audio.running.store(
        false, std::memory_order_release
    );

    if (g_audio.stream) {
        AAudioStream_requestStop(g_audio.stream);
        AAudioStream_close(g_audio.stream);
        g_audio.stream = nullptr;
    }

    if (g_audio.producer.joinable()) {
        g_audio.producer.join();
    }

    g_audio.render = {};
}

} // namespace

namespace audio {

void StartOutput(
    std::function<void(float*, uint32_t)> render
) {
    if (!render) return;

    std::lock_guard lock(g_control);

    if (g_audio.stream) return;

    AAudioStreamBuilder* builder = nullptr;

    aaudio_result_t result =
        AAudio_createStreamBuilder(&builder);

    if (result != AAUDIO_OK || !builder) {
        __android_log_print(
            ANDROID_LOG_ERROR, "NFSU2Audio",
            "AAudio builder failed: %d", result
        );
        return;
    }

    AAudioStreamBuilder_setDirection(
        builder, AAUDIO_DIRECTION_OUTPUT
    );

    AAudioStreamBuilder_setSampleRate(
        builder, int32_t(audio::kRate)
    );

    AAudioStreamBuilder_setChannelCount(
        builder, int32_t(kChannels)
    );

    AAudioStreamBuilder_setFormat(
        builder, AAUDIO_FORMAT_PCM_FLOAT
    );

    AAudioStreamBuilder_setSharingMode(
        builder, AAUDIO_SHARING_MODE_SHARED
    );

    AAudioStreamBuilder_setPerformanceMode(
        builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY
    );

    AAudioStreamBuilder_setDataCallback(
        builder, AudioCallback, &g_audio
    );

    AAudioStream* stream = nullptr;

    result = AAudioStreamBuilder_openStream(
        builder, &stream
    );

    AAudioStreamBuilder_delete(builder);

    if (result != AAUDIO_OK || !stream) {
        __android_log_print(
            ANDROID_LOG_ERROR, "NFSU2Audio",
            "AAudio open failed: %d", result
        );
        return;
    }

    if (AAudioStream_getSampleRate(stream) !=
            int32_t(audio::kRate) ||
        AAudioStream_getChannelCount(stream) !=
            int32_t(kChannels) ||
        AAudioStream_getFormat(stream) !=
            AAUDIO_FORMAT_PCM_FLOAT) {

        __android_log_print(
            ANDROID_LOG_ERROR, "NFSU2Audio",
            "Unexpected audio stream format"
        );

        AAudioStream_close(stream);
        return;
    }

    g_audio.read.store(0);
    g_audio.write.store(0);
    g_audio.render = std::move(render);
    g_audio.stream = stream;

    g_audio.running.store(
        true, std::memory_order_release
    );

    g_audio.producer = std::thread(Produce);

    result = AAudioStream_requestStart(stream);

    if (result != AAUDIO_OK) {
        __android_log_print(
            ANDROID_LOG_ERROR, "NFSU2Audio",
            "AAudio start failed: %d", result
        );

        StopLocked();
        return;
    }

    __android_log_print(
        ANDROID_LOG_INFO, "NFSU2Audio",
        "Started 48000 Hz stereo float output"
    );
}

} // namespace audio

extern "C" __attribute__((visibility("default")))
void nfsu2_android_stop_audio() {
    std::lock_guard lock(g_control);
    StopLocked();
}
