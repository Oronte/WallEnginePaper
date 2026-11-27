#include <windows.h>

#include "app/Application.h"

int APIENTRY wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
    // Le manifeste declare deja Per-Monitor V2 ; cet appel couvre une execution sans manifeste
    // (il echoue sans consequence si la sensibilite DPI est deja fixee).
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);

    lwe::Application app(instance, lwe::ParseCommandLine());
    return app.Run();
}
