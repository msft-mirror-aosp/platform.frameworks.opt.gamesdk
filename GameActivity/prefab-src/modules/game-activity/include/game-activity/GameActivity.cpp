/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#define LOG_TAG "GameActivity"

#include "GameActivity.h"

#include <android/api-level.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <android/looper.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <jni.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/system_properties.h>
#include <sys/types.h>
#include <unistd.h>

#include <memory>
#include <string>

// TODO(b/187147166): these functions were extracted from the Game SDK
// (gamesdk/src/common/system_utils.h). system_utils.h/cpp should be used instead.
namespace {

#if __ANDROID_API__ >= 26
std::string getSystemPropViaCallback(const char *key,
                                     const char *default_value = "") {
  const prop_info *prop = __system_property_find(key);
  if (prop == nullptr) {
    return default_value;
  }
  std::string return_value;
  auto thunk = [](void *cookie, const char * /*name*/, const char *value,
                  uint32_t /*serial*/) {
    if (value != nullptr) {
      std::string *r = static_cast<std::string *>(cookie);
      *r = value;
    }
  };
  __system_property_read_callback(prop, thunk, &return_value);
  return return_value;
}
#else
std::string getSystemPropViaGet(const char *key,
                                const char *default_value = "") {
  char buffer[PROP_VALUE_MAX + 1] = "";  // +1 for terminator
  int bufferLen = __system_property_get(key, buffer);
  if (bufferLen > 0)
    return buffer;
  else
    return "";
}
#endif

std::string GetSystemProp(const char *key, const char *default_value = "") {
#if __ANDROID_API__ >= 26
  return getSystemPropViaCallback(key, default_value);
#else
  return getSystemPropViaGet(key, default_value);
#endif
}

int GetSystemPropAsInt(const char *key, int default_value = 0) {
  std::string prop = GetSystemProp(key);
  return prop == "" ? default_value : strtoll(prop.c_str(), nullptr, 10);
}

}  // namespace

#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__);
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__);
#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__);
#ifdef NDEBUG
#define ALOGV(...)
#else
#define ALOGV(...) \
  __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__);
#endif

/* Returns 2nd arg.  Used to substitute default value if caller's vararg list
 * is empty.
 */
#define __android_second(first, second, ...) second

/* If passed multiple args, returns ',' followed by all but 1st arg, otherwise
 * returns nothing.
 */
#define __android_rest(first, ...) , ##__VA_ARGS__

