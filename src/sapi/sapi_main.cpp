#include <new>

#include <sapi.h>

#include "com.hpp"
#include "ISpTTSEngineImpl.hpp"
#include "pipe_client.h"
#include "voice_registry.hpp"

namespace {

HINSTANCE g_dll_handle = nullptr;
Infovox::com::class_object_factory g_cls_obj_factory;

[[nodiscard]] std::wstring clsid_to_string(const GUID& clsid) {
    wchar_t buf[64];
    StringFromGUID2(clsid, buf, 64);
    return std::wstring(buf);
}

}  // namespace

namespace Infovox {
namespace sapi {
// pipe_client.cpp needs the module handle to find the install directory.
HMODULE moduleHandle() { return g_dll_handle; }
}  // namespace sapi
}  // namespace Infovox

BOOL APIENTRY DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID) {
    if (dwReason == DLL_PROCESS_ATTACH) {
        g_dll_handle = hInstance;
        DisableThreadLibraryCalls(hInstance);
        try {
            g_cls_obj_factory.register_class<Infovox::sapi::ISpTTSEngineImpl>();
        } catch (...) {
            return FALSE;
        }
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    return g_cls_obj_factory.create(rclsid, riid, ppv);
}

STDAPI DllCanUnloadNow() {
    return Infovox::com::object_counter::is_zero() ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer() {
    try {
        Infovox::com::class_registrar r(g_dll_handle);
        r.register_class<Infovox::sapi::ISpTTSEngineImpl>();
        // Voice tokens land in whichever registry view matches this dll's
        // architecture: the 32-bit build fills WOW6432Node, where 32-bit SAPI
        // looks, and the 64-bit build the native view.  Both are installed.
        Infovox::sapi::write_voice_tokens(
            HKEY_LOCAL_MACHINE,
            clsid_to_string(__uuidof(Infovox::sapi::ISpTTSEngineImpl)));
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_UNEXPECTED;
    }
}

STDAPI DllUnregisterServer() {
    try {
        // The worker holds the engine packs open; stop it before the installer
        // starts deleting files underneath it.
        Infovox::sapi::PipeClient::shutdownWorker();
        Infovox::sapi::remove_voice_tokens(HKEY_LOCAL_MACHINE);
        Infovox::com::class_registrar r(g_dll_handle);
        r.unregister_class<Infovox::sapi::ISpTTSEngineImpl>();
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_UNEXPECTED;
    }
}
