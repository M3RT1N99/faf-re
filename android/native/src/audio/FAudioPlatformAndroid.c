// FAudio's platform on Android (its FAudio_internal.h, "Platform Functions"), in place of its SDL
// and Win32 files: the output on AAudio, threads, mutexes and files on POSIX.
//
// Each FAudio engine has one stream and a thread of its own that mixes and writes it with blocking
// AAudioStream_write. Not AAudio's data callback: FACT reads streaming wave banks on the mix path
// (FACT_INTERNAL_OnBufferEnd), and memory-mapped banks page in there, which that thread must not do.

#include "FAudioPlatformAndroid.h"

#include <aaudio/AAudio.h>
#include <android/log.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include "FAudio_internal.h"

#define LOG_TAG "FAudio"

// How late the thread sees a pause or the end while the stream takes nothing.
#define WRITE_TIMEOUT_NANOSECONDS (100 * 1000 * 1000)
// Until the next try after the output failed: a route that just changed may take a moment.
#define REOPEN_DELAY_MILLISECONDS 500
// How many lines an output says of its failures. One that stays comes back at each try to open the
// stream, for as long as the game runs, and the engine's log is what a phone's tester sends.
#define MAX_FAILURE_LINES 8

// --- The output --------------------------------------------------------------------------------

typedef struct Output
{
  FAudio* audio;
  float* mix;
  uint32_t frames; // a quantum, the size of `mix`
  uint32_t channels;
  uint32_t rate;
  pthread_t thread;
  AAudioStream* stream; // the thread's: changed under sLock, for the stop that ends a write
  uint32_t failureLines; // the thread's
  bool quit;
} Output;

static pthread_mutex_t sLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sChanged = PTHREAD_COND_INITIALIZER;
static bool sForeground = true;
static bool sFocus = true;
static FAudioAndroidMixObserver sObserver;
static void* sObserverContext;
static FAudioAndroidLogHandler sLogHandler;

void FAudioAndroid_SetLogHandler(const FAudioAndroidLogHandler handler)
{
  sLogHandler = handler;
}

static void Log(const int priority, const char* const format, ...)
{
  char line[256];
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(line, sizeof(line), format, arguments);
  va_end(arguments);
  line[strcspn(line, "\n")] = '\0'; // FAudio's own lines end in one
  __android_log_write(priority, LOG_TAG, line);
  if (sLogHandler != NULL) {
    sLogHandler(line);
  }
}

static void LogFailure(Output* const output, const char* const what, const aaudio_result_t result)
{
  if (output->failureLines < MAX_FAILURE_LINES) {
    output->failureLines += 1;
    Log(ANDROID_LOG_WARN, "%s: %s", what, AAudio_convertResultToText(result));
  }
}

void FAudioAndroid_SetForeground(const int foreground)
{
  pthread_mutex_lock(&sLock);
  sForeground = foreground != 0;
  pthread_cond_broadcast(&sChanged);
  pthread_mutex_unlock(&sLock);
}

void FAudioAndroid_SetAudioFocus(const int held)
{
  pthread_mutex_lock(&sLock);
  sFocus = held != 0;
  pthread_cond_broadcast(&sChanged);
  pthread_mutex_unlock(&sLock);
}

void FAudioAndroid_SetMixObserver(const FAudioAndroidMixObserver observer, void* const context)
{
  pthread_mutex_lock(&sLock);
  sObserver = observer;
  sObserverContext = context;
  pthread_mutex_unlock(&sLock);
}

static AAudioStream* OpenStream(Output* const output)
{
  AAudioStreamBuilder* builder = NULL;
  aaudio_result_t result = AAudio_createStreamBuilder(&builder);
  if (result != AAUDIO_OK) {
    LogFailure(output, "AAudio_createStreamBuilder", result);
    return NULL;
  }
  AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
  AAudioStreamBuilder_setChannelCount(builder, (int32_t)output->channels);
  AAudioStreamBuilder_setSampleRate(builder, (int32_t)output->rate);
  // Not low latency: the mix reads from storage, which this mode's larger buffer rides out, and
  // an RTS can spare the milliseconds.
  AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_NONE);
  if (__builtin_available(android 28, *)) {
    AAudioStreamBuilder_setUsage(builder, AAUDIO_USAGE_GAME);
  }
  AAudioStream* stream = NULL;
  result = AAudioStreamBuilder_openStream(builder, &stream);
  AAudioStreamBuilder_delete(builder);
  if (result != AAUDIO_OK) {
    LogFailure(output, "AAudioStreamBuilder_openStream", result);
    return NULL;
  }
  if (output->failureLines < MAX_FAILURE_LINES) {
    Log(ANDROID_LOG_INFO, "Output: %d Hz, %d channels, buffer %d of %d frames, burst %d, performance mode %d",
      AAudioStream_getSampleRate(stream), AAudioStream_getChannelCount(stream), AAudioStream_getBufferSizeInFrames(stream),
      AAudioStream_getBufferCapacityInFrames(stream), AAudioStream_getFramesPerBurst(stream), AAudioStream_getPerformanceMode(stream));
  }
  return stream;
}

