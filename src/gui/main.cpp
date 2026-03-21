#include "gui/app.h"
#include <Windows.h>

// Entry point for Win32 GUI application
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                     LPSTR lpCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    memforge::App app;
    return app.Run();
}
