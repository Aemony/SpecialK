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
#define _CRT_NON_CONFORMING_SWPRINTFS

#include <Windows.h>
#include <SpecialK/diagnostics/compatibility.h>
#include <SpecialK/diagnostics/crash_handler.h>
#include <process.h>

#include <psapi.h>
#pragma comment (lib, "psapi.lib")

#include <Commctrl.h>
#pragma comment (lib,    "advapi32.lib")
#pragma comment (lib,    "user32.lib")
#pragma comment (lib,    "comctl32.lib")
#pragma comment (linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' "  \
                         "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df'" \
                         " language='*'\"")

#include <SpecialK/config.h>
#include <SpecialK/hooks.h>
#include <SpecialK/core.h>
#include <SpecialK/log.h>
#include <SpecialK/utility.h>
#include <SpecialK/steam_api.h>
#include <SpecialK/window.h>
#include <SpecialK/render_backend.h>


#include <SpecialK/DLL_VERSION.H>

#define SK_CHAR(x) (      _T ) constexpr _T      (typeid (_T) == typeid (wchar_t)) ? (      _T )_L(x) : (      _T )(x)
#define SK_TEXT(x) (const _T*) constexpr LPCVOID (typeid (_T) == typeid (wchar_t)) ? (const _T*)_L(x) : (const _T*)(x)

typedef PSTR    (__stdcall *StrStrI_pfn)            (LPCVOID lpFirst,   LPCVOID lpSearch);
typedef BOOL    (__stdcall *PathRemoveFileSpec_pfn) (LPVOID  lpPath);
typedef HMODULE (__stdcall *LoadLibrary_pfn)        (LPCVOID lpLibFileName);
typedef LPVOID  (__cdecl   *strncpy_pfn)            (LPVOID  lpDest,    LPCVOID lpSource,     size_t   nCount);
typedef LPVOID  (__stdcall *lstrcat_pfn)            (LPVOID  lpString1, LPCVOID lpString2);
typedef BOOL    (__stdcall *GetModuleHandleEx_pfn)  (DWORD   dwFlags,   LPCVOID lpModuleName, HMODULE* phModule);


extern DWORD __stdcall SK_RaptrWarn (LPVOID user);

BOOL __stdcall SK_TerminateParentProcess    (UINT uExitCode);
BOOL __stdcall SK_ValidateGlobalRTSSProfile (void);
void __stdcall SK_ReHookLoadLibrary         (void);
void __stdcall SK_UnhookLoadLibrary         (void);

bool SK_LoadLibrary_SILENCE = false;



#ifdef _WIN64
#define SK_STEAM_BIT_WSTRING L"64"
#define SK_STEAM_BIT_STRING   "64"
#else
#define SK_STEAM_BIT_WSTRING L""
#define SK_STEAM_BIT_STRING   ""
#endif

static const wchar_t* wszSteamClientDLL = L"SteamClient";
static const char*     szSteamClientDLL =  "SteamClient";

static const wchar_t* wszSteamNativeDLL = L"SteamNative.dll";
static const char*     szSteamNativeDLL =  "SteamNative.dll";

static const wchar_t* wszSteamAPIDLL    = L"steam_api" SK_STEAM_BIT_WSTRING L".dll";
static const char*     szSteamAPIDLL    =  "steam_api" SK_STEAM_BIT_STRING   ".dll";


struct sk_loader_hooks_t {
  // Manually unhooked for compatibility, DO NOT rehook!
  bool   unhooked              = false;

  LPVOID LoadLibraryA_target   = nullptr;
  LPVOID LoadLibraryExA_target = nullptr;
  LPVOID LoadLibraryW_target   = nullptr;
  LPVOID LoadLibraryExW_target = nullptr;

  LPVOID FreeLibrary_target    = nullptr;
} _loader_hooks;

#include <Shlwapi.h>
#pragma comment (lib, "Shlwapi.lib")

extern CRITICAL_SECTION loader_lock;

void
__stdcall
SK_LockDllLoader (void)
{
  if (config.system.strict_compliance)
  {
    //bool unlocked = TryEnterCriticalSection (&loader_lock);
                       EnterCriticalSection (&loader_lock);
     //if (unlocked)
                       //LeaveCriticalSection (&loader_lock);
    //else
      //dll_log.Log (L"[DLL Loader]  *** DLL Loader Lock Contention ***");
  }
}

void
__stdcall
SK_UnlockDllLoader (void)
{
  if (config.system.strict_compliance)
    LeaveCriticalSection (&loader_lock);
}

template <typename _T>
BOOL
__stdcall
BlacklistLibrary (const _T* lpFileName)
{
#pragma push_macro ("StrStrI")
#pragma push_macro ("GetModuleHandleEx")

#undef StrStrI
#undef GetModuleHandleEx

  static StrStrI_pfn            StrStrI =
    (StrStrI_pfn)
      constexpr LPCVOID ( typeid (_T) == typeid (wchar_t) ? (StrStrI_pfn)           &StrStrIW           :
                                                            (StrStrI_pfn)           &StrStrIA           );

  static GetModuleHandleEx_pfn  GetModuleHandleEx =
    (GetModuleHandleEx_pfn)
      constexpr LPCVOID ( typeid (_T) == typeid (wchar_t) ? (GetModuleHandleEx_pfn) &GetModuleHandleExW : 
                                                            (GetModuleHandleEx_pfn) &GetModuleHandleExA );

  //
  // TODO: This option is practically obsolete, Raptr is very compatible these days...
  //         (either that, or I've conquered Raptr ;))
  //
  if (config.compatibility.disable_raptr)
  {
    if ( StrStrI (lpFileName, SK_TEXT("ltc_game")) )
    {
      dll_log.Log (L"[Black List] Preventing Raptr's overlay (ltc_game), it likes to crash games!");
      return TRUE;
    }
  }

  if (config.compatibility.disable_nv_bloat)
  {
    static bool init = false;

    static std::vector < const _T* >
                nv_blacklist;

    if (! init)
    {
      nv_blacklist.emplace_back (SK_TEXT("rxgamepadinput.dll"));
      nv_blacklist.emplace_back (SK_TEXT("rxcore.dll"));
      nv_blacklist.emplace_back (SK_TEXT("nvinject.dll"));
      nv_blacklist.emplace_back (SK_TEXT("rxinput.dll"));
#ifdef _WIN64
      nv_blacklist.emplace_back (SK_TEXT("nvspcap64.dll"));
      nv_blacklist.emplace_back (SK_TEXT("nvSCPAPI64.dll"));
#else
      nv_blacklist.emplace_back (SK_TEXT("nvspcap.dll"));
      nv_blacklist.emplace_back (SK_TEXT("nvSCPAPI.dll"));
#endif
      init = true;
    }

    for ( auto&& it : nv_blacklist )
    {
      if (StrStrI (lpFileName, it))
      {
        HMODULE hModNV;

        //dll_log.Log ( L"[Black List] Disabling NVIDIA BloatWare ('%s'), so long we hardly knew ye', "
                                   //L"but you did bad stuff in a lot of games.",
                        //lpFileName );

        if (GetModuleHandleEx (GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, it, &hModNV))
          FreeLibrary_Original (hModNV);

        return TRUE;
      }
    }
  }

  if (StrStrIW (SK_GetHostApp (), L"RiME.exe"))
  {
    if (StrStrI (lpFileName, SK_TEXT("openvr_api.dll")))
      return TRUE;
  }

  return FALSE;

#pragma pop_macro ("StrStrI")
#pragma pop_macro ("GetModuleHandleEx")
}

#include <array>
#include <string>

template <typename _T>
BOOL
__stdcall
SK_LoadLibrary_PinModule (const _T* pStr)
{
#pragma push_macro ("GetModuleHandleEx")

#undef GetModuleHandleEx

  static GetModuleHandleEx_pfn  GetModuleHandleEx =
    (GetModuleHandleEx_pfn)
      constexpr LPCVOID ( typeid (_T) == typeid (wchar_t) ? (GetModuleHandleEx_pfn) &GetModuleHandleExW : 
                                                            (GetModuleHandleEx_pfn) &GetModuleHandleExA );

  HMODULE hModDontCare;

  return
    GetModuleHandleEx ( GET_MODULE_HANDLE_EX_FLAG_PIN,
                          pStr,
                            &hModDontCare );

#pragma pop_macro ("GetModuleHandleEx")
}

