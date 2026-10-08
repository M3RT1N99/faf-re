#include "AndroidArgs.h"

#include <jni.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <string_view>
#include <utility>

#include "faf/port/FileSystem.h"

namespace faf::android {

  namespace {

    /// Attaches the calling thread to the VM for the lifetime of the object
    /// unless it is attached already. android_main runs on a thread the NDK
    /// glue created, which starts detached; a native thread must detach before
    /// it exits or ART aborts the process.
    class ScopedJniEnv
    {
    public:
      explicit ScopedJniEnv(JavaVM* vm)
        : mVm(vm)
      {
        void* env = nullptr;
        const jint state = vm->GetEnv(&env, JNI_VERSION_1_6);
        if (state == JNI_OK) {
          mEnv = static_cast<JNIEnv*>(env);
        } else if (state == JNI_EDETACHED) {
          JavaVMAttachArgs args{JNI_VERSION_1_6, "faf_android", nullptr};
          if (vm->AttachCurrentThread(&mEnv, &args) == JNI_OK) {
            mAttached = true;
          } else {
            mEnv = nullptr;
          }
        }
      }

      ~ScopedJniEnv()
      {
        if (mAttached) {
          mVm->DetachCurrentThread();
        }
      }

      ScopedJniEnv(const ScopedJniEnv&) = delete;
      ScopedJniEnv& operator=(const ScopedJniEnv&) = delete;

      [[nodiscard]] JNIEnv* Env() const
      {
        return mEnv;
      }

    private:
      JavaVM* mVm;
      JNIEnv* mEnv = nullptr;
      bool mAttached = false;
    };

