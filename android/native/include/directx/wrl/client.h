#pragma once

// <wrl/client.h> for the Android client build: Microsoft::WRL::ComPtr as the engine uses it (Get,
// operator->, operator& as an out parameter, comparison with nullptr), with WRL's ownership
// rules: a copy adds a reference, the destructor and Reset release one, operator& releases the
// held pointer before handing out its address.

#include <cstddef>
#include <utility>

namespace Microsoft::WRL
{
  template <class T>
  class ComPtr
  {
  public:
    ComPtr() noexcept = default;

    ComPtr(std::nullptr_t) noexcept {}

    ComPtr(const ComPtr& other) noexcept
      : mPointer(other.mPointer)
    {
      if (mPointer != nullptr) {
        mPointer->AddRef();
      }
    }

    ComPtr(ComPtr&& other) noexcept
      : mPointer(std::exchange(other.mPointer, nullptr))
    {}

    ~ComPtr()
    {
      Reset();
    }

    ComPtr& operator=(ComPtr other) noexcept
    {
      std::swap(mPointer, other.mPointer);
      return *this;
    }

    [[nodiscard]] T* Get() const noexcept
    {
      return mPointer;
    }

    T* operator->() const noexcept
    {
      return mPointer;
    }

    explicit operator bool() const noexcept
    {
      return mPointer != nullptr;
    }

    /// The out parameter of a creating call.
    T** operator&() noexcept
    {
      Reset();
      return &mPointer;
    }

    T** GetAddressOf() noexcept
    {
      return &mPointer;
    }

    T** ReleaseAndGetAddressOf() noexcept
    {
      Reset();
      return &mPointer;
    }

    void Reset() noexcept
    {
      if (T* const held = std::exchange(mPointer, nullptr); held != nullptr) {
        held->Release();
      }
    }

    [[nodiscard]] T* Detach() noexcept
    {
      return std::exchange(mPointer, nullptr);
    }

    friend bool operator==(const ComPtr& pointer, std::nullptr_t) noexcept
    {
      return pointer.mPointer == nullptr;
    }

    friend bool operator!=(const ComPtr& pointer, std::nullptr_t) noexcept
    {
      return pointer.mPointer != nullptr;
    }

  private:
    T* mPointer = nullptr;
  };
} // namespace Microsoft::WRL