static void WaitLocked(const int milliseconds)
{
  struct timespec until;
  clock_gettime(CLOCK_REALTIME, &until);
  until.tv_sec += milliseconds / 1000;
  until.tv_nsec += (long)(milliseconds % 1000) * 1000000L;
  if (until.tv_nsec >= 1000000000L) {
    until.tv_sec += 1;
    until.tv_nsec -= 1000000000L;
  }
  pthread_cond_timedwait(&sChanged, &sLock, &until);
}

static void* OutputThread(void* const data)
{
  Output* const output = data;
  uint32_t written = output->frames; // of the mix: nothing waits to go out yet
  bool started = false;
  uint32_t nextOpen = FAudio_timems();

  // As FACT's thread is: one that takes a lock the other waits for must not wait behind the game.
  FAudio_PlatformThreadPriority(FAUDIO_THREAD_PRIORITY_HIGH);
  pthread_setname_np(pthread_self(), "FAudio Output");

  pthread_mutex_lock(&sLock);
  while (!output->quit) {
    const bool paused = !sForeground || !sFocus;
    const FAudioAndroidMixObserver observer = sObserver;
    void* const observerContext = sObserverContext;
    AAudioStream* stream = output->stream;
    pthread_mutex_unlock(&sLock);

    if (!paused && stream == NULL && (int32_t)(FAudio_timems() - nextOpen) >= 0) {
      stream = OpenStream(output);
      nextOpen = FAudio_timems() + REOPEN_DELAY_MILLISECONDS;
      pthread_mutex_lock(&sLock);
      output->stream = stream;
      pthread_mutex_unlock(&sLock);
    }

    // On this thread alone: its write runs AAudio's state machine (AudioStreamTrack::write), and a
    // pause from another thread soon after the start was lost to it, which left the stream paused
    // and calling itself started, so that it could not start again.
    if (stream != NULL && started == paused) {
      const aaudio_result_t result = paused ? AAudioStream_requestPause(stream) : AAudioStream_requestStart(stream);
      if (result != AAUDIO_OK) {
        // The next write finds out what became of the stream.
        LogFailure(output, paused ? "AAudioStream_requestPause" : "AAudioStream_requestStart", result);
      }
      started = !paused;
    }

    const bool heard = !paused && stream != NULL;
    if (written == output->frames) {
      // The mix adds to what is there, and leaves it when the engine is stopped.
      FAudio_zero(output->mix, sizeof(float) * output->frames * output->channels);
      FAudio_INTERNAL_UpdateEngine(output->audio, output->mix);
      if (heard && observer != NULL) {
        observer(output->mix, output->frames, output->channels, observerContext);
      }
      written = 0;
    }

    // The game goes on behind the activity and while no stream opens, so its sounds do, unheard. A
    // mix that stood still would hold them all, up to the instance limits, and start them together
    // on the return.
    if (!heard) {
      written = output->frames;
      pthread_mutex_lock(&sLock);
      WaitLocked((int)(1000 * output->frames / output->rate));
      continue;
    }

    const aaudio_result_t result = AAudioStream_write(
      stream, output->mix + written * output->channels, (int32_t)(output->frames - written), WRITE_TIMEOUT_NANOSECONDS
    );
    if (result >= 0) {
      written += (uint32_t)result;
      pthread_mutex_lock(&sLock);
      continue;
    }

    // Headphones out, Bluetooth gone (AAUDIO_ERROR_DISCONNECTED), or a stream stopped by the end:
    // a new stream goes to the device that is there now.
    if (result != AAUDIO_ERROR_DISCONNECTED) {
      LogFailure(output, "AAudioStream_write", result);
    }
    pthread_mutex_lock(&sLock);
    output->stream = NULL;
    pthread_mutex_unlock(&sLock);
    AAudioStream_close(stream);
    started = false;
    nextOpen = FAudio_timems() + REOPEN_DELAY_MILLISECONDS;
    pthread_mutex_lock(&sLock);
  }
  pthread_mutex_unlock(&sLock);
  return NULL;
}

