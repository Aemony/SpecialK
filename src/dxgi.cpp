/**
 * This file is part of Special K.
 *
 * Special K is free software : you can redistribute it
 * and/or modify it under the terms of the GNU General Public License
 * as published by The Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * Special K is distributed in the hope that it will be useful,
 *
 * But WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Special K.
 *
 *   If not, see <http://www.gnu.org/licenses/>.
 *
**/
#define _CRT_SECURE_NO_WARNINGS

#include "dxgi_backend.h"

#include "dxgi_interfaces.h"
#include <d3d11.h>
#include <d3d11_1.h>

#include <atlbase.h>

#include "nvapi.h"
#include "config.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#include "log.h"

#include "core.h"
#include "command.h"
#include "framerate.h"

struct sk_window_s {
  HWND    hWnd;
  WNDPROC WndProc_Original;
};

extern sk_window_s game_window;


extern std::wstring host_app;

extern BOOL __stdcall SK_NvAPI_SetFramerateLimit (uint32_t limit);
extern void __stdcall SK_NvAPI_SetAppFriendlyName (const wchar_t* wszFriendlyName);

HRESULT
WINAPI
D3D11Dev_CreateTexture2D_Override (
  _In_            ID3D11Device           *This,
  _In_      const D3D11_TEXTURE2D_DESC   *pDesc,
  _In_opt_  const D3D11_SUBRESOURCE_DATA *pInitialData,
  _Out_opt_       ID3D11Texture2D        **ppTexture2D );

extern "C++" bool SK_FO4_IsFullscreen       (void);
extern "C++" bool SK_FO4_IsBorderlessWindow (void);


// TODO: Get this stuff out of here, it's breaking what _DSlittle design work there is.
extern "C++" void SK_DS3_InitPlugin         (void);
extern "C++" bool SK_DS3_UseFlipMode        (void);
extern "C++" bool SK_DS3_IsBorderless       (void);

extern "C++" HRESULT STDMETHODCALLTYPE
                  SK_DS3_PresentFirstFrame   (IDXGISwapChain *, UINT, UINT);

//#define FULL_RESOLUTION


extern int                      gpu_prio;

bool             bAlwaysAllowFullscreen = false;
HWND             hWndRender             = 0;
ID3D11Device*    g_pDevice              = nullptr;

bool bFlipMode = false;
bool bWait     = false;

// Used for integrated GPU override
int              SK_DXGI_preferred_adapter = 0;

bool
WINAPI
SK_DXGI_EnableFlipMode (bool bFlip)
{
  bool before = bFlipMode;

  bFlipMode = bFlip;

  return before;
}

void
WINAPI
SKX_D3D11_EnableFullscreen (bool bFullscreen)
{
  bAlwaysAllowFullscreen = bFullscreen;
}

bool SK_D3D11_TextureIsCached    (ID3D11Texture2D* pTex);
void SK_D3D11_RemoveTexFromCache (ID3D11Texture2D* pTex);


struct dxgi_caps_t {
  struct {
    bool latency_control = false;
    bool enqueue_event   = false;
  } device;

  struct {
    bool flip_sequential = false;
    bool flip_discard    = false;
    bool waitable        = false;
  } present;
} dxgi_caps;

extern "C" {
  typedef HRESULT (STDMETHODCALLTYPE *CreateDXGIFactory2_pfn) \
    (UINT Flags, REFIID riid,  void** ppFactory);
  typedef HRESULT (STDMETHODCALLTYPE *CreateDXGIFactory1_pfn) \
    (REFIID riid,  void** ppFactory);
  typedef HRESULT (STDMETHODCALLTYPE *CreateDXGIFactory_pfn)  \
    (REFIID riid,  void** ppFactory);

  typedef HRESULT (WINAPI *D3D11CreateDeviceAndSwapChain_pfn)(
    _In_opt_                             IDXGIAdapter*, 
                                         D3D_DRIVER_TYPE,
                                         HMODULE,
                                         UINT, 
    _In_reads_opt_ (FeatureLevels) CONST D3D_FEATURE_LEVEL*, 
                                         UINT FeatureLevels,
                                         UINT,
    _In_opt_                       CONST DXGI_SWAP_CHAIN_DESC*,
    _Out_opt_                            IDXGISwapChain**,
    _Out_opt_                            ID3D11Device**, 
    _Out_opt_                            D3D_FEATURE_LEVEL*,
    _Out_opt_                            ID3D11DeviceContext**);


  typedef HRESULT (STDMETHODCALLTYPE *PresentSwapChain_pfn)(
                                         IDXGISwapChain *This,
                                         UINT            SyncInterval,
                                         UINT            Flags);

  typedef HRESULT (STDMETHODCALLTYPE *CreateSwapChain_pfn)(
                                         IDXGIFactory          *This,
                                   _In_  IUnknown              *pDevice,
                                   _In_  DXGI_SWAP_CHAIN_DESC  *pDesc,
                                  _Out_  IDXGISwapChain       **ppSwapChain);

  typedef HRESULT (STDMETHODCALLTYPE *SetFullscreenState_pfn)(
                                         IDXGISwapChain *This,
                                         BOOL            Fullscreen,
                                         IDXGIOutput    *pTarget);

  typedef HRESULT (STDMETHODCALLTYPE *GetFullscreenState_pfn)(
                                         IDXGISwapChain  *This,
                              _Out_opt_  BOOL            *pFullscreen,
                              _Out_opt_  IDXGIOutput    **ppTarget );

  typedef HRESULT (STDMETHODCALLTYPE *ResizeBuffers_pfn)(
                                         IDXGISwapChain *This,
                              /* [in] */ UINT            BufferCount,
                              /* [in] */ UINT            Width,
                              /* [in] */ UINT            Height,
                              /* [in] */ DXGI_FORMAT     NewFormat,
                              /* [in] */ UINT            SwapChainFlags);

  typedef HRESULT (STDMETHODCALLTYPE *ResizeTarget_pfn)(
                                    _In_ IDXGISwapChain  *This,
                              _In_ const DXGI_MODE_DESC  *pNewTargetParameters );

  typedef HRESULT (STDMETHODCALLTYPE *GetDisplayModeList_pfn)(
                                         IDXGIOutput     *This,
                              /* [in] */ DXGI_FORMAT      EnumFormat,
                              /* [in] */ UINT             Flags,
                              /* [annotation][out][in] */ 
                                _Inout_  UINT            *pNumModes,
                              /* [annotation][out] */ 
_Out_writes_to_opt_(*pNumModes,*pNumModes)
                                         DXGI_MODE_DESC *pDesc );

  typedef HRESULT (STDMETHODCALLTYPE *FindClosestMatchingMode_pfn)(
                                         IDXGIOutput    *This,
                             /* [annotation][in] */ 
                             _In_  const DXGI_MODE_DESC *pModeToMatch,
                             /* [annotation][out] */ 
                             _Out_       DXGI_MODE_DESC *pClosestMatch,
                             /* [annotation][in] */ 
                              _In_opt_  IUnknown *pConcernedDevice );

  typedef HRESULT (STDMETHODCALLTYPE *WaitForVBlank_pfn)(
                                         IDXGIOutput    *This );


  typedef HRESULT (STDMETHODCALLTYPE *GetDesc1_pfn)(IDXGIAdapter1      *This,
                                           _Out_    DXGI_ADAPTER_DESC1 *pDesc);
  typedef HRESULT (STDMETHODCALLTYPE *GetDesc2_pfn)(IDXGIAdapter2      *This,
                                             _Out_  DXGI_ADAPTER_DESC2 *pDesc);
  typedef HRESULT (STDMETHODCALLTYPE *GetDesc_pfn) (IDXGIAdapter       *This,
                                             _Out_  DXGI_ADAPTER_DESC  *pDesc);

  typedef HRESULT (STDMETHODCALLTYPE *EnumAdapters_pfn)(
                                          IDXGIFactory  *This,
                                          UINT           Adapter,
                                    _Out_ IDXGIAdapter **ppAdapter);

  typedef HRESULT (STDMETHODCALLTYPE *EnumAdapters1_pfn)(
                                          IDXGIFactory1  *This,
                                          UINT            Adapter,
                                    _Out_ IDXGIAdapter1 **ppAdapter);

  volatile
    D3D11CreateDeviceAndSwapChain_pfn
    D3D11CreateDeviceAndSwapChain_Import = nullptr;

  CreateSwapChain_pfn     CreateSwapChain_Original     = nullptr;
  PresentSwapChain_pfn    Present_Original             = nullptr;
  SetFullscreenState_pfn  SetFullscreenState_Original  = nullptr;
  GetFullscreenState_pfn  GetFullscreenState_Original  = nullptr;
  ResizeBuffers_pfn       ResizeBuffers_Original       = nullptr;
  ResizeTarget_pfn        ResizeTarget_Original        = nullptr;

  GetDisplayModeList_pfn      GetDisplayModeList_Original      = nullptr;
  FindClosestMatchingMode_pfn FindClosestMatchingMode_Original = nullptr;
  WaitForVBlank_pfn           WaitForVBlank_Original           = nullptr;

  GetDesc_pfn             GetDesc_Original             = nullptr;
  GetDesc1_pfn            GetDesc1_Original            = nullptr;
  GetDesc2_pfn            GetDesc2_Original            = nullptr;

  EnumAdapters_pfn        EnumAdapters_Original        = nullptr;
  EnumAdapters1_pfn       EnumAdapters1_Original       = nullptr;

  CreateDXGIFactory_pfn   CreateDXGIFactory_Import     = nullptr;
  CreateDXGIFactory1_pfn  CreateDXGIFactory1_Import    = nullptr;
  CreateDXGIFactory2_pfn  CreateDXGIFactory2_Import    = nullptr;

  const wchar_t*
  SK_DescribeVirtualProtectFlags (DWORD dwProtect)
  {
    switch (dwProtect)
    {
    case 0x10:
      return L"Execute";
    case 0x20:
      return L"Execute + Read-Only";
    case 0x40:
      return L"Execute + Read/Write";
    case 0x80:
      return L"Execute + Read-Only or Copy-on-Write)";
    case 0x01:
      return L"No Access";
    case 0x02:
      return L"Read-Only";
    case 0x04:
      return L"Read/Write";
    case 0x08:
      return L" Read-Only or Copy-on-Write";
    default:
      return L"UNKNOWN";
    }
  }
#define DXGI_CALL(_Ret, _Call) {                                     \
  dll_log.LogEx (true, L"[   DXGI   ]  Calling original function: ");\
  (_Ret) = (_Call);                                                  \
  dll_log.LogEx (false, L" (ret=%s)\n", SK_DescribeHRESULT (_Ret)); \
}

  // Interface-based DXGI call
#define DXGI_LOG_CALL_I(_Interface,_Name,_Format)                           \
  dll_log.LogEx (true, L"[   DXGI   ] [!] %s::%s (", _Interface, _Name);    \
  dll_log.LogEx (false, _Format
  // Global DXGI call
#define DXGI_LOG_CALL(_Name,_Format)                                        \
  dll_log.LogEx (true, L"[   DXGI   ] [!] %s (", _Name);                    \
  dll_log.LogEx (false, _Format
#define DXGI_LOG_CALL_END                                                   \
  dll_log.LogEx (false, L") -- [Calling Thread: 0x%04x]\n",                 \
    GetCurrentThreadId ());

#define DXGI_LOG_CALL_I0(_Interface,_Name) {                                 \
  DXGI_LOG_CALL_I   (_Interface,_Name, L"void"));                            \
  DXGI_LOG_CALL_END                                                          \
}

#define DXGI_LOG_CALL_I1(_Interface,_Name,_Format,_Args) {                   \
  DXGI_LOG_CALL_I   (_Interface,_Name, _Format), _Args);                     \
  DXGI_LOG_CALL_END                                                          \
}

#define DXGI_LOG_CALL_I2(_Interface,_Name,_Format,_Args0,_Args1) {           \
  DXGI_LOG_CALL_I   (_Interface,_Name, _Format), _Args0, _Args1);            \
  DXGI_LOG_CALL_END                                                          \
}

#define DXGI_LOG_CALL_I3(_Interface,_Name,_Format,_Args0,_Args1,_Args2) {    \
  DXGI_LOG_CALL_I   (_Interface,_Name, _Format), _Args0, _Args1, _Args2);    \
  DXGI_LOG_CALL_END                                                          \
}
#define DXGI_LOG_CALL_I5(_Interface,_Name,_Format,_Args0,_Args1,_Args2,      \
                         _Args3,_Args4) {                                    \
  DXGI_LOG_CALL_I   (_Interface,_Name, _Format), _Args0, _Args1, _Args2,     \
                                                 _Args3, _Args4);            \
  DXGI_LOG_CALL_END                                                          \
}


#define DXGI_LOG_CALL_0(_Name) {                               \
  DXGI_LOG_CALL   (_Name, L"void"));                           \
  DXGI_LOG_CALL_END                                            \
}

#define DXGI_LOG_CALL_1(_Name,_Format,_Args0) {                \
  DXGI_LOG_CALL   (_Name, _Format), _Args0);                   \
  DXGI_LOG_CALL_END                                            \
}

#define DXGI_LOG_CALL_2(_Name,_Format,_Args0,_Args1) {         \
  DXGI_LOG_CALL     (_Name, _Format), _Args0, _Args1);         \
  DXGI_LOG_CALL_END                                            \
}

#define DXGI_LOG_CALL_3(_Name,_Format,_Args0,_Args1,_Args2) {  \
  DXGI_LOG_CALL     (_Name, _Format), _Args0, _Args1, _Args2); \
  DXGI_LOG_CALL_END                                            \
}

