#pragma once

#include <windows.h>
#include <objbase.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace lwe {

// Exception portee par les erreurs COM/DirectX fatales a l'initialisation.
class HrError : public std::runtime_error {
public:
    HrError(HRESULT hr, const char* what);
    HRESULT Code() const noexcept { return hr_; }

private:
    HRESULT hr_;
};

inline void ThrowIfFailed(HRESULT hr, const char* what) {
    if (FAILED(hr)) {
        throw HrError(hr, what);
    }
}

// "0x887A0005 : L'instance de l'appareil GPU a ete suspendue..." (message systeme localise).
std::wstring HResultToString(HRESULT hr);

inline uintptr_t HwndValue(HWND hwnd) noexcept {
    return reinterpret_cast<uintptr_t>(hwnd);
}

struct HandleCloser {
    void operator()(HANDLE handle) const noexcept {
        if (handle && handle != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle);
        }
    }
};
using UniqueHandle = std::unique_ptr<void, HandleCloser>;

// Initialisation COM liee a la portee (STA : requis par WebView2 et le Shell).
class ScopedComInit {
public:
    explicit ScopedComInit(DWORD model = COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE) noexcept
        : hr_(CoInitializeEx(nullptr, model)) {}
    ~ScopedComInit() {
        if (SUCCEEDED(hr_)) {
            CoUninitialize();
        }
    }
    ScopedComInit(const ScopedComInit&) = delete;
    ScopedComInit& operator=(const ScopedComInit&) = delete;

    HRESULT Result() const noexcept { return hr_; }

private:
    HRESULT hr_;
};

}  // namespace lwe