void FAudio_PlatformInit(
  FAudio* const audio,
  const uint32_t flags,
  const uint32_t deviceIndex,
  FAudioWaveFormatExtensible* const mixFormat,
  uint32_t* const updateSize,
  void** const platformDevice
)
{
  Output* const output = calloc(1, sizeof(Output));
  output->audio = audio;
  output->channels = mixFormat->Format.nChannels;
  output->rate = mixFormat->Format.nSamplesPerSec;
  output->frames = output->rate / 100;
  output->mix = malloc(sizeof(float) * output->frames * output->channels);

  WriteWaveFormatExtensible(mixFormat, (int)output->channels, (int)output->rate, &DATAFORMAT_SUBTYPE_IEEE_FLOAT);
  *updateSize = output->frames;

  const int failed = pthread_create(&output->thread, NULL, &OutputThread, output);
  if (failed != 0) {
    Log(ANDROID_LOG_ERROR, "No thread for the output: %d", failed);
    free(output->mix);
    free(output);
    *platformDevice = NULL;
    return;
  }
  *platformDevice = output;
}

void FAudio_PlatformQuit(void* const platformDevice)
{
  Output* const output = platformDevice;
  pthread_mutex_lock(&sLock);
  output->quit = true;
  if (output->stream != NULL) {
    // A write that waits ends with this.
    AAudioStream_requestStop(output->stream);
  }
  pthread_cond_broadcast(&sChanged);
  pthread_mutex_unlock(&sLock);

  pthread_join(output->thread, NULL);
  if (output->stream != NULL) {
    AAudioStream_close(output->stream);
  }
  free(output->mix);
  free(output);
}

void FAudio_PlatformAddRef(void)
{
#if defined(__aarch64__)
  FAudio_INTERNAL_InitSIMDFunctions(0, 1);
#else
  FAudio_INTERNAL_InitSIMDFunctions(1, 0);
#endif
}

void FAudio_PlatformRelease(void) {}

uint32_t FAudio_PlatformGetDeviceCount(void)
{
  return 1;
}

uint32_t FAudio_PlatformGetDeviceDetails(const uint32_t index, FAudioDeviceDetails* const details)
{
  static const char kName[] = "Default Device";

  FAudio_zero(details, sizeof(FAudioDeviceDetails));
  if (index != 0) {
    return FAUDIO_E_INVALID_CALL;
  }
  details->DeviceID[0] = '0';
  for (size_t at = 0; at < sizeof(kName); ++at) {
    details->DisplayName[at] = kName[at];
  }
  details->Role = FAudioGlobalDefaultDevice;
  WriteWaveFormatExtensible(&details->OutputFormat, 2, 48000, &DATAFORMAT_SUBTYPE_IEEE_FLOAT);
  return 0;
}

void FAudio_Log(const char* const message)
{
  Log(ANDROID_LOG_INFO, "%s", message);
}

// --- Threads -----------------------------------------------------------------------------------

typedef struct PlatformThread
{
  pthread_t thread;
  FAudioThreadFunc function;
  void* data;
  int32_t result;
  char name[16]; // the kernel's limit
} PlatformThread;

static void* ThreadStart(void* const data)
{
  PlatformThread* const thread = data;
  pthread_setname_np(pthread_self(), thread->name);
  thread->result = thread->function(thread->data);
  return NULL;
}

FAudioThread FAudio_PlatformCreateThread(const FAudioThreadFunc function, const char* const name, void* const data)
{
  PlatformThread* const thread = calloc(1, sizeof(PlatformThread));
  thread->function = function;
  thread->data = data;
  strlcpy(thread->name, name, sizeof(thread->name));
  if (pthread_create(&thread->thread, NULL, &ThreadStart, thread) != 0) {
    free(thread);
    return NULL;
  }
  return thread;
}

// FACTAudioEngine_ShutDown comes here a second time without a thread, from its Release.
void FAudio_PlatformWaitThread(const FAudioThread thread, int32_t* const result)
{
  PlatformThread* const platformThread = thread;
  if (platformThread == NULL) {
    return;
  }
  pthread_join(platformThread->thread, NULL);
  if (result != NULL) {
    *result = platformThread->result;
  }
  free(platformThread);
}

void FAudio_PlatformThreadPriority(const FAudioThreadPriority priority)
{
  // android.os.Process's levels for a thread: an app's threads may take them.
  int nice = 0;
  if (priority == FAUDIO_THREAD_PRIORITY_LOW) {
    nice = 10;
  } else if (priority == FAUDIO_THREAD_PRIORITY_HIGH) {
    nice = -16;
  }
  if (setpriority(PRIO_PROCESS, (id_t)gettid(), nice) != 0) {
    Log(ANDROID_LOG_WARN, "setpriority %d failed", nice);
  }
}

