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
