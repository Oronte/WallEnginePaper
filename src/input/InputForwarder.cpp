#include "input/InputForwarder.h"

#include "desktop/DesktopHost.h"

namespace lwe {

void InputForwarder::Update() {
    POINT cursor{};
    state_.valid = GetCursorPos(&cursor) != FALSE;
    if (state_.valid) {
        state_.cursor = cursor;
    }

    const int primaryButton = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
    state_.primaryDown = (GetAsyncKeyState(primaryButton) & 0x8000) != 0;

    // WindowFromPoint n'est interroge que pendant un clic : aucun cout quand la souris est relachee.
    state_.overDesktop = false;
    if (state_.primaryDown && state_.valid) {
        if (HWND hit = WindowFromPoint(cursor)) {
            state_.overDesktop = DesktopHost::IsShellDesktopWindow(GetAncestor(hit, GA_ROOT));
        }
    }
}

}  // namespace lwe