template <typename _T>
bool
__stdcall
SK_LoadLibrary_IsPinnable (const _T* pStr)
{
#pragma push_macro ("StrStrI")

#undef StrStrI

  static StrStrI_pfn            StrStrI =
    (StrStrI_pfn)
      constexpr LPCVOID ( typeid (_T) == typeid (wchar_t) ? (StrStrI_pfn) &StrStrIW :
                                                            (StrStrI_pfn) &StrStrIA );
  static std::vector <const _T*> pinnable_libs =
  {
    SK_TEXT ("CEGUI"), SK_TEXT ("OpenCL"),

    // Some software repeatedly loads and unloads this, which can
    //   cause TLS-related problems if left unchecked... just leave
    //     the damn thing loaded permanently!
    SK_TEXT ("d3dcompiler_")
  };

  for (auto it : pinnable_libs)
  {
    if (StrStrI (pStr, it))
      return true;
  }

  return false;

#pragma pop_macro ("StrStrI")
}

template <typename _T>
void
__stdcall
SK_TraceLoadLibrary (       HMODULE hCallingMod,
                      const _T*     lpFileName,
                      const _T*     lpFunction,
                            LPVOID  lpCallerFunc )
{
#pragma push_macro ("StrStrI")
#pragma push_macro ("PathRemoveFileSpec")
#pragma push_macro ("LoadLibrary")
#pragma push_macro ("lstrcat")
#pragma push_macro ("GetModuleHandleEx")

#undef StrStrI
#undef PathRemoveFileSpec
#undef LoadLibrary
#undef lstrcat
#undef GetModuleHandleEx

  static StrStrI_pfn            StrStrI =
    (StrStrI_pfn)
      constexpr LPCVOID ( typeid (_T) == typeid (wchar_t) ? (StrStrI_pfn)           &StrStrIW            :
                                                            (StrStrI_pfn)           &StrStrIA            );

  static PathRemoveFileSpec_pfn PathRemoveFileSpec =
    (PathRemoveFileSpec_pfn)
      constexpr LPCVOID ( typeid (_T) == typeid (wchar_t) ? (PathRemoveFileSpec_pfn)&PathRemoveFileSpecW :
                                                            (PathRemoveFileSpec_pfn)&PathRemoveFileSpecA );

  static LoadLibrary_pfn        LoadLibrary =
    (LoadLibrary_pfn)
      constexpr LPCVOID ( typeid (_T) == typeid (wchar_t) ? (LoadLibrary_pfn)       &LoadLibraryW        : 
                                                            (LoadLibrary_pfn)       &LoadLibraryA        );

  static strncpy_pfn            strncpy_ =
    (strncpy_pfn)
      constexpr LPCVOID ( typeid (_T) == typeid (wchar_t) ? (strncpy_pfn)           &wcsncpy             :
                                                            (strncpy_pfn)           &strncpy             );

  static lstrcat_pfn            lstrcat =
    (lstrcat_pfn)
      constexpr LPCVOID ( typeid (_T) == typeid (wchar_t) ? (lstrcat_pfn)           &lstrcatW            :
                                                            (lstrcat_pfn)           &lstrcatA            );

  static GetModuleHandleEx_pfn  GetModuleHandleEx =
    (GetModuleHandleEx_pfn)
      constexpr LPCVOID ( typeid (_T) == typeid (wchar_t) ? (GetModuleHandleEx_pfn) &GetModuleHandleExW  : 
                                                            (GetModuleHandleEx_pfn) &GetModuleHandleExA  );

  if (config.steam.preload_client)
  {
    if (StrStrI (lpFileName, SK_TEXT("gameoverlayrenderer64")))
    {
       _T       szSteamPath             [MAX_PATH + 1] = { SK_CHAR('\0') };
      strncpy_ (szSteamPath, lpFileName, MAX_PATH - 1);

      PathRemoveFileSpec (szSteamPath);

#ifdef _WIN64
      lstrcat (szSteamPath, SK_TEXT("\\steamclient64.dll"));
#else
      lstrcat (szSteamPath, SK_TEXT("\\steamclient.dll"));
#endif

      LoadLibrary (szSteamPath);

      HMODULE hModClient;
      GetModuleHandleEx ( GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT |
                          GET_MODULE_HANDLE_EX_FLAG_PIN,
                            szSteamPath,
                              &hModClient );
    }
  }


  wchar_t wszModName [MAX_PATH] = { L'\0' };
  wcsncpy (wszModName, SK_GetModuleName (hCallingMod).c_str (), MAX_PATH);

  if (! SK_LoadLibrary_SILENCE)
  {    
    char  szSymbol [1024] = { 0 };
    ULONG ulLen  =  1024;
    
    ulLen = SK_GetSymbolNameFromModuleAddr (SK_GetCallingDLL (lpCallerFunc), (uintptr_t)lpCallerFunc, szSymbol, ulLen);

    if (constexpr (typeid (_T) == typeid (char)))
      dll_log.Log ( "[DLL Loader]   ( %-28ls ) loaded '%#64hs' <%hs> { '%hs' }",
                      wszModName,
                        lpFileName,
                          lpFunction,
                            szSymbol );
    else
      dll_log.Log ( L"[DLL Loader]   ( %-28ls ) loaded '%#64ls' <%ls> { '%hs' }",
                      wszModName,
                        lpFileName,
                          lpFunction,
                            szSymbol );
  }

  if (hCallingMod != SK_GetDLL ())
  {
    if (config.compatibility.rehook_loadlibrary)
    {
      // This is silly, this many string comparions per-load is
      //   not good. Hash the string and compare it in the future.
      if ( StrStrIW (wszModName, L"gameoverlayrenderer") ||
           StrStrIW (wszModName, L"Activation")          ||
           StrStrIW (wszModName, L"ReShade")             ||
           StrStrIW (wszModName, L"rxcore")              ||
           StrStrIW (wszModName, L"RTSSHooks")           ||
           StrStrIW (wszModName, L"GeDoSaTo")            ||
           StrStrIW (wszModName, L"Nahimic2DevProps") )
      {   
        SK_ReHookLoadLibrary ();
      }
    }
  }

  if (hCallingMod != SK_GetDLL ()/* && SK_IsInjected ()*/)
  {
         if ( (! (SK_GetDLLRole () & DLL_ROLE::D3D9)) && config.apis.d3d9.hook &&
              ( StrStrI  (lpFileName, SK_TEXT("d3d9.dll"))  ||
                StrStrIW (wszModName,        L"d3d9.dll")   ||
                                                      
                StrStrI  (lpFileName, SK_TEXT("d3dx9_"))    ||
                StrStrIW (wszModName,        L"d3dx9_")     ||

                StrStrI  (lpFileName, SK_TEXT("Direct3D9")) ||
                StrStrIW (wszModName,        L"Direct3D9")  ||

                // NVIDIA's User-Mode D3D Frontend
                StrStrI  (lpFileName, SK_TEXT("nvd3dum.dll")) ||
                StrStrIW (wszModName,        L"nvd3dum.dll")  ) )
      SK_BootD3D9   ();
    else if ( (! (SK_GetDLLRole () & DLL_ROLE::D3D8)) && config.apis.d3d8.hook &&
              ( StrStrI  (lpFileName, SK_TEXT("d3d8.dll")) ||
                StrStrIW (wszModName,        L"d3d8.dll")    ) )
      SK_BootD3D8   ();
    else if ( (! (SK_GetDLLRole () & DLL_ROLE::DDraw)) && config.apis.ddraw.hook &&
              ( StrStrI  (lpFileName, SK_TEXT("ddraw.dll")) ||
                StrStrIW (wszModName,        L"ddraw.dll")   ) )
      SK_BootDDraw  ();
    else if ( (! (SK_GetDLLRole () & DLL_ROLE::DXGI)) && config.apis.dxgi.d3d11.hook &&
              ( StrStrI  (lpFileName, SK_TEXT("d3d11.dll")) ||
                StrStrIW (wszModName,        L"d3d11.dll") ))
      SK_BootDXGI   ();
    else if ( (! (SK_GetDLLRole () & DLL_ROLE::DXGI)) && config.apis.dxgi.d3d12.hook &&
              ( StrStrI  (lpFileName, SK_TEXT("d3d12.dll")) ||
                StrStrIW (wszModName,        L"d3d12.dll") ))
      SK_BootDXGI   ();
    else if (  (! (SK_GetDLLRole () & DLL_ROLE::OpenGL)) && config.apis.OpenGL.hook &&
              ( StrStrI  (lpFileName, SK_TEXT("OpenGL32.dll")) ||
                StrStrIW (wszModName,        L"OpenGL32.dll") ))
      SK_BootOpenGL ();
    else if (   StrStrI  (lpFileName, SK_TEXT("vulkan-1.dll")) ||
                StrStrIW (wszModName,        L"vulkan-1.dll")  )
      SK_BootVulkan ();
    else if (   StrStrI (lpFileName, SK_TEXT("xinput1_3.dll")) )
      SK_Input_HookXInput1_3 ();
    else if (   StrStrI (lpFileName, SK_TEXT("xinput1_4.dll")) )
      SK_Input_HookXInput1_4 ();
    else if (   StrStrI (lpFileName, SK_TEXT("xinput9_1_0.dll")) )
      SK_Input_HookXInput9_1_0 ();
    else if (   StrStrI (lpFileName, SK_TEXT("dinput8.dll")) )
      SK_Input_HookDI8 ();
    else if (   StrStrI (lpFileName, SK_TEXT("hid.dll")) )
      SK_Input_HookHID ();

#if 0
    if (! config.steam.silent) {
      if ( StrStrIA (lpFileName, szSteamAPIDLL)    ||
           StrStrIA (lpFileName, szSteamNativeDLL) ||
           StrStrIA (lpFileName, szSteamClientDLL) ) {
        SK_HookSteamAPI ();
      }
    }
#endif
  }

  if (SK_LoadLibrary_IsPinnable (lpFileName))
       SK_LoadLibrary_PinModule (lpFileName);

#pragma pop_macro ("StrStrI")
#pragma pop_macro ("PathRemoveFileSpec")
#pragma pop_macro ("LoadLibrary")
#pragma pop_macro ("lstrcat")
#pragma pop_macro ("GetModuleHandleEx")
}


