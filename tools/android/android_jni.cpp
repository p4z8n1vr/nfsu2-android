#include <cstdint>

#include <jni.h>
#include <android/native_window_jni.h>

extern "C" {

bool nfsu2_android_set_data_dir(const char* directory);
bool nfsu2_android_attach_window(ANativeWindow* window);
bool nfsu2_android_detach_window();

bool nfsu2_android_update_pad(
    int32_t port,
    int32_t connected,
    uint32_t buttons,
    uint64_t analog,
    int32_t lx,
    int32_t ly,
    int32_t rx,
    int32_t ry
);

uint32_t nfsu2_android_get_rumble(int32_t port);
bool nfsu2_android_first_swap_seen();
void nfsu2_android_reset_first_swap();

JNIEXPORT jint JNICALL
Java_com_p4z8n1vr_nfsu2_NativeBridge_abiVersion(
    JNIEnv*, jobject
) {
    return 1;
}

JNIEXPORT jboolean JNICALL
Java_com_p4z8n1vr_nfsu2_NativeBridge_setDataDirectory(
    JNIEnv* env,
    jobject,
    jstring path
) {
    if (!path) return JNI_FALSE;

    const char* directory = env->GetStringUTFChars(
        path, nullptr
    );

    if (!directory) return JNI_FALSE;

    bool success = nfsu2_android_set_data_dir(directory);

    env->ReleaseStringUTFChars(path, directory);

    return success ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_p4z8n1vr_nfsu2_NativeBridge_attachSurface(
    JNIEnv* env,
    jobject,
    jobject surface
) {
    if (!surface) return JNI_FALSE;

    ANativeWindow* window =
        ANativeWindow_fromSurface(env, surface);

    if (!window) return JNI_FALSE;

    bool success = nfsu2_android_attach_window(window);

    // Android host acquires its own reference on success.
    ANativeWindow_release(window);

    return success ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_p4z8n1vr_nfsu2_NativeBridge_detachSurface(
    JNIEnv*, jobject
) {
    return nfsu2_android_detach_window()
        ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_p4z8n1vr_nfsu2_NativeBridge_updatePad(
    JNIEnv*,
    jobject,
    jint port,
    jboolean connected,
    jint buttons,
    jlong analog,
    jint lx,
    jint ly,
    jint rx,
    jint ry
) {
    bool success = nfsu2_android_update_pad(
        port,
        connected == JNI_TRUE,
        static_cast<uint32_t>(buttons),
        static_cast<uint64_t>(analog),
        lx, ly, rx, ry
    );

    return success ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_p4z8n1vr_nfsu2_NativeBridge_getRumble(
    JNIEnv*, jobject, jint port
) {
    return static_cast<jint>(
        nfsu2_android_get_rumble(port)
    );
}

JNIEXPORT jboolean JNICALL
Java_com_p4z8n1vr_nfsu2_NativeBridge_firstSwapSeen(
    JNIEnv*, jobject
) {
    return nfsu2_android_first_swap_seen()
        ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_p4z8n1vr_nfsu2_NativeBridge_resetFirstSwap(
    JNIEnv*, jobject
) {
    nfsu2_android_reset_first_swap();
}

} // extern "C"


#include <exception>
#include <string>


#include "android_game_validation.h"

namespace {

std::string JavaString(JNIEnv* env, jstring value) {
    if (!value) return {};

    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (!chars) return {};

    std::string result(chars);
    env->ReleaseStringUTFChars(value, chars);

    return result;
}

jobject MakeJavaValidation(
    JNIEnv* env,
    const android_game::ValidationResult& result
) {
    jclass clazz = env->FindClass(
        "com/p4z8n1vr/nfsu2/GameValidationResult"
    );

    if (!clazz) return nullptr;

    jmethodID constructor = env->GetMethodID(
        clazz,
        "<init>",
        "(IIJJLjava/lang/String;Ljava/lang/String;)V"
    );

    if (!constructor) {
        env->DeleteLocalRef(clazz);
        return nullptr;
    }

    jstring sha1 = env->NewStringUTF(
        result.sha1.c_str()
    );

    jstring diagnostic = env->NewStringUTF(
        result.diagnostic.c_str()
    );

    if (!sha1 || !diagnostic) {
        if (sha1) env->DeleteLocalRef(sha1);
        if (diagnostic) env->DeleteLocalRef(diagnostic);
        env->DeleteLocalRef(clazz);
        return nullptr;
    }

    jobject value = env->NewObject(
        clazz,
        constructor,
        static_cast<jint>(result.status),
        static_cast<jint>(result.type),
        static_cast<jlong>(result.title_id),
        static_cast<jlong>(result.entry_point),
        sha1,
        diagnostic
    );

    env->DeleteLocalRef(sha1);
    env->DeleteLocalRef(diagnostic);
    env->DeleteLocalRef(clazz);

    return value;
}

} // namespace

extern "C" JNIEXPORT jobject JNICALL
Java_com_p4z8n1vr_nfsu2_NativeBridge_validateGame(
    JNIEnv* env,
    jobject,
    jint descriptor,
    jstring filename
) {
    const std::string name = JavaString(env, filename);

    if (env->ExceptionCheck()) {
        return nullptr;
    }

    android_game::ValidationResult result;

    try {
        result = android_game::ValidateFile(
            descriptor,
            name
        );
    } catch (const std::exception& exception) {
        result.status =
            android_game::ValidationStatus::ReadError;

        result.diagnostic = exception.what();
    } catch (...) {
        result.status =
            android_game::ValidationStatus::ReadError;

        result.diagnostic =
            "Unknown native validation failure";
    }

    return MakeJavaValidation(env, result);
}

#include <memory>
#include <vector>

#include "kernel/disc.h"

namespace {

struct PreparedIsoSession {
    std::unique_ptr<kernel::Disc> disc;
    std::vector<uint8_t> executable;
};

jobject MakeJavaGameSessionResult(
    JNIEnv* env,
    android_game::ValidationStatus status,
    jlong handle,
    jlong executable_bytes,
    const std::string& diagnostic
) {
    jclass clazz = env->FindClass(
        "com/p4z8n1vr/nfsu2/GameSessionResult"
    );

    if (!clazz) return nullptr;

    jmethodID constructor = env->GetMethodID(
        clazz,
        "<init>",
        "(IJJLjava/lang/String;)V"
    );

    if (!constructor) {
        env->DeleteLocalRef(clazz);
        return nullptr;
    }

    jstring message = env->NewStringUTF(
        diagnostic.c_str()
    );

    if (!message) {
        env->DeleteLocalRef(clazz);
        return nullptr;
    }

    jobject result = env->NewObject(
        clazz,
        constructor,
        static_cast<jint>(status),
        handle,
        executable_bytes,
        message
    );

    env->DeleteLocalRef(message);
    env->DeleteLocalRef(clazz);

    return result;
}

} // namespace

extern "C" JNIEXPORT jobject JNICALL
Java_com_p4z8n1vr_nfsu2_NativeBridge_prepareGame(
    JNIEnv* env,
    jobject,
    jint descriptor,
    jstring filename
) {
    const std::string name = JavaString(env, filename);

    if (env->ExceptionCheck()) {
        return nullptr;
    }

    android_game::ValidationStatus status =
        android_game::ValidationStatus::ReadError;

    std::string diagnostic;
    std::unique_ptr<PreparedIsoSession> session;

    try {
        auto validation = android_game::ValidateFile(
            descriptor,
            name
        );

        status = validation.status;
        diagnostic = validation.diagnostic;

        if (validation.Ready()) {
            session = std::make_unique<PreparedIsoSession>();
            session->disc = std::make_unique<kernel::Disc>();

            std::string error;

            if (!session->disc->OpenFd(descriptor, error)) {
                status = android_game::ValidationStatus::ReadError;
                diagnostic = error;
                session.reset();
            } else if (!session->disc->Load(
                "default.xbe",
                session->executable
            )) {
                status = android_game::ValidationStatus::ReadError;
                diagnostic = "Unable to load executable from ISO";
                session.reset();
            } else {
                diagnostic = "ISO session prepared";
            }
        }
    } catch (const std::exception& error) {
        status = android_game::ValidationStatus::ReadError;
        diagnostic = error.what();
        session.reset();
    } catch (...) {
        status = android_game::ValidationStatus::ReadError;
        diagnostic = "Unexpected ISO preparation failure";
        session.reset();
    }

    const jlong handle = session
        ? reinterpret_cast<jlong>(session.get())
        : 0;

    const jlong executable_bytes = session
        ? static_cast<jlong>(session->executable.size())
        : 0;

    jobject result = MakeJavaGameSessionResult(
        env,
        status,
        handle,
        executable_bytes,
        diagnostic
    );

    if (result && session) {
        session.release();
    }

    return result;
}

extern "C" JNIEXPORT void JNICALL
Java_com_p4z8n1vr_nfsu2_NativeBridge_closeGameSession(
    JNIEnv*,
    jobject,
    jlong handle
) {
    if (handle == 0) return;

    auto* session =
        reinterpret_cast<PreparedIsoSession*>(handle);

    delete session;
}