    /// Java strings are UTF-16; the port works in UTF-8. GetStringUTFChars
    /// would return modified UTF-8 (surrogate pairs encoded separately, NUL as
    /// two bytes), which is not what the file system APIs expect.
    std::string ToUtf8(JNIEnv* env, jstring string)
    {
      const jsize length = env->GetStringLength(string);
      std::u16string units(static_cast<std::size_t>(length), u'\0');
      env->GetStringRegion(string, 0, length, reinterpret_cast<jchar*>(units.data()));

      std::string out;
      out.reserve(units.size());
      for (std::size_t i = 0; i < units.size(); ++i) {
        char32_t cp = units[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < units.size() && units[i + 1] >= 0xDC00 && units[i + 1] <= 0xDFFF) {
          cp = 0x10000 + ((cp - 0xD800) << 10) + (units[i + 1] - 0xDC00u);
          ++i;
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
          cp = 0xFFFD; // unpaired surrogate
        }
        if (cp == 0) {
          continue; // NUL cannot be part of a path or an option
        }
        if (cp < 0x80) {
          out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
          out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
          out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
          out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
          out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
          out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
          out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
          out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
          out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
          out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
      }
      return out;
    }

    /// If a Java exception is pending: clears it and returns its description.
    std::optional<std::string> TakeException(JNIEnv* env)
    {
      if (!env->ExceptionCheck()) {
        return std::nullopt;
      }
      jthrowable exception = env->ExceptionOccurred();
      env->ExceptionClear();
      std::string text = "Java exception";
      if (exception != nullptr) {
        jclass type = env->GetObjectClass(exception);
        jmethodID toString = env->GetMethodID(type, "toString", "()Ljava/lang/String;");
        if (toString != nullptr) {
          auto description = static_cast<jstring>(env->CallObjectMethod(exception, toString));
          if (!env->ExceptionCheck() && description != nullptr) {
            text = ToUtf8(env, description);
          }
        }
        env->ExceptionClear();
      }
      return text;
    }

    std::string Lower(std::string_view text)
    {
      std::string out(text);
      std::transform(out.begin(), out.end(), out.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      return out;
    }

    /// Options this build reads, with the number of values each takes.
    struct KnownOption
    {
      std::string_view name;
      std::size_t values;
    };
    constexpr KnownOption kHandledOptions[] = {
      {"/init", 1},
      {"/log", 1},
      {"/renderer", 1},
    };

  } // namespace

  std::string DataRoot(const ANativeActivity* activity)
  {
    if (activity == nullptr || activity->externalDataPath == nullptr || activity->externalDataPath[0] == '\0') {
      return {};
    }
    std::string root = faf::port::fs::NormalizePath(activity->externalDataPath);
    while (root.size() > 1 && root.back() == '/') {
      root.pop_back();
    }
    return root;
  }

  IntentArgs ReadIntentArgs(ANativeActivity* activity)
  {
    IntentArgs result;
    const ScopedJniEnv scoped(activity->vm);
    JNIEnv* env = scoped.Env();
    if (env == nullptr) {
      result.error = "cannot attach the native thread to the Java VM";
      return result;
    }
    // Every local reference below lives in this frame; an attached native
    // thread never returns to Java, so nothing else would free them.
    if (env->PushLocalFrame(32) != JNI_OK) {
      result.error = TakeException(env).value_or("PushLocalFrame failed");
      return result;
    }

    const auto fail = [&](std::string what) {
      const std::optional<std::string> exception = TakeException(env);
      result.error = exception ? what + ": " + *exception : std::move(what);
    };

    // activity->clazz is the GameActivity instance, despite its name.
    jobject activityObject = activity->clazz;
    jclass activityClass = env->GetObjectClass(activityObject);
    jmethodID getIntent = env->GetMethodID(activityClass, "getIntent", "()Landroid/content/Intent;");
    if (getIntent == nullptr) {
      fail("Activity.getIntent not found");
      env->PopLocalFrame(nullptr);
      return result;
    }
    jobject intent = env->CallObjectMethod(activityObject, getIntent);
    if (env->ExceptionCheck()) {
      fail("getIntent failed");
      env->PopLocalFrame(nullptr);
      return result;
    }
    if (intent == nullptr) {
      result.ok = true; // no intent at all: run with the defaults
      env->PopLocalFrame(nullptr);
      return result;
    }

    jclass intentClass = env->GetObjectClass(intent);
    jmethodID getStringArrayExtra =
      env->GetMethodID(intentClass, "getStringArrayExtra", "(Ljava/lang/String;)[Ljava/lang/String;");
    jstring key = env->NewStringUTF("argv");
    if (getStringArrayExtra == nullptr || key == nullptr) {
      fail("Intent.getStringArrayExtra not available");
      env->PopLocalFrame(nullptr);
      return result;
    }
    auto array = static_cast<jobjectArray>(env->CallObjectMethod(intent, getStringArrayExtra, key));
    if (env->ExceptionCheck()) {
      fail("reading the \"argv\" extra failed");
      env->PopLocalFrame(nullptr);
      return result;
    }

    if (array != nullptr) {
      result.present = true;
      const jsize count = env->GetArrayLength(array);
      result.argv.reserve(static_cast<std::size_t>(count));
      for (jsize i = 0; i < count; ++i) {
        auto element = static_cast<jstring>(env->GetObjectArrayElement(array, i));
        if (env->ExceptionCheck()) {
          fail("reading the \"argv\" extra failed");
          env->PopLocalFrame(nullptr);
          return result;
        }
        if (element != nullptr) {
          result.argv.push_back(ToUtf8(env, element));
          env->DeleteLocalRef(element);
        }
      }
    }

    // The mode, and the menu replay's extras (GalPlay.h).
    jmethodID getStringExtra = env->GetMethodID(intentClass, "getStringExtra", "(Ljava/lang/String;)Ljava/lang/String;");
    jmethodID getBooleanExtra = env->GetMethodID(intentClass, "getBooleanExtra", "(Ljava/lang/String;Z)Z");
    if (getStringExtra == nullptr || getBooleanExtra == nullptr) {
      fail("Intent.getStringExtra/getBooleanExtra not available");
      env->PopLocalFrame(nullptr);
      return result;
    }
    bool extrasOk = true;
    const auto stringExtra = [&](const char* const name) -> std::string {
      jstring nameString = env->NewStringUTF(name);
      auto value = nameString != nullptr ? static_cast<jstring>(env->CallObjectMethod(intent, getStringExtra, nameString)) : nullptr;
      if (nameString == nullptr || env->ExceptionCheck()) {
        extrasOk = false;
        fail(std::string("reading the \"") + name + "\" extra failed");
        return {};
      }
      std::string text = value != nullptr ? ToUtf8(env, value) : std::string();
      env->DeleteLocalRef(nameString);
      if (value != nullptr) {
        env->DeleteLocalRef(value);
      }
      return text;
    };
    const auto booleanExtra = [&](const char* const name) -> bool {
      jstring nameString = env->NewStringUTF(name);
      const jboolean value = nameString != nullptr ? env->CallBooleanMethod(intent, getBooleanExtra, nameString, JNI_FALSE) : JNI_FALSE;
      if (nameString == nullptr || env->ExceptionCheck()) {
        extrasOk = false;
        fail(std::string("reading the \"") + name + "\" extra failed");
        return false;
      }
      env->DeleteLocalRef(nameString);
      return value == JNI_TRUE;
    };
    result.mode = stringExtra("mode");
    if (extrasOk && result.mode == kModeMenuReplay) {
      result.menuReplay.trace = stringExtra("menuReplay.trace");
      result.menuReplay.runDir = stringExtra("menuReplay.runDir");
      result.menuReplay.fast = booleanExtra("menuReplay.fast");
      result.menuReplay.forceCpuDecode = booleanExtra("menuReplay.forceCpuDecode");
      result.menuReplay.noShaderCache = booleanExtra("menuReplay.noShaderCache");
    }
    if (!extrasOk) {
      env->PopLocalFrame(nullptr);
      return result;
    }
    result.ok = true;
    env->PopLocalFrame(nullptr);
    return result;
  }

  const char* ToString(const Backend backend)
  {
    return backend == Backend::Gles ? "gles" : "vulkan";
  }

  LaunchOptions ResolveLaunchOptions(const faf::port::CommandLine& commandLine, const std::string& root)
  {
    namespace fs = faf::port::fs;
    LaunchOptions options;
    const std::string binDir = root + "/faf/bin";

    const auto resolvePath = [&](const std::string& value) {
      std::string path = fs::NormalizePath(value);
      if (!path.empty() && path.front() != '/') {
        path = fs::NormalizePath(binDir + "/" + path);
      }
      return path;
    };
    const auto pathOption = [&](const std::string_view name) -> std::string {
      if (!commandLine.Has(name)) {
        return {};
      }
      const std::optional<std::string> value = commandLine.Value(name);
      if (!value || value->empty()) {
        options.warnings.push_back(std::string(name) + " has no value; ignored");
        return {};
      }
      return resolvePath(*value);
    };

    options.initScript = pathOption("/init");
    if (options.initScript.empty()) {
      options.initScript = binDir + "/init_faf.lua";
    }
    options.gameLog = pathOption("/log");

    if (commandLine.Has("/renderer")) {
      const std::optional<std::string> value = commandLine.Value("/renderer");
      const std::string renderer = value ? Lower(*value) : std::string();
      if (renderer == "vulkan") {
        options.backend = Backend::Vulkan;
      } else if (renderer == "gles") {
        options.backend = Backend::Gles;
      } else {
        options.warnings.push_back(
          "/renderer '" + (value ? *value : std::string()) + "' is not 'vulkan' or 'gles'; using vulkan"
        );
      }
    }

    // Everything else is kept for the engine. Report option names (a '/'
    // followed by a name without further '/', so paths given as values do not
    // show up) that are not values of the options handled above.
    const std::vector<std::string>& args = commandLine.Args();
    for (std::size_t i = 0; i < args.size(); ++i) {
      const std::string lower = Lower(args[i]);
      const auto handled = std::find_if(std::begin(kHandledOptions), std::end(kHandledOptions), [&](const auto& o) {
        return o.name == lower;
      });
      if (handled != std::end(kHandledOptions)) {
        i += handled->values;
        continue;
      }
      if (lower.size() > 1 && lower.front() == '/' && lower.find('/', 1) == std::string::npos) {
        options.unusedOptions.push_back(args[i]);
      }
    }
    return options;
  }

  std::string DescribeArgs(const std::vector<std::string>& argv)
  {
    std::string out;
    for (const std::string& arg : argv) {
      if (!out.empty()) {
        out.push_back(' ');
      }
      if (arg.empty() || arg.find_first_of(" \t") != std::string::npos) {
        out.push_back('"');
        out.append(arg);
        out.push_back('"');
      } else {
        out.append(arg);
      }
    }
    return out;
  }

} // namespace faf::android
