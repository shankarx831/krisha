#ifndef MOCK_JNI_H
#define MOCK_JNI_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* jobject;
typedef jobject jclass;
typedef jobject jstring;
typedef jobject jarray;
typedef jarray jfloatArray;
typedef int32_t jsize;
typedef float jfloat;
typedef uint8_t jboolean;

#define JNI_FALSE 0
#define JNI_TRUE 1

struct JNINativeInterface;

#ifdef __cplusplus
struct _JNIEnv;
typedef _JNIEnv* JNIEnv;
#else
typedef const struct JNINativeInterface* JNIEnv;
#endif

struct JNINativeInterface {
    void* reserved0;
    void* reserved1;
    void* reserved2;
    void* reserved3;
    
    const char* (*GetStringUTFChars)(JNIEnv env, jstring str, jboolean* isCopy);
    void (*ReleaseStringUTFChars)(JNIEnv env, jstring str, const char* chars);
    
    jfloatArray (*NewFloatArray)(JNIEnv env, jsize len);
    void (*SetFloatArrayRegion)(JNIEnv env, jfloatArray array, jsize start, jsize len, const jfloat* buf);
    jfloat* (*GetFloatArrayElements)(JNIEnv env, jfloatArray array, jboolean* isCopy);
    void (*ReleaseFloatArrayElements)(JNIEnv env, jfloatArray array, jfloat* elems, int32_t mode);
};

#ifdef __cplusplus
} // extern "C"

// C++ JNIEnv wrapper class mirroring the real Android NDK
struct _JNIEnv {
    const JNINativeInterface* functions;

    const char* GetStringUTFChars(jstring str, jboolean* isCopy) {
        return functions->GetStringUTFChars(this, str, isCopy);
    }

    void ReleaseStringUTFChars(jstring str, const char* chars) {
        functions->ReleaseStringUTFChars(this, str, chars);
    }

    jfloatArray NewFloatArray(jsize len) {
        return functions->NewFloatArray(this, len);
    }

    void SetFloatArrayRegion(jfloatArray array, jsize start, jsize len, const jfloat* buf) {
        functions->SetFloatArrayRegion(this, array, start, len, buf);
    }

    jfloat* GetFloatArrayElements(jfloatArray array, jboolean* isCopy) {
        return functions->GetFloatArrayElements(this, array, isCopy);
    }

    void ReleaseFloatArrayElements(jfloatArray array, jfloat* elems, int32_t mode) {
        functions->ReleaseFloatArrayElements(this, array, elems, mode);
    }
};
#endif

#define JNIEXPORT __attribute__((visibility("default")))
#define JNICALL

#endif // MOCK_JNI_H