extern volatile ULONG __SK_DLL_Ending;

BOOL
WINAPI
FreeLibrary_Detour (HMODULE hLibModule)
{
  if (InterlockedCompareExchange (&__SK_DLL_Ending, 0, 0) != 0)
  {
    return FreeLibrary_Original (hLibModule);
  }

  SK_LockDllLoader ();

  std::wstring name = SK_GetModuleName (hLibModule);

  if (name == L"NvCamera64.dll")
    return FALSE;

  BOOL bRet = FreeLibrary_Original (hLibModule);

  if ( (! (SK_LoadLibrary_SILENCE)) ||
           SK_GetModuleName (hLibModule).find (L"steam") != std::wstring::npos )
  {
    if ( SK_GetModuleName (hLibModule).find (L"steam") != std::wstring::npos || 
        (bRet && GetModuleHandle (name.c_str ()) == nullptr ) )
    {
      if (config.system.log_level > 2)
      {
        char  szSymbol [1024] = { };
        ULONG ulLen  =  1024;
    
        ulLen = SK_GetSymbolNameFromModuleAddr (SK_GetCallingDLL (), (uintptr_t)_ReturnAddress (), szSymbol, ulLen);

        dll_log.Log ( L"[DLL Loader]   ( %-28ls ) freed  '%#64ls' from { '%hs' }",
                        SK_GetModuleName (SK_GetCallingDLL ()).c_str (),
                          name.c_str (),
                            szSymbol
                    );
      }
    }
  }

  SK_UnlockDllLoader ();

  return bRet;
}

HMODULE
WINAPI
LoadLibraryA_Detour (LPCSTR lpFileName)
{
  if (lpFileName == nullptr)
    return NULL;

  SK_LockDllLoader ();

  HMODULE hModEarly = nullptr;

  __try {
    GetModuleHandleExA ( GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)lpFileName, &hModEarly );
  } 

  __except ( (GetExceptionCode () == EXCEPTION_INVALID_HANDLE) ?
                       EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH  )
  {
    SetLastError (0);
  }


  if (hModEarly == nullptr && BlacklistLibrary (lpFileName))
  {
    SK_UnlockDllLoader ();
    return NULL;
  }

  HMODULE hMod =
    LoadLibraryA_Original (lpFileName);

  if (hModEarly != hMod)
  {
    SK_TraceLoadLibrary ( SK_GetCallingDLL (),
                            lpFileName,
                              "LoadLibraryA", _ReturnAddress () );
  }

  SK_UnlockDllLoader ();
  return hMod;
}

HMODULE
WINAPI
LoadLibraryW_Detour (LPCWSTR lpFileName)
{
  if (lpFileName == nullptr)
    return NULL;

 SK_LockDllLoader ();

  HMODULE hModEarly = nullptr;

  __try {
    GetModuleHandleExW ( GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, lpFileName, &hModEarly );
  }
  __except ( (GetExceptionCode () == EXCEPTION_INVALID_HANDLE) ?
                           EXCEPTION_EXECUTE_HANDLER :
                           EXCEPTION_CONTINUE_SEARCH )
  {
    SetLastError (0);
  }


  if (hModEarly == nullptr && BlacklistLibrary (lpFileName))
  {
    SK_UnlockDllLoader ();
    return NULL;
  }


  HMODULE hMod =
    LoadLibraryW_Original (lpFileName);

  if (hModEarly != hMod)
  {
    SK_TraceLoadLibrary ( SK_GetCallingDLL (),
                            lpFileName,
                              L"LoadLibraryW", _ReturnAddress () );
  }

  SK_UnlockDllLoader ();
  return hMod;
}

HMODULE
WINAPI
LoadLibraryExA_Detour (
  _In_       LPCSTR lpFileName,
  _Reserved_ HANDLE hFile,
  _In_       DWORD  dwFlags )
{
  if (lpFileName == nullptr)
    return NULL;

  SK_LockDllLoader ();

  if ((dwFlags & LOAD_LIBRARY_AS_DATAFILE) && (! BlacklistLibrary (lpFileName))) {
    SK_UnlockDllLoader ();
    return LoadLibraryExA_Original (lpFileName, hFile, dwFlags);
  }

  HMODULE hModEarly = nullptr;

  __try {
    GetModuleHandleExA ( GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, lpFileName, &hModEarly );
  }
  __except ( (GetExceptionCode () == EXCEPTION_INVALID_HANDLE) ?
                           EXCEPTION_EXECUTE_HANDLER :
                           EXCEPTION_CONTINUE_SEARCH )
  {
    SetLastError (0);
  }

  if (hModEarly == NULL && BlacklistLibrary (lpFileName))
  {
    SK_UnlockDllLoader ();
    return NULL;
  }

  HMODULE hMod = LoadLibraryExA_Original (lpFileName, hFile, dwFlags);

  if ( hModEarly != hMod && (! ((dwFlags & LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE) ||
                                (dwFlags & LOAD_LIBRARY_AS_IMAGE_RESOURCE))) ) {
    SK_TraceLoadLibrary ( SK_GetCallingDLL (),
                            lpFileName,
                              "LoadLibraryExA", _ReturnAddress () );
  }

  SK_UnlockDllLoader ();
  return hMod;
}

HMODULE
WINAPI
LoadLibraryExW_Detour (
  _In_       LPCWSTR lpFileName,
  _Reserved_ HANDLE  hFile,
  _In_       DWORD   dwFlags )
{
  if (lpFileName == nullptr)
    return NULL;

  SK_LockDllLoader ();

  if ((dwFlags & LOAD_LIBRARY_AS_DATAFILE) && (! BlacklistLibrary (lpFileName))) {
    SK_UnlockDllLoader ();
    return LoadLibraryExW_Original (lpFileName, hFile, dwFlags);
  }

  HMODULE hModEarly = nullptr;

  __try {
    GetModuleHandleExW ( GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, lpFileName, &hModEarly );
  }
  __except ( (GetExceptionCode () == EXCEPTION_INVALID_HANDLE) ?
                           EXCEPTION_EXECUTE_HANDLER :
                           EXCEPTION_CONTINUE_SEARCH )
  {
    SetLastError (0);
  }

  if (hModEarly == NULL && BlacklistLibrary (lpFileName)) {
    SK_UnlockDllLoader ();
    return NULL;
  }

  HMODULE hMod = LoadLibraryExW_Original (lpFileName, hFile, dwFlags);

  if ( hModEarly != hMod && (! ((dwFlags & LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE) ||
                                (dwFlags & LOAD_LIBRARY_AS_IMAGE_RESOURCE))) ) {
    SK_TraceLoadLibrary ( SK_GetCallingDLL (),
                            lpFileName,
                              L"LoadLibraryExW", _ReturnAddress () );
  }

  SK_UnlockDllLoader ();
  return hMod;
}

