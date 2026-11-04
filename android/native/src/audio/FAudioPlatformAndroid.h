#pragma once

// What the rest of the build says to FAudio's platform (FAudioPlatformAndroid.c). Any thread may
// call these.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct FAudioIOStream;

/// The output plays while the activity is in front and holds the audio focus; both are so until
/// the host says otherwise. Otherwise it has no stream, and nothing in the engine knows. These do not
/// wait for it.
void FAudioAndroid_SetForeground(int foreground);
void FAudioAndroid_SetAudioFocus(int held);

/// A stream on the caller's functions, for FAudio_close, which leaves `data` to the caller.
struct FAudioIOStream* FAudioAndroid_OpenStream(
  void* data, size_t (*read)(void* data, void* destination, size_t size, size_t count), int64_t (*seek)(void* data, int64_t offset, int origin)
);

/// Hears every mix that goes out, on the output's thread: `frames` frames of 32-bit floats,
/// interleaved. A test's ears, as nothing on the device can listen to the output. One call may still
/// be under way when this returns.
typedef void (*FAudioAndroidMixObserver)(const float* samples, uint32_t frames, uint32_t channels, void* context);
void FAudioAndroid_SetMixObserver(FAudioAndroidMixObserver observer, void* context);

/// Where the output's and FAudio's lines go besides logcat, which a phone's tester does not have.
/// To be set before there is an engine.
typedef void (*FAudioAndroidLogHandler)(const char* line);
void FAudioAndroid_SetLogHandler(FAudioAndroidLogHandler handler);

#ifdef __cplusplus
}
#endif