#define android_printAssert(cond, tag, fmt...) \
  __android_log_assert(cond, tag,              \
                       __android_second(0, ##fmt, NULL) __android_rest(fmt))

#define CONDITION(cond) (__builtin_expect((cond) != 0, 0))

#ifndef LOG_ALWAYS_FATAL_IF
#define LOG_ALWAYS_FATAL_IF(cond, ...)                              \
  ((CONDITION(cond))                                                \
       ? ((void)android_printAssert(#cond, LOG_TAG, ##__VA_ARGS__)) \
       : (void)0)
#endif

#ifndef LOG_ALWAYS_FATAL
#define LOG_ALWAYS_FATAL(...) \
  (((void)android_printAssert(NULL, LOG_TAG, ##__VA_ARGS__)))
#endif

/*
 * Simplified macro to send a warning system log message using current LOG_TAG.
 */
#ifndef SLOGW
#define SLOGW(...) \
  ((void)__android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__))
#endif

#ifndef SLOGW_IF
#define SLOGW_IF(cond, ...)                                                  \
  ((__predict_false(cond))                                                   \
       ? ((void)__android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)) \
       : (void)0)
#endif

/*
 * Versions of LOG_ALWAYS_FATAL_IF and LOG_ALWAYS_FATAL that
 * are stripped out of release builds.
 */
#if LOG_NDEBUG

#ifndef LOG_FATAL_IF
#define LOG_FATAL_IF(cond, ...) ((void)0)
#endif
#ifndef LOG_FATAL
#define LOG_FATAL(...) ((void)0)
#endif

#else

#ifndef LOG_FATAL_IF
#define LOG_FATAL_IF(cond, ...) LOG_ALWAYS_FATAL_IF(cond, ##__VA_ARGS__)
#endif
#ifndef LOG_FATAL
#define LOG_FATAL(...) LOG_ALWAYS_FATAL(__VA_ARGS__)
#endif

#endif

/*
 * Assertion that generates a log message when the assertion fails.
 * Stripped out of release builds.  Uses the current LOG_TAG.
 */
#ifndef ALOG_ASSERT
#define ALOG_ASSERT(cond, ...) LOG_FATAL_IF(!(cond), ##__VA_ARGS__)
#endif

#define LOG_TRACE(...)

#ifndef NELEM
#define NELEM(x) ((int)(sizeof(x) / sizeof((x)[0])))
#endif

/*
 * JNI methods of the GameActivity Java class.
 */
static struct {
  jmethodID finish;
  jmethodID setWindowFlags;
  jmethodID setWindowFormat;
} gGameActivityClassInfo;

/*
 * Contains a command to be executed by the GameActivity
 * on the application main thread.
 */
struct ActivityWork {
  int32_t cmd;
  int64_t arg1;
  int64_t arg2;
};

/*
 * The type of commands that can be passed to the GameActivity and that
 * are executed on the application main thread.
 */
enum {
  CMD_FINISH = 1,
  CMD_SET_WINDOW_FORMAT,
  CMD_SET_WINDOW_FLAGS,
  CMD_SHOW_SOFT_INPUT,
  CMD_HIDE_SOFT_INPUT,
  CMD_SET_SOFT_INPUT_STATE
};

/*
 * Write a command to be executed by the GameActivity on the application main
 * thread.
 */
static void write_work(int fd, int32_t cmd, int64_t arg1 = 0,
                       int64_t arg2 = 0) {
  ActivityWork work;
  work.cmd = cmd;
  work.arg1 = arg1;
  work.arg2 = arg2;

  LOG_TRACE("write_work: cmd=%d", cmd);
restart:
  int res = write(fd, &work, sizeof(work));
  if (res < 0 && errno == EINTR) {
    goto restart;
  }

  if (res == sizeof(work)) return;

  if (res < 0) {
    ALOGW("Failed writing to work fd: %s", strerror(errno));
  } else {
    ALOGW("Truncated writing to work fd: %d", res);
  }
}

/*
 * Read commands to be executed by the GameActivity on the application main
 * thread.
 */
static bool read_work(int fd, ActivityWork *outWork) {
  int res = read(fd, outWork, sizeof(ActivityWork));
  // no need to worry about EINTR, poll loop will just come back again.
  if (res == sizeof(ActivityWork)) return true;

  if (res < 0) {
    ALOGW("Failed reading work fd: %s", strerror(errno));
  } else {
    ALOGW("Truncated reading work fd: %d", res);
  }
  return false;
}

/*
 * Native state for interacting with the GameActivity class.
 */
struct NativeCode : public GameActivity {
  NativeCode(void *_dlhandle, GameActivity_createFunc *_createFunc) {
    memset((GameActivity *)this, 0, sizeof(GameActivity));
    memset(&callbacks, 0, sizeof(callbacks));
    dlhandle = _dlhandle;
    createActivityFunc = _createFunc;
    nativeWindow = NULL;
    mainWorkRead = mainWorkWrite = -1;
    gameInput = NULL;
  }

  ~NativeCode() {
    if (callbacks.onDestroy != NULL) {
      callbacks.onDestroy(this);
    }
    if (env != NULL) {
      if (javaGameActivity != NULL) {
        env->DeleteGlobalRef(javaGameActivity);
      }
      if (javaAssetManager != NULL) {
        env->DeleteGlobalRef(javaAssetManager);
      }
    }
    GameInput_destroy(gameInput);
    if (looper != NULL && mainWorkRead >= 0) {
      ALooper_removeFd(looper, mainWorkRead);
    }
    ALooper_release(looper);
    looper = NULL;

    setSurface(NULL);
    if (mainWorkRead >= 0) close(mainWorkRead);
    if (mainWorkWrite >= 0) close(mainWorkWrite);
    if (dlhandle != NULL) {
      // for now don't unload...  we probably should clean this
      // up and only keep one open dlhandle per proc, since there
      // is really no benefit to unloading the code.
      // dlclose(dlhandle);
    }
  }

  void setSurface(jobject _surface) {
    if (nativeWindow != NULL) {
      ANativeWindow_release(nativeWindow);
    }
    if (_surface != NULL) {
      nativeWindow = ANativeWindow_fromSurface(env, _surface);
    } else {
      nativeWindow = NULL;
    }
  }

  GameActivityCallbacks callbacks;

  void *dlhandle;
  GameActivity_createFunc *createActivityFunc;

  std::string internalDataPathObj;
  std::string externalDataPathObj;
  std::string obbPathObj;

  ANativeWindow *nativeWindow;
  int32_t lastWindowWidth;
  int32_t lastWindowHeight;

  // These are used to wake up the main thread to process work.
  int mainWorkRead;
  int mainWorkWrite;
  ALooper *looper;

  // Need to hold on to a reference here in case the upper layers destroy our
  // AssetManager.
  jobject javaAssetManager;

  GameInput *gameInput;
};

extern "C" void GameActivity_finish(GameActivity *activity) {
  NativeCode *code = static_cast<NativeCode *>(activity);
  write_work(code->mainWorkWrite, CMD_FINISH, 0);
}

extern "C" void GameActivity_setWindowFormat(GameActivity *activity,
                                             int32_t format) {
  NativeCode *code = static_cast<NativeCode *>(activity);
  write_work(code->mainWorkWrite, CMD_SET_WINDOW_FORMAT, format);
}

extern "C" void GameActivity_setWindowFlags(GameActivity *activity,
                                            uint32_t values, uint32_t mask) {
  NativeCode *code = static_cast<NativeCode *>(activity);
  write_work(code->mainWorkWrite, CMD_SET_WINDOW_FLAGS, values, mask);
}

extern "C" void GameActivity_showSoftInput(GameActivity *activity,
                                           uint32_t flags) {
  NativeCode *code = static_cast<NativeCode *>(activity);
  write_work(code->mainWorkWrite, CMD_SHOW_SOFT_INPUT, flags);
}

extern "C" void GameActivity_setTextInputState(GameActivity *activity,
                                               const GameInputState *state) {
  NativeCode *code = static_cast<NativeCode *>(activity);
  // This state is freed in the loop where it is processed.
  GameInputState *state_copy = (GameInputState *)malloc(sizeof(GameInputState));
  GameInputState_constructEmpty(state_copy);
  GameInputState_set(state_copy, state);
  write_work(code->mainWorkWrite, CMD_SET_SOFT_INPUT_STATE,
             reinterpret_cast<int64_t>(state_copy), 0);
}

extern "C" const GameInputState *GameActivity_getTextInputState(
    GameActivity *activity) {
  NativeCode *code = static_cast<NativeCode *>(activity);
  return GameInput_getState(code->gameInput);
}

extern "C" void GameActivity_hideSoftInput(GameActivity *activity,
                                           uint32_t flags) {
  NativeCode *code = static_cast<NativeCode *>(activity);
  write_work(code->mainWorkWrite, CMD_HIDE_SOFT_INPUT, flags);
}

/*
 * Log the JNI exception, if any.
 */
static void checkAndClearException(JNIEnv *env, const char *methodName) {
  if (env->ExceptionCheck()) {
    ALOGE("Exception while running %s", methodName);
    env->ExceptionDescribe();
    env->ExceptionClear();
  }
}

/*
 * Callback for handling native events on the application's main thread.
 */
static int mainWorkCallback(int fd, int events, void *data) {
  ALOGD("************** mainWorkCallback *********");
  NativeCode *code = (NativeCode *)data;
  if ((events & POLLIN) == 0) {
    return 1;
  }

  ActivityWork work;
  if (!read_work(code->mainWorkRead, &work)) {
    return 1;
  }
  LOG_TRACE("mainWorkCallback: cmd=%d", work.cmd);
  switch (work.cmd) {
    case CMD_FINISH: {
      code->env->CallVoidMethod(code->javaGameActivity,
                                gGameActivityClassInfo.finish);
      checkAndClearException(code->env, "finish");
    } break;
    case CMD_SET_WINDOW_FORMAT: {
      code->env->CallVoidMethod(code->javaGameActivity,
                                gGameActivityClassInfo.setWindowFormat,
                                work.arg1);
      checkAndClearException(code->env, "setWindowFormat");
    } break;
    case CMD_SET_WINDOW_FLAGS: {
      code->env->CallVoidMethod(code->javaGameActivity,
                                gGameActivityClassInfo.setWindowFlags,
                                work.arg1, work.arg2);
      checkAndClearException(code->env, "setWindowFlags");
    } break;
    case CMD_SHOW_SOFT_INPUT: {
      GameInput_showIme(code->gameInput, work.arg1);
    } break;
    case CMD_SET_SOFT_INPUT_STATE: {
      GameInputState *state = reinterpret_cast<GameInputState *>(work.arg1);
      GameInput_setState(code->gameInput, state);
      GameInputState_destruct(state);
      free(state);
      checkAndClearException(code->env, "setTextInputState");
    } break;
    case CMD_HIDE_SOFT_INPUT: {
      GameInput_hideIme(code->gameInput, work.arg1);
    } break;
    default:
      ALOGW("Unknown work command: %d", work.cmd);
      break;
  }

  return 1;
}

// ------------------------------------------------------------------------

static thread_local std::string g_error_msg;

static jlong loadNativeCode_native(JNIEnv *env, jobject javaGameActivity,
                                   jstring path, jstring funcName,
                                   jstring internalDataDir, jstring obbDir,
                                   jstring externalDataDir, jobject jAssetMgr,
                                   jbyteArray savedState) {
  LOG_TRACE("loadNativeCode_native");
  const char *pathStr = env->GetStringUTFChars(path, NULL);
  NativeCode *code = NULL;

  void *handle = dlopen(pathStr, RTLD_LAZY);

  env->ReleaseStringUTFChars(path, pathStr);

  if (handle == nullptr) {
    g_error_msg = dlerror();
    ALOGE("GameActivity dlopen(\"%s\") failed: %s", pathStr,
          g_error_msg.c_str());
    return 0;
  }

  const char *funcStr = env->GetStringUTFChars(funcName, NULL);
  code =
      new NativeCode(handle, (GameActivity_createFunc *)dlsym(handle, funcStr));
  env->ReleaseStringUTFChars(funcName, funcStr);

  if (code->createActivityFunc == nullptr) {
    g_error_msg = dlerror();
    ALOGW("GameActivity_onCreate not found: %s", g_error_msg.c_str());
    delete code;
    return 0;
  }

  code->looper = ALooper_forThread();
  if (code->looper == nullptr) {
    g_error_msg = "Unable to retrieve native ALooper";
    ALOGW("%s", g_error_msg.c_str());
    delete code;
    return 0;
  }
  ALooper_acquire(code->looper);

  int msgpipe[2];
  if (pipe(msgpipe)) {
    g_error_msg = "could not create pipe: ";
    g_error_msg += strerror(errno);

    ALOGW("%s", g_error_msg.c_str());
    delete code;
    return 0;
  }
  code->mainWorkRead = msgpipe[0];
  code->mainWorkWrite = msgpipe[1];
  int result = fcntl(code->mainWorkRead, F_SETFL, O_NONBLOCK);
  SLOGW_IF(result != 0,
           "Could not make main work read pipe "
           "non-blocking: %s",
           strerror(errno));
  result = fcntl(code->mainWorkWrite, F_SETFL, O_NONBLOCK);
  SLOGW_IF(result != 0,
           "Could not make main work write pipe "
           "non-blocking: %s",
           strerror(errno));
  ALooper_addFd(code->looper, code->mainWorkRead, 0, ALOOPER_EVENT_INPUT,
                mainWorkCallback, code);

  code->GameActivity::callbacks = &code->callbacks;
  if (env->GetJavaVM(&code->vm) < 0) {
    ALOGW("GameActivity GetJavaVM failed");
    delete code;
    return 0;
  }
  code->env = env;
  code->javaGameActivity = env->NewGlobalRef(javaGameActivity);

  const char *dirStr =
      internalDataDir ? env->GetStringUTFChars(internalDataDir, NULL) : "";
  code->internalDataPathObj = dirStr;
  code->internalDataPath = code->internalDataPathObj.c_str();
  if (internalDataDir) env->ReleaseStringUTFChars(internalDataDir, dirStr);

  dirStr = externalDataDir ? env->GetStringUTFChars(externalDataDir, NULL) : "";
  code->externalDataPathObj = dirStr;
  code->externalDataPath = code->externalDataPathObj.c_str();
  if (externalDataDir) env->ReleaseStringUTFChars(externalDataDir, dirStr);

  code->javaAssetManager = env->NewGlobalRef(jAssetMgr);
  code->assetManager = AAssetManager_fromJava(env, jAssetMgr);

  dirStr = obbDir ? env->GetStringUTFChars(obbDir, NULL) : "";
  code->obbPathObj = dirStr;
  code->obbPath = code->obbPathObj.c_str();
  if (obbDir) env->ReleaseStringUTFChars(obbDir, dirStr);

  jbyte *rawSavedState = NULL;
  jsize rawSavedSize = 0;
  if (savedState != NULL) {
    rawSavedState = env->GetByteArrayElements(savedState, NULL);
    rawSavedSize = env->GetArrayLength(savedState);
  }
  code->createActivityFunc(code, rawSavedState, rawSavedSize);

  code->gameInput = GameInput_init(env);
  GameInput_setEventCallback(
      code->gameInput,
      reinterpret_cast<void (*)(void *, const GameInputState *)>(
          code->callbacks.onTextInputEvent),
      code);

  if (rawSavedState != NULL) {
    env->ReleaseByteArrayElements(savedState, rawSavedState, 0);
  }

  return reinterpret_cast<jlong>(code);
}

static jstring getDlError_native(JNIEnv *env, jobject javaGameActivity) {
  jstring result = env->NewStringUTF(g_error_msg.c_str());
  g_error_msg.clear();
  return result;
}

static void unloadNativeCode_native(JNIEnv *env, jobject javaGameActivity,
                                    jlong handle) {
  LOG_TRACE("unloadNativeCode_native");
  if (handle != 0) {
    NativeCode *code = (NativeCode *)handle;
    delete code;
  }
}

static void onStart_native(JNIEnv *env, jobject javaGameActivity,
                           jlong handle) {
  ALOGV("onStart_native");
  if (handle != 0) {
    NativeCode *code = (NativeCode *)handle;
    if (code->callbacks.onStart != NULL) {
      code->callbacks.onStart(code);
    }
  }
}

static void onResume_native(JNIEnv *env, jobject javaGameActivity,
                            jlong handle) {
  LOG_TRACE("onResume_native");
  if (handle != 0) {
    NativeCode *code = (NativeCode *)handle;
    if (code->callbacks.onResume != NULL) {
      code->callbacks.onResume(code);
    }
  }
}

static jbyteArray onSaveInstanceState_native(JNIEnv *env,
                                             jobject javaGameActivity,
                                             jlong handle) {
  LOG_TRACE("onSaveInstanceState_native");

  jbyteArray array = NULL;

  if (handle != 0) {
    NativeCode *code = (NativeCode *)handle;
    if (code->callbacks.onSaveInstanceState != NULL) {
      size_t len = 0;
      jbyte *state = (jbyte *)code->callbacks.onSaveInstanceState(code, &len);
      if (len > 0) {
        array = env->NewByteArray(len);
        if (array != NULL) {
          env->SetByteArrayRegion(array, 0, len, state);
        }
      }
      if (state != NULL) {
        free(state);
      }
    }
  }
  return array;
}

static void onPause_native(JNIEnv *env, jobject javaGameActivity,
                           jlong handle) {
  LOG_TRACE("onPause_native");
  if (handle != 0) {
    NativeCode *code = (NativeCode *)handle;
    if (code->callbacks.onPause != NULL) {
      code->callbacks.onPause(code);
    }
  }
}

static void onStop_native(JNIEnv *env, jobject javaGameActivity, jlong handle) {
  LOG_TRACE("onStop_native");
  if (handle != 0) {
    NativeCode *code = (NativeCode *)handle;
    if (code->callbacks.onStop != NULL) {
      code->callbacks.onStop(code);
    }
  }
}

static void onConfigurationChanged_native(JNIEnv *env, jobject javaGameActivity,
                                          jlong handle) {
  LOG_TRACE("onConfigurationChanged_native");
  if (handle != 0) {
    NativeCode *code = (NativeCode *)handle;
    if (code->callbacks.onConfigurationChanged != NULL) {
      code->callbacks.onConfigurationChanged(code);
    }
  }
}

static void onLowMemory_native(JNIEnv *env, jobject javaGameActivity,
                               jlong handle) {
  LOG_TRACE("onLowMemory_native");
  if (handle != 0) {
    NativeCode *code = (NativeCode *)handle;
    if (code->callbacks.onLowMemory != NULL) {
      code->callbacks.onLowMemory(code);
    }
  }
}

static void onWindowFocusChanged_native(JNIEnv *env, jobject javaGameActivity,
                                        jlong handle, jboolean focused) {
  LOG_TRACE("onWindowFocusChanged_native");
  if (handle != 0) {
    NativeCode *code = (NativeCode *)handle;
    if (code->callbacks.onWindowFocusChanged != NULL) {
      code->callbacks.onWindowFocusChanged(code, focused ? 1 : 0);
    }
  }
}

static void onSurfaceCreated_native(JNIEnv *env, jobject javaGameActivity,
                                    jlong handle, jobject surface) {
  ALOGV("onSurfaceCreated_native");
  LOG_TRACE("onSurfaceCreated_native");
  if (handle != 0) {
    NativeCode *code = (NativeCode *)handle;
    code->setSurface(surface);

    if (code->nativeWindow != NULL &&
        code->callbacks.onNativeWindowCreated != NULL) {
      code->callbacks.onNativeWindowCreated(code, code->nativeWindow);
    }
  }
}

static void onSurfaceChanged_native(JNIEnv *env, jobject javaGameActivity,
                                    jlong handle, jobject surface, jint format,
                                    jint width, jint height) {
  LOG_TRACE("onSurfaceChanged_native");
  if (handle != 0) {
    NativeCode *code = (NativeCode *)handle;
    ANativeWindow *oldNativeWindow = code->nativeWindow;
    code->setSurface(surface);
    if (oldNativeWindow != code->nativeWindow) {
      if (oldNativeWindow != NULL &&
          code->callbacks.onNativeWindowDestroyed != NULL) {
        code->callbacks.onNativeWindowDestroyed(code, oldNativeWindow);
      }
      if (code->nativeWindow != NULL) {
        if (code->callbacks.onNativeWindowCreated != NULL) {
          code->callbacks.onNativeWindowCreated(code, code->nativeWindow);
        }

        code->lastWindowWidth = ANativeWindow_getWidth(code->nativeWindow);
        code->lastWindowHeight = ANativeWindow_getHeight(code->nativeWindow);
      }
    } else {
      // Maybe it was resized?
      int32_t newWidth = ANativeWindow_getWidth(code->nativeWindow);
      int32_t newHeight = ANativeWindow_getHeight(code->nativeWindow);
      if (newWidth != code->lastWindowWidth ||
          newHeight != code->lastWindowHeight) {
        if (code->callbacks.onNativeWindowResized != NULL) {
          code->callbacks.onNativeWindowResized(code, code->nativeWindow);
        }
      }
    }
  }
}

static void onSurfaceRedrawNeeded_native(JNIEnv *env, jobject javaGameActivity,
                                         jlong handle) {
  LOG_TRACE("onSurfaceRedrawNeeded_native");
  if (handle != 0) {
    NativeCode *code = (NativeCode *)handle;
    if (code->nativeWindow != NULL &&
        code->callbacks.onNativeWindowRedrawNeeded != NULL) {
      code->callbacks.onNativeWindowRedrawNeeded(code, code->nativeWindow);
    }
  }
}

static void onSurfaceDestroyed_native(JNIEnv *env, jobject javaGameActivity,
                                      jlong handle) {
  LOG_TRACE("onSurfaceDestroyed_native");
  if (handle != 0) {
    NativeCode *code = (NativeCode *)handle;
    if (code->nativeWindow != NULL &&
        code->callbacks.onNativeWindowDestroyed != NULL) {
      code->callbacks.onNativeWindowDestroyed(code, code->nativeWindow);
    }
    code->setSurface(NULL);
  }
}

static void onContentRectChanged_native(JNIEnv *env, jobject javaGameActivity,
                                        jlong handle, jint x, jint y, jint w,
                                        jint h) {
  LOG_TRACE("onContentRectChanged_native");
  if (handle != 0) {
    NativeCode *code = (NativeCode *)handle;
    if (code->callbacks.onContentRectChanged != NULL) {
      ARect rect;
      rect.left = x;
      rect.top = y;
      rect.right = x + w;
      rect.bottom = y + h;
      code->callbacks.onContentRectChanged(code, &rect);
    }
  }
}

static bool enabledAxes[GAME_ACTIVITY_POINTER_INFO_AXIS_COUNT] = {
    /* AMOTION_EVENT_AXIS_X */ true,
    /* AMOTION_EVENT_AXIS_Y */ true,
    // Disable all other axes by default (they can be enabled using
    // `GameActivityInputInfo_enableAxis`).
    false};

extern "C" void GameActivityInputInfo_enableAxis(int32_t axis) {
  if (axis < 0 || axis >= GAME_ACTIVITY_POINTER_INFO_AXIS_COUNT) {
    return;
  }

  enabledAxes[axis] = true;
}

extern "C" void GameActivityInputInfo_disableAxis(int32_t axis) {
  if (axis < 0 || axis >= GAME_ACTIVITY_POINTER_INFO_AXIS_COUNT) {
    return;
  }

  enabledAxes[axis] = false;
}

static struct {
  jmethodID getDeviceId;
  jmethodID getSource;
  jmethodID getAction;

  jmethodID getEventTime;
  jmethodID getDownTime;

  jmethodID getFlags;
  jmethodID getMetaState;

  jmethodID getActionButton;
  jmethodID getButtonState;
  jmethodID getClassification;
  jmethodID getEdgeFlags;

  jmethodID getPointerCount;
  jmethodID getPointerId;
  jmethodID getRawX;
  jmethodID getRawY;
  jmethodID getXPrecision;
  jmethodID getYPrecision;
  jmethodID getAxisValue;
} gMotionEventClassInfo;

extern "C" GameActivityMotionEvent *GameActivityMotionEvent_fromJava(
    JNIEnv *env, jobject motionEvent) {
  static bool gMotionEventClassInfoInitialized = false;
  if (!gMotionEventClassInfoInitialized) {
    int sdkVersion = GetSystemPropAsInt("ro.build.version.sdk");
    gMotionEventClassInfo = {0};
    jclass motionEventClass = env->FindClass("android/view/MotionEvent");
    gMotionEventClassInfo.getDeviceId =
        env->GetMethodID(motionEventClass, "getDeviceId", "()I");
    gMotionEventClassInfo.getSource =
        env->GetMethodID(motionEventClass, "getSource", "()I");
    gMotionEventClassInfo.getAction =
        env->GetMethodID(motionEventClass, "getAction", "()I");
    gMotionEventClassInfo.getEventTime =
        env->GetMethodID(motionEventClass, "getEventTime", "()J");
    gMotionEventClassInfo.getDownTime =
        env->GetMethodID(motionEventClass, "getDownTime", "()J");
    gMotionEventClassInfo.getFlags =
        env->GetMethodID(motionEventClass, "getFlags", "()I");
    gMotionEventClassInfo.getMetaState =
        env->GetMethodID(motionEventClass, "getMetaState", "()I");
    if (sdkVersion >= 23) {
      gMotionEventClassInfo.getActionButton =
          env->GetMethodID(motionEventClass, "getActionButton", "()I");
    }
    if (sdkVersion >= 14) {
      gMotionEventClassInfo.getButtonState =
          env->GetMethodID(motionEventClass, "getButtonState", "()I");
    }
    if (sdkVersion >= 29) {
      gMotionEventClassInfo.getClassification =
          env->GetMethodID(motionEventClass, "getClassification", "()I");
    }
    gMotionEventClassInfo.getEdgeFlags =
        env->GetMethodID(motionEventClass, "getEdgeFlags", "()I");
    gMotionEventClassInfo.getPointerCount =
        env->GetMethodID(motionEventClass, "getPointerCount", "()I");
    gMotionEventClassInfo.getPointerId =
        env->GetMethodID(motionEventClass, "getPointerId", "(I)I");
    if (sdkVersion >= 29) {
      gMotionEventClassInfo.getRawX =
          env->GetMethodID(motionEventClass, "getRawX", "(I)F");
      gMotionEventClassInfo.getRawY =
          env->GetMethodID(motionEventClass, "getRawY", "(I)F");
    }
    gMotionEventClassInfo.getXPrecision =
        env->GetMethodID(motionEventClass, "getXPrecision", "()F");
    gMotionEventClassInfo.getYPrecision =
        env->GetMethodID(motionEventClass, "getYPrecision", "()F");
    gMotionEventClassInfo.getAxisValue =
        env->GetMethodID(motionEventClass, "getAxisValue", "(II)F");

    gMotionEventClassInfoInitialized = true;
  }

  uint32_t pointerCount =
      env->CallIntMethod(motionEvent, gMotionEventClassInfo.getPointerCount);
  GameActivityInputInfo *pointers = new GameActivityInputInfo[pointerCount];
  for (uint32_t i = 0; i < pointerCount; ++i) {
    pointers[i] = {
        /*id=*/env->CallIntMethod(motionEvent,
                                  gMotionEventClassInfo.getPointerId, i),
        /*axisValues=*/{0},
        /*rawX=*/gMotionEventClassInfo.getRawX
            ? env->CallFloatMethod(motionEvent, gMotionEventClassInfo.getRawX,
                                   i)
            : 0,
        /*rawY=*/gMotionEventClassInfo.getRawY
            ? env->CallFloatMethod(motionEvent, gMotionEventClassInfo.getRawY,
                                   i)
            : 0,
    };

    for (uint32_t axisIndex = 0;
         axisIndex < GAME_ACTIVITY_POINTER_INFO_AXIS_COUNT; ++axisIndex) {
      if (enabledAxes[axisIndex]) {
        pointers[i].axisValues[axisIndex] = env->CallFloatMethod(
            motionEvent, gMotionEventClassInfo.getAxisValue, axisIndex, i);
      }
    }
  }

  GameActivityMotionEvent *event = new GameActivityMotionEvent{
      /*deviceId=*/env->CallIntMethod(motionEvent,
                                      gMotionEventClassInfo.getDeviceId),
      /*source=*/
      env->CallIntMethod(motionEvent, gMotionEventClassInfo.getSource),
      /*action=*/
      env->CallIntMethod(motionEvent, gMotionEventClassInfo.getAction),
      // TODO: introduce a millisecondsToNanoseconds helper:
      /*eventTime=*/
      env->CallLongMethod(motionEvent, gMotionEventClassInfo.getEventTime) *
          1000000,
      /*downTime=*/
      env->CallLongMethod(motionEvent, gMotionEventClassInfo.getDownTime) *
          1000000,
      /*flags=*/
      env->CallIntMethod(motionEvent, gMotionEventClassInfo.getFlags),
      /*metaState=*/
      env->CallIntMethod(motionEvent, gMotionEventClassInfo.getMetaState),
      /*actionButton=*/gMotionEventClassInfo.getActionButton
          ? env->CallIntMethod(motionEvent,
                               gMotionEventClassInfo.getActionButton)
          : 0,
      /*buttonState=*/gMotionEventClassInfo.getButtonState
          ? env->CallIntMethod(motionEvent,
                               gMotionEventClassInfo.getButtonState)
          : 0,
      /*classification=*/gMotionEventClassInfo.getClassification
          ? env->CallIntMethod(motionEvent,
                               gMotionEventClassInfo.getClassification)
          : 0,
      /*edgeFlags=*/
      env->CallIntMethod(motionEvent, gMotionEventClassInfo.getEdgeFlags),
      pointerCount,
      pointers,
      /*precisionX=*/
      env->CallFloatMethod(motionEvent, gMotionEventClassInfo.getXPrecision),
      /*precisionY=*/
      env->CallFloatMethod(motionEvent, gMotionEventClassInfo.getYPrecision),
  };

  return event;
}

extern "C" void GameActivityMotionEvent_release(
    GameActivityMotionEvent *event) {
  delete[] event->pointers;
  delete event;
}

static struct {
  jmethodID getDeviceId;
  jmethodID getSource;
  jmethodID getAction;

  jmethodID getEventTime;
  jmethodID getDownTime;

  jmethodID getFlags;
  jmethodID getMetaState;

  jmethodID getModifiers;
  jmethodID getRepeatCount;
  jmethodID getKeyCode;
} gKeyEventClassInfo;

extern "C" GameActivityKeyEvent *GameActivityKeyEvent_fromJava(
    JNIEnv *env, jobject keyEvent) {
  static bool gKeyEventClassInfoInitialized = false;
  if (!gKeyEventClassInfoInitialized) {
    int sdkVersion = GetSystemPropAsInt("ro.build.version.sdk");
    gKeyEventClassInfo = {0};
    jclass keyEventClass = env->FindClass("android/view/KeyEvent");
    gKeyEventClassInfo.getDeviceId =
        env->GetMethodID(keyEventClass, "getDeviceId", "()I");
    gKeyEventClassInfo.getSource =
        env->GetMethodID(keyEventClass, "getSource", "()I");
    gKeyEventClassInfo.getAction =
        env->GetMethodID(keyEventClass, "getAction", "()I");
    gKeyEventClassInfo.getEventTime =
        env->GetMethodID(keyEventClass, "getEventTime", "()J");
    gKeyEventClassInfo.getDownTime =
        env->GetMethodID(keyEventClass, "getDownTime", "()J");
    gKeyEventClassInfo.getFlags =
        env->GetMethodID(keyEventClass, "getFlags", "()I");
    gKeyEventClassInfo.getMetaState =
        env->GetMethodID(keyEventClass, "getMetaState", "()I");
    if (sdkVersion >= 13) {
      gKeyEventClassInfo.getModifiers =
          env->GetMethodID(keyEventClass, "getModifiers", "()I");
    }
    gKeyEventClassInfo.getRepeatCount =
        env->GetMethodID(keyEventClass, "getRepeatCount", "()I");
    gKeyEventClassInfo.getKeyCode =
        env->GetMethodID(keyEventClass, "getKeyCode", "()I");

    gKeyEventClassInfoInitialized = true;
  }

  GameActivityKeyEvent *event = new GameActivityKeyEvent{
      /*deviceId=*/env->CallIntMethod(keyEvent, gKeyEventClassInfo.getDeviceId),
      /*source=*/env->CallIntMethod(keyEvent, gKeyEventClassInfo.getSource),
      /*action=*/env->CallIntMethod(keyEvent, gKeyEventClassInfo.getAction),
      // TODO: introduce a millisecondsToNanoseconds helper:
      /*eventTime=*/
      env->CallLongMethod(keyEvent, gKeyEventClassInfo.getEventTime) * 1000000,
      /*downTime=*/
      env->CallLongMethod(keyEvent, gKeyEventClassInfo.getDownTime) * 1000000,
      /*flags=*/env->CallIntMethod(keyEvent, gKeyEventClassInfo.getFlags),
      /*metaState=*/
      env->CallIntMethod(keyEvent, gKeyEventClassInfo.getMetaState),
      /*modifiers=*/gKeyEventClassInfo.getModifiers
          ? env->CallIntMethod(keyEvent, gKeyEventClassInfo.getModifiers)
          : 0,
      /*repeatCount=*/
      env->CallIntMethod(keyEvent, gKeyEventClassInfo.getRepeatCount),
      /*keyCode=*/
      env->CallIntMethod(keyEvent, gKeyEventClassInfo.getKeyCode)};

  return event;
}

extern "C" void GameActivityKeyEvent_release(GameActivityKeyEvent *event) {
  delete event;
}

static void onTouchEvent_native(JNIEnv *env, jobject javaGameActivity,
                                jlong handle, jobject motionEvent) {
  if (handle == 0) return;
  NativeCode *code = (NativeCode *)handle;
  if (code->callbacks.onTouchEvent == nullptr) return;

  code->callbacks.onTouchEvent(
      code, GameActivityMotionEvent_fromJava(env, motionEvent));
}

static void onKeyUp_native(JNIEnv *env, jobject javaGameActivity, jlong handle,
                           jobject keyEvent) {
  if (handle == 0) return;
  NativeCode *code = (NativeCode *)handle;
  if (code->callbacks.onKeyUp == nullptr) return;

  code->callbacks.onKeyUp(code, GameActivityKeyEvent_fromJava(env, keyEvent));
}

static void onKeyDown_native(JNIEnv *env, jobject javaGameActivity,
                             jlong handle, jobject keyEvent) {
  if (handle == 0) return;
  NativeCode *code = (NativeCode *)handle;
  if (code->callbacks.onKeyDown == nullptr) return;

  code->callbacks.onKeyDown(code, GameActivityKeyEvent_fromJava(env, keyEvent));
}

static void onTextInput_native(JNIEnv *env, jobject activity, jlong handle,
                               jobject textInputEvent) {
  if (handle == 0) return;
  NativeCode *code = (NativeCode *)handle;
  GameInput_processEvent(code->gameInput, textInputEvent);
}

static void setInputConnection_native(JNIEnv *env, jobject activity,
                                      jlong handle, jobject inputConnection) {
  NativeCode *code = (NativeCode *)handle;
  GameInput_setInputConnection(code->gameInput, inputConnection);
}

static const JNINativeMethod g_methods[] = {
    {"loadNativeCode",
     "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/"
     "String;Ljava/lang/String;Landroid/content/res/AssetManager;[B)J",
     (void *)loadNativeCode_native},
    {"getDlError", "()Ljava/lang/String;", (void *)getDlError_native},
    {"unloadNativeCode", "(J)V", (void *)unloadNativeCode_native},
    {"onStartNative", "(J)V", (void *)onStart_native},
    {"onResumeNative", "(J)V", (void *)onResume_native},
    {"onSaveInstanceStateNative", "(J)[B", (void *)onSaveInstanceState_native},
    {"onPauseNative", "(J)V", (void *)onPause_native},
    {"onStopNative", "(J)V", (void *)onStop_native},
    {"onConfigurationChangedNative", "(J)V",
     (void *)onConfigurationChanged_native},
    {"onLowMemoryNative", "(J)V", (void *)onLowMemory_native},
    {"onWindowFocusChangedNative", "(JZ)V",
     (void *)onWindowFocusChanged_native},
    {"onSurfaceCreatedNative", "(JLandroid/view/Surface;)V",
     (void *)onSurfaceCreated_native},
    {"onSurfaceChangedNative", "(JLandroid/view/Surface;III)V",
     (void *)onSurfaceChanged_native},
    {"onSurfaceRedrawNeededNative", "(JLandroid/view/Surface;)V",
     (void *)onSurfaceRedrawNeeded_native},
    {"onSurfaceDestroyedNative", "(J)V", (void *)onSurfaceDestroyed_native},
    {"onContentRectChangedNative", "(JIIII)V",
     (void *)onContentRectChanged_native},
    {"onTouchEventNative", "(JLandroid/view/MotionEvent;)V",
     (void *)onTouchEvent_native},
    {"onKeyDownNative", "(JLandroid/view/KeyEvent;)V",
     (void *)onKeyDown_native},
    {"onKeyUpNative", "(JLandroid/view/KeyEvent;)V", (void *)onKeyUp_native},
    {"onTextInputEventNative",
     "(JLcom/google/androidgamesdk/gameinput/State;)V",
     (void *)onTextInput_native},
    {"setInputConnectionNative",
     "(JLcom/google/androidgamesdk/gameinput/InputConnection;)V",
     (void *)setInputConnection_native},
};
static const char *const kGameActivityPathName =
    "com/google/androidgamesdk/GameActivity";
#define FIND_CLASS(var, className) \
  var = env->FindClass(className); \
  LOG_FATAL_IF(!var, "Unable to find class %s", className);
#define GET_METHOD_ID(var, clazz, methodName, fieldDescriptor) \
  var = env->GetMethodID(clazz, methodName, fieldDescriptor);  \
  LOG_FATAL_IF(!var, "Unable to find method" methodName);

static int jniRegisterNativeMethods(JNIEnv *env, const char *className,
                                    const JNINativeMethod *methods,
                                    int numMethods) {
  ALOGV("Registering %s's %d native methods...", className, numMethods);
  jclass clazz = env->FindClass(className);
  LOG_FATAL_IF(clazz == nullptr,
               "Native registration unable to find class '%s'; aborting...",
               className);
  int result = env->RegisterNatives(clazz, methods, numMethods);
  env->DeleteLocalRef(clazz);
  if (result == 0) {
    return 0;
  }

  // Failure to register natives is fatal. Try to report the corresponding
  // exception, otherwise abort with generic failure message.
  jthrowable thrown = env->ExceptionOccurred();
  if (thrown != NULL) {
    env->ExceptionDescribe();
    env->DeleteLocalRef(thrown);
  }
  LOG_FATAL("RegisterNatives failed for '%s'; aborting...", className);
}

extern "C" int GameActivity_register(JNIEnv *env) {
  ALOGD("GameActivity_register");
  jclass clazz;
  FIND_CLASS(clazz, kGameActivityPathName);
  GET_METHOD_ID(gGameActivityClassInfo.finish, clazz, "finish", "()V");
  GET_METHOD_ID(gGameActivityClassInfo.setWindowFlags, clazz, "setWindowFlags",
                "(II)V");
  GET_METHOD_ID(gGameActivityClassInfo.setWindowFormat, clazz,
                "setWindowFormat", "(I)V");
  return jniRegisterNativeMethods(env, kGameActivityPathName, g_methods,
                                  NELEM(g_methods));
}

// Register this method so that GameActiviy_register does not need to be called
// manually.
extern "C" jlong Java_com_google_androidgamesdk_GameActivity_loadNativeCode(
    JNIEnv *env, jobject javaGameActivity, jstring path, jstring funcName,
    jstring internalDataDir, jstring obbDir, jstring externalDataDir,
    jobject jAssetMgr, jbyteArray savedState) {
  GameActivity_register(env);
  jlong nativeCode = loadNativeCode_native(
      env, javaGameActivity, path, funcName, internalDataDir, obbDir,
      externalDataDir, jAssetMgr, savedState);
  return nativeCode;
}