struct SK_ThirdPartyDLLs {
  struct {
    HMODULE rtss_hooks    = nullptr;
    HMODULE steam_overlay = nullptr;
  } overlays;
  struct {
    HMODULE gedosato      = nullptr;
  } misc;
} third_party_dlls;

bool
__stdcall
SK_CheckForGeDoSaTo (void)
{
  if (third_party_dlls.misc.gedosato)
    return true;

  return false;
}

//
// Gameoverlayrenderer{64}.dll messes with LoadLibrary hooking, which
//   means identifying which DLL loaded another becomes impossible
//     unless we remove and re-install our hooks.
//
//   ** GeDoSaTo and RTSS may also do the same depending on config. **
//
// Hook order with LoadLibrary is not traditionally important for a mod
//   system such as Special K, but the compatibility layer benefits from
//     knowing exactly WHAT was responsible for loading a library.
//
void
__stdcall
SK_ReHookLoadLibrary (void)
{
  if (! config.system.trace_load_library)
    return;

  if (_loader_hooks.unhooked)
    return;

  SK_LockDllLoader ();

  if (_loader_hooks.LoadLibraryA_target != nullptr) {
    SK_RemoveHook (_loader_hooks.LoadLibraryA_target);
    _loader_hooks.LoadLibraryA_target = nullptr;
  }

  SK_CreateDLLHook2 ( L"kernel32.dll", "LoadLibraryA",
                     LoadLibraryA_Detour,
           (LPVOID*)&LoadLibraryA_Original,
                    &_loader_hooks.LoadLibraryA_target );

  MH_QueueEnableHook (_loader_hooks.LoadLibraryA_target);


  if (_loader_hooks.LoadLibraryW_target != nullptr) {
    SK_RemoveHook (_loader_hooks.LoadLibraryW_target);
    _loader_hooks.LoadLibraryW_target = nullptr;
  }

  SK_CreateDLLHook2 ( L"kernel32.dll", "LoadLibraryW",
                     LoadLibraryW_Detour,
           (LPVOID*)&LoadLibraryW_Original,
                    &_loader_hooks.LoadLibraryW_target );

  MH_QueueEnableHook (_loader_hooks.LoadLibraryW_target);


  if (_loader_hooks.LoadLibraryExA_target != nullptr) {
    SK_RemoveHook (_loader_hooks.LoadLibraryExA_target);
    _loader_hooks.LoadLibraryExA_target = nullptr;
  }

  SK_CreateDLLHook2 ( L"kernel32.dll", "LoadLibraryExA",
                     LoadLibraryExA_Detour,
           (LPVOID*)&LoadLibraryExA_Original,
                    &_loader_hooks.LoadLibraryExA_target );

  MH_QueueEnableHook (_loader_hooks.LoadLibraryExA_target);


  if (_loader_hooks.LoadLibraryExW_target != nullptr) {
    SK_RemoveHook (_loader_hooks.LoadLibraryExW_target);
    _loader_hooks.LoadLibraryExW_target = nullptr;
  }

  SK_CreateDLLHook2 ( L"kernel32.dll", "LoadLibraryExW",
                     LoadLibraryExW_Detour,
           (LPVOID*)&LoadLibraryExW_Original,
                    &_loader_hooks.LoadLibraryExW_target );

  MH_QueueEnableHook (_loader_hooks.LoadLibraryExW_target);


  if (_loader_hooks.FreeLibrary_target != nullptr) {
    SK_RemoveHook (_loader_hooks.FreeLibrary_target);
    _loader_hooks.FreeLibrary_target = nullptr;
  }


  // Steamclient64.dll leaks heap memory when unloaded,
  //   to prevent this from showing up during debug sessions,
  //     don't hook this function :)
#if 0
  SK_CreateDLLHook2 ( L"kernel32.dll", "FreeLibrary",
                     FreeLibrary_Detour,
           (LPVOID*)&FreeLibrary_Original,
                    &_loader_hooks.FreeLibrary_target );

  MH_QueueEnableHook (_loader_hooks.FreeLibrary_target);
#endif

  MH_ApplyQueued ();

  SK_UnlockDllLoader ();
}

void
__stdcall
SK_UnhookLoadLibrary (void)
{
  SK_LockDllLoader ();

  _loader_hooks.unhooked = true;

  if (_loader_hooks.LoadLibraryA_target != nullptr)
    MH_QueueDisableHook (_loader_hooks.LoadLibraryA_target);
  if (_loader_hooks.LoadLibraryW_target != nullptr)
    MH_QueueDisableHook(_loader_hooks.LoadLibraryW_target);
  if (_loader_hooks.LoadLibraryExA_target != nullptr)
    MH_QueueDisableHook (_loader_hooks.LoadLibraryExA_target);
  if (_loader_hooks.LoadLibraryExW_target != nullptr)
    MH_QueueDisableHook (_loader_hooks.LoadLibraryExW_target);
  if (_loader_hooks.FreeLibrary_target != nullptr)
    MH_QueueDisableHook (_loader_hooks.FreeLibrary_target);

  MH_ApplyQueued ();

  if (_loader_hooks.LoadLibraryA_target != nullptr)
    MH_RemoveHook (_loader_hooks.LoadLibraryA_target);
  if (_loader_hooks.LoadLibraryW_target != nullptr)
    MH_RemoveHook (_loader_hooks.LoadLibraryW_target);
  if (_loader_hooks.LoadLibraryExA_target != nullptr)
    MH_RemoveHook (_loader_hooks.LoadLibraryExA_target);
  if (_loader_hooks.LoadLibraryExW_target != nullptr)
    MH_RemoveHook (_loader_hooks.LoadLibraryExW_target);
  if (_loader_hooks.FreeLibrary_target != nullptr)
    MH_RemoveHook (_loader_hooks.FreeLibrary_target);

  _loader_hooks.LoadLibraryW_target   = nullptr;
  _loader_hooks.LoadLibraryA_target   = nullptr;
  _loader_hooks.LoadLibraryExW_target = nullptr;
  _loader_hooks.LoadLibraryExA_target = nullptr;
  _loader_hooks.FreeLibrary_target    = nullptr;

  // Re-establish the non-hooked functions
  SK_PreInitLoadLibrary ();

  SK_UnlockDllLoader ();
}



void
__stdcall
SK_InitCompatBlacklist (void)
{
  memset (&_loader_hooks, 0, sizeof sk_loader_hooks_t);
  SK_ReHookLoadLibrary ();
}


static bool loaded_gl     = false;
static bool loaded_vulkan = false;
static bool loaded_d3d9   = false;
static bool loaded_d3d8   = false;
static bool loaded_ddraw  = false;
static bool loaded_dxgi   = false;


struct enum_working_set_s {
  HMODULE     modules [1024] = { 0 };
  int         count          =   0;
  iSK_Logger* logger         = nullptr;
  HANDLE      proc           = INVALID_HANDLE_VALUE;
};


#include <unordered_set>
std::unordered_set <HMODULE> logged_modules;

void
_SK_SummarizeModule ( LPVOID   base_addr,  ptrdiff_t   mod_size,
                      HMODULE  hMod,       uintptr_t   addr,
                      wchar_t* wszModName, iSK_Logger* pLogger )
{
  char  szSymbol [1024] = { };
  ULONG ulLen  =  1024;

  ulLen = SK_GetSymbolNameFromModuleAddr (hMod, addr, szSymbol, ulLen);
  
  if (ulLen != 0) {
    pLogger->Log ( L"[ Module ]  ( %ph + %08lu )   -:< %-64hs >:-   %s",
                      (void *)base_addr, (uint32_t)mod_size,
                        szSymbol, wszModName );
  } else {
    pLogger->Log ( L"[ Module ]  ( %ph + %08li )       %-64hs       %s",
                      base_addr, mod_size, "", wszModName );
  }

  std::wstring ver_str = SK_GetDLLVersionStr (wszModName);

  if (ver_str != L"  ") {
    pLogger->LogEx ( false,
      L" ----------------------  [File Ver]    %s\n",
        ver_str.c_str () );
  }
}

