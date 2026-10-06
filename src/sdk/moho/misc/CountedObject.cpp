#include "CountedObject.h"

#if defined(_MSC_VER)
#include <intrin.h>
#else
#include "platform/Atomic32.h"
#endif

namespace moho
{
  /**
   * Address: 0x004228D0 (FUN_004228D0, sub_4228D0)
   *
   * What it does:
   * Initializes the base counted-object lanes and clears reference count.
   */
  CountedObject::CountedObject() noexcept
    : mRefCount(0)
  {}

  /**
   * Address: 0x004228C0 (FUN_004228C0, sub_4228C0 non-deleting body lane)
   * Address: 0x004228E0 (FUN_004228E0, scalar deleting destructor thunk)
   * Mangled: ??_GCountedObject@Moho@@UAEPAXI@Z
   *
   * What it does:
   * Resets this object's vtable to `CountedObject` and optionally deletes `this`.
   */
  CountedObject::~CountedObject() = default;

  void CountedObject::AddReference() noexcept
  {
    ++mRefCount;
  }

  void CountedObject::AddReferenceAtomic() noexcept
  {
#if defined(_MSC_VER)
    (void)_InterlockedExchangeAdd(reinterpret_cast<volatile long*>(&mRefCount), 1);
#else
    (void)platform::AtomicExchangeAdd32(&mRefCount, 1);
#endif
  }

  [[nodiscard]] bool CountedObject::ReleaseReference() noexcept
  {
    --mRefCount;
    if (mRefCount != 0) {
      return false;
    }

    delete this;
    return true;
  }

  [[nodiscard]] bool CountedObject::ReleaseReferenceAtomic() noexcept
  {
#if defined(_MSC_VER)
    const long previous = _InterlockedExchangeAdd(reinterpret_cast<volatile long*>(&mRefCount), -1);
#else
    const std::int32_t previous = platform::AtomicExchangeAdd32(&mRefCount, -1);
#endif
    if (previous != 1) {
      return false;
    }

    delete this;
    return true;
  }
} // namespace moho