#define DXGI_STUB(_Return, _Name, _Proto, _Args)                            \
  __declspec (nothrow) _Return STDMETHODCALLTYPE                            \
  _Name _Proto {                                                            \
    WaitForInit ();                                                         \
                                                                            \
    typedef _Return (STDMETHODCALLTYPE *passthrough_pfn) _Proto;            \
    static passthrough_pfn _default_impl = nullptr;                         \
                                                                            \
    if (_default_impl == nullptr) {                                         \
      static const char* szName = #_Name;                                   \
      _default_impl = (passthrough_pfn)GetProcAddress (backend_dll, szName);\
                                                                            \
      if (_default_impl == nullptr) {                                       \
        dll_log.Log (                                                       \
          L"Unable to locate symbol  %s in dxgi.dll",                       \
          L#_Name);                                                         \
        return E_NOTIMPL;                                                   \
      }                                                                     \
    }                                                                       \
                                                                            \
    dll_log.Log (L"[   DXGI   ] [!] %s %s - "                               \
             L"[Calling Thread: 0x%04x]",                                   \
      L#_Name, L#_Proto, GetCurrentThreadId ());                            \
                                                                            \
    return _default_impl _Args;                                             \
}

  extern "C++" {
    int
    SK_GetDXGIFactoryInterfaceVer (const IID& riid)
    {
      if (riid == __uuidof (IDXGIFactory))
        return 0;
      if (riid == __uuidof (IDXGIFactory1))
        return 1;
      if (riid == __uuidof (IDXGIFactory2))
        return 2;
      if (riid == __uuidof (IDXGIFactory3))
        return 3;
      if (riid == __uuidof (IDXGIFactory4))
        return 4;

      //assert (false);
      return -1;
    }

    std::wstring
    SK_GetDXGIFactoryInterfaceEx (const IID& riid)
    {
      std::wstring interface_name;

      if (riid == __uuidof (IDXGIFactory))
        interface_name = L"IDXGIFactory";
      else if (riid == __uuidof (IDXGIFactory1))
        interface_name = L"IDXGIFactory1";
      else if (riid == __uuidof (IDXGIFactory2))
        interface_name = L"IDXGIFactory2";
      else if (riid == __uuidof (IDXGIFactory3))
        interface_name = L"IDXGIFactory3";
      else if (riid == __uuidof (IDXGIFactory4))
        interface_name = L"IDXGIFactory4";
      else {
        wchar_t *pwszIID;

        if (SUCCEEDED (StringFromIID (riid, (LPOLESTR *)&pwszIID)))
        {
          interface_name = pwszIID;
          CoTaskMemFree (pwszIID);
        }
      }

      return interface_name;
    }

    int
    SK_GetDXGIFactoryInterfaceVer (IUnknown *pFactory)
    {
      CComPtr <IUnknown> pTemp;

      if (SUCCEEDED (
        pFactory->QueryInterface (__uuidof (IDXGIFactory4), (void **)&pTemp)))
      {
        dxgi_caps.device.enqueue_event    = true;
        dxgi_caps.device.latency_control  = true;
        dxgi_caps.present.flip_sequential = true;
        dxgi_caps.present.waitable        = true;
        dxgi_caps.present.flip_discard    = true;
        pTemp.Release ();
        return 4;
      }
      if (SUCCEEDED (
        pFactory->QueryInterface (__uuidof (IDXGIFactory3), (void **)&pTemp)))
      {
        dxgi_caps.device.enqueue_event    = true;
        dxgi_caps.device.latency_control  = true;
        dxgi_caps.present.flip_sequential = true;
        dxgi_caps.present.waitable        = true;
        pTemp.Release ();
        return 3;
      }

      if (SUCCEEDED (
        pFactory->QueryInterface (__uuidof (IDXGIFactory2), (void **)&pTemp)))
      {
        dxgi_caps.device.enqueue_event    = true;
        dxgi_caps.device.latency_control  = true;
        dxgi_caps.present.flip_sequential = true;
        pTemp.Release ();
        return 2;
      }

      if (SUCCEEDED (
        pFactory->QueryInterface (__uuidof (IDXGIFactory1), (void **)&pTemp)))
      {
        dxgi_caps.device.latency_control  = true;
        pTemp.Release ();
        return 1;
      }

      if (SUCCEEDED (
        pFactory->QueryInterface (__uuidof (IDXGIFactory), (void **)&pTemp)))
      {
        pTemp.Release ();
        return 0;
      }

      //assert (false);
      return -1;
    }

    std::wstring
    SK_GetDXGIFactoryInterface (IUnknown *pFactory)
    {
      int iver = SK_GetDXGIFactoryInterfaceVer (pFactory);

      if (iver == 4)
        return SK_GetDXGIFactoryInterfaceEx (__uuidof (IDXGIFactory4));

      if (iver == 3)
        return SK_GetDXGIFactoryInterfaceEx (__uuidof (IDXGIFactory3));

      if (iver == 2)
        return SK_GetDXGIFactoryInterfaceEx (__uuidof (IDXGIFactory2));

      if (iver == 1)
        return SK_GetDXGIFactoryInterfaceEx (__uuidof (IDXGIFactory1));

      if (iver == 0)
        return SK_GetDXGIFactoryInterfaceEx (__uuidof (IDXGIFactory));

      return L"{Invalid-Factory-UUID}";
    }

    int
    SK_GetDXGIAdapterInterfaceVer (const IID& riid)
    {
      if (riid == __uuidof (IDXGIAdapter))
        return 0;
      if (riid == __uuidof (IDXGIAdapter1))
        return 1;
      if (riid == __uuidof (IDXGIAdapter2))
        return 2;
      if (riid == __uuidof (IDXGIAdapter3))
        return 3;

      //assert (false);
      return -1;
    }

    std::wstring
    SK_GetDXGIAdapterInterfaceEx (const IID& riid)
    {
      std::wstring interface_name;

      if (riid == __uuidof (IDXGIAdapter))
        interface_name = L"IDXGIAdapter";
      else if (riid == __uuidof (IDXGIAdapter1))
        interface_name = L"IDXGIAdapter1";
      else if (riid == __uuidof (IDXGIAdapter2))
        interface_name = L"IDXGIAdapter2";
      else if (riid == __uuidof (IDXGIAdapter3))
        interface_name = L"IDXGIAdapter3";
      else {
        wchar_t* pwszIID;

        if (SUCCEEDED (StringFromIID (riid, (LPOLESTR *)&pwszIID)))
        {
          interface_name = pwszIID;
          CoTaskMemFree (pwszIID);
        }
      }

      return interface_name;
    }

    int
    SK_GetDXGIAdapterInterfaceVer (IUnknown *pAdapter)
    {
      CComPtr <IUnknown> pTemp;

      if (SUCCEEDED(
        pAdapter->QueryInterface (__uuidof (IDXGIAdapter3), (void **)&pTemp)))
      {
        pTemp.Release ();
        return 3;
      }

      if (SUCCEEDED(
        pAdapter->QueryInterface (__uuidof (IDXGIAdapter2), (void **)&pTemp)))
      {
        pTemp.Release ();
        return 2;
      }

      if (SUCCEEDED(
        pAdapter->QueryInterface (__uuidof (IDXGIAdapter1), (void **)&pTemp)))
      {
        pTemp.Release ();
        return 1;
      }

      if (SUCCEEDED(
        pAdapter->QueryInterface (__uuidof (IDXGIAdapter), (void **)&pTemp)))
      {
        pTemp.Release ();
        return 0;
      }

      //assert (false);
      return -1;
    }

    std::wstring
    SK_GetDXGIAdapterInterface (IUnknown *pAdapter)
    {
      int iver = SK_GetDXGIAdapterInterfaceVer (pAdapter);

      if (iver == 3)
        return SK_GetDXGIAdapterInterfaceEx (__uuidof (IDXGIAdapter3));

      if (iver == 2)
        return SK_GetDXGIAdapterInterfaceEx (__uuidof (IDXGIAdapter2));

      if (iver == 1)
        return SK_GetDXGIAdapterInterfaceEx (__uuidof (IDXGIAdapter1));

      if (iver == 0)
        return SK_GetDXGIAdapterInterfaceEx (__uuidof (IDXGIAdapter));

      return L"{Invalid-Adapter-UUID}";
    }
  }

#define __PTR_SIZE   sizeof LPCVOID
#define __PAGE_PRIVS PAGE_EXECUTE_READWRITE

#define DXGI_VIRTUAL_OVERRIDE(_Base,_Index,_Name,_Override,_Original,_Type) { \
  void** vftable = *(void***)*_Base;                                          \
                                                                              \
  if (vftable [_Index] != _Override) {                                        \
    DWORD dwProtect;                                                          \
                                                                              \
    VirtualProtect (&vftable [_Index], __PTR_SIZE, __PAGE_PRIVS, &dwProtect); \
                                                                              \
    /*dll_log.Log (L" Old VFTable entry for %s: %08Xh  (Memory Policy: %s)",*/\
                 /*L##_Name, vftable [_Index],                              */\
                 /*SK_DescribeVirtualProtectFlags (dwProtect));             */\
                                                                              \
    if (_Original == NULL)                                                    \
      _Original = (##_Type)vftable [_Index];                                  \
                                                                              \
    /*dll_log.Log (L"  + %s: %08Xh", L#_Original, _Original);*/               \
                                                                              \
    vftable [_Index] = _Override;                                             \
                                                                              \
    VirtualProtect (&vftable [_Index], __PTR_SIZE, dwProtect, &dwProtect);    \
                                                                              \
    /*dll_log.Log (L" New VFTable entry for %s: %08Xh  (Memory Policy: %s)\n",*/\
                  /*L##_Name, vftable [_Index],                               */\
                  /*SK_DescribeVirtualProtectFlags (dwProtect));              */\
  }                                                                           \
}

#define DARK_SOULS
#ifdef DARK_SOULS
  extern "C++" int* __DS3_WIDTH;
  extern "C++" int* __DS3_HEIGHT;
#endif

  HRESULT
    STDMETHODCALLTYPE PresentCallback (IDXGISwapChain *This,
                                       UINT            SyncInterval,
                                       UINT            Flags)
  {
#ifdef DARK_SOULS
    if (__DS3_HEIGHT != nullptr) {
      DXGI_SWAP_CHAIN_DESC swap_desc;
      if (SUCCEEDED (This->GetDesc (&swap_desc))) {
        *__DS3_WIDTH  = swap_desc.BufferDesc.Width;
        *__DS3_HEIGHT = swap_desc.BufferDesc.Height;
      }
    }
#endif

    SK_BeginBufferSwap ();

    HRESULT hr = E_FAIL;

    if (This != NULL) {
      CComPtr <ID3D11Device> pDev;

      int interval = config.render.framerate.present_interval;
      int flags    = Flags;

      // Application preference
      if (interval == -1)
        interval = SyncInterval;

      if (bFlipMode) {
        flags = Flags | DXGI_PRESENT_RESTART;

        if (bWait)
          flags |= DXGI_PRESENT_DO_NOT_WAIT;
      }

      if (! bFlipMode) {
        // Test first, then do
        //if (S_OK == ((HRESULT (*)(IDXGISwapChain *, UINT, UINT))Present_Original)
                      //(This, interval, flags | DXGI_PRESENT_TEST)) {
          hr =
            ((HRESULT (*)(IDXGISwapChain *, UINT, UINT))Present_Original)
                         (This, interval, flags);
        //}

        if (hr != S_OK) {
            dll_log.Log (L"[   DXGI   ] *** IDXGISwapChain::Present (...) returned non-S_OK (%x :: %s)",
                         hr, SUCCEEDED (hr) ? L"Success" : L"Fail");
        }
      } else {
        // No overlays will work if we don't do this...
        /////if (config.osd.show) {
          hr =
            ((HRESULT (*)(IDXGISwapChain *, UINT, UINT))Present_Original)
            (This, 0, flags | DXGI_PRESENT_DO_NOT_SEQUENCE | DXGI_PRESENT_DO_NOT_WAIT);
        /////}

        DXGI_PRESENT_PARAMETERS pparams;
        pparams.DirtyRectsCount = 0;
        pparams.pDirtyRects     = nullptr;
        pparams.pScrollOffset   = nullptr;
        pparams.pScrollRect     = nullptr;

        CComPtr <IDXGISwapChain1> pSwapChain1;
        if (SUCCEEDED (This->QueryInterface (IID_PPV_ARGS (&pSwapChain1))))
        {
          hr = pSwapChain1->Present1 (interval, flags, &pparams);
        }
      }

      if (bWait) {
        CComPtr <IDXGISwapChain2> pSwapChain2;

        if (SUCCEEDED (This->QueryInterface (IID_PPV_ARGS (&pSwapChain2))))
        {
          if (pSwapChain2 != nullptr) {
            HANDLE hWait = pSwapChain2->GetFrameLatencyWaitableObject ();

            pSwapChain2.Release ();

            WaitForSingleObjectEx ( hWait,
                                      config.render.framerate.max_delta_time,
                                        TRUE );
          }
        }
      }

      static bool first_frame = true;

      if (first_frame) {
        if (sk::NVAPI::app_name == L"Fallout4.exe") {
          // Fix the broken borderless window system that doesn't scale the swapchain
          //   properly.
          if (SK_FO4_IsBorderlessWindow ()) {
            DEVMODE devmode = { 0 };
            devmode.dmSize = sizeof DEVMODE;
            EnumDisplaySettings (nullptr, ENUM_CURRENT_SETTINGS, &devmode);

            DXGI_SWAP_CHAIN_DESC desc;
            This->GetDesc (&desc);

            if (devmode.dmPelsHeight != desc.BufferDesc.Height ||
                devmode.dmPelsWidth  != desc.BufferDesc.Width) {
              devmode.dmPelsWidth  = desc.BufferDesc.Width;
              devmode.dmPelsHeight = desc.BufferDesc.Height;
              ChangeDisplaySettings (&devmode, CDS_FULLSCREEN);
            }
          }
        }

        else if (sk::NVAPI::app_name == L"DarkSoulsIII.exe") {
          SK_DS3_PresentFirstFrame (This, SyncInterval, Flags);
        }

        // TODO: Clean this code up
        if ( SUCCEEDED (This->GetDevice (IID_PPV_ARGS (&pDev))) )
        {
          CComPtr <IDXGIDevice>  pDevDXGI;
          CComPtr <IDXGIAdapter> pAdapter;
          CComPtr <IDXGIFactory> pFactory;

          if ( SUCCEEDED (pDev->QueryInterface (IID_PPV_ARGS (&pDevDXGI))) &&
               SUCCEEDED (pDevDXGI->GetAdapter               (&pAdapter))  &&
               SUCCEEDED (pAdapter->GetParent  (IID_PPV_ARGS (&pFactory))) ) {
            DXGI_SWAP_CHAIN_DESC desc;
            This->GetDesc (&desc);

            if (bAlwaysAllowFullscreen)
              pFactory->MakeWindowAssociation (desc.OutputWindow, DXGI_MWA_NO_WINDOW_CHANGES);

            hWndRender       = desc.OutputWindow;
            game_window.hWnd = hWndRender;
          }
        }
      }

      first_frame = false;

      if (SUCCEEDED (This->GetDevice (IID_PPV_ARGS (&pDev))))
      {
        HRESULT ret = E_FAIL;

        if (pDev != nullptr) {
          ret = SK_EndBufferSwap (hr, pDev);
        }

        return ret;
      }
    }

    // Not a D3D11 device -- weird...
    HRESULT ret = SK_EndBufferSwap (hr);

    return ret;
  }


  COM_DECLSPEC_NOTHROW
  __declspec (noinline)
  HRESULT
  STDMETHODCALLTYPE
  DXGIOutput_GetDisplayModeList_Override ( IDXGIOutput    *This,
                                /* [in] */ DXGI_FORMAT     EnumFormat,
                                /* [in] */ UINT            Flags,
                                /* [annotation][out][in] */ 
                                  _Inout_  UINT           *pNumModes,
                                /* [annotation][out] */ 
_Out_writes_to_opt_(*pNumModes,*pNumModes)
                                           DXGI_MODE_DESC *pDesc)
  {
//    dll_log.Log (L"[   DXGI   ] [!] IDXGIOutput::GetDisplayModeList (...)");

    HRESULT hr =
      GetDisplayModeList_Original (This, EnumFormat, DXGI_ENUM_MODES_SCALING, pNumModes, pDesc);

//    dll_log.Log (L" >> %lu modes", *pNumModes);
    return hr;
  }

  COM_DECLSPEC_NOTHROW
  __declspec (noinline)
  HRESULT
  STDMETHODCALLTYPE
  DXGIOutput_FindClosestMatchingMode_Override ( IDXGIOutput    *This,
                                                /* [annotation][in] */ 
                                    _In_  const DXGI_MODE_DESC *pModeToMatch,
                                                /* [annotation][out] */ 
                                         _Out_  DXGI_MODE_DESC *pClosestMatch,
                                                /* [annotation][in] */ 
                                      _In_opt_  IUnknown       *pConcernedDevice )
  {
//    dll_log.Log (L"[   DXGI   ] [!] IDXGIOutput::FindClosestMatchingMode (...)");
    return FindClosestMatchingMode_Original (This, pModeToMatch, pClosestMatch, pConcernedDevice );
  }

  COM_DECLSPEC_NOTHROW
  __declspec (noinline)
  HRESULT
  STDMETHODCALLTYPE
  DXGIOutput_WaitForVBlank_Override ( IDXGIOutput    *This )
  {
//    dll_log.Log (L"[   DXGI   ] [!] IDXGIOutput::WaitForVBlank (...)");
    return WaitForVBlank_Original (This);
  }

  COM_DECLSPEC_NOTHROW
  __declspec (noinline)
  HRESULT
  STDMETHODCALLTYPE
  DXGISwap_GetFullscreenState_Override ( IDXGISwapChain  *This,
                              _Out_opt_  BOOL            *pFullscreen,
                              _Out_opt_  IDXGIOutput    **ppTarget )
  {
    return GetFullscreenState_Original (This, pFullscreen, ppTarget);// nullptr, nullptr);
  }

  COM_DECLSPEC_NOTHROW
__declspec (noinline)
  HRESULT
  STDMETHODCALLTYPE
  DXGISwap_SetFullscreenState_Override ( IDXGISwapChain *This,
                                         BOOL            Fullscreen,
                                         IDXGIOutput    *pTarget )
  {
    DXGI_LOG_CALL_I2 (L"IDXGISwapChain", L"SetFullscreenState", L"%lu, %08Xh",
                      Fullscreen, pTarget);

#if 0
    DXGI_MODE_DESC mode_desc = { 0 };

    if (Fullscreen) {
      DXGI_SWAP_CHAIN_DESC desc;
      if (SUCCEEDED (This->GetDesc (&desc))) {
        mode_desc.Format                  = desc.BufferDesc.Format;
        mode_desc.Width                   = desc.BufferDesc.Width;
        mode_desc.Height                  = desc.BufferDesc.Height;

        mode_desc.RefreshRate.Denominator = desc.BufferDesc.RefreshRate.Denominator;
        mode_desc.RefreshRate.Numerator   = desc.BufferDesc.RefreshRate.Numerator;

        mode_desc.Scaling                 = desc.BufferDesc.Scaling;
        mode_desc.ScanlineOrdering        = desc.BufferDesc.ScanlineOrdering;
        ResizeTarget_Original           (This, &mode_desc);
      }
    }
#endif

    //UINT BufferCount = max (1, min (6, config.render.framerate.buffer_count));


    HRESULT ret;
    DXGI_CALL (ret, SetFullscreenState_Original (This, Fullscreen, pTarget));

#if 0
    if (Fullscreen && SUCCEEDED (ret)) {
      mode_desc.RefreshRate = { 0 };
      ResizeTarget_Original (This, &mode_desc);

      if (bFlipMode)
        ResizeBuffers_Original (This, 0, 0, 0, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH);
    }
#endif

    return ret;
  }

  COM_DECLSPEC_NOTHROW
  __declspec (noinline)
  HRESULT
  STDMETHODCALLTYPE
  DXGISwap_ResizeBuffers_Override ( IDXGISwapChain *This,
                               _In_ UINT            BufferCount,
                               _In_ UINT            Width,
                               _In_ UINT            Height,
                               _In_ DXGI_FORMAT     NewFormat,
                               _In_ UINT            SwapChainFlags )
  {
    DXGI_LOG_CALL_I5 (L"IDXGISwapChain", L"ResizeBuffers", L"%lu,%lu,%lu,...,0x%08X,0x%08X",
                        BufferCount, Width, Height, NewFormat, SwapChainFlags);

    if ( config.render.framerate.buffer_count != -1 && 
         config.render.framerate.buffer_count !=  BufferCount &&
         BufferCount                          !=  0 ) {
      BufferCount = config.render.framerate.buffer_count;
      dll_log.Log (L"[   DXGI   ] >> Buffer Count Override: %lu buffers", BufferCount);
    }

    // Fake it
    if (bWait)
      return S_OK;

    HRESULT ret;
    DXGI_CALL (ret, ResizeBuffers_Original (This, BufferCount, Width, Height, NewFormat, SwapChainFlags));

    return ret;
  }

  COM_DECLSPEC_NOTHROW
  __declspec (noinline)
  HRESULT
  STDMETHODCALLTYPE
  DXGISwap_ResizeTarget_Override ( IDXGISwapChain *This,
                        _In_ const DXGI_MODE_DESC *pNewTargetParameters )
  {
    DXGI_LOG_CALL_I5 (L"IDXGISwapChain", L"ResizeTarget", L"{ (%lux%lu@%3.1fHz), fmt=%lu, scaling=0x%02x }",
                        pNewTargetParameters->Width, pNewTargetParameters->Height,
                        pNewTargetParameters->RefreshRate.Denominator != 0 ?
                          (float)pNewTargetParameters->RefreshRate.Numerator /
                          (float)pNewTargetParameters->RefreshRate.Denominator :
                            0.0f,
                        pNewTargetParameters->Format,
                        pNewTargetParameters->Scaling );

    HRESULT ret;
    DXGI_CALL (ret, ResizeTarget_Original (This, pNewTargetParameters));

    return ret;
  }

  HRESULT
  STDMETHODCALLTYPE
  DXGIFactory_CreateSwapChain_Override ( IDXGIFactory          *This,
                                   _In_  IUnknown              *pDevice,
                                   _In_  DXGI_SWAP_CHAIN_DESC  *pDesc,
                                  _Out_  IDXGISwapChain       **ppSwapChain )
  {
    std::wstring iname = SK_GetDXGIFactoryInterface (This);

    DXGI_LOG_CALL_I3 (iname.c_str (), L"CreateSwapChain", L"%08Xh, %08Xh, %08Xh",
                      pDevice, pDesc, ppSwapChain);

    HRESULT ret;

    if (pDesc != nullptr) {
      dll_log.LogEx ( true,
        L"[   DXGI   ]  SwapChain: (%lux%lu @ %4.1f Hz - Scaling: %s) - {%s}"
        L" [%lu Buffers] :: Flags=0x%04X, SwapEffect: %s\n",
        pDesc->BufferDesc.Width,
        pDesc->BufferDesc.Height,
        pDesc->BufferDesc.RefreshRate.Denominator > 0 ? 
        (float)pDesc->BufferDesc.RefreshRate.Numerator /
        (float)pDesc->BufferDesc.RefreshRate.Denominator :
        (float)pDesc->BufferDesc.RefreshRate.Numerator,
        pDesc->BufferDesc.Scaling == 0 ?
        L"Unspecified" :
        pDesc->BufferDesc.Scaling == 1 ?
        L"Centered" : L"Stretched",
        pDesc->Windowed ? L"Windowed" : L"Fullscreen",
        pDesc->BufferCount,
        pDesc->Flags,
        pDesc->SwapEffect == 0 ?
        L"Discard" :
        pDesc->SwapEffect == 1 ?
        L"Sequential" :
        pDesc->SwapEffect == 2 ?
        L"<Unknown>" :
        pDesc->SwapEffect == 3 ?
        L"Flip Sequential" :
        pDesc->SwapEffect == 4 ?
        L"Flip Discard" : L"<Unknown>");

      // Set things up to make the swap chain Alt+Enter friendly
      if (bAlwaysAllowFullscreen) {
        pDesc->Flags                             |= DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        pDesc->Windowed                           = true;
        pDesc->BufferDesc.RefreshRate.Denominator = 0;
        pDesc->BufferDesc.RefreshRate.Numerator   = 0;
      }

      if (! bFlipMode)
        bFlipMode =
          (dxgi_caps.present.flip_sequential && ((host_app == L"Fallout4.exe") || SK_DS3_UseFlipMode ()));

      if (host_app == L"Fallout4.exe") {
        if (bFlipMode) {
          bFlipMode = (! SK_FO4_IsFullscreen ());
          if (bFlipMode) {
            bFlipMode = (! config.nvidia.sli.override);
          }
        }

        bFlipMode = bFlipMode && pDesc->BufferDesc.Scaling == 0;
      }

      bWait     = bFlipMode && dxgi_caps.present.waitable;

      // We cannot change the swapchain parameters if this is used...
      bWait = false;

      //if (config.render.framerate.max_delta_time == 0)
        //bWait = false;

      if (SK_DS3_IsBorderless ())
        pDesc->Flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

      if (bFlipMode) {
        if (bWait)
          pDesc->Flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

        // Flip Presentation Model requires 3 Buffers (1 is already counted)
        config.render.framerate.buffer_count =
          max (2, config.render.framerate.buffer_count);

        if (config.render.framerate.flip_discard &&
            dxgi_caps.present.flip_discard)
          pDesc->SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        else
          pDesc->SwapEffect  = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
      }

      else {
        // Resort to triple-buffering if flip mode is not available
        if (config.render.framerate.buffer_count > 2)
          config.render.framerate.buffer_count = 2;

        pDesc->SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
      }

      if (config.render.framerate.buffer_count != -1)
        pDesc->BufferCount = config.render.framerate.buffer_count;

      // We cannot switch modes on a waitable swapchain
      if (bFlipMode && bWait) {
        pDesc->Flags |=  DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        pDesc->Flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
      }

      dll_log.Log ( L"[ DXGI 1.2 ] >> Using %s Presentation Model  [Waitable: %s]",
                     bFlipMode ? L"Flip" : L"Traditional",
                       bWait ? L"Yes" : L"No" );
    }

    DXGI_CALL(ret, CreateSwapChain_Original (This, pDevice, pDesc, ppSwapChain));

    if ( SUCCEEDED (ret)      &&
         ppSwapChain  != NULL &&
       (*ppSwapChain) != NULL )
    {
      //if (bFlipMode || bWait)
        //DXGISwap_ResizeBuffers_Override (*ppSwapChain, config.render.framerate.buffer_count, pDesc->BufferDesc.Width, pDesc->BufferDesc.Height, pDesc->BufferDesc.Format, pDesc->Flags);

      DXGI_VIRTUAL_OVERRIDE ( ppSwapChain, 10, "IDXGISwapChain::SetFullscreenState",
                                DXGISwap_SetFullscreenState_Override,
                                         SetFullscreenState_Original,
                                           SetFullscreenState_pfn );

      DXGI_VIRTUAL_OVERRIDE ( ppSwapChain, 11, "IDXGISwapChain::GetFullscreenState",
                                DXGISwap_GetFullscreenState_Override,
                                         GetFullscreenState_Original,
                                           GetFullscreenState_pfn );

      DXGI_VIRTUAL_OVERRIDE ( ppSwapChain, 13, "IDXGISwapChain::ResizeBuffers",
                               DXGISwap_ResizeBuffers_Override,
                                        ResizeBuffers_Original,
                                          ResizeBuffers_pfn );

      DXGI_VIRTUAL_OVERRIDE ( ppSwapChain, 14, "IDXGISwapChain::ResizeTarget",
                                DXGISwap_ResizeTarget_Override,
                                         ResizeTarget_Original,
                                           ResizeTarget_pfn );

      CComPtr <IDXGIOutput> pOutput;
      if (SUCCEEDED ((*ppSwapChain)->GetContainingOutput (&pOutput))) {
        if (pOutput != nullptr) {
          DXGI_VIRTUAL_OVERRIDE ( &pOutput, 8, "IDXGIOutput::GetDisplayModeList",
                                    DXGIOutput_GetDisplayModeList_Override,
                                               GetDisplayModeList_Original,
                                               GetDisplayModeList_pfn );

          DXGI_VIRTUAL_OVERRIDE ( &pOutput, 9, "IDXGIOutput::FindClosestMatchingMode",
                                    DXGIOutput_FindClosestMatchingMode_Override,
                                               FindClosestMatchingMode_Original,
                                               FindClosestMatchingMode_pfn );

          DXGI_VIRTUAL_OVERRIDE ( &pOutput, 10, "IDXGIOutput::WaitForVBlank",
                                   DXGIOutput_WaitForVBlank_Override,
                                              WaitForVBlank_Original,
                                              WaitForVBlank_pfn );
        }
      }

      const uint32_t max_latency = config.render.framerate.pre_render_limit;

      CComPtr <IDXGISwapChain2> pSwapChain2;
      if ( bFlipMode && bWait &&
           SUCCEEDED ( (*ppSwapChain)->QueryInterface (IID_PPV_ARGS (&pSwapChain2)) )
          )
      {
        if (max_latency < 16) {
          dll_log.Log (L"[   DXGI   ] Setting Swapchain Frame Latency: %lu", max_latency);
          pSwapChain2->SetMaximumFrameLatency (max_latency);
        }

        HANDLE hWait = pSwapChain2->GetFrameLatencyWaitableObject ();

        pSwapChain2.Release ();

        WaitForSingleObjectEx ( hWait,
                                  config.render.framerate.max_delta_time,
                                    TRUE );
      }
      else
      {
        if (max_latency != -1) {
          CComPtr <IDXGIDevice1> pDevice1;

          if (SUCCEEDED ( (*ppSwapChain)->GetDevice (
                             IID_PPV_ARGS (&pDevice1)
                          )
                        )
             )
          {
            dll_log.Log (L"[   DXGI   ] Setting Device Frame Latency: %lu", max_latency);
            pDevice1->SetMaximumFrameLatency (max_latency);
          }
        }
      }

      CComPtr <ID3D11Device> pDev;

      if (SUCCEEDED ( pDevice->QueryInterface ( IID_PPV_ARGS (&pDev) )
                    )
         )
      {
        // Dangerous to hold a reference to this don't you think?!
        g_pDevice = pDev;

        DXGI_VIRTUAL_OVERRIDE (ppSwapChain, 8, "IDXGISwapChain::Present",
                               PresentCallback, Present_Original,
                               PresentSwapChain_pfn);
      }
    }

    return ret;
  }

  typedef HRESULT (WINAPI *D3D11Dev_CreateTexture2D_pfn)(
    _In_            ID3D11Device           *This,
    _In_      const D3D11_TEXTURE2D_DESC   *pDesc,
    _In_opt_  const D3D11_SUBRESOURCE_DATA *pInitialData,
    _Out_opt_       ID3D11Texture2D        **ppTexture2D
  );
  D3D11Dev_CreateTexture2D_pfn D3D11Dev_CreateTexture2D_Original = nullptr;

  typedef void (WINAPI *D3D11_RSSetScissorRects_pfn)(
    _In_           ID3D11DeviceContext *This,
    _In_           UINT                 NumRects,
    _In_opt_ const D3D11_RECT          *pRects
  );
  D3D11_RSSetScissorRects_pfn D3D11_RSSetScissorRects_Original = nullptr;


  typedef void (WINAPI *D3D11_RSSetViewports_pfn)(
  _In_           ID3D11DeviceContext* This,
  _In_           UINT                 NumViewports,
  _In_opt_ const D3D11_VIEWPORT     * pViewports
  );
  D3D11_RSSetViewports_pfn D3D11_RSSetViewports_Original = nullptr;

  typedef void (WINAPI *D3D11_VSSetConstantBuffers_pfn)(
  _In_     ID3D11DeviceContext* This,
  _In_     UINT                 StartSlot,
  _In_     UINT                 NumBuffers,
  _In_opt_ ID3D11Buffer *const *ppConstantBuffers
  );
  D3D11_VSSetConstantBuffers_pfn D3D11_VSSetConstantBuffers_Original = nullptr;

  typedef void (WINAPI *D3D11_UpdateSubresource_pfn)(
    _In_           ID3D11DeviceContext *This,
    _In_           ID3D11Resource      *pDstResource,
    _In_           UINT                 DstSubresource,
    _In_opt_ const D3D11_BOX           *pDstBox,
    _In_     const void                *pSrcData,
    _In_           UINT                 SrcRowPitch,
    _In_           UINT                 SrcDepthPitch
  );
  D3D11_UpdateSubresource_pfn D3D11_UpdateSubresource_Original = nullptr;


  typedef HRESULT (WINAPI *D3D11_Map_pfn)(
     _In_ ID3D11DeviceContext      *This,
     _In_ ID3D11Resource           *pResource,
     _In_ UINT                      Subresource,
     _In_ D3D11_MAP                 MapType,
     _In_ UINT                      MapFlags,
_Out_opt_ D3D11_MAPPED_SUBRESOURCE *pMappedResource
  );

  D3D11_Map_pfn D3D11_Map_Original = nullptr;

  typedef void (WINAPI *D3D11_CopyResource_pfn)(
    _In_ ID3D11DeviceContext *This,
    _In_ ID3D11Resource      *pDstResource,
    _In_ ID3D11Resource      *pSrcResource
  );

  D3D11_CopyResource_pfn D3D11_CopyResource_Original = nullptr;


  typedef void (WINAPI *D3D11_UpdateSubresource1_pfn)(
    _In_           ID3D11DeviceContext1 *This,
    _In_           ID3D11Resource       *pDstResource,
    _In_           UINT                  DstSubresource,
    _In_opt_ const D3D11_BOX            *pDstBox,
    _In_     const void                 *pSrcData,
    _In_           UINT                  SrcRowPitch,
    _In_           UINT                  SrcDepthPitch,
    _In_           UINT                  CopyFlags
  );
  D3D11_UpdateSubresource1_pfn D3D11_UpdateSubresource1_Original = nullptr;

  void
  WINAPI
  D3D11_RSSetScissorRects_Override (
          ID3D11DeviceContext *This,
          UINT                 NumRects,
    const D3D11_RECT          *pRects )
  {
  }

  void
  WINAPI
  D3D11_VSSetConstantBuffers_Override (
    ID3D11DeviceContext*  This,
    UINT                  StartSlot,
    UINT                  NumBuffers,
    ID3D11Buffer *const  *ppConstantBuffers )
  {
    //dll_log.Log (L"[   DXGI   ] [!]D3D11_VSSetConstantBuffers (%lu, %lu, ...)", StartSlot, NumBuffers);
    D3D11_VSSetConstantBuffers_Original (This, StartSlot, NumBuffers, ppConstantBuffers );
  }

  void
  WINAPI
  D3D11_UpdateSubresource_Override (
          ID3D11DeviceContext* This,
          ID3D11Resource      *pDstResource,
          UINT                 DstSubresource,
    const D3D11_BOX           *pDstBox,
    const void                *pSrcData,
          UINT                 SrcRowPitch,
          UINT                 SrcDepthPitch)
  {
    //dll_log.Log (L"[   DXGI   ] [!]D3D11_UpdateSubresource (%ph, %lu, %ph, %ph, %lu, %lu)", pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);

    CComPtr <ID3D11Texture2D> pTex;
    if (SUCCEEDED (pDstResource->QueryInterface (IID_PPV_ARGS (&pTex)))) {
      //if (SK_D3D11_TextureIsCached (pTex)) {
        //dll_log.Log (L"[DX11TexMgr] Cached texture was updated... removing from cache!");
        //SK_D3D11_RemoveTexFromCache (pTex);
      //}
    }

    D3D11_UpdateSubresource_Original (This, pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
  }

  HRESULT
  WINAPI
  D3D11_Map_Override (
     _In_ ID3D11DeviceContext      *This,
     _In_ ID3D11Resource           *pResource,
     _In_ UINT                      Subresource,
     _In_ D3D11_MAP                 MapType,
     _In_ UINT                      MapFlags,
_Out_opt_ D3D11_MAPPED_SUBRESOURCE *pMappedResource )
  {
    CComPtr <ID3D11Texture2D> pTex;
    if (SUCCEEDED (pResource->QueryInterface (IID_PPV_ARGS (&pTex)))) {
      //if (SK_D3D11_TextureIsCached (pTex)) {
        //dll_log.Log (L"[DX11TexMgr] Cached texture was updated... removing from cache!");
        //SK_D3D11_RemoveTexFromCache (pTex);
      //}
    }

    return D3D11_Map_Original (This, pResource, Subresource, MapType, MapFlags, pMappedResource);
  }

  void
  WINAPI
  D3D11_CopyResource_Override (
         ID3D11DeviceContext *This,
    _In_ ID3D11Resource      *pDstResource,
    _In_ ID3D11Resource      *pSrcResource )
  {
    dll_log.Log (L"[DX11TexMgr] Cached texture was updated... removing from cache!");

    D3D11_CopyResource_Original (This, pDstResource, pSrcResource);
  }




  void
  WINAPI
  D3D11_RSSetViewports_Override (
    ID3D11DeviceContext*  This,
    UINT                  NumViewports,
    const D3D11_VIEWPORT* pViewports )
  {
    D3D11_RSSetViewports_Original (This, NumViewports, pViewports);
  }

    typedef enum skUndesirableVendors {
    Microsoft = 0x1414,
    Intel     = 0x8086
  } Vendors;

  __declspec (nothrow)
  HRESULT
  STDMETHODCALLTYPE CreateDXGIFactory (REFIID   riid,
                                 _Out_ void   **ppFactory);

  // Do this in a thread because it is not safe to do from
  //   the thread that created the window or drives the message
  //     pump...
  DWORD
  WINAPI
  SK_DXGI_BringRenderWindowToTop (LPVOID user)
  {
    SetActiveWindow     (hWndRender);
    SetForegroundWindow (hWndRender);
    BringWindowToTop    (hWndRender);

    return 0;
  }

  HRESULT
  WINAPI
  D3D11CreateDeviceAndSwapChain_Detour (IDXGIAdapter          *pAdapter,
                                        D3D_DRIVER_TYPE        DriverType,
                                        HMODULE                Software,
                                        UINT                   Flags,
   _In_reads_opt_ (FeatureLevels) CONST D3D_FEATURE_LEVEL     *pFeatureLevels,
                                        UINT                   FeatureLevels,
                                        UINT                   SDKVersion,
   _In_opt_                       CONST DXGI_SWAP_CHAIN_DESC  *pSwapChainDesc,
   _Out_opt_                            IDXGISwapChain       **ppSwapChain,
   _Out_opt_                            ID3D11Device         **ppDevice,
   _Out_opt_                            D3D_FEATURE_LEVEL     *pFeatureLevel,
   _Out_opt_                            ID3D11DeviceContext  **ppImmediateContext)
  {
    DXGI_LOG_CALL_0 (L"D3D11CreateDeviceAndSwapChain");

    dll_log.LogEx (true, L"[  D3D 11  ]  <~> Preferred Feature Level(s): <%u> - ", FeatureLevels);

    for (UINT i = 0; i < FeatureLevels; i++) {
      switch (pFeatureLevels [i])
      {
      case D3D_FEATURE_LEVEL_9_1:
        dll_log.LogEx (false, L" 9_1");
        break;
      case D3D_FEATURE_LEVEL_9_2:
        dll_log.LogEx (false, L" 9_2");
        break;
      case D3D_FEATURE_LEVEL_9_3:
        dll_log.LogEx (false, L" 9_3");
        break;
      case D3D_FEATURE_LEVEL_10_0:
        dll_log.LogEx (false, L" 10_0");
        break;
      case D3D_FEATURE_LEVEL_10_1:
        dll_log.LogEx (false, L" 10_1");
        break;
      case D3D_FEATURE_LEVEL_11_0:
        dll_log.LogEx (false, L" 11_0");
        break;
      case D3D_FEATURE_LEVEL_11_1:
        dll_log.LogEx (false, L" 11_1");
        break;
        //case D3D_FEATURE_LEVEL_12_0:
        //dll_log.LogEx (false, L" 12_0");
        //break;
        //case D3D_FEATURE_LEVEL_12_1:
        //dll_log.LogEx (false, L" 12_1");
        //break;
      }
    }

    dll_log.LogEx (false, L"\n");

    //
    // DXGI Adapter Override (for performance)
    //
    
    IDXGIAdapter* pGameAdapter     = pAdapter;
    IDXGIAdapter* pOverrideAdapter = nullptr;

    if (pAdapter == nullptr) {
      IDXGIFactory* pFactory = nullptr;

      if (SUCCEEDED (CreateDXGIFactory (__uuidof (IDXGIFactory), (void **)&pFactory))) {
        if (pFactory != nullptr) {
          EnumAdapters_Original (pFactory, 0, &pGameAdapter);

          pAdapter   = pGameAdapter;
          DriverType = D3D_DRIVER_TYPE_UNKNOWN;

          if ( SK_DXGI_preferred_adapter != 0 &&
               SUCCEEDED (EnumAdapters_Original (pFactory, SK_DXGI_preferred_adapter, &pOverrideAdapter)) )
          {
            DXGI_ADAPTER_DESC game_desc;
            DXGI_ADAPTER_DESC override_desc;

            pGameAdapter->GetDesc     (&game_desc);
            pOverrideAdapter->GetDesc (&override_desc);

            if ( game_desc.VendorId     == Vendors::Intel     &&
                 override_desc.VendorId != Vendors::Microsoft &&
                 override_desc.VendorId != Vendors::Intel )
            {
              dll_log.Log ( L"[   DXGI   ] !!! DXGI Adapter Override: (Using '%s' instead of '%s') !!!",
                              override_desc.Description, game_desc.Description );

              pAdapter = pOverrideAdapter;
              pGameAdapter->Release ();
            } else {
              SK_DXGI_preferred_adapter = 0;
              pOverrideAdapter->Release ();
            }
          }

          pFactory->Release ();
        }
      }
    }

    if (pAdapter != nullptr) {
      int iver = SK_GetDXGIAdapterInterfaceVer (pAdapter);

      // IDXGIAdapter3 = DXGI 1.4 (Windows 10+)
      if (iver >= 3) {
        SK_StartDXGI_1_4_BudgetThread (&pAdapter);
      }
    }



    if (pSwapChainDesc != nullptr) {
      dll_log.LogEx ( true,
                        L"[   DXGI   ]  SwapChain: (%lux%lu@%lu Hz - Scaling: %s) - "
                        L"[%lu Buffers] :: Flags=0x%04X, SwapEffect: %s\n",
                          pSwapChainDesc->BufferDesc.Width,
                          pSwapChainDesc->BufferDesc.Height,
                          pSwapChainDesc->BufferDesc.RefreshRate.Numerator / 
                          pSwapChainDesc->BufferDesc.RefreshRate.Denominator,
                          pSwapChainDesc->BufferDesc.Scaling == 0 ?
                            L"Unspecified" :
                          pSwapChainDesc->BufferDesc.Scaling == 1 ?
                            L"Centered" : L"Stretched",
                          pSwapChainDesc->BufferCount,
                          pSwapChainDesc->Flags,
                          pSwapChainDesc->SwapEffect == 0 ?
                            L"Discard" :
                          pSwapChainDesc->SwapEffect == 1 ?
                            L"Sequential" :
                          pSwapChainDesc->SwapEffect == 2 ?
                            L"<Unknown>" :
                          pSwapChainDesc->SwapEffect == 3 ?
                            L"Flip Sequential" :
                          pSwapChainDesc->SwapEffect == 4 ?
                            L"Flip Discard" : L"<Unknown>");

      if ( pSwapChainDesc->BufferDesc.Width  != 256 ||
           pSwapChainDesc->BufferDesc.Height != 256 ) {
        hWndRender       = pSwapChainDesc->OutputWindow;
        game_window.hWnd = hWndRender;
      }
    }

    HRESULT res;
    DXGI_CALL(res, 
      D3D11CreateDeviceAndSwapChain_Import (pAdapter,
                                            DriverType,
                                            Software,
                                            Flags,
                                            pFeatureLevels,
                                            FeatureLevels,
                                            SDKVersion,
                                            pSwapChainDesc,
                                            ppSwapChain,
                                            ppDevice,
                                            pFeatureLevel,
                                            ppImmediateContext));

    if (res == S_OK && (ppDevice != NULL))
    {
      dll_log.Log (L"[  D3D 11  ] >> Device = 0x%08Xh", *ppDevice);
    }

    //
    // 256x256 Swapchains are created whenever RTSS has Allow Custom D3D enabled...
    //
    //  DO NOT hook those!
    //
    if (SUCCEEDED (res) && (pSwapChainDesc == nullptr || pSwapChainDesc->BufferDesc.Width  != 256)) {
    if (ppImmediateContext != nullptr) {
      DXGI_VIRTUAL_OVERRIDE (ppImmediateContext, 7, "ID3D11DeviceContext::VSSetConstantBuffers",
                             D3D11_VSSetConstantBuffers_Override, D3D11_VSSetConstantBuffers_Original,
                             D3D11_VSSetConstantBuffers_pfn);

      DXGI_VIRTUAL_OVERRIDE (ppImmediateContext, 14, "ID3D11DeviceContext::Map",
                             D3D11_Map_Override, D3D11_Map_Original,
                             D3D11_Map_pfn);

      DXGI_VIRTUAL_OVERRIDE (ppImmediateContext, 44, "ID3D11DeviceContext::RSSetViewports",
                             D3D11_RSSetViewports_Override, D3D11_RSSetViewports_Original,
                             D3D11_RSSetViewports_pfn);

#if 0
      DXGI_VIRTUAL_OVERRIDE (ppImmediateContext, 45, "ID3D11DeviceContext::RSSetScissorRects",
                             D3D11_RSSetScissorRects_Override, D3D11_RSSetScissorRects_Original,
                             D3D11_RSSetScissorRects_pfn);
#endif

      DXGI_VIRTUAL_OVERRIDE (ppImmediateContext, 47, "ID3D11DeviceContext::CopyResource",
                             D3D11_CopyResource_Override, D3D11_CopyResource_Original,
                             D3D11_CopyResource_pfn);

      DXGI_VIRTUAL_OVERRIDE (ppImmediateContext, 48, "ID3D11DeviceContext::UpdateSubresource",
                             D3D11_UpdateSubresource_Override, D3D11_UpdateSubresource_Original,
                             D3D11_UpdateSubresource_pfn);
    }

    if (ppDevice != nullptr) {
      DXGI_VIRTUAL_OVERRIDE (ppDevice, 5, "ID3D11Device::CreateTexture2D",
                             D3D11Dev_CreateTexture2D_Override, D3D11Dev_CreateTexture2D_Original,
                             D3D11Dev_CreateTexture2D_pfn);
    }

    if (pSwapChainDesc != nullptr) {
      CreateThread (nullptr, 0, SK_DXGI_BringRenderWindowToTop, nullptr, 0, nullptr);
    }
    }

    return res;
  }

  HRESULT
  STDMETHODCALLTYPE GetDesc2_Override (IDXGIAdapter2      *This,
                                _Out_  DXGI_ADAPTER_DESC2 *pDesc)
  {
    std::wstring iname = SK_GetDXGIAdapterInterface (This);

    DXGI_LOG_CALL_I2 (iname.c_str (), L"GetDesc2", L"%08Xh, %08Xh", This, pDesc);

    HRESULT ret;
    DXGI_CALL (ret, GetDesc2_Original (This, pDesc));

    //// OVERRIDE VRAM NUMBER
    if (nvapi_init && sk::NVAPI::CountSLIGPUs () > 0) {
      dll_log.LogEx ( true,
        L" <> GetDesc2_Override: Looking for matching NVAPI GPU for %s...: ",
        pDesc->Description );

      DXGI_ADAPTER_DESC* match =
        sk::NVAPI::FindGPUByDXGIName (pDesc->Description);

      if (match != NULL) {
        dll_log.LogEx (false, L"Success! (%s)\n", match->Description);
        pDesc->DedicatedVideoMemory = match->DedicatedVideoMemory;
      }
      else
        dll_log.LogEx (false, L"Failure! (No Match Found)\n");
    }

    return ret;
  }

  HRESULT
  STDMETHODCALLTYPE GetDesc1_Override (IDXGIAdapter1      *This,
                                _Out_  DXGI_ADAPTER_DESC1 *pDesc)
  {
    std::wstring iname = SK_GetDXGIAdapterInterface (This);

    DXGI_LOG_CALL_I2 (iname.c_str (), L"GetDesc1", L"%08Xh, %08Xh", This, pDesc);

    HRESULT ret;
    DXGI_CALL (ret, GetDesc1_Original (This, pDesc));

    //// OVERRIDE VRAM NUMBER
    if (nvapi_init && sk::NVAPI::CountSLIGPUs () > 0) {
      dll_log.LogEx ( true,
        L" <> GetDesc1_Override: Looking for matching NVAPI GPU for %s...: ",
        pDesc->Description );

      DXGI_ADAPTER_DESC* match =
        sk::NVAPI::FindGPUByDXGIName (pDesc->Description);

      if (match != NULL) {
        dll_log.LogEx (false, L"Success! (%s)\n", match->Description);
        pDesc->DedicatedVideoMemory = match->DedicatedVideoMemory;
      }
      else
        dll_log.LogEx (false, L"Failure! (No Match Found)\n");
    }

    return ret;
  }

  HRESULT
  STDMETHODCALLTYPE GetDesc_Override (IDXGIAdapter      *This,
                               _Out_  DXGI_ADAPTER_DESC *pDesc)
  {
    std::wstring iname = SK_GetDXGIAdapterInterface (This);

    DXGI_LOG_CALL_I2 (iname.c_str (), L"GetDesc",L"%08Xh, %08Xh", This, pDesc);

    HRESULT ret;
    DXGI_CALL (ret, GetDesc_Original (This, pDesc));

    //// OVERRIDE VRAM NUMBER
    if (nvapi_init && sk::NVAPI::CountSLIGPUs () > 0) {
      dll_log.LogEx ( true,
        L" <> GetDesc_Override: Looking for matching NVAPI GPU for %s...: ",
        pDesc->Description );

      DXGI_ADAPTER_DESC* match =
        sk::NVAPI::FindGPUByDXGIName (pDesc->Description);

      if (match != NULL) {
        dll_log.LogEx (false, L"Success! (%s)\n", match->Description);
        pDesc->DedicatedVideoMemory = match->DedicatedVideoMemory;
      }
      else
        dll_log.LogEx (false, L"Failure! (No Match Found)\n");
    }

    return ret;
  }

  HRESULT
  STDMETHODCALLTYPE EnumAdapters_Common (IDXGIFactory       *This,
                                         UINT                Adapter,
                                _Inout_  IDXGIAdapter      **ppAdapter,
                                         EnumAdapters_pfn    pFunc)
  {
    DXGI_ADAPTER_DESC desc;

    bool silent = dll_log.silent;
    dll_log.silent = true;
    {
      // Don't log this call
      (*ppAdapter)->GetDesc (&desc);
    }
    dll_log.silent = false;

    int iver = SK_GetDXGIAdapterInterfaceVer (*ppAdapter);

    // Only do this for NVIDIA SLI GPUs on Windows 10 (DXGI 1.4)
    if (false) {//nvapi_init && sk::NVAPI::CountSLIGPUs () > 0 && iver >= 3) {
      if (! GetDesc_Original) {
        DXGI_VIRTUAL_OVERRIDE (ppAdapter, 8, "(*ppAdapter)->GetDesc",
          GetDesc_Override, GetDesc_Original, GetDesc_pfn);
      }

      if (! GetDesc1_Original) {
        CComPtr <IDXGIAdapter1> pAdapter1;

       if (SUCCEEDED ((*ppAdapter)->QueryInterface (IID_PPV_ARGS (&pAdapter1)))) {
          DXGI_VIRTUAL_OVERRIDE (&pAdapter1, 10, "(pAdapter1)->GetDesc1",
            GetDesc1_Override, GetDesc1_Original, GetDesc1_pfn);
       }
      }

      if (! GetDesc2_Original) {
        CComPtr <IDXGIAdapter2> pAdapter2;
        if (SUCCEEDED ((*ppAdapter)->QueryInterface (IID_PPV_ARGS (&pAdapter2)))) {

          DXGI_VIRTUAL_OVERRIDE (ppAdapter, 11, "(*pAdapter2)->GetDesc2",
            GetDesc2_Override, GetDesc2_Original, GetDesc2_pfn);
        }
      }
    }

    // Logic to skip Intel and Microsoft adapters and return only AMD / NV
    //if (lstrlenW (pDesc->Description)) {
    if (true) {
      if (! lstrlenW (desc.Description))
        dll_log.LogEx (false, L" >> Assertion filed: Zero-length adapter name!\n");

#ifdef SKIP_INTEL
      if ((desc.VendorId == Microsoft || desc.VendorId == Intel) && Adapter == 0) {
#else
      if (false) {
#endif
        // We need to release the reference we were just handed before
        //   skipping it.
        (*ppAdapter)->Release ();

        dll_log.LogEx (false,
          L"[   DXGI   ] >> (Host Application Tried To Enum Intel or Microsoft Adapter "
          L"as Adapter 0) -- Skipping Adapter '%s' <<\n", desc.Description);

        return (pFunc (This, Adapter + 1, ppAdapter));
      }
      else {
        // Only do this for NVIDIA SLI GPUs on Windows 10 (DXGI 1.4)
        if (false) { //nvapi_init && sk::NVAPI::CountSLIGPUs () > 0 && iver >= 3) {
          DXGI_ADAPTER_DESC* match =
            sk::NVAPI::FindGPUByDXGIName (desc.Description);

          if (match != NULL &&
            desc.DedicatedVideoMemory > match->DedicatedVideoMemory) {
// This creates problems in 32-bit environments...
#ifdef _WIN64
            if (sk::NVAPI::app_name != L"Fallout4.exe") {
              dll_log.Log (
                L"   # SLI Detected (Corrected Memory Total: %llu MiB -- "
                L"Original: %llu MiB)",
                match->DedicatedVideoMemory >> 20ULL,
                desc.DedicatedVideoMemory   >> 20ULL);
            } else {
              match->DedicatedVideoMemory = desc.DedicatedVideoMemory;
            }
#endif
          }
        }
      }

      dll_log.LogEx(true,L"[   DXGI   ]   @ Returning Adapter %lu: '%s' (LUID: %08X:%08X)",
        Adapter,
          desc.Description,
            desc.AdapterLuid.HighPart,
              desc.AdapterLuid.LowPart );

      //
      // Windows 8 has a software implementation, which we can detect.
      //
      CComPtr <IDXGIAdapter1> pAdapter1;
      HRESULT hr =
        (*ppAdapter)->QueryInterface (IID_PPV_ARGS (&pAdapter1));

      if (SUCCEEDED (hr)) {
        bool silence = dll_log.silent;
        dll_log.silent = true; // Temporarily disable logging

        DXGI_ADAPTER_DESC1 desc1;

        if (SUCCEEDED (pAdapter1->GetDesc1 (&desc1))) {
          dll_log.silent = silence; // Restore logging
#define DXGI_ADAPTER_FLAG_REMOTE   0x1
#define DXGI_ADAPTER_FLAG_SOFTWARE 0x2
          if (desc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            dll_log.LogEx (false, L" <Software>");
          else
            dll_log.LogEx (false, L" <Hardware>");
          if (desc1.Flags & DXGI_ADAPTER_FLAG_REMOTE)
            dll_log.LogEx (false, L" [Remote]");
        }

        dll_log.silent = silence; // Restore logging
      }

      dll_log.LogEx (false, L"\n");
    }

    return S_OK;
  }

  HRESULT
  STDMETHODCALLTYPE EnumAdapters1_Override (IDXGIFactory1  *This,
                                            UINT            Adapter,
                                     _Out_  IDXGIAdapter1 **ppAdapter)
  {
    std::wstring iname = SK_GetDXGIFactoryInterface    (This);

    DXGI_LOG_CALL_I3 (iname.c_str (), L"EnumAdapters1", L"%08Xh, %u, %08Xh",
      This, Adapter, ppAdapter);

#if 0
    if (Adapter == 0 && SK_DXGI_preferred_adapter != 0) {
       IDXGIAdapter1* pAdapter1 = nullptr;
      if (SUCCEEDED (EnumAdapters1_Original (This, SK_DXGI_preferred_adapter, &pAdapter1))) {
        dll_log.Log ( L" (Reported values reflect user override: DXGI Adapter %lu -> %lu)",
                        0, SK_DXGI_preferred_adapter );
        Adapter = SK_DXGI_preferred_adapter;

        if (pAdapter1 != nullptr)
          pAdapter1->Release ();
      }
    }
#endif

    HRESULT ret;
    DXGI_CALL (ret, EnumAdapters1_Original (This,Adapter,ppAdapter));

    if (SUCCEEDED (ret) && ppAdapter != nullptr && (*ppAdapter) != nullptr) {
      return EnumAdapters_Common (This, Adapter, (IDXGIAdapter **)ppAdapter,
                                  (EnumAdapters_pfn)EnumAdapters1_Override);
    }

    return ret;
  }

  HRESULT
  STDMETHODCALLTYPE EnumAdapters_Override (IDXGIFactory  *This,
                                           UINT           Adapter,
                                    _Out_  IDXGIAdapter **ppAdapter)
  {
    std::wstring iname = SK_GetDXGIFactoryInterface    (This);

    DXGI_LOG_CALL_I3 (iname.c_str (), L"EnumAdapters", L"%08Xh, %u, %08Xh",
      This, Adapter, ppAdapter);

#if 0
    if (Adapter == 0 && SK_DXGI_preferred_adapter != 0) {
       IDXGIAdapter* pAdapter = nullptr;
      if (SUCCEEDED (EnumAdapters_Original (This, SK_DXGI_preferred_adapter, &pAdapter))) {
        dll_log.Log ( L" (Reported values reflect user override: DXGI Adapter %lu -> %lu)",
                        0, SK_DXGI_preferred_adapter );
        Adapter = SK_DXGI_preferred_adapter;

        if (pAdapter != nullptr)
          pAdapter->Release ();
      }
    }
#endif

    HRESULT ret;
    DXGI_CALL (ret, EnumAdapters_Original (This, Adapter, ppAdapter));

    if (SUCCEEDED (ret) && ppAdapter != nullptr && (*ppAdapter) != nullptr) {
      return EnumAdapters_Common (This, Adapter, ppAdapter,
                                  (EnumAdapters_pfn)EnumAdapters_Override);
    }

    return ret;
  }

  __declspec (nothrow)
    HRESULT
    STDMETHODCALLTYPE CreateDXGIFactory (REFIID   riid,
                                   _Out_ void   **ppFactory)
  {
    WaitForInit ();

    std::wstring iname = SK_GetDXGIFactoryInterfaceEx  (riid);
    int          iver  = SK_GetDXGIFactoryInterfaceVer (riid);

    DXGI_LOG_CALL_2 (L"CreateDXGIFactory", L"%s, %08Xh",
      iname.c_str (), ppFactory);

    HRESULT ret;
    DXGI_CALL (ret, CreateDXGIFactory_Import (riid, ppFactory));

    DXGI_VIRTUAL_OVERRIDE ( ppFactory, 7, "IDXGIFactory::EnumAdapters",
                              EnumAdapters_Override,
                              EnumAdapters_Original,
                                EnumAdapters_pfn );

    DXGI_VIRTUAL_OVERRIDE ( ppFactory, 10, "IDXGIFactory::CreateSwapChain",
                              DXGIFactory_CreateSwapChain_Override,
                                          CreateSwapChain_Original,
                                            CreateSwapChain_pfn );

    // DXGI 1.1+
    if (iver > 0) {
      DXGI_VIRTUAL_OVERRIDE ( ppFactory, 12, "IDXGIFactory::EnumAdapters1",
                                EnumAdapters1_Override,
                                EnumAdapters1_Original,
                                  EnumAdapters1_pfn );
    }

    return ret;
  }

  __declspec (nothrow)
    HRESULT
    STDMETHODCALLTYPE CreateDXGIFactory1 (REFIID   riid,
                                    _Out_ void   **ppFactory)
  {
    WaitForInit ();

    std::wstring iname = SK_GetDXGIFactoryInterfaceEx  (riid);
    int          iver  = SK_GetDXGIFactoryInterfaceVer (riid);

    DXGI_LOG_CALL_2 (L"CreateDXGIFactory1", L"%s, %08Xh",
      iname.c_str (), ppFactory);

    // Windows Vista does not have this function -- wrap it with CreateDXGIFactory
    if (CreateDXGIFactory1_Import == nullptr) {
      dll_log.Log (L"[   DXGI   ]  >> Falling back to CreateDXGIFactory on Vista...");
      return CreateDXGIFactory (riid, ppFactory);
    }

    HRESULT ret;
    DXGI_CALL (ret, CreateDXGIFactory1_Import (riid, ppFactory));

    DXGI_VIRTUAL_OVERRIDE ( ppFactory, 7,  "IDXGIFactory1::EnumAdapters",
                              EnumAdapters_Override,  EnumAdapters_Original,
                              EnumAdapters_pfn );

    DXGI_VIRTUAL_OVERRIDE ( ppFactory, 10, "IDXGIFactory1::CreateSwapChain",
                              DXGIFactory_CreateSwapChain_Override,
                                          CreateSwapChain_Original,
                                            CreateSwapChain_pfn );

    // DXGI 1.1+
    if (iver > 0) {
      DXGI_VIRTUAL_OVERRIDE ( ppFactory, 12, "IDXGIFactory1::EnumAdapters1",
                                EnumAdapters1_Override, EnumAdapters1_Original,
                                EnumAdapters1_pfn );
    }

    return ret;
  }

  __declspec (nothrow)
    HRESULT
    STDMETHODCALLTYPE CreateDXGIFactory2 (UINT     Flags,
                                          REFIID   riid,
                                    _Out_ void   **ppFactory)
  {
    WaitForInit ();

    std::wstring iname = SK_GetDXGIFactoryInterfaceEx  (riid);
    int          iver  = SK_GetDXGIFactoryInterfaceVer (riid);

    DXGI_LOG_CALL_3 (L"CreateDXGIFactory2", L"0x%04X, %s, %08Xh",
      Flags, iname.c_str (), ppFactory);

    // Windows 7 does not have this function -- wrap it with CreateDXGIFactory1
    if (CreateDXGIFactory2_Import == nullptr) {
      dll_log.Log (L"[   DXGI   ]  >> Falling back to CreateDXGIFactory1 on Vista/7...");
      return CreateDXGIFactory1 (riid, ppFactory);
    }

    HRESULT ret;
    DXGI_CALL (ret, CreateDXGIFactory2_Import (Flags, riid, ppFactory));

    DXGI_VIRTUAL_OVERRIDE ( ppFactory, 7, "IDXGIFactory2::EnumAdapters",
                              EnumAdapters_Override, EnumAdapters_Original,
                              EnumAdapters_pfn );

    DXGI_VIRTUAL_OVERRIDE ( ppFactory, 10, "IDXGIFactory2::CreateSwapChain",
                              DXGIFactory_CreateSwapChain_Override,
                                          CreateSwapChain_Original,
                                            CreateSwapChain_pfn );

    // DXGI 1.1+
    if (iver > 0) {
      DXGI_VIRTUAL_OVERRIDE ( ppFactory, 12, "IDXGIFactory2::EnumAdapters1",
                                EnumAdapters1_Override, EnumAdapters1_Original,
                                EnumAdapters1_pfn );
    }

    return ret;
  }

  DXGI_STUB (HRESULT, DXGID3D10CreateDevice,
    (HMODULE hModule, IDXGIFactory *pFactory, IDXGIAdapter *pAdapter,
      UINT    Flags,   void         *unknown,  void         *ppDevice),
    (hModule, pFactory, pAdapter, Flags, unknown, ppDevice));

  struct UNKNOWN5 {
    DWORD unknown [5];
  };

  DXGI_STUB (HRESULT, DXGID3D10CreateLayeredDevice,
    (UNKNOWN5 Unknown),
    (Unknown))

  DXGI_STUB (SIZE_T, DXGID3D10GetLayeredDeviceSize,
    (const void *pLayers, UINT NumLayers),
    (pLayers, NumLayers))

  DXGI_STUB (HRESULT, DXGID3D10RegisterLayers,
    (const void *pLayers, UINT NumLayers),
    (pLayers, NumLayers))

  HRESULT
  STDMETHODCALLTYPE DXGIDumpJournal (void)
  {
    DXGI_LOG_CALL_0 (L"DXGIDumpJournal");

    return E_NOTIMPL;
  }

  HRESULT
  STDMETHODCALLTYPE DXGIReportAdapterConfiguration (void)
  {
    DXGI_LOG_CALL_0 (L"DXGIReportAdapterConfiguration");

    return E_NOTIMPL;
  }
}


LPVOID pfnD3D11CreateDeviceAndSwapChain = nullptr;
LPVOID pfnChangeDisplaySettingsA        = nullptr;
LPVOID pfnGetClientRect                 = nullptr;

typedef LONG (WINAPI *ChangeDisplaySettingsA_pfn)(
  _In_ DEVMODEA *lpDevMode,
  _In_ DWORD    dwFlags
);
ChangeDisplaySettingsA_pfn ChangeDisplaySettingsA_Original = nullptr;

typedef BOOL (WINAPI *GetClientRect_pfn)(
  _In_ HWND hWnd,
  _Out_ LPRECT lpRect
);
GetClientRect_pfn GetClientRect_Original = nullptr;

BOOL
WINAPI
GetClientRect_Detour (
  _In_ HWND hWnd,
  _Out_ LPRECT lpRect
)
{
  lpRect->bottom = 2160;
  lpRect->top    = 0;

  lpRect->left  = 0;
  lpRect->right = 3840;

  return TRUE;
}

LONG
WINAPI
ChangeDisplaySettingsA_Detour (
  _In_ DEVMODEA *lpDevMode,
  _In_ DWORD     dwFlags )
{
  static bool called = false;

  DEVMODEW dev_mode;
  dev_mode.dmSize = sizeof (DEVMODEW);

  EnumDisplaySettings (nullptr, 0, &dev_mode);

  if (dwFlags != CDS_TEST) {
    if (called)
      ChangeDisplaySettingsA_Original (0, CDS_RESET);

    called = true;

    return ChangeDisplaySettingsA_Original (lpDevMode, CDS_FULLSCREEN);
  } else {
    return ChangeDisplaySettingsA_Original (lpDevMode, dwFlags);
  }
}

typedef void (WINAPI *finish_pfn)(void);

void
WINAPI
dxgi_init_callback (finish_pfn finish)
{
  HMODULE hBackend = backend_dll;

  dll_log.Log (L"[   DXGI   ] Importing CreateDXGIFactory{1|2}");
  dll_log.Log (L"[   DXGI   ] ================================");

  dll_log.Log (L"[ DXGI 1.0 ]   CreateDXGIFactory:  %08Xh", 
    (CreateDXGIFactory_Import =  \
      (CreateDXGIFactory_pfn)GetProcAddress (hBackend, "CreateDXGIFactory")));
  dll_log.Log (L"[ DXGI 1.1 ]   CreateDXGIFactory1: %08Xh",
    (CreateDXGIFactory1_Import = \
      (CreateDXGIFactory1_pfn)GetProcAddress (hBackend, "CreateDXGIFactory1")));
  dll_log.Log (L"[ DXGI 1.3 ]   CreateDXGIFactory2: %08Xh",
    (CreateDXGIFactory2_Import = \
      (CreateDXGIFactory2_pfn)GetProcAddress (hBackend, "CreateDXGIFactory2")));

  SK_CreateDLLHook ( L"d3d11.dll", "D3D11CreateDeviceAndSwapChain",
                       D3D11CreateDeviceAndSwapChain_Detour,
            (LPVOID *)&D3D11CreateDeviceAndSwapChain_Import,
                      &pfnD3D11CreateDeviceAndSwapChain );

  SK_EnableHook (pfnD3D11CreateDeviceAndSwapChain);


  SK_CommandProcessor* pCommandProc =
    SK_GetCommandProcessor ();

  pCommandProc->AddVariable ( "PresentationInterval",
          new SK_VarStub <int> (&config.render.framerate.present_interval));
  pCommandProc->AddVariable ( "PreRenderLimit",
          new SK_VarStub <int> (&config.render.framerate.pre_render_limit));
  pCommandProc->AddVariable ( "BufferCount",
          new SK_VarStub <int> (&config.render.framerate.buffer_count));
  pCommandProc->AddVariable ( "UseFlipDiscard",
          new SK_VarStub <bool> (&config.render.framerate.flip_discard));
  pCommandProc->AddVariable ( "FudgeFactor",
          new SK_VarStub <float> (&config.render.framerate.fudge_factor));

  finish ();

}

HMODULE
SK_LoadRealDXGI (void)
{
  wchar_t wszBackendDLL [MAX_PATH] = { L'\0' };

#ifdef _WIN64
  GetSystemDirectory (wszBackendDLL, MAX_PATH);
#else
  BOOL bWOW64;
  ::IsWow64Process (GetCurrentProcess (), &bWOW64);

  if (bWOW64)
    GetSystemWow64Directory (wszBackendDLL, MAX_PATH);
  else
    GetSystemDirectory (wszBackendDLL, MAX_PATH);
#endif

  lstrcatW (wszBackendDLL, L"\\dxgi.dll");

  return LoadLibraryW (wszBackendDLL);
}

bool
SK::DXGI::Startup (void)
{
  SK_LoadRealDXGI ();

  return SK_StartupCore (L"dxgi", dxgi_init_callback);
}

extern "C" bool WINAPI SK_DS3_ShutdownPlugin (const wchar_t* backend);






#include <unordered_set>

std::unordered_map <uint32_t, std::wstring> tex_hashes;

std::unordered_set <uint32_t>               dumped_textures;
std::unordered_set <uint32_t>               injectable_textures;

struct SK_D3D11_TexMgr {
  SK_D3D11_TexMgr (void) {
    QueryPerformanceFrequency (&PerfFreq);
  }

  bool             isTexture2D  (uint32_t crc32);

  ID3D11Texture2D* getTexture2D ( uint32_t              crc32,
                            const D3D11_TEXTURE2D_DESC *pDesc,
                                  size_t               *pMemSize   = nullptr,
                                  float                *pTimeSaved = nullptr );

  void             refTexture2D ( ID3D11Texture2D      *pTex,
                            const D3D11_TEXTURE2D_DESC *pDesc,
                                  uint32_t              crc32,
                                  size_t                mem_size,
                                  uint64_t              load_time );

  bool             purgeTextures (size_t size_to_free, int* pCount, size_t* pFreed);

  struct tex2D_descriptor_s {
    ID3D11Texture2D      *texture    = nullptr;
    D3D11_TEXTURE2D_DESC  desc       = { 0 };
    size_t                mem_size   = 0L;
    uint64_t              load_time  = 0ULL;
    uint32_t              crc32      = 0x00;
    uint32_t              hits       = 0;
  };

  std::unordered_set      <ID3D11Texture2D *>      TexRefs_2D;

  std::unordered_map < uint32_t,
                            ID3D11Texture2D *  >   HashMap_2D;
  std::unordered_map      < ID3D11Texture2D *,
                            tex2D_descriptor_s* >  Textures_2D;

  uint64_t                                         RedundantData_2D;
  uint32_t                                         RedundantLoads_2D;
  float                                            RedundantTime_2D;
  LARGE_INTEGER                                    PerfFreq;
} SK_D3D11_Textures;






bool
SK::DXGI::Shutdown (void)
{
  ////SK_RemoveHook (pfnD3D11CreateDeviceAndSwapChain);

  if (sk::NVAPI::app_name == L"DarkSoulsIII.exe") {
    SK_DS3_ShutdownPlugin (L"dxgi");
  }

  if (SK_D3D11_Textures.RedundantLoads_2D > 0) {
    dll_log.Log ( L"[Perf Stats] At shutdown: %7.2f seconds and %7.2f MiB of"
                  L" CPU->GPU I/O avoided by %lu texture cache hits.",
                    SK_D3D11_Textures.RedundantTime_2D / 1000.0f,
                      (float)SK_D3D11_Textures.RedundantData_2D /
                                 (1024.0f * 1024.0f),
                        SK_D3D11_Textures.RedundantLoads_2D );

    std::unordered_map      < ID3D11Texture2D *,
                            SK_D3D11_TexMgr::tex2D_descriptor_s* >::iterator it
      = SK_D3D11_Textures.Textures_2D.begin ();

#if 0
    while (it != SK_D3D11_Textures.Textures_2D.end ()) {
      if (it->second->hits > 0) {
        dll_log.Log ( L"[DX11TexMgr] Texture '%8X' (%7.2f KiB) had %lu cache hits"
                     L" (saved %7.2f seconds)",
                        it->second->crc32,
                   (float)it->second->mem_size / 1024.0f,
                            it->second->hits,
                              ((float)it->second->hits *
                               (float)it->second->load_time) / 1000.0f );
      }
      ++it;
    }
#endif
  }

  return SK_ShutdownCore (L"dxgi");
}



bool         SK_D3D11_dump_textures   = false;// true;
bool         SK_D3D11_inject_textures = false;
bool         SK_D3D11_cache_textures  = false;
bool         SK_D3D11_mark_textures   = false;
std::wstring SK_D3D11_res_root        = L"";

// From core.cpp
extern std::wstring host_app;

bool
SK_D3D11_TexMgr::isTexture2D (uint32_t crc32)
{
  if (! SK_D3D11_cache_textures)
    return false;

  if (crc32 != 0x00 && HashMap_2D.count (crc32))
    return true;

  return false;
}

ID3D11Texture2D*
SK_D3D11_TexMgr::getTexture2D (uint32_t crc32, const D3D11_TEXTURE2D_DESC* pDesc, size_t* pMemSize, float* pTimeSaved)
{
  if (! SK_D3D11_cache_textures)
    return nullptr;

  ID3D11Texture2D* pTex2D = nullptr;

  if (isTexture2D (crc32)) {
    std::unordered_map <uint32_t, ID3D11Texture2D *>::iterator it =
      HashMap_2D.begin ();

    while (it != HashMap_2D.end ()) {
      if (! SK_D3D11_TextureIsCached (it->second)) {
        ++it;
        continue;
      }

      tex2D_descriptor_s* desc2d =
        Textures_2D [it->second];

      D3D11_TEXTURE2D_DESC desc = desc2d->desc;

      if ( desc2d->crc32       == crc32                 &&
           desc.Format         == pDesc->Format         &&
           desc.Width          == pDesc->Width          &&
           desc.Height         == pDesc->Height         &&
           desc.MipLevels      >= pDesc->MipLevels      &&
           desc.BindFlags      == pDesc->BindFlags      &&
           desc.CPUAccessFlags == pDesc->CPUAccessFlags &&
           desc.Usage          == pDesc->Usage ) {
        pTex2D = desc2d->texture;

        size_t   size = desc2d->mem_size;
        uint64_t time = desc2d->load_time;

        float   fTime = (float)time * 1000.0f / (float)PerfFreq.QuadPart;

        if (pMemSize != nullptr) {
          *pMemSize = size;
        }

        if (pTimeSaved != nullptr) {
          *pTimeSaved = fTime;
        }

        desc2d->hits++;

        RedundantData_2D += size;
        RedundantTime_2D += fTime;
        RedundantLoads_2D++;

        return pTex2D;
      }

      else if (desc2d->crc32 == crc32) {
        dll_log.Log ( L"[DX11TexMgr] ## Hash Collision for Texture: '%08X'!! ## ",
                      crc32 );
      }

      ++it;
    }
  }

  return pTex2D;
}

bool
SK_D3D11_TextureIsCached (ID3D11Texture2D* pTex)
{
  if (! SK_D3D11_cache_textures)
    return false;

  if (SK_D3D11_Textures.Textures_2D.count (pTex))
    return true;

  return false;
}

void
SK_D3D11_RemoveTexFromCache (ID3D11Texture2D* pTex)
{
  if (! SK_D3D11_TextureIsCached (pTex))
    return;

  uint32_t crc32 = SK_D3D11_Textures.Textures_2D [pTex]->crc32;

  SK_D3D11_Textures.Textures_2D.erase (pTex);
  SK_D3D11_Textures.HashMap_2D.erase  (crc32);
  SK_D3D11_Textures.TexRefs_2D.erase  (pTex);

  pTex->Release ();
}

void
SK_D3D11_TexMgr::refTexture2D ( ID3D11Texture2D*      pTex, 
                          const D3D11_TEXTURE2D_DESC *pDesc,
                                uint32_t              crc32,
                                size_t                mem_size,
                                uint64_t              load_time )
{
  if (! SK_D3D11_cache_textures)
    return;

  if (pTex == nullptr || crc32 == 0x00)
    return;

  //if (! injectable_textures.count (crc32))
    //return;

  if (SK_D3D11_TextureIsCached (pTex)) {
    dll_log.Log (L"[DX11TexMgr] Texture is already cached?!");
  }

  if (pDesc->Usage != D3D11_USAGE_DEFAULT &&
      pDesc->Usage != D3D11_USAGE_IMMUTABLE) {
    dll_log.Log ( L"[DX11TexMgr] Texture '%08X' Is Not Cacheable Due To Usage: %lu",
                  crc32, pDesc->Usage );
    return;
  }

  if (pDesc->CPUAccessFlags != 0x00) {
    dll_log.Log ( L"[DX11TexMgr] Texture '%08X' Is Not Cacheable Due To CPUAccessFlags: 0x%X",
                  crc32, pDesc->CPUAccessFlags );
    return;
  }

  // This leaks, but I do not care.
  tex2D_descriptor_s* desc2d = new tex2D_descriptor_s;

  desc2d->desc      = *pDesc;
  desc2d->texture   =  pTex;
  desc2d->load_time =  load_time;
  desc2d->mem_size  =  mem_size;
  desc2d->crc32     =  crc32;

  TexRefs_2D.insert  (pTex);
  HashMap_2D.insert  (std::pair <uint32_t,          ID3D11Texture2D *>  (crc32, pTex));
  Textures_2D.insert (std::pair <ID3D11Texture2D *, tex2D_descriptor_s*> (pTex, desc2d));

  // Hold a reference ourselves so that the game cannot free it
  pTex->AddRef ();
}

#include <Shlwapi.h>

void
WINAPI
SK_D3D11_PopulateResourceList (void)
{
  static bool init = false;

  if (init || SK_D3D11_res_root.empty ())
    return;

  init = true;

  wchar_t wszTexDumpDir [MAX_PATH] = { L'\0' };


  lstrcatW (wszTexDumpDir, SK_D3D11_res_root.c_str ());
  lstrcatW (wszTexDumpDir, L"\\dump\\textures\\");
  lstrcatW (wszTexDumpDir, host_app.c_str ());

  //
  // Walk custom textures so we don't have to query the filesystem on every
  //   texture load to check if a custom one exists.
  //
  if ( GetFileAttributesW (wszTexDumpDir) !=
         INVALID_FILE_ATTRIBUTES ) {
    WIN32_FIND_DATA fd;
    HANDLE          hFind  = INVALID_HANDLE_VALUE;
    int             files  = 0;
    LARGE_INTEGER   liSize = { 0 };

    LARGE_INTEGER   liCompressed   = { 0 };
    LARGE_INTEGER   liUncompressed = { 0 };

    dll_log.LogEx ( true, L"[DX11TexMgr] Enumerating dumped...    " );

    lstrcatW (wszTexDumpDir, L"\\*");

    hFind = FindFirstFileW (wszTexDumpDir, &fd);

    if (hFind != INVALID_HANDLE_VALUE) {
      do {
        if (fd.dwFileAttributes != INVALID_FILE_ATTRIBUTES) {
          if (StrStrIW (fd.cFileName, L".dds")) {
            uint32_t checksum;

            bool compressed = false;

            if (StrStrIW (fd.cFileName, L"Uncompressed_"))
              swscanf (fd.cFileName, L"Uncompressed_%x.dds", &checksum);
            else {
              swscanf (fd.cFileName, L"Compressed_%x.dds", &checksum);
              compressed = true;
            }

            ++files;

            LARGE_INTEGER fsize;

            fsize.HighPart = fd.nFileSizeHigh;
            fsize.LowPart  = fd.nFileSizeLow;

            liSize.QuadPart += fsize.QuadPart;

            if (compressed)
              liCompressed.QuadPart += fsize.QuadPart;
            else
              liUncompressed.QuadPart += fsize.QuadPart;

            dumped_textures.insert (checksum);
          }
        }
      } while (FindNextFileW (hFind, &fd) != 0);

      FindClose (hFind);
    }

    dll_log.LogEx ( false, L" %lu files (%3.1f MiB -- %3.1f:%3.1f MiB Un:Compressed)\n",
                      files, (double)liSize.QuadPart / (1024.0 * 1024.0),
                               (double)liUncompressed.QuadPart / (1024.0 * 1024.0),
                                 (double)liCompressed.QuadPart /  (1024.0 * 1024.0) );
  }

  wchar_t wszTexInjectDir [MAX_PATH] = { L'\0' };

  lstrcatW (wszTexInjectDir, SK_D3D11_res_root.c_str ());
  lstrcatW (wszTexInjectDir, L"\\inject\\textures");

  if ( GetFileAttributesW (wszTexInjectDir) !=
         INVALID_FILE_ATTRIBUTES ) {
    WIN32_FIND_DATA fd;
    HANDLE          hFind  = INVALID_HANDLE_VALUE;
    int             files  = 0;
    LARGE_INTEGER   liSize = { 0 };

    dll_log.LogEx ( true, L"[DX11TexMgr] Enumerating injectable..." );

    lstrcatW (wszTexInjectDir, L"\\*");

    hFind = FindFirstFileW (wszTexInjectDir, &fd);

    if (hFind != INVALID_HANDLE_VALUE) {
      do {
        if (fd.dwFileAttributes != INVALID_FILE_ATTRIBUTES) {
          if (StrStrIW (fd.cFileName, L".dds")) {
            uint32_t checksum;

            swscanf (fd.cFileName, L"%x.dds", &checksum);
            ++files;

            LARGE_INTEGER fsize;

            fsize.HighPart = fd.nFileSizeHigh;
            fsize.LowPart  = fd.nFileSizeLow;

            liSize.QuadPart += fsize.QuadPart;

            injectable_textures.insert (checksum);
          }
        }
      } while (FindNextFileW (hFind, &fd) != 0);

      FindClose (hFind);
    }

    dll_log.LogEx ( false, L" %lu files (%3.1f MiB)\n",
                      files, (double)liSize.QuadPart / (1024.0 * 1024.0) );
  }
}

void
WINAPI
SK_D3D11_SetResourceRoot (std::wstring root)
{
  SK_D3D11_res_root = root;
}

void
WINAPI
SK_D3D11_EnableTexDump (bool enable)
{
  SK_D3D11_dump_textures = enable;
}

void
WINAPI
SK_D3D11_EnableTexInject (bool enable)
{
  SK_D3D11_inject_textures = enable;
}

void
WINAPI
SK_D3D11_EnableTexCache (bool enable)
{
  SK_D3D11_cache_textures = enable;
}

void
WINAPI
SKX_D3D11_MarkTextures (bool x, bool y, bool z)
{
  return;
}


void
WINAPI
SK_D3D11_AddTexHash ( std::wstring name, uint32_t hash )
{
  if (! tex_hashes.count (hash))
    tex_hashes.insert (std::make_pair (hash, name));
  //tex_hashes [hash] = name;
}

void
WINAPI
SK_D3D11_RemoveTexHash (uint32_t hash)
{
  if (tex_hashes.count (hash))
    tex_hashes.erase (hash);
}


static uint32_t crc32_tab[] = { 
   0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 
   0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 
   0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2, 
   0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7, 
   0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 
   0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 
   0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c, 
   0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59, 
   0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 
   0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 
   0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106, 
   0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433, 
   0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 
   0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 
   0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950, 
   0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65, 
   0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 
   0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 
   0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa, 
   0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f, 
   0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 
   0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 
   0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84, 
   0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1, 
   0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 
   0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 
   0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e, 
   0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b, 
   0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 
   0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 
   0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28, 
   0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d, 
   0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f, 
   0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 
   0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242, 
   0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777, 
   0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 
   0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 
   0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc, 
   0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9, 
   0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 
   0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 
   0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d 
 };

__declspec (noinline)
uint32_t
__cdecl
crc32 (uint32_t crc, const void *buf, size_t size)
{
  const uint8_t *p;

  p = (uint8_t *)buf;
  crc = crc ^ ~0U;

  while (size--)
    crc = crc32_tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);

  return crc ^ ~0U;
}

typedef enum D3DX11_IMAGE_FILE_FORMAT { 
  D3DX11_IFF_BMP          = 0,
  D3DX11_IFF_JPG          = 1,
  D3DX11_IFF_PNG          = 3,
  D3DX11_IFF_DDS          = 4,
  D3DX11_IFF_TIFF         = 10,
  D3DX11_IFF_GIF          = 11,
  D3DX11_IFF_WMP          = 12,
  D3DX11_IFF_FORCE_DWORD  = 0x7fffffff
} D3DX11_IMAGE_FILE_FORMAT, *LPD3DX11_IMAGE_FILE_FORMAT;

typedef struct D3DX11_IMAGE_INFO {
  UINT                     Width;
  UINT                     Height;
  UINT                     Depth;
  UINT                     ArraySize;
  UINT                     MipLevels;
  UINT                     MiscFlags;
  DXGI_FORMAT              Format;
  D3D11_RESOURCE_DIMENSION ResourceDimension;
  D3DX11_IMAGE_FILE_FORMAT ImageFileFormat;
} D3DX11_IMAGE_INFO, *LPD3DX11_IMAGE_INFO;


typedef struct D3DX11_IMAGE_LOAD_INFO {
  UINT              Width;
  UINT              Height;
  UINT              Depth;
  UINT              FirstMipLevel;
  UINT              MipLevels;
  D3D11_USAGE       Usage;
  UINT              BindFlags;
  UINT              CpuAccessFlags;
  UINT              MiscFlags;
  DXGI_FORMAT       Format;
  UINT              Filter;
  UINT              MipFilter;
  D3DX11_IMAGE_INFO *pSrcInfo;
} D3DX11_IMAGE_LOAD_INFO, *LPD3DX11_IMAGE_LOAD_INFO;

typedef HRESULT (WINAPI *D3DX11SaveTextureToFileW_pfn)(
       ID3D11DeviceContext      *pContext,
  _In_ ID3D11Resource           *pSrcTexture,
  _In_ D3DX11_IMAGE_FILE_FORMAT DestFormat,
  _In_ LPCWSTR                  pDestFile
);

typedef HRESULT (WINAPI *D3DX11CreateTextureFromFileW_pfn)(
  _In_  ID3D11Device           *pDevice,
  _In_  LPCWSTR                pSrcFile,
  _In_  D3DX11_IMAGE_LOAD_INFO *pLoadInfo,
  _In_  IUnknown               *pPump,
  _Out_ ID3D11Resource         **ppTexture,
  _Out_ HRESULT                *pHResult
);


D3DX11CreateTextureFromFileW_pfn D3DX11CreateTextureFromFileW = nullptr;
D3DX11SaveTextureToFileW_pfn     D3DX11SaveTextureToFileW     = nullptr;
HMODULE                          hModD3DX11_43                = nullptr;

HRESULT
__stdcall
SK_D3D11_DumpTexture2D ( ID3D11DeviceContext    *pCtx,
                         ID3D11Resource         *pSrcTexture,
                         uint32_t                checksum,
         _In_      const D3D11_TEXTURE2D_DESC   *pDesc,
         _In_opt_  const D3D11_SUBRESOURCE_DATA *pInitialData )
{
  if (! SK_D3D11_dump_textures || SK_D3D11_res_root.empty ())
    return E_FAIL;

  if (pDesc->MipLevels == 0 || pDesc->ArraySize == 0 || pInitialData == nullptr || pInitialData [0].pSysMem == nullptr || pDesc->Width == 0 || pDesc->Height == 0)
    return E_INVALIDARG;

  if (pDesc->BindFlags & D3D11_BIND_RENDER_TARGET || pDesc->BindFlags & D3D11_BIND_DEPTH_STENCIL)
    return E_INVALID_PROTOCOL_FORMAT;

  if (hModD3DX11_43 == nullptr) {
    hModD3DX11_43 =
      LoadLibrary (L"d3dx11_43.dll");

    if (hModD3DX11_43 == nullptr)
      hModD3DX11_43 = (HMODULE)1;
  }

  if (D3DX11SaveTextureToFileW == nullptr && (uintptr_t)hModD3DX11_43 > 1) {
    D3DX11SaveTextureToFileW =
      (D3DX11SaveTextureToFileW_pfn)
        GetProcAddress (hModD3DX11_43, "D3DX11SaveTextureToFileW");
  }

  if (D3DX11SaveTextureToFileW != nullptr) {
    wchar_t wszPath [MAX_PATH] = { L'\0' };

    lstrcatW (wszPath, SK_D3D11_res_root.c_str ());

    if (GetFileAttributes (wszPath) == INVALID_FILE_ATTRIBUTES)
      CreateDirectoryW (wszPath, nullptr);

    lstrcatW (wszPath, L"/dump");

    if (GetFileAttributes (wszPath) == INVALID_FILE_ATTRIBUTES)
      CreateDirectoryW (wszPath, nullptr);

    lstrcatW (wszPath, L"/textures");

    if (GetFileAttributes (wszPath) == INVALID_FILE_ATTRIBUTES)
      CreateDirectoryW (wszPath, nullptr);

    lstrcatW (wszPath, L"/");
    lstrcatW (wszPath, host_app.c_str ());

    if (GetFileAttributes (wszPath) == INVALID_FILE_ATTRIBUTES)
      CreateDirectoryW (wszPath, nullptr);

    bool compressed = false;

    if (pDesc->Format >= DXGI_FORMAT_BC1_TYPELESS && pDesc->Format <= DXGI_FORMAT_BC5_SNORM)
      compressed = true;

    if (pDesc->Format >= DXGI_FORMAT_BC6H_TYPELESS && pDesc->Format <= DXGI_FORMAT_BC7_UNORM_SRGB)
      compressed = true;

    wchar_t wszOutPath [MAX_PATH] = { L'\0' };
    wchar_t wszOutName [MAX_PATH] = { L'\0' };

    lstrcatW (wszOutPath, SK_D3D11_res_root.c_str ());
    lstrcatW (wszOutPath, L"\\dump\\textures\\");
    lstrcatW (wszOutPath, host_app.c_str ());

    if (compressed) {
      wsprintfW ( wszOutName, L"%s\\Compressed_%X.dds",
                    wszOutPath, checksum );
    } else {
      wsprintfW ( wszOutName, L"%s\\Uncompressed_%X.dds",
                    wszOutPath, checksum );
    }

    if (wcslen (wszOutName)) {
      // Already dumped this
      if (GetFileAttributes (wszOutName) != INVALID_FILE_ATTRIBUTES)
        return E_FAIL;

      D3DX11_IMAGE_FILE_FORMAT fmt = D3DX11_IFF_DDS;
      return D3DX11SaveTextureToFileW ( pCtx, pSrcTexture, fmt, wszOutName );
    }
  }

  return E_FAIL;
}

uint32_t
crc32_tex (  _In_        const D3D11_TEXTURE2D_DESC   *pDesc,
             _In_opt_    const D3D11_SUBRESOURCE_DATA *pInitialData,
             _Out_opt_         size_t                 *pSize )
{
  uint32_t checksum = 0;

  bool compressed = false;

  if (pDesc->Format >= DXGI_FORMAT_BC1_TYPELESS && pDesc->Format <= DXGI_FORMAT_BC5_SNORM)
    compressed = true;

  if (pDesc->Format >= DXGI_FORMAT_BC6H_TYPELESS && pDesc->Format <= DXGI_FORMAT_BC7_UNORM_SRGB)
    compressed = true;

//dll_log.Log (L"[  D3D 11  ] [!] Dumping - (%lux%lu @ %lu LODs) - Format: %lu, Usage: %lu, Bind/CPU/Misc Flags: %X/%X/%X, ArraySize: %lu, Samples: %lu, Data: %ph, Pitch: %lu",
//             pDesc->Width, pDesc->Height, pDesc->MipLevels, pDesc->Format, pDesc->Usage, pDesc->BindFlags, pDesc->CPUAccessFlags, pDesc->MiscFlags, pDesc->ArraySize, pDesc->SampleDesc.Count, pInitialData [0].pSysMem, pInitialData [0].SysMemPitch);

  int block_size = pDesc->Format == DXGI_FORMAT_BC1_UNORM ? 8 : 16;

  int width      = pDesc->Width;
  int height     = pDesc->Height;

  size_t size = 0;

  for (unsigned int i = 0; i < pDesc->MipLevels; i++) {
    if (compressed) {
      size += (pInitialData [i].SysMemPitch / block_size) * (height >> i);

      checksum =
        crc32 (checksum, (const char *)pInitialData [i].pSysMem, (pInitialData [i].SysMemPitch / block_size) * (height >> i));
    } else {
      size += (pInitialData [i].SysMemPitch) * (height >> i);

      checksum =
        crc32 (checksum, (const char *)pInitialData [i].pSysMem, (pInitialData [i].SysMemPitch) * (height >> i));
    }
  }

  if (pSize != nullptr)
    *pSize = size;

  return checksum;
}

CRITICAL_SECTION auto_cs = { 0 };


class SK_D3D11CriticalSection {
public:
  SK_D3D11CriticalSection ( CRITICAL_SECTION* pCS,
                            bool              try_only = false )
  {
    cs_ = pCS;

    if (try_only)
      TryEnter ();
    else {
      Enter ();
    }
  }

  ~SK_D3D11CriticalSection (void)
  {
    Leave ();
  }

  bool try_result (void)
  {
    return acquired_;
  }

protected:
  bool TryEnter (_Acquires_lock_(* this->cs_) void)
  {
    return (acquired_ = (TryEnterCriticalSection (cs_) != FALSE));
  }

  void Enter (_Acquires_lock_(* this->cs_) void)
  {
    EnterCriticalSection (cs_);

    acquired_ = true;
  }

  void Leave (_Releases_lock_(* this->cs_) void)
  {
    if (acquired_ != false)
      LeaveCriticalSection (cs_);

    acquired_ = false;
  }

private:
  bool              acquired_;
  CRITICAL_SECTION* cs_;
};

__declspec (thread) bool self_invoke = false;

HRESULT
WINAPI
D3D11Dev_CreateTexture2D_Override (
  _In_            ID3D11Device           *This,
  _In_      const D3D11_TEXTURE2D_DESC   *pDesc,
  _In_opt_  const D3D11_SUBRESOURCE_DATA *pInitialData,
  _Out_opt_       ID3D11Texture2D        **ppTexture2D )
{
  static bool init = false;
  if (! init) {
    InitializeCriticalSectionAndSpinCount (&auto_cs, 50000UL);
    init = true;
  }

  if (ppTexture2D != nullptr)
  {
    SK_D3D11CriticalSection critical_scope (&auto_cs);

    if (SK_D3D11_TextureIsCached (*ppTexture2D)) {
      dll_log.Log (L"[DX11TexMgr] The game is replacing an existing texture, consider making a shadow copy...");
      SK_D3D11_RemoveTexFromCache (*ppTexture2D);
    }
  }

  LARGE_INTEGER             load_start;
  QueryPerformanceCounter (&load_start);

  uint32_t checksum  = 0;
  uint32_t cache_tag = 0;
  size_t   size      = 0;

  ID3D11Texture2D* pCachedTex = nullptr;

  bool cacheable =
    (! (pDesc->BindFlags & D3D11_BIND_DEPTH_STENCIL ||
        pDesc->BindFlags & D3D11_BIND_RENDER_TARGET) );

  //cacheable = cacheable && pDesc->Usage != D3D11_USAGE_DYNAMIC &&
                           //pDesc->Usage != D3D11_USAGE_STAGING &&
                           //pDesc->CPUAccessFlags == 0x00;

  cacheable = cacheable && pInitialData != nullptr && ppTexture2D != nullptr;

  if (cacheable) {
    checksum = crc32_tex (pDesc, pInitialData, &size);

    if ( (cacheable && (pDesc->MipLevels > 1 || pDesc->Format <= DXGI_FORMAT_BC1_UNORM)) ||
          injectable_textures.count (checksum) > 0 )
      cache_tag = crc32 (checksum, pDesc, sizeof D3D11_TEXTURE2D_DESC);

    // Known Hash Collisions
    //if (checksum != 0x44692BAA && checksum != 0x696AD6E8 && checksum != 0x5AFCD8C5 && checksum != 0xDF8D3D80) {
      SK_D3D11CriticalSection critical_scope (&auto_cs);
      pCachedTex = SK_D3D11_Textures.getTexture2D (cache_tag, pDesc);
    //}
  }

  if (pCachedTex != nullptr) {
    //dll_log.Log ( L"[DX11TexMgr] >> Redundant 2D Texture Load (Hash=0x%08X [%05.03f MiB]) <<",
                  //checksum, (float)size / (1024.0f * 1024.0f) );
    pCachedTex->AddRef ();
    *ppTexture2D = pCachedTex;
    return S_OK;
  }

  if (cacheable && pInitialData->pSysMem != nullptr && pDesc->Width > 0 && pDesc->Height > 0) {
    SK_D3D11_PopulateResourceList ();

    if (hModD3DX11_43 == nullptr) {
      hModD3DX11_43 =
        LoadLibrary (L"d3dx11_43.dll");

      if (hModD3DX11_43 == nullptr)
        hModD3DX11_43 = (HMODULE)1;
    }

    if (D3DX11CreateTextureFromFileW == nullptr && (uintptr_t)hModD3DX11_43 > 1) {
      D3DX11CreateTextureFromFileW =
        (D3DX11CreateTextureFromFileW_pfn)
          GetProcAddress (hModD3DX11_43, "D3DX11CreateTextureFromFileW");
    }

    if ((! self_invoke) && D3DX11CreateTextureFromFileW != nullptr && SK_D3D11_res_root.length ()) {
      wchar_t wszTex [MAX_PATH] = { L'\0' };

      {
        SK_D3D11CriticalSection critical_scope (&auto_cs);

        if (tex_hashes.count (checksum))
          wsprintfW (wszTex, L"%s\\%s", SK_D3D11_res_root.c_str (), tex_hashes [checksum].c_str ());

        else if (SK_D3D11_inject_textures && injectable_textures.count (checksum)) {
          wsprintfW (wszTex, L"%s\\inject\\textures\\%X.dds", SK_D3D11_res_root.c_str (), checksum);
        }
      }

      if (wcslen (wszTex) && GetFileAttributes (wszTex) != INVALID_FILE_ATTRIBUTES) {
        ID3D11Resource* pRes = nullptr;

#define D3DX11_DEFAULT -1

        self_invoke = true;

        D3DX11_IMAGE_LOAD_INFO load_info;

        load_info.BindFlags      = pDesc->BindFlags;
        load_info.CpuAccessFlags = pDesc->CPUAccessFlags;
        load_info.Depth          = D3DX11_DEFAULT;
        load_info.Filter         = D3DX11_DEFAULT;
        load_info.FirstMipLevel  = 0;
        load_info.Format         = pDesc->Format;  // (DXGI_FORMAT)D3DX11_DEFAULT;
        load_info.Height         = D3DX11_DEFAULT; // pDesc->Height;
        load_info.MipFilter      = D3DX11_DEFAULT;
        load_info.MipLevels      = D3DX11_DEFAULT; // pDesc->MipLevels;
        load_info.MiscFlags      = pDesc->MiscFlags;
        load_info.pSrcInfo       = nullptr;
        load_info.Usage          = pDesc->Usage;
        load_info.Width          = D3DX11_DEFAULT;// pDesc->Width;

        if (SUCCEEDED (D3DX11CreateTextureFromFileW (This, wszTex, &load_info, nullptr, (ID3D11Resource**)ppTexture2D, nullptr))) {
          LARGE_INTEGER             load_end;
          QueryPerformanceCounter (&load_end);

          self_invoke = false;

          if ( cacheable ) {
            SK_D3D11CriticalSection critical_scope (&auto_cs);
            SK_D3D11_Textures.refTexture2D (
              *ppTexture2D,
                pDesc,
                  cache_tag,
                    size,
                      load_end.QuadPart - load_start.QuadPart
            );
          }

          return S_OK;
        }

        self_invoke = false;
      }
    }
  }

  HRESULT ret =
    D3D11Dev_CreateTexture2D_Original (This, pDesc, pInitialData, ppTexture2D);

  LARGE_INTEGER             load_end;
  QueryPerformanceCounter (&load_end);

  if ( SUCCEEDED (ret) && (! self_invoke) && cacheable ) {
    SK_D3D11CriticalSection critical_scope (&auto_cs);
    SK_D3D11_Textures.refTexture2D (
      *ppTexture2D,
        pDesc,
          cache_tag,
            size,
              load_end.QuadPart - load_start.QuadPart
    );
  }

  if ( checksum != 0x00 && SK_D3D11_dump_textures && SUCCEEDED (ret) && cacheable ) {
    SK_D3D11CriticalSection critical_scope (&auto_cs);

    if ((! self_invoke) && (! dumped_textures.count (checksum))) {
      self_invoke = true;

      CComPtr <ID3D11DeviceContext> pCtx;
      This->GetImmediateContext (&pCtx);

      if (pCtx != nullptr) {
        SK_D3D11_DumpTexture2D (pCtx, *ppTexture2D, checksum, pDesc, pInitialData);
        dumped_textures.insert (checksum);
      }
    }
  }

  self_invoke = false;

  return ret;
}

void
WINAPI
SK_DXGI_SetPreferredAdapter (int override_id)
{
  SK_DXGI_preferred_adapter = override_id;
}