void
SK_ThreadWalkModules (enum_working_set_s* pWorkingSet)
{
  SK_LockDllLoader ();

  iSK_Logger* pLogger = pWorkingSet->logger;

  for (int i = 0; i < pWorkingSet->count; i++ )
  {
        wchar_t wszModName [MAX_PATH + 2] = { };

    __try
    {
      // Get the full path to the module's file.
      if ( (! logged_modules.count (pWorkingSet->modules [i])) &&
              GetModuleFileNameExW ( pWorkingSet->proc,
                                       pWorkingSet->modules [i],
                                         wszModName,
                                           MAX_PATH ) )
      {
        MODULEINFO mi = { 0 };

        uintptr_t entry_pt  = 0;
        uintptr_t base_addr = 0;
        uint32_t  mod_size  = 0UL;

        if (GetModuleInformation (pWorkingSet->proc, pWorkingSet->modules [i], &mi, sizeof (MODULEINFO))) {
          entry_pt  = (uintptr_t)mi.EntryPoint;
          base_addr = (uintptr_t)mi.lpBaseOfDll;
          mod_size  =            mi.SizeOfImage;
        }
        else {
          break;
        }

        _SK_SummarizeModule ((void *)base_addr, mod_size, pWorkingSet->modules [i], entry_pt, wszModName, pLogger);

        logged_modules.insert (pWorkingSet->modules [i]);
      }
    }

    __except ( EXCEPTION_EXECUTE_HANDLER )
    {
      // Sometimes a DLL will be unloaded in the middle of doing this... just ignore that.
    }
  }

  SK_UnlockDllLoader ();
}

void
SK_WalkModules (int cbNeeded, HANDLE hProc, HMODULE* hMods, SK_ModuleEnum when)
{
  SK_LockDllLoader ();

  for ( int i = 0; i < (int)(cbNeeded / sizeof (HMODULE)); i++ )
  {
    wchar_t wszModName [MAX_PATH + 2] = { L'\0' };
            ZeroMemory (wszModName, sizeof (wchar_t) * (MAX_PATH + 2));

    __try {
      // Get the full path to the module's file.
      if ( GetModuleFileNameExW ( hProc,
                                    hMods [i],
                                      wszModName,
                                        MAX_PATH ) )
      {
        if ( (! third_party_dlls.overlays.rtss_hooks) &&
              StrStrIW (wszModName, L"RTSSHooks") ) {
          // Hold a reference to this DLL so it is not unloaded prematurely
          GetModuleHandleEx ( GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                wszModName,
                                  &third_party_dlls.overlays.rtss_hooks );

          if (config.compatibility.rehook_loadlibrary) {
            SK_ReHookLoadLibrary ();
            Sleep (16);
          }
        }

        else if ( (! third_party_dlls.overlays.steam_overlay) &&
                   StrStrIW (wszModName, L"gameoverlayrenderer") ) {
          // Hold a reference to this DLL so it is not unloaded prematurely
          GetModuleHandleEx ( GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                wszModName,
                                  &third_party_dlls.overlays.steam_overlay );

          if (config.compatibility.rehook_loadlibrary) {
            SK_ReHookLoadLibrary ();
            Sleep (16);
          }
        }

        else if ( (! third_party_dlls.misc.gedosato) &&
                   StrStrIW (wszModName, L"GeDoSaTo") ) {
          // Hold a reference to this DLL so it is not unloaded prematurely
          GetModuleHandleEx ( GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                wszModName,
                                  &third_party_dlls.misc.gedosato );

          if (config.compatibility.rehook_loadlibrary) {
            SK_ReHookLoadLibrary ();
            Sleep (16);
          }
        }

        else if ( StrStrIW (wszModName, L"ltc_help") && (! (config.compatibility.ignore_raptr || config.compatibility.disable_raptr)) ) {
          static bool warned = false;

          // When Raptr's in full effect, it has its own overlay plus PlaysTV ...
          //   only warn about ONE of these things!
          if (! warned) {
            warned = true;

            CreateThread ( nullptr, 0, SK_RaptrWarn, nullptr, 0x00, nullptr );
          }
        }

        if (when == SK_ModuleEnum::PostLoad)
        {
          if (SK_IsInjected ())
          {
            if ( config.apis.OpenGL.hook && StrStrIW (wszModName, L"opengl32.dll") && (SK_IsInjected () || (! (SK_GetDLLRole () & DLL_ROLE::OpenGL)))) {
              SK_BootOpenGL ();

              loaded_gl = true;
            }

            else if ( config.apis.Vulkan.hook && StrStrIW (wszModName, L"vulkan-1.dll") && (SK_IsInjected () || (! (SK_GetDLLRole () & DLL_ROLE::Vulkan)))) {
              SK_BootVulkan ();
            
              loaded_vulkan = true;
            }

            //else if ( config.apis.dxgi.d3d11.hook && StrStrIW (wszModName, L"\\dxgi.dll") && (SK_IsInjected () || (! (SK_GetDLLRole () & DLL_ROLE::DXGI)))) {
            //  SK_BootDXGI ();
            //
            //  loaded_dxgi = true;
            //}

            else if ( config.apis.dxgi.d3d11.hook && StrStrIW (wszModName, L"d3d11.dll") && (SK_IsInjected () || (! (SK_GetDLLRole () & DLL_ROLE::DXGI)))) {
              SK_BootDXGI ();
            
              loaded_dxgi = true;
            }

            else if ( config.apis.dxgi.d3d12.hook && StrStrIW (wszModName, L"d3d12.dll") && (SK_IsInjected () || (! (SK_GetDLLRole () & DLL_ROLE::DXGI)))) {
              SK_BootDXGI ();
            
              loaded_dxgi = true;
            }

            else if ( config.apis.d3d9.hook && StrStrIW (wszModName, L"d3d9.dll") && (SK_IsInjected () || (! (SK_GetDLLRole () & DLL_ROLE::D3D9)))) {
              SK_BootD3D9 ();
            
              loaded_d3d9 = true;
            }

            else if ( config.apis.d3d8.hook && StrStrIW (wszModName, L"d3d8.dll") && (SK_IsInjected () || (! (SK_GetDLLRole () & DLL_ROLE::D3D8)))) {
              SK_BootD3D8 ();
            
              loaded_d3d8 = true;
            }

            else if ( config.apis.ddraw.hook && StrStrIW (wszModName, L"\\ddraw.dll") && (SK_IsInjected () || (! (SK_GetDLLRole () & DLL_ROLE::DDraw)))) {
              SK_BootDDraw ();
            
              loaded_ddraw = true;
            }
          }
        }

        if (! config.steam.silent)
        {
          if ( StrStrIW (wszModName, wszSteamAPIDLL)    ||
               StrStrIW (wszModName, wszSteamNativeDLL) ||
               StrStrIW (wszModName, wszSteamClientDLL) ) {
            SK_HookSteamAPI ();
          }
        }
      }
    }

    __except ( (GetExceptionCode () == EXCEPTION_INVALID_HANDLE) ?
                           EXCEPTION_EXECUTE_HANDLER :
                           EXCEPTION_CONTINUE_SEARCH  )
    {
      // Sometimes a DLL will be unloaded in the middle of doing this... just ignore that.
    }
  }

  SK_UnlockDllLoader ();
}

