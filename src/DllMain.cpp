// DllMain.cpp - MeowPAPI.dll 入口
//
// MeowPAPI.dll 不是 LeviLamina 模块，只是一个被静态库通过 LoadLibrary 加载的工具 DLL。
// DllMain 仅做最小化初始化。
#include <Windows.h>

BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD ulReasonForCall, LPVOID /*lpReserved*/) {
    switch (ulReasonForCall) {
        case DLL_PROCESS_ATTACH:
            break;
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
