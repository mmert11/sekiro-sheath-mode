#include <libmem/libmem.h>

#include <windows.h>
#include <Xinput.h>
#include <stdio.h>

#include "minhook/include/MinHook.h"

#pragma comment(lib, "Xinput.lib")

static HMODULE real_dinput8 = nullptr;
typedef HRESULT(WINAPI* DirectInput8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);

static DirectInput8Create_t pDirectInput8Create = nullptr;
static FARPROC pDllCanUnloadNow = nullptr;
static FARPROC pDllGetClassObject = nullptr;
static FARPROC pDllRegisterServer = nullptr;
static FARPROC pDllUnregisterServer = nullptr;

typedef __int64(*TargetFunction_t)(void*, void*);
TargetFunction_t originalTargetFunction = nullptr;

__int64 hTargetFunction(void* arg1, void* arg2)
{
    static bool wasDown = false;

    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(XINPUT_STATE));

    if (XInputGetState(0, &state) == ERROR_SUCCESS)
    {
        bool isDown = (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN);

        if (isDown && !wasDown)
        {
            char* flag = ((char*)arg2 + 8);
            *flag ^= 0x01;
        }

        wasDown = isDown;
    }

    __int64 result = originalTargetFunction(arg1, arg2);
    return result;
}

DWORD WINAPI InitThread(LPVOID)
{
    Sleep(5000);
    lm_module_t sekiro;
    LM_FindModule("sekiro.exe", &sekiro);

    if (sekiro.base <= 0)
    {
        MessageBoxA(NULL, "Sheath mode failed at getting sekiro module!", "Error", MB_ICONERROR);
        return -1;
    }

    lm_address_t sigAddr = LM_SigScan("48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 41 56 48 83 EC ? 0F 10 42 ? 48 8B FA 48 8D 59 ? 4C 8B F2 48 8B E9 0F 11 41 ? 48 2B F9 BE ? ? ? ? F2 0F 10 4A ? F2 0F 11 49 ? 8B 42 ? 89 41 ? 66 0F 1F 84 00 ? ? ? ? 48 8D 14 1F 48 8B CB E8 ? ? ? ? 48 83 C3 ? 48 83 EE ? 75 ? 33 D2",
        sekiro.base, sekiro.size);
    __int64 hookAddress = sigAddr; //0x1409cc9a0;

    if (MH_Initialize() != MH_OK)
        return 0;

    if (MH_CreateHook(
        reinterpret_cast<LPVOID>(hookAddress),
        &hTargetFunction,
        reinterpret_cast<LPVOID*>(&originalTargetFunction)) == MH_OK)
    {
        MH_EnableHook(reinterpret_cast<LPVOID>(hookAddress));
    }

    return 0;
}

static void LoadRealDInput8()
{
    if (real_dinput8) return;

    wchar_t sysPath[MAX_PATH];
    GetSystemDirectoryW(sysPath, MAX_PATH);
    wcscat_s(sysPath, L"\\dinput8.dll");

    real_dinput8 = LoadLibraryW(sysPath);
    if (!real_dinput8) return;

    pDirectInput8Create = (DirectInput8Create_t)GetProcAddress(real_dinput8, "DirectInput8Create");
    pDllCanUnloadNow = GetProcAddress(real_dinput8, "DllCanUnloadNow");
    pDllGetClassObject = GetProcAddress(real_dinput8, "DllGetClassObject");
    pDllRegisterServer = GetProcAddress(real_dinput8, "DllRegisterServer");
    pDllUnregisterServer = GetProcAddress(real_dinput8, "DllUnregisterServer");
}

extern "C" __declspec(dllexport)
HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD dwVersion,
    REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter)
{
    LoadRealDInput8();
    return pDirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
}

extern "C" HRESULT WINAPI My_DllCanUnloadNow(void)
{
    LoadRealDInput8();
    return pDllCanUnloadNow ? ((HRESULT(WINAPI*)())pDllCanUnloadNow)() : S_OK;
}

extern "C" HRESULT WINAPI My_DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
    LoadRealDInput8();
    return pDllGetClassObject ? ((HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*))pDllGetClassObject)(rclsid, riid, ppv) : E_FAIL;
}

extern "C" HRESULT WINAPI My_DllRegisterServer(void)
{
    LoadRealDInput8();
    return pDllRegisterServer ? ((HRESULT(WINAPI*)())pDllRegisterServer)() : S_OK;
}

extern "C" HRESULT WINAPI My_DllUnregisterServer(void)
{
    LoadRealDInput8();
    return pDllUnregisterServer ? ((HRESULT(WINAPI*)())pDllUnregisterServer)() : S_OK;
}

#pragma comment(linker, "/export:DllCanUnloadNow=My_DllCanUnloadNow")
#pragma comment(linker, "/export:DllGetClassObject=My_DllGetClassObject")
#pragma comment(linker, "/export:DllRegisterServer=My_DllRegisterServer")
#pragma comment(linker, "/export:DllUnregisterServer=My_DllUnregisterServer")

BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved)
{
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        LoadRealDInput8();
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        break;

    case DLL_PROCESS_DETACH:
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        if (real_dinput8) {
            FreeLibrary(real_dinput8);
            real_dinput8 = nullptr;
        }
        break;
    }
    return TRUE;
}