void
__stdcall
SK_EnumLoadedModules (SK_ModuleEnum when)
{
  // Begin logging new loads after this
  SK_LoadLibrary_SILENCE = false;

  static iSK_Logger*
               pLogger  = SK_CreateLog (L"logs/modules.log");
  DWORD        dwProcID = GetCurrentProcessId ();

  HMODULE      hMods [1024] = { 0 };
  HANDLE       hProc        = nullptr;
  DWORD        cbNeeded     =   0;

  // Get a handle to the process.
  hProc = OpenProcess ( PROCESS_QUERY_INFORMATION |
                        PROCESS_VM_READ,
                          FALSE,
                            dwProcID );

  if (hProc == nullptr && (when != SK_ModuleEnum::PreLoad))
  {
    pLogger->close ();
    delete pLogger;
    return;
  }

  if ( when   == SK_ModuleEnum::PostLoad &&
      pLogger != nullptr )
  {
    pLogger->LogEx (
      false,
        L"================================================================== "
        L"(End Preloads) "
        L"==================================================================\n"
    );
  }


  if (hProc == nullptr)
    return;

  if ( EnumProcessModules ( hProc,
                              hMods,
                                sizeof (hMods),
                                  &cbNeeded) )
  {
    enum_working_set_s working_set;

            working_set.proc   = hProc;
            working_set.logger = pLogger;
            working_set.count  = cbNeeded / sizeof HMODULE;
    memcpy (working_set.modules, hMods, cbNeeded);

    enum_working_set_s* pWorkingSet = (enum_working_set_s *)&working_set;
    SK_ThreadWalkModules (pWorkingSet);

    //pLogger->close ();
    //delete pLogger;

    SK_WalkModules (cbNeeded, hProc, hMods, when);
  }

  if (third_party_dlls.overlays.rtss_hooks != nullptr) {
    SK_ValidateGlobalRTSSProfile ();
  }

  // In 64-bit builds, RTSS is really sneaky :-/
  else if (SK_GetRTSSInstallDir ().length ()) {
    SK_ValidateGlobalRTSSProfile ();
  }

#ifdef _WIN64
  else if ( GetModuleHandle (L"RTSSHooks64.dll") )
#else
  else if ( GetModuleHandle (L"RTSSHooks.dll") )
#endif
  {
    SK_ValidateGlobalRTSSProfile ();
    // RTSS is in High App Detection or Stealth Mode
    //
    //   The software is probably going to crash.
    dll_log.Log ( L"[RTSSCompat] RTSS appears to be in High App Detection or Stealth mode, "
                  L"your game is probably going to crash." );
  }
}


#include <Commctrl.h>
#include <comdef.h>

extern volatile LONG SK_bypass_dialog_active;
                HWND SK_bypass_dialog_hwnd;

HRESULT
CALLBACK
TaskDialogCallback (
  _In_ HWND     hWnd,
  _In_ UINT     uNotification,
  _In_ WPARAM   wParam,
  _In_ LPARAM   lParam,
  _In_ LONG_PTR dwRefData
)
{
  UNREFERENCED_PARAMETER (dwRefData);
  UNREFERENCED_PARAMETER (wParam);

  if (uNotification == TDN_TIMER)
    SK_RealizeForegroundWindow (SK_bypass_dialog_hwnd);

  if (uNotification == TDN_HYPERLINK_CLICKED)
  {
    ShellExecuteW (nullptr, L"open", (wchar_t *)lParam, nullptr, nullptr, SW_SHOW);
    return S_OK;
  }

  if (uNotification == TDN_DIALOG_CONSTRUCTED)
  {
    while (InterlockedCompareExchange (&SK_bypass_dialog_active, 0, 0) > 1)
      Sleep (10);

    SK_bypass_dialog_hwnd = hWnd;

    InterlockedIncrementAcquire (&SK_bypass_dialog_active);
  }

  if (uNotification == TDN_CREATED)
    SK_bypass_dialog_hwnd = hWnd;

  if (uNotification == TDN_DESTROYED) {
    SK_bypass_dialog_hwnd = 0;
    InterlockedDecrementRelease (&SK_bypass_dialog_active);
  }

  return S_OK;
}

#include <SpecialK/config.h>
#include <SpecialK/ini.h>

BOOL
__stdcall
SK_ValidateGlobalRTSSProfile (void)
{
  if (config.system.ignore_rtss_delay)
    return TRUE;

  wchar_t wszRTSSHooks [MAX_PATH + 2] = { L'\0' };

  if (third_party_dlls.overlays.rtss_hooks) {
    GetModuleFileNameW (
      third_party_dlls.overlays.rtss_hooks,
        wszRTSSHooks,
          MAX_PATH );

    wchar_t* pwszShortName = wszRTSSHooks + lstrlenW (wszRTSSHooks);

    while (  pwszShortName      >  wszRTSSHooks &&
           *(pwszShortName - 1) != L'\\')
      --pwszShortName;

    *(pwszShortName - 1) = L'\0';
  } else {
    wcscpy (wszRTSSHooks, SK_GetRTSSInstallDir ().c_str ());
  }

  lstrcatW (wszRTSSHooks, L"\\Profiles\\Global");


  iSK_INI rtss_global (wszRTSSHooks);

  rtss_global.parse ();

  iSK_INISection& rtss_hooking =
    rtss_global.get_section (L"Hooking");


  bool valid = true;


  if ( (! rtss_hooking.contains_key (L"InjectionDelay")) ) {
    rtss_hooking.add_key_value (L"InjectionDelay", L"10000");
    valid = false;
  }
  else if (_wtol (rtss_hooking.get_value (L"InjectionDelay").c_str()) < 10000) {
    rtss_hooking.get_value (L"InjectionDelay") = L"10000";
    valid = false;
  }


  if ( (! rtss_hooking.contains_key (L"InjectionDelayTriggers")) ) {
    rtss_hooking.add_key_value (
      L"InjectionDelayTriggers",
        L"SpecialK32.dll,d3d9.dll,steam_api.dll,steam_api64.dll,dxgi.dll,SpecialK64.dll"
    );
    valid = false;
  }

  else {
    std::wstring& triggers =
      rtss_hooking.get_value (L"InjectionDelayTriggers");

    const wchar_t* delay_dlls [] = { L"SpecialK32.dll",
                                     L"d3d9.dll",
                                     L"steam_api.dll",
                                     L"steam_api64.dll",
                                     L"dxgi.dll",
                                     L"SpecialK64.dll" };

    const int     num_delay_dlls =
      sizeof (delay_dlls) / sizeof (const wchar_t *);

    for (int i = 0; i < num_delay_dlls; i++) {
      if (triggers.find (delay_dlls [i]) == std::wstring::npos) {
        valid = false;
        triggers += L",";
        triggers += delay_dlls [i];
      }
    }
  }

  // No action is necessary, delay triggers are working as intended.
  if (valid)
    return TRUE;

  static BOOL warned = FALSE;

  // Prevent the dialog from repeatedly popping up if the user decides to ignore
  if (warned)
    return TRUE;

  TASKDIALOGCONFIG task_config    = {0};

  task_config.cbSize              = sizeof (task_config);
  task_config.hInstance           = SK_GetDLL ();
  task_config.hwndParent          = GetActiveWindow ();
  task_config.pszWindowTitle      = L"Special K Compatibility Layer";
  task_config.dwCommonButtons     = TDCBF_OK_BUTTON;
  task_config.pButtons            = nullptr;
  task_config.cButtons            = 0;
  task_config.dwFlags             = TDF_ENABLE_HYPERLINKS;
  task_config.pfCallback          = TaskDialogCallback;
  task_config.lpCallbackData      = 0;

  task_config.pszMainInstruction  = L"RivaTuner Statistics Server Incompatibility";

  wchar_t wszFooter [1024];

  // Delay triggers are invalid, but we can do nothing about it due to
  //   privilige issues.
  if (! SK_IsAdmin ()) {
    task_config.pszMainIcon        = TD_WARNING_ICON;
    task_config.pszContent         = L"RivaTuner Statistics Server requires a 10 second injection delay to workaround "
                                     L"compatibility issues.";

    task_config.pszFooterIcon      = TD_SHIELD_ICON;
    task_config.pszFooter          = L"This can be fixed by starting the game as Admin once.";

    task_config.pszVerificationText = L"Check here if you do not care (risky).";

    BOOL verified;

    TaskDialogIndirect (&task_config, nullptr, nullptr, &verified);

    if (verified)
      config.system.ignore_rtss_delay = true;
    else
      ExitProcess (0);
  } else {
    task_config.pszMainIcon        = TD_INFORMATION_ICON;

    task_config.pszContent         = L"RivaTuner Statistics Server requires a 10 second injection delay to workaround "
                                     L"compatibility issues.";

    task_config.dwCommonButtons    = TDCBF_YES_BUTTON | TDCBF_NO_BUTTON;
    task_config.nDefaultButton     = IDNO;

    wsprintf ( wszFooter,

                L"\r\n\r\n"

                L"Proposed Changes\r\n\r\n"

                L"<A HREF=\"%s\">%s</A>\r\n\r\n"

                L"[Hooking]\r\n"
                L"InjectionDelay=%s\r\n"
                L"InjectionDelayTriggers=%s",

                  wszRTSSHooks, wszRTSSHooks,
                    rtss_global.get_section (L"Hooking").get_value (L"InjectionDelay").c_str (),
                      rtss_global.get_section (L"Hooking").get_value (L"InjectionDelayTriggers").c_str () );

    task_config.pszExpandedInformation = wszFooter;
    task_config.pszExpandedControlText = L"Apply Proposed Config Changes?";

    int nButton;

    TaskDialogIndirect (&task_config, &nButton, nullptr, nullptr);

    if (nButton == IDYES) {
      // Delay triggers are invalid, and we are going to fix them...
      dll_log.Log ( L"[RTSSCompat] NEW Global Profile:  InjectDelay=%s,  DelayTriggers=%s",
                      rtss_global.get_section (L"Hooking").get_value (L"InjectionDelay").c_str (),
                        rtss_global.get_section (L"Hooking").get_value (L"InjectionDelayTriggers").c_str () );

      rtss_global.write  (wszRTSSHooks);

      STARTUPINFO         sinfo = { 0 };
      PROCESS_INFORMATION pinfo = { 0 };

      sinfo.cb          = sizeof STARTUPINFO;
      sinfo.dwFlags     = STARTF_USESHOWWINDOW | STARTF_RUNFULLSCREEN;
      sinfo.wShowWindow = SW_SHOWNORMAL;

      CreateProcess (
        nullptr,
          (LPWSTR)SK_GetHostApp (),
            nullptr, nullptr,
              TRUE,
                CREATE_SUSPENDED,
                  nullptr, nullptr,
                    &sinfo, &pinfo );

      while (ResumeThread (pinfo.hThread))
        ;

      CloseHandle  (pinfo.hThread);
      CloseHandle  (pinfo.hProcess);

      SK_TerminateParentProcess (0x00);
    }
  }

  warned = TRUE;

  return TRUE;
}