uint64_t FAudio_PlatformGetThreadID(void)
{
  return (uint64_t)gettid();
}

// Recursive, as SDL's are: FACT takes its lock again in calls that call each other.
FAudioMutex FAudio_PlatformCreateMutex(void)
{
  pthread_mutexattr_t attributes;
  pthread_mutexattr_init(&attributes);
  pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_t* const mutex = malloc(sizeof(pthread_mutex_t));
  pthread_mutex_init(mutex, &attributes);
  pthread_mutexattr_destroy(&attributes);
  return mutex;
}

// FACTAudioEngine_ShutDown zeroes the engine, so its Release destroys two null mutexes.
void FAudio_PlatformDestroyMutex(const FAudioMutex mutex)
{
  if (mutex != NULL) {
    pthread_mutex_destroy(mutex);
    free(mutex);
  }
}

void FAudio_PlatformLockMutex(const FAudioMutex mutex)
{
  pthread_mutex_lock(mutex);
}

void FAudio_PlatformUnlockMutex(const FAudioMutex mutex)
{
  pthread_mutex_unlock(mutex);
}

void FAudio_sleep(const uint32_t milliseconds)
{
  if (milliseconds == 0) {
    sched_yield();
    return;
  }
  const struct timespec duration = {(time_t)(milliseconds / 1000), (long)(milliseconds % 1000) * 1000000L};
  nanosleep(&duration, NULL);
}

uint32_t FAudio_timems(void)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint32_t)(now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

// --- Streams -----------------------------------------------------------------------------------

static FAudioIOStream* NewStream(void* const data, const FAudio_readfunc read, const FAudio_seekfunc seek, const FAudio_closefunc close)
{
  FAudioIOStream* const stream = malloc(sizeof(FAudioIOStream));
  stream->data = data;
  stream->read = read;
  stream->seek = seek;
  stream->close = close;
  stream->lock = FAudio_PlatformCreateMutex();
  return stream;
}

void FAudio_close(FAudioIOStream* const stream)
{
  stream->close(stream->data);
  FAudio_PlatformDestroyMutex(stream->lock);
  free(stream);
}

static int CloseNothing(void* const data)
{
  (void)data;
  return 0;
}

struct FAudioIOStream* FAudioAndroid_OpenStream(
  void* const data, size_t (*const read)(void*, void*, size_t, size_t), int64_t (*const seek)(void*, int64_t, int)
)
{
  return NewStream(data, read, seek, &CloseNothing);
}

static size_t FileRead(void* const data, void* const destination, const size_t size, const size_t count)
{
  return fread(destination, size, count, data);
}

static int64_t FileSeek(void* const data, const int64_t offset, const int origin)
{
  return fseeko(data, offset, origin) == 0 ? ftello(data) : -1;
}

static int FileClose(void* const data)
{
  return fclose(data);
}

FAudioIOStream* FAudio_fopen(const char* const path)
{
  FILE* const file = fopen(path, "rb");
  return file != NULL ? NewStream(file, &FileRead, &FileSeek, &FileClose) : NULL;
}

typedef struct MemoryStream
{
  uint8_t* memory;
  size_t size;
  size_t position;
} MemoryStream;

static size_t MemoryRead(void* const data, void* const destination, const size_t size, const size_t count)
{
  MemoryStream* const stream = data;
  const size_t left = stream->size - stream->position;
  // A bank's empty region reads zero items of no size.
  const size_t items = size != 0 ? FAudio_min(count, left / size) : 0;
  FAudio_memcpy(destination, stream->memory + stream->position, items * size);
  stream->position += items * size;
  return items;
}

static int64_t MemorySeek(void* const data, const int64_t offset, const int origin)
{
  MemoryStream* const stream = data;
  int64_t base = 0;
  if (origin == FAUDIO_SEEK_CUR) {
    base = (int64_t)stream->position;
  } else if (origin == FAUDIO_SEEK_END) {
    base = (int64_t)stream->size;
  }
  const int64_t position = base + offset;
  if (position < 0 || position > (int64_t)stream->size) {
    return -1;
  }
  stream->position = (size_t)position;
  return position;
}

static int MemoryClose(void* const data)
{
  free(data);
  return 0;
}

FAudioIOStream* FAudio_memopen(void* const memory, const int size)
{
  MemoryStream* const stream = calloc(1, sizeof(MemoryStream));
  stream->memory = memory;
  stream->size = (size_t)size;
  return NewStream(stream, &MemoryRead, &MemorySeek, &MemoryClose);
}

uint8_t* FAudio_memptr(FAudioIOStream* const stream, const size_t offset)
{
  return ((MemoryStream*)stream->data)->memory + offset;
}