HRESULT
__stdcall
SK_TaskBoxWithConfirm ( wchar_t* wszMainInstruction,
                        PCWSTR   wszMainIcon,
                        wchar_t* wszContent,
                        wchar_t* wszConfirmation,
                        wchar_t* wszFooter,
                        PCWSTR   wszFooterIcon,
                        wchar_t* wszVerifyText,
                        BOOL*    verify )
{
  bool timer = true;

  int              nButtonPressed = 0;
  TASKDIALOGCONFIG task_config    = {0};

  task_config.cbSize              = sizeof (task_config);
  task_config.hInstance           = SK_GetDLL ();
  task_config.hwndParent          = GetActiveWindow ();
  task_config.pszWindowTitle      = L"Special K Compatibility Layer";
  task_config.dwCommonButtons     = TDCBF_OK_BUTTON;
  task_config.pButtons            = nullptr;
  task_config.cButtons            = 0;
  task_config.dwFlags             = 0x00;
  task_config.pfCallback          = TaskDialogCallback;
  task_config.lpCallbackData      = 0;

  task_config.pszMainInstruction  = wszMainInstruction;

  task_config.pszMainIcon         = wszMainIcon;
  task_config.pszContent          = wszContent;

  task_config.pszFooterIcon       = wszFooterIcon;
  task_config.pszFooter           = wszFooter;

  task_config.pszVerificationText = wszVerifyText;

  if (verify != nullptr && *verify)
    task_config.dwFlags |= TDF_VERIFICATION_FLAG_CHECKED;

  if (timer)
    task_config.dwFlags |= TDF_CALLBACK_TIMER;

  HRESULT hr =
    TaskDialogIndirect ( &task_config,
                          &nButtonPressed,
                            nullptr,
                              verify );

  return hr;
}

HRESULT
__stdcall
SK_TaskBoxWithConfirmEx ( wchar_t* wszMainInstruction,
                          PCWSTR   wszMainIcon,
                          wchar_t* wszContent,
                          wchar_t* wszConfirmation,
                          wchar_t* wszFooter,
                          PCWSTR   wszFooterIcon,
                          wchar_t* wszVerifyText,
                          BOOL*    verify,
                          wchar_t* wszCommand )
{
  bool timer = true;

  int              nButtonPressed =   0;
  TASKDIALOGCONFIG task_config    = { 0 };

  task_config.cbSize              = sizeof    (task_config);
  task_config.hInstance           = SK_GetDLL ();
  task_config.pszWindowTitle      = L"Special K Compatibility Layer";
  task_config.dwCommonButtons     = TDCBF_OK_BUTTON;

  TASKDIALOG_BUTTON button;
  button.nButtonID               = 0xdead01ae;
  button.pszButtonText           = wszCommand;

  task_config.pButtons           = &button;
  task_config.cButtons           = 1;

  task_config.dwFlags            = 0x00;
  task_config.dwFlags           |= TDF_USE_COMMAND_LINKS | TDF_SIZE_TO_CONTENT |
                                   TDF_POSITION_RELATIVE_TO_WINDOW;

  task_config.pfCallback         = TaskDialogCallback;
  task_config.lpCallbackData     = 0;

  task_config.pszMainInstruction = wszMainInstruction;

  task_config.hwndParent         = GetActiveWindow ();

  task_config.pszMainIcon        = wszMainIcon;
  task_config.pszContent         = wszContent;

  task_config.pszFooterIcon      = wszFooterIcon;
  task_config.pszFooter          = wszFooter;

  task_config.pszVerificationText = wszVerifyText;

  if (verify != nullptr && *verify)
    task_config.dwFlags |= TDF_VERIFICATION_FLAG_CHECKED;

  if (timer)
    task_config.dwFlags |= TDF_CALLBACK_TIMER;

  HRESULT hr =
    TaskDialogIndirect ( &task_config,
                          &nButtonPressed,
                            nullptr,
                              verify );

  if (nButtonPressed == 0xdead01ae)
    config.compatibility.disable_raptr = true;

  return hr;
}

enum {
  SK_BYPASS_UNKNOWN    = 0x0,
  SK_BYPASS_ACTIVATE   = 0x1,
  SK_BYPASS_DEACTIVATE = 0x2
};

volatile LONG SK_BypassResult = SK_BYPASS_UNKNOWN;

DWORD
WINAPI
SK_RaptrWarn (LPVOID user)
{
  UNREFERENCED_PARAMETER (user);

  // Don't check for Raptr while installing something...
  if (SK_IsHostAppSKIM ()) {
    CloseHandle (GetCurrentThread ());
    return 0;
  }

  HRESULT
  __stdcall
  SK_TaskBoxWithConfirmEx ( wchar_t* wszMainInstruction,
                            PCWSTR   wszMainIcon,
                            wchar_t* wszContent,
                            wchar_t* wszConfirmation,
                            wchar_t* wszFooter,
                            PCWSTR   wszFooterIcon,
                            wchar_t* wszVerifyText,
                            BOOL*    verify,
                            wchar_t* wszCommand );

  SK_TaskBoxWithConfirmEx ( L"AMD Gaming Evolved or Raptr is running",
                            TD_WARNING_ICON,
                            L"In some software you can expect weird things to happen, including"
                            L" the game mysteriously disappearing.\n\n"
                            L"If the game behaves strangely, you may need to disable it.",
                            nullptr,
                            nullptr,
                            nullptr,
                            L"Check here to ignore this warning in the future.",
                            (BOOL *)&config.compatibility.ignore_raptr,
                            L"Disable Raptr / Plays.TV\n\n"
                            L"Special K will disable it (for this game)." );

  CloseHandle (GetCurrentThread ());

  return 0;
}

struct sk_bypass_s {
  BOOL    disable;
  wchar_t wszBlacklist [MAX_PATH];
} __bypass;

DWORD
WINAPI
SK_Bypass_CRT (LPVOID user)
{
  UNREFERENCED_PARAMETER (user);

  static BOOL     disable      = __bypass.disable;
         wchar_t* wszBlacklist = __bypass.wszBlacklist;

  bool timer = true;

  int              nButtonPressed = 0;
  TASKDIALOGCONFIG task_config    = {0};

  const TASKDIALOG_BUTTON buttons [] = {  { 0, L"Auto-Detect"   },
                                          { 6, L"Direct3D8"     },
                                          { 7, L"DirectDraw"    },
                                          { 1, L"Direct3D9{Ex}" },
                                          { 2, L"Direct3D11"    },
                                          { 3, L"Direct3D12"    },
                                          { 4, L"OpenGL"        },
                                          { 5, L"Vulkan"        }
                                       };

  task_config.cbSize              = sizeof (task_config);
  task_config.hInstance           = SK_GetDLL ();
  task_config.hwndParent          = GetActiveWindow ();
  task_config.pszWindowTitle      = L"Special K Compatibility Layer";
  task_config.dwCommonButtons     = TDCBF_OK_BUTTON;
  task_config.pRadioButtons       = buttons;
  task_config.cRadioButtons       = ARRAYSIZE (buttons);
  task_config.pButtons            = nullptr;
  task_config.cButtons            = 0;
  task_config.dwFlags             = 0x00;
  task_config.pfCallback          = TaskDialogCallback;
  task_config.lpCallbackData      = 0;

  task_config.pszMainInstruction  = L"Special K Injection Compatibility Options";

  task_config.pszMainIcon         = TD_SHIELD_ICON;
  task_config.pszContent          = L"By pressing Ctrl + Shift at application start, you"
                          L" have opted into compatibility mode.\n\nUse the"
                          L" menu options provided to troubleshoot problems"
                          L" that may be caused by the mod.";

  task_config.pszFooterIcon       = TD_INFORMATION_ICON;
  task_config.pszFooter           = L"You can re-enable auto-injection at any time by "
                          L"holding Ctrl + Shift down at startup.";

  task_config.pszVerificationText = L"Check here to DISABLE Special K for this game.";

  task_config.dwFlags |= TDF_VERIFICATION_FLAG_CHECKED;

  if (timer)
    task_config.dwFlags |= TDF_CALLBACK_TIMER;

  int nRadioPressed = 0;

  HRESULT hr =
    TaskDialogIndirect ( &task_config,
                          &nButtonPressed,
                            &nRadioPressed,
                              &disable );

  SK_LoadConfig (L"SpecialK");

  if (SUCCEEDED (hr))
  {
    switch (nRadioPressed)
    {
      case 0:
        config.apis.d3d9.hook       = true;
        config.apis.d3d9ex.hook     = true;

        config.apis.dxgi.d3d11.hook = true;
        config.apis.dxgi.d3d12.hook = true;

        config.apis.OpenGL.hook     = true;
        config.apis.Vulkan.hook     = true;

        config.apis.d3d8.hook       = true;
        config.apis.ddraw.hook      = true;
        break;

      case 1:
        config.apis.d3d9.hook       = true;
        config.apis.d3d9ex.hook     = true;

        config.apis.dxgi.d3d11.hook = false;
        config.apis.dxgi.d3d12.hook = false;

        config.apis.OpenGL.hook     = false;
        config.apis.Vulkan.hook     = false;

        config.apis.d3d8.hook       = false;
        config.apis.ddraw.hook      = false;
        break;

      case 2:
        config.apis.d3d9.hook       = false;
        config.apis.d3d9ex.hook     = false;

        config.apis.dxgi.d3d11.hook = true;
        config.apis.dxgi.d3d12.hook = false;

        config.apis.OpenGL.hook     = false;
        config.apis.Vulkan.hook     = false;

        config.apis.d3d8.hook       = false;
        config.apis.ddraw.hook      = false;
        break;

      case 3:
        config.apis.d3d9.hook       = false;
        config.apis.d3d9ex.hook     = false;

        config.apis.dxgi.d3d11.hook = false;
        config.apis.dxgi.d3d12.hook = true;

        config.apis.OpenGL.hook     = false;
        config.apis.Vulkan.hook     = false;

        config.apis.d3d8.hook       = false;
        config.apis.ddraw.hook      = false;
        break;

      case 4:
        config.apis.d3d9.hook       = false;
        config.apis.d3d9ex.hook     = false;

        config.apis.dxgi.d3d11.hook = false;
        config.apis.dxgi.d3d12.hook = false;

        config.apis.OpenGL.hook     = true;
        config.apis.Vulkan.hook     = false;

        config.apis.d3d8.hook       = false;
        config.apis.ddraw.hook      = false;
        break;

      case 5:
        config.apis.d3d9.hook       = false;
        config.apis.d3d9ex.hook     = false;

        config.apis.dxgi.d3d11.hook = false;
        config.apis.dxgi.d3d12.hook = false;

        config.apis.OpenGL.hook     = false;
        config.apis.Vulkan.hook     = true;

        config.apis.d3d8.hook       = false;
        config.apis.ddraw.hook      = false;
        break;

      case 6:
        config.apis.d3d9.hook       = false;
        config.apis.d3d9ex.hook     = false;

        config.apis.dxgi.d3d11.hook = false;
        config.apis.dxgi.d3d12.hook = false;

        config.apis.OpenGL.hook     = false;
        config.apis.Vulkan.hook     = false;

        config.apis.d3d8.hook       = true;
        config.apis.ddraw.hook      = false;
        break;

      case 7:
        config.apis.d3d9.hook       = false;
        config.apis.d3d9ex.hook     = false;

        config.apis.dxgi.d3d11.hook = false;
        config.apis.dxgi.d3d12.hook = false;

        config.apis.OpenGL.hook     = false;
        config.apis.Vulkan.hook     = false;

        config.apis.d3d8.hook       = false;
        config.apis.ddraw.hook      = true;
        break;
    }

    extern iSK_INI* dll_ini;

    // TEMPORARY: There will be a function to disable plug-ins here, for now
    //              just disable ReShade.
#ifdef _WIN64
    dll_ini->remove_section (L"Import.ReShade64");
#else
    dll_ini->remove_section (L"Import.ReShade32");  
#endif

    SK_SaveConfig (L"SpecialK");


    if (disable) {
      FILE* fDeny = _wfopen (wszBlacklist, L"w");

      if (fDeny) {
        fputc  ('\0', fDeny);
        fflush (      fDeny);
        fclose (      fDeny);
      }

      InterlockedExchange (&SK_BypassResult, SK_BYPASS_ACTIVATE);
    } else {
      DeleteFileW (wszBlacklist);
      InterlockedExchange (&SK_BypassResult, SK_BYPASS_DEACTIVATE);
    }

    TerminateProcess (GetCurrentProcess (), 0x00);
  }

  InterlockedDecrement (&SK_bypass_dialog_active);

  if (disable != __bypass.disable)
  {
    STARTUPINFO         sinfo = { 0 };
    PROCESS_INFORMATION pinfo = { 0 };

    sinfo.cb          = sizeof STARTUPINFO;
    sinfo.dwFlags     = STARTF_USESHOWWINDOW | STARTF_RUNFULLSCREEN;
    sinfo.wShowWindow = SW_SHOWNORMAL;

    CreateProcess (
      nullptr,
        (LPWSTR)SK_GetHostApp (),
          nullptr, nullptr,
            TRUE,
              CREATE_SUSPENDED,
                nullptr, nullptr,
                  &sinfo, &pinfo );

    ResumeThread     (pinfo.hThread);
    CloseHandle      (pinfo.hThread);
    CloseHandle      (pinfo.hProcess);

    SK_TerminateParentProcess (0x00);
  }

  CloseHandle (GetCurrentThread ());
  ExitProcess (0);

//return 0;
}

#include <process.h>

std::pair <std::queue <DWORD>, BOOL>
__stdcall
SK_BypassInject (void)
{
  std::queue <DWORD> tids =
    SK_SuspendAllOtherThreads ();

  lstrcpyW (__bypass.wszBlacklist, SK_GetBlacklistFilename ());

  __bypass.disable = 
    (GetFileAttributesW (__bypass.wszBlacklist) != INVALID_FILE_ATTRIBUTES);

  IsGUIThread (TRUE);

  InterlockedIncrement (&SK_bypass_dialog_active);

  CreateThread ( nullptr,
                   0,
                     SK_Bypass_CRT, nullptr,
                       0x00,
                         nullptr );

  return std::make_pair (tids, __bypass.disable);
}












void
__stdcall
SK_PreInitLoadLibrary (void)
{
  FreeLibrary_Original    = &FreeLibrary;
  LoadLibraryA_Original   = &LoadLibraryA;
  LoadLibraryW_Original   = &LoadLibraryW;
  LoadLibraryExA_Original = &LoadLibraryExA;
  LoadLibraryExW_Original = &LoadLibraryExW;
}

FreeLibrary_pfn    FreeLibrary_Original    = nullptr;

LoadLibraryA_pfn   LoadLibraryA_Original   = nullptr;
LoadLibraryW_pfn   LoadLibraryW_Original   = nullptr;

LoadLibraryExA_pfn LoadLibraryExA_Original = nullptr;
LoadLibraryExW_pfn LoadLibraryExW_Original = nullptr;