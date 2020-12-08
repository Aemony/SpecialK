﻿/**
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

#include <SpecialK/stdafx.h>

int
SK_MessageBox (std::wstring caption, std::wstring title, uint32_t flags)
{
  return
    MessageBox (nullptr, caption.c_str (), title.c_str (),
                flags | MB_SYSTEMMODAL | MB_TOPMOST | MB_SETFOREGROUND);
}

std::string
SK_WideCharToUTF8 (const std::wstring& in)
{
  size_t len =
    WideCharToMultiByte ( CP_UTF8, 0x00, in.c_str (), -1,
                           nullptr, 0, nullptr, FALSE );

  std::string out (
    len * 2 + 2,
      '\0'
  );

  WideCharToMultiByte   ( CP_UTF8, 0x00,          in.c_str  (),
                          gsl::narrow_cast <int> (in.length ()),
                                                 out.data   (),
                          gsl::narrow_cast <DWORD>       (len),
                            nullptr,                   FALSE );

  return out;
}

std::wstring
SK_UTF8ToWideChar (const std::string& in)
{
  size_t len =
    MultiByteToWideChar ( CP_UTF8, 0x00, in.c_str (), -1,
                           nullptr, 0 );

  std::wstring out (
    len * 2 + 2,
      L'\0'
  );

  MultiByteToWideChar   ( CP_UTF8, 0x00,          in.c_str  (),
                          gsl::narrow_cast <int> (in.length ()),
                                                 out.data   (),
                          gsl::narrow_cast <DWORD>       (len) );

  return out;
}

bool
SK_COM_TestInit (void)
{
  SK_AutoHandle hToken (INVALID_HANDLE_VALUE);
  wchar_t*      str    = nullptr;

  if (! OpenProcessToken (
          SK_GetCurrentProcess (), TOKEN_QUERY | TOKEN_IMPERSONATE |
                                   TOKEN_READ, &hToken.m_h )
     )
  {
    return false;
  }

  HRESULT hr =
    SHGetKnownFolderPath (FOLDERID_Documents, 0, hToken, &str);

  if (SUCCEEDED (hr))
  {
    CoTaskMemFree (str);

    return true;
  }

  return false;
}

HRESULT
SK_Shell32_GetKnownFolderPath ( _In_ REFKNOWNFOLDERID rfid,
                                     std::wstring&     dir,
                            volatile LONG*             _RunOnce )
{
  // Use the current directory if COM or permissions are mucking things up
  auto _FailFastAndDie =
  [&] (void)->HRESULT
  {
    wchar_t wszCurrentDir[MAX_PATH + 2] = { };
    GetCurrentDirectoryW (MAX_PATH, wszCurrentDir);

    dir = wszCurrentDir;

    InterlockedIncrementRelease (_RunOnce);

    return E_ACCESSDENIED;
  };

  auto _TrySHGetKnownFolderPath =
    [&](HANDLE hToken, wchar_t** ppStr)->HRESULT
  {
    HRESULT try_result =
      SHGetKnownFolderPath (rfid, 0, hToken, ppStr);

    if (SUCCEEDED (try_result))
    {
      dir = *ppStr;

      CoTaskMemFree (*ppStr);

      InterlockedIncrementRelease (_RunOnce);
    }

    return try_result;
  };


  HRESULT hr =
    S_OK;

  if (! InterlockedCompareExchangeAcquire (_RunOnce, 1, 0))
  {
    SK_AutoHandle hToken (INVALID_HANDLE_VALUE);
    wchar_t*      str    = nullptr;

    if (! OpenProcessToken (
            SK_GetCurrentProcess (), TOKEN_QUERY | TOKEN_IMPERSONATE |
                                     TOKEN_READ, &hToken.m_h )
       )
    {
      return
        _FailFastAndDie ();
    }

    hr =
      _TrySHGetKnownFolderPath (hToken.m_h, &str);

    // Second chance
    if (FAILED (hr))
    {
      SK_AutoCOMInit _com_base;

      hr =
        _TrySHGetKnownFolderPath (hToken.m_h, &str);
    }

    //We're @#$%'d
    if (FAILED (hr))
      return _FailFastAndDie ();
  }

  return hr;
}

std::wstring&
SK_GetDocumentsDir (void)
{
  static volatile LONG __init = 0;

  // Fast Path  (cached)
  //
  static std::wstring dir;

  if (ReadAcquire (&__init) == 2)
  {
    if (! dir.empty ())
      return dir;
  }

  HRESULT hr =
    SK_Shell32_GetKnownFolderPath (FOLDERID_Documents, dir, &__init);

  if (FAILED (hr))
  {
    SK_LOG0 ( ( L"ERROR: Could not get User's Documents Directory!  [HRESULT=%x]",
                  hr ),
                L" SpecialK " );
  }

  SK_Thread_SpinUntilAtomicMin (&__init, 2);

  return dir;
}

std::wstring&
SK_GetRoamingDir (void)
{
  static volatile LONG __init = 0;

  // Fast Path  (cached)
  //
  static std::wstring dir;

  if (ReadAcquire (&__init) == 2)
  {
    if (! dir.empty ())
      return dir;
  }

  HRESULT hr =
    SK_Shell32_GetKnownFolderPath (FOLDERID_RoamingAppData, dir, &__init);

  if (FAILED (hr))
  {
    SK_LOG0 ( ( L"ERROR: Could not get User's Roaming Directory!  [HRESULT=%x]",
                  hr ),
                L" SpecialK " );
  }

  SK_Thread_SpinUntilAtomicMin (&__init, 2);

  return dir;
}

std::wstring
SK_GetFontsDir (void)
{
  static volatile LONG __init = 0;

  // Fast Path  (cached)
  //
  static std::wstring dir;

  if (ReadAcquire (&__init) == 2)
  {
    if (! dir.empty ())
      return dir;
  }

  HRESULT hr =
    SK_Shell32_GetKnownFolderPath (FOLDERID_Fonts, dir, &__init);

  if (FAILED (hr))
  {
    SK_LOG0 ( ( L"ERROR: Could not get Font Directory!  [HRESULT=%x]",
                  hr ),
                L" SpecialK " );
  }

  SK_Thread_SpinUntilAtomicMin (&__init, 2);

  return dir;
}

bool
SK_GetDocumentsDir (wchar_t* buf, uint32_t* pdwLen)
{
  const std::wstring& docs =
    SK_GetDocumentsDir ();

  if (buf != nullptr)
  {
    if (pdwLen != nullptr && *pdwLen > 0)
    {
      *buf = '\0';

      if (docs.empty ())
      {
        *pdwLen = 0;
        return false;
      }

      wcsncat (buf, docs.c_str (), *pdwLen);

      *pdwLen =
        gsl::narrow_cast <uint32_t> (wcslen (buf));

      return true;
    }
  }

  return false;
}

bool
SK_GetUserProfileDir (wchar_t* buf, uint32_t* pdwLen)
{
  using GetUserProfileDirectoryW_pfn =
    BOOL (WINAPI *)(HANDLE, LPWSTR, LPDWORD);

  static auto      hModUserEnv =
    SK_LoadLibraryW (L"USERENV.DLL");

  static auto _GetUserProfileDirectoryW =
              (GetUserProfileDirectoryW_pfn)
            SK_GetProcAddress ( hModUserEnv,
              "GetUserProfileDirectoryW" );

  if (! _GetUserProfileDirectoryW)
    return false;

  SK_AutoHandle hToken (
    INVALID_HANDLE_VALUE
  );

  if (! OpenProcessToken (SK_GetCurrentProcess (), TOKEN_READ, &hToken.m_h))
    return false;

  if (! _GetUserProfileDirectoryW ( hToken, buf,
                                      reinterpret_cast <DWORD *> (pdwLen)
                                  )
     )
  {
    return false;
  }

  return true;
}

bool
__stdcall
SK_CreateDirectoriesEx ( const wchar_t* wszPath, bool strip_filespec )
{
    wchar_t   wszDirPath [MAX_PATH + 2] = { };
  wcsncpy_s ( wszDirPath, MAX_PATH,
                wszPath, _TRUNCATE );

  wchar_t* wszTest =         wszDirPath;
  size_t   len     = wcslen (wszDirPath);
  for (size_t
            i = 0; i <   len; i++)
  {
    wszTest =
      CharNextW (wszTest);
  }

  if ( *wszTest == L'\\' ||
       *wszTest == L'/'     )
  {
    strip_filespec = false;
  }

  if (strip_filespec)
  {
    PathRemoveFileSpecW (wszDirPath);
    lstrcatW            (wszDirPath, LR"(\)");
  }

  // If the final path already exists, well... there's no work to be done, so
  //   don't do that crazy loop of crap below and just abort early !
  if (GetFileAttributesW (wszDirPath) != INVALID_FILE_ATTRIBUTES)
    return true;


  wchar_t     wszSubDir [MAX_PATH + 2] = { };
  wcsncpy_s ( wszSubDir, MAX_PATH,
                wszDirPath, _TRUNCATE );

  wchar_t* iter;
  wchar_t* wszLastSlash     = wcsrchr (wszSubDir, L'/');
  wchar_t* wszLastBackslash = wcsrchr (wszSubDir, L'\\');

  if (wszLastSlash > wszLastBackslash)
    *wszLastSlash     = L'\0';
  else if (wszLastBackslash != nullptr)
    *wszLastBackslash = L'\0';
  else
    return false;

  for ( iter  = wszSubDir;
       *iter != L'\0';
        iter  = CharNextW (iter) )
  {
    if (*iter == L'\\' || *iter == L'/')
    {
      *iter = L'\0';

      if (GetFileAttributes (wszDirPath) == INVALID_FILE_ATTRIBUTES)
        CreateDirectoryW (wszSubDir, nullptr);

      *iter = L'\\';
    }
  }

  // The final subdirectory (FULL PATH)
  if (GetFileAttributes (wszDirPath) == INVALID_FILE_ATTRIBUTES)
    CreateDirectoryW (wszSubDir, nullptr);

  return true;
}

bool
__stdcall
SK_CreateDirectories ( const wchar_t* wszPath )
{
  return
    SK_CreateDirectoriesEx (wszPath, false);// true);
}
std::wstring
SK_EvalEnvironmentVars (const wchar_t* wszEvaluateMe)
{
  if (! wszEvaluateMe)
    return std::wstring ();

  wchar_t wszEvaluated [MAX_PATH * 4 + 2] = { };

  ExpandEnvironmentStringsW ( wszEvaluateMe,
                                wszEvaluated,
                                  MAX_PATH * 4 );

  return
    wszEvaluated;
}

bool
SK_IsTrue (const wchar_t* string)
{
  const wchar_t* pstr = string;

  if ( std::wstring (string).length () == 1 &&
                                 *pstr == L'1' )
    return true;

  if (std::wstring (string).length () != 4)
    return false;

  if (towlower (*pstr) != L't')
    return false;

  pstr = CharNextW (pstr);

  if (towlower (*pstr) != L'r')
    return false;

  pstr = CharNextW (pstr);

  if (towlower (*pstr) != L'u')
    return false;

  pstr = CharNextW (pstr);

  return
    towlower (*pstr) != L'e';
}

time_t
SK_Win32_FILETIME_to_time_t ( FILETIME const& ft)
{
  ULARGE_INTEGER ull;

  ull.LowPart  = ft.dwLowDateTime;
  ull.HighPart = ft.dwHighDateTime;

  return ull.QuadPart / 10000000ULL - 11644473600ULL;
}

bool
SK_File_IsDirectory (const wchar_t* wszPath)
{
  DWORD dwAttrib =
    GetFileAttributes (wszPath);

  return
    ( dwAttrib != INVALID_FILE_ATTRIBUTES &&
      dwAttrib  & FILE_ATTRIBUTE_DIRECTORY );
}

void
SK_File_MoveNoFail ( const wchar_t* wszOld, const wchar_t* wszNew )
{
  WIN32_FIND_DATA OldFileData  = { };
  HANDLE          hOldFind     =
    FindFirstFile (wszOld, &OldFileData);

  // Strip read-only if need be
  SK_File_SetNormalAttribs (wszNew);

  if (! MoveFileExW ( wszOld,
                        wszNew,
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED ) )
  {
    wchar_t wszTemp [MAX_PATH + 2] = { };
    GetTempFileNameW (SK_SYS_GetInstallPath ().c_str (), L"SKI", timeGetTime (), wszTemp);

    MoveFileExW ( wszNew, wszTemp, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED );
    MoveFileExW ( wszOld, wszNew,  MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED );
  }

  // Preserve file times
  if (hOldFind != INVALID_HANDLE_VALUE)
  {
    SK_AutoHandle hNewFile (
      CreateFile ( wszNew,
                     GENERIC_READ      | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE |
                                         FILE_SHARE_DELETE,
                         nullptr,
                           OPEN_EXISTING,
                             GetFileAttributes (wszNew),
                               nullptr ) );

    FindClose         (hOldFind);
    SetFileTime       ( hNewFile,
                          &OldFileData.ftCreationTime,
                            &OldFileData.ftLastAccessTime,
                              &OldFileData.ftLastWriteTime );
    SetFileAttributes (wszNew, OldFileData.dwFileAttributes);
  }
}

// Copies a file preserving file times
void
SK_File_FullCopy ( const wchar_t* from,
                   const wchar_t* to )
{
  // Strip Read-Only
  SK_File_SetNormalAttribs (to);

  DeleteFile (      to);
  CopyFile   (from, to, FALSE);

  WIN32_FIND_DATA FromFileData;
  HANDLE          hFrom =
    FindFirstFile (from, &FromFileData);

  SK_AutoHandle hTo (
    CreateFile ( to,
                   GENERIC_READ      | GENERIC_WRITE,
                     FILE_SHARE_READ | FILE_SHARE_WRITE |
                                       FILE_SHARE_DELETE,
                       nullptr,
                         OPEN_EXISTING,
                           GetFileAttributes (to),
                             nullptr )
  );

  // Here's where the magic happens, apply the attributes from the
  //   original file to the new one!
  SetFileTime ( hTo,
                  &FromFileData.ftCreationTime,
                    &FromFileData.ftLastAccessTime,
                      &FromFileData.ftLastWriteTime );

  FindClose   (hFrom);
}

//BOOL TakeOwnership (LPTSTR lpszOwnFile);

BOOL
SK_File_SetAttribs ( const wchar_t *file,
                             DWORD  dwAttribs )
{
  return
    SetFileAttributesW (
      file,
        dwAttribs );
}

BOOL
SK_File_ApplyAttribMask ( const wchar_t *file,
                                  DWORD  dwAttribMask,
                                   bool  clear )
{
  DWORD dwFileMask =
    GetFileAttributesW (file);

  if (clear)
    dwAttribMask = ( dwFileMask & ~dwAttribMask );

  else
    dwAttribMask = ( dwFileMask |  dwAttribMask );

  return
    SK_File_SetAttribs (file, dwAttribMask);
}

BOOL
SK_File_SetHidden (const wchar_t *file, bool hidden)
{
  return
    SK_File_ApplyAttribMask (file, FILE_ATTRIBUTE_HIDDEN, (! hidden));
}

BOOL
SK_File_SetTemporary (const wchar_t *file, bool temp)
{
  return
    SK_File_ApplyAttribMask (file, FILE_ATTRIBUTE_TEMPORARY, (! temp));
}


void
SK_File_SetNormalAttribs (const wchar_t *file)
{
  SK_File_SetAttribs (file, FILE_ATTRIBUTE_NORMAL);
}


bool
SK_IsAdmin (void)
{
  bool          bRet = false;
  SK_AutoHandle hToken (INVALID_HANDLE_VALUE);

  if ( OpenProcessToken ( SK_GetCurrentProcess (),
                            TOKEN_QUERY,
                              &hToken.m_h )
     )
  {
    TOKEN_ELEVATION Elevation = { };

    DWORD cbSize =
      sizeof (TOKEN_ELEVATION);

    if ( GetTokenInformation ( hToken,
                                 TokenElevation,
                                   &Elevation,
                                     sizeof (Elevation),
                                       &cbSize )
       )
    {
      bRet =
        ( Elevation.TokenIsElevated != 0 );
    }
  }

  return bRet;
}

bool
SK_IsProcessRunning (const wchar_t* wszProcName)
{
  PROCESSENTRY32W pe32 = { };

  SK_AutoHandle hProcSnap (
    CreateToolhelp32Snapshot ( TH32CS_SNAPPROCESS,
                                 0 )
  );

  if ((intptr_t)hProcSnap.m_h <= 0)
    return false;

  pe32.dwSize =
    sizeof (PROCESSENTRY32W);

  if (! Process32FirstW ( hProcSnap,
                            &pe32    )
     )
  {
    return false;
  }

  do
  {
    if (! SK_Path_wcsicmp ( wszProcName,
                              pe32.szExeFile )
       )
    {
      return true;
    }
  } while ( Process32NextW ( hProcSnap,
                               &pe32    )
          );

  return false;
}



typedef FARPROC (WINAPI *GetProcAddress_pfn)(HMODULE,LPCSTR);
                  extern GetProcAddress_pfn
                         GetProcAddress_Original;

FARPROC
WINAPI
SK_GetProcAddress (HMODULE hMod, const char* szFunc) noexcept
{
  FARPROC proc = nullptr;

  if (hMod != nullptr)
  {
    SK_SetLastError (NO_ERROR);

    if (GetProcAddress_Original != nullptr)
    {
      proc =
        GetProcAddress_Original (hMod, szFunc);

      if (GetLastError () == NO_ERROR)
        return proc;

      return nullptr;
    }

    proc =
      GetProcAddress (hMod, szFunc);

    if (GetLastError () == NO_ERROR)
      return proc;
  }

  return nullptr;
}

FARPROC
WINAPI
SK_GetProcAddress (const wchar_t* wszModule, const char* szFunc)
{
  HMODULE hMod =
    SK_GetModuleHandle (wszModule);

  if (hMod != nullptr)
  {
    return
      SK_GetProcAddress (hMod, szFunc);
  }

  return nullptr;
}

std::wstring
SK_GetModuleFullName (HMODULE hDll)
{
  wchar_t wszDllFullName [MAX_PATH + 2] = { };

  GetModuleFileName ( hDll,
                        wszDllFullName,
                          MAX_PATH );

  return wszDllFullName;
}

std::wstring
SK_GetModuleName (HMODULE hDll)
{
  wchar_t wszDllFullName [MAX_PATH + 2] = { };

  GetModuleFileName ( hDll,
                        wszDllFullName,
                          MAX_PATH );

  const wchar_t* wszShort =
    wcsrchr (wszDllFullName, L'\\');

  if (wszShort == nullptr)
    wszShort = wszDllFullName;
  else
    wszShort = CharNextW (wszShort);

  return wszShort;
}

HMODULE
SK_GetModuleFromAddr (LPCVOID addr) noexcept
{
  HMODULE hModOut = nullptr;

  if ( GetModuleHandleEx ( GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT |
                           GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                             (LPCWSTR)addr,
                               &hModOut
                         )
     )
  {
    return hModOut;
  }

  return
    static_cast <HMODULE> (INVALID_HANDLE_VALUE);
}

std::wstring
SK_GetModuleNameFromAddr (LPCVOID addr)
{
  HMODULE hModOut =
    SK_GetModuleFromAddr (addr);

  if (hModOut != INVALID_HANDLE_VALUE && (intptr_t)hModOut > 0)
    return SK_GetModuleName (hModOut);

  return
    L"#Invalid.dll#";
}

std::wstring
SK_GetModuleFullNameFromAddr (LPCVOID addr)
{
  HMODULE hModOut =
    SK_GetModuleFromAddr (addr);

  if (hModOut != INVALID_HANDLE_VALUE && (intptr_t)hModOut > 0)
    return SK_GetModuleFullName (hModOut);

  return
    L"#Extremely#Invalid.dll#";
}

std::wstring
SK_MakePrettyAddress (LPCVOID addr, DWORD /*dwFlags*/)
{
  return
    SK_FormatStringW ( L"( %s ) + %6xh",
      SK_ConcealUserDir ( SK_GetModuleFullNameFromAddr (addr).data ()),
                                             (uintptr_t)addr -
                       (uintptr_t)SK_GetModuleFromAddr (addr)
    );
}




struct SK_MemScan_Params__v0
{
  enum Privilege
  {
    Allowed    = true,
    Disallowed = false,
    DontCare   = (DWORD_PTR)-1
  };

  struct
  {
    Privilege execute = DontCare;
    Privilege read    = Allowed;
    Privilege write   = DontCare;
  } privileges;

  enum MemType
  {
    ImageCode  = SEC_IMAGE,
    FileData   = SEC_FILE,
    HeapMemory = SEC_COMMIT
  } mem_type;

  bool testPrivs (const MEMORY_BASIC_INFORMATION& mi)
  {
    if (mi.AllocationProtect == 0)
      return false;

    bool valid = true;


    if (privileges.execute != DontCare)
    {
      bool exec_matches = true;

      switch (mi.Protect)
      {
        case PAGE_EXECUTE:
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
          if (privileges.execute != Allowed)
            exec_matches = false;
          break;

        default:
          if (privileges.execute == Disallowed)
            exec_matches = false;
          break;
      }

      valid &= exec_matches;
    }


    if (privileges.read != DontCare)
    {
      bool read_matches = true;

      switch (mi.Protect)
      {
        case PAGE_READONLY:
        case PAGE_READWRITE:
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
          if (privileges.read != Allowed)
            read_matches = false;
          break;

        default:
          if (privileges.read == Disallowed)
            read_matches = false;
          break;
      }

      valid &= read_matches;
    }


    if (privileges.write != DontCare)
    {
      bool write_matches = true;

      switch (mi.Protect)
      {
        case PAGE_READWRITE:
        case PAGE_WRITECOPY:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
          if (privileges.write != Allowed)
            write_matches = false;
          break;

        default:
          if (privileges.write == Disallowed)
            write_matches = false;
          break;
      }

      valid &= write_matches;
    }

    return valid;
  }
};


bool
SK_ValidatePointer (LPCVOID addr, bool silent)
{
  MEMORY_BASIC_INFORMATION minfo = { };

  bool bFail = true;

  if (VirtualQuery (addr, &minfo, sizeof (minfo)))
  {
    bFail = false;

    if ((minfo.AllocationProtect == 0 ) ||
      (  minfo.Protect & PAGE_NOACCESS)   )
    {
      bFail = true;
    }
  }

  if (bFail && (! silent))
  {
    SK_LOG0 ( ( L"Address Validation for addr. %s FAILED!  --  %s",
                  SK_MakePrettyAddress (addr).c_str (),
                  SK_SummarizeCaller   (    ).c_str () ),
                L" SK Debug " );
  }

  return (! bFail);
}

bool
SK_IsAddressExecutable (LPCVOID addr, bool silent)
{
  MEMORY_BASIC_INFORMATION minfo = { };

  if (VirtualQuery (addr, &minfo, sizeof (minfo)))
  {
    if (SK_ValidatePointer (addr, silent))
    {
      static SK_MemScan_Params__v0 test_exec;

      SK_RunOnce(
        test_exec.privileges.execute = SK_MemScan_Params__v0::Allowed
      );

      if (test_exec.testPrivs (minfo))
      {
        return true;
      }
    }
  }

  if (! silent)
  {
    SK_LOG0 ( ( L"Executable Address Validation for addr. %s FAILED!  --  %s",
                  SK_MakePrettyAddress (addr).c_str (),
                  SK_SummarizeCaller   (    ).c_str () ),
                L" SK Debug " );
  }

  return false;
}

void
SK_LogSymbolName (LPCVOID addr)
{
  UNREFERENCED_PARAMETER (addr);

#ifdef _DEBUG
  char szSymbol [256] = { };

  SK_GetSymbolNameFromModuleAddr ( SK_GetModuleFromAddr (addr),
                                              (uintptr_t)addr,
                                                         szSymbol, 255 );

  SK_LOG0 ( ( L"=> %hs", szSymbol ), L"SymbolName" );
#endif
}


PROCESSENTRY32W
FindProcessByName (const wchar_t* wszName)
{
  PROCESSENTRY32W pe32 = { };

  SK_AutoHandle hProcessSnap (
    CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0)
  );

  if ((intptr_t)hProcessSnap.m_h <= 0)// == INVALID_HANDLE_VALUE)
    return pe32;

  pe32.dwSize = sizeof (PROCESSENTRY32W);

  if (! Process32FirstW (hProcessSnap, &pe32))
  {
    return pe32;
  }

  do
  {
    if (wcsstr (pe32.szExeFile, wszName))
      return pe32;
  } while (Process32NextW (hProcessSnap, &pe32));

  return pe32;
}

iSK_INI*
SK_GetDLLConfig (void)
{
  extern iSK_INI* dll_ini;
  return dll_ini;
}

iSK_INI*
SK_GetOSDConfig (void)
{
  extern iSK_INI* osd_ini;
  return osd_ini;
}


extern BOOL APIENTRY DllMain (HMODULE hModule,
                              DWORD   ul_reason_for_call,
                              LPVOID  /* lpReserved */);


void
__stdcall
SK_SelfDestruct (void) noexcept
{
  if (! InterlockedCompareExchange (&__SK_DLL_Ending, 1, 0))
  {
    SK_Detach (SK_GetDLLRole ());
  }
}

HMODULE
SK_GetCallingDLL (LPCVOID pReturn)
{
  HMODULE hCallingMod = nullptr;

  GetModuleHandleEx ( GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT |
                      GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                        static_cast <const wchar_t *> (pReturn),
                          &hCallingMod );

  return hCallingMod;
}

std::wstring
SK_GetCallerName (LPCVOID pReturn)
{
  return
    SK_GetModuleName (SK_GetCallingDLL (pReturn));
}

std::queue <DWORD>
SK_SuspendAllOtherThreads (void)
{
  std::queue <DWORD> threads;

  SK_AutoHandle hSnap (
    CreateToolhelp32Snapshot (TH32CS_SNAPTHREAD, 0)
  );

  if ((intptr_t)hSnap.m_h > 0)// != INVALID_HANDLE_VALUE)
  {
    THREADENTRY32 tent        = {                    };
                  tent.dwSize = sizeof (THREADENTRY32);

    if (Thread32First (hSnap, &tent))
    {
      //bool locked =
      //  dll_log.lock ();

      do
      {
        if ( tent.dwSize >= FIELD_OFFSET (THREADENTRY32, th32OwnerProcessID) +
                                  sizeof (tent.th32OwnerProcessID) )
        {
          if ( tent.th32ThreadID       != SK_Thread_GetCurrentId () &&
               tent.th32OwnerProcessID == GetCurrentProcessId    () )
          {
            SK_AutoHandle hThread (
              OpenThread (THREAD_SUSPEND_RESUME, FALSE, tent.th32ThreadID)
            );

            if ((intptr_t)hThread.m_h > 0)
            {
              threads.push  (tent.th32ThreadID);

              SuspendThread (hThread);
            }
          }
        }

        tent.dwSize = sizeof (tent);
      } while (Thread32Next (hSnap, &tent));

      //if (locked)
      //  dll_log.unlock ();
    }
  }

  return threads;
}

std::queue <DWORD>
SK_SuspendAllThreadsExcept (std::set <DWORD>& exempt_tids)
{
  std::queue <DWORD> threads;

  SK_AutoHandle hSnap (
    CreateToolhelp32Snapshot (TH32CS_SNAPTHREAD, 0)
  );

  if ((intptr_t)hSnap.m_h > 0)//!= INVALID_HANDLE_VALUE)
  {
    THREADENTRY32 tent        = {                    };
                  tent.dwSize = sizeof (THREADENTRY32);

    if (Thread32First (hSnap, &tent))
    {
      //bool locked =
      //  dll_log.lock ();

      do
      {
        if ( tent.dwSize >= FIELD_OFFSET (THREADENTRY32, th32OwnerProcessID) +
                                  sizeof (tent.th32OwnerProcessID) )
        {
          if ( (! exempt_tids.count (tent.th32ThreadID)) &&
                                     tent.th32ThreadID       != SK_Thread_GetCurrentId () &&
                                     tent.th32OwnerProcessID == GetCurrentProcessId    () )
          {
            SK_AutoHandle hThread (
              OpenThread (THREAD_SUSPEND_RESUME, FALSE, tent.th32ThreadID)
            );

            if ((intptr_t)hThread.m_h > 0)
            {
              threads.push  (tent.th32ThreadID);

              SuspendThread (hThread);
            }
          }
        }

        tent.dwSize = sizeof (tent);
      } while (Thread32Next (hSnap, &tent));

      //if (locked)
      //  dll_log.unlock ();
    }
  }

  return threads;
}

void
SK_ResumeThreads (std::queue <DWORD> threads)
{
  while (! threads.empty ())
  {
    DWORD tid = threads.front ();

    SK_AutoHandle hThread (
      OpenThread (THREAD_SUSPEND_RESUME, FALSE, tid)
    );

    if ((intptr_t)hThread.m_h > 0)
    {
      ResumeThread (hThread);
    }

    threads.pop ();
  }
}

class SK_PreHashed_String
{
public:
  template <size_t N>
  constexpr SK_PreHashed_String (const wchar_t (&wstr)[N]) :
                  hash_       { hash_function ( &wstr [0] ) },
                  size_       { N   -  1  },
                  wstrptr_    { &wstr [0] }
  { }

  auto operator== (const SK_PreHashed_String& s) const
  {
    return (
      ( size_ == s.size_ ) &&
        std::equal (c_str (),
                    c_str () + size_,
                  s.c_str () )
    );
  }
  auto operator!= (const SK_PreHashed_String& s) const
  {
    return
      ! (*this == s);
  }
  constexpr auto size     (void) const { return size_;    }
  constexpr auto get_hash (void) const { return hash_;    }
  constexpr auto c_str    (void) const ->
      const wchar_t*                   { return wstrptr_; }

private:
        size_t   hash_    {         };
        size_t   size_    {         };
  const wchar_t* wstrptr_ { nullptr };
};


extern
LPVOID SK_Debug_GetImageBaseAddr (void);

bool
SK_PE32_IsLargeAddressAware (void)
{
#ifdef _M_AMD64
  return true;
#else
  PIMAGE_NT_HEADERS pNtHdr   = nullptr;
  uintptr_t         pImgBase = (uintptr_t)
    SK_Debug_GetImageBaseAddr ();

  __try
  {
    pNtHdr   =
      PIMAGE_NT_HEADERS (
        pImgBase + PIMAGE_DOS_HEADER (pImgBase)->e_lfanew
      );

    assert (pNtHdr->FileHeader.Machine == IMAGE_FILE_MACHINE_I386);

    return
      ( pNtHdr->FileHeader.Characteristics &
          IMAGE_FILE_LARGE_ADDRESS_AWARE ) ==
          IMAGE_FILE_LARGE_ADDRESS_AWARE;
  }

  __except (EXCEPTION_EXECUTE_HANDLER)
  {
    // If the host has tampered with its header, we will assume
    //   it is unsafe to use addresses > 2 GiB.
    return false;
  }
#endif
}

void
__stdcall
SK_TestImports (          HMODULE  hMod,
                 sk_import_test_s *pTests,
                              int  nCount )
{
  DBG_UNREFERENCED_PARAMETER (hMod);

  if (! pTests)
    return;

  // This thing has been chopped to bits and pieces to try
  //   and compensate for ScumVM's hoepelessly corrupted
  //     Import Address Table that is a crash in the making.

  int i    = 0,
      hits = 0;

  PIMAGE_NT_HEADERS        pNtHdr   = nullptr;
  PIMAGE_DATA_DIRECTORY    pImgDir  = nullptr;
  PIMAGE_IMPORT_DESCRIPTOR pImpDesc = nullptr;
  uintptr_t                pImgBase = (uintptr_t)
    SK_Debug_GetImageBaseAddr ();

  const int      MAX_IMPORTS  =  16;
  size_t hashes [MAX_IMPORTS] = { };

  int  idx     = 0;
  auto to_hash = pTests;

  while (idx < nCount)
  {
    hashes [idx++] =
      hash_string_utf8 ((to_hash++)->szModuleName, true);
  }

  __try
  {
    pNtHdr   =
      PIMAGE_NT_HEADERS (
        pImgBase + PIMAGE_DOS_HEADER (pImgBase)->e_lfanew
      );

    pImgDir  =
        &pNtHdr->OptionalHeader.DataDirectory [IMAGE_DIRECTORY_ENTRY_IMPORT];

    pImpDesc =
      PIMAGE_IMPORT_DESCRIPTOR (
        pImgBase + pImgDir->VirtualAddress
      );

    //dll_log->Log (L"[Import Tbl] Size=%lu", pImgDir->Size);
    //

    PIMAGE_IMPORT_DESCRIPTOR start =
                                     reinterpret_cast <PIMAGE_IMPORT_DESCRIPTOR>
      ( pImgBase + pImgDir->VirtualAddress ),
                             end   = reinterpret_cast <PIMAGE_IMPORT_DESCRIPTOR> (
                                     reinterpret_cast <uintptr_t>
      (    start + pImgDir->Size )                                               );

    for (   pImpDesc = start ;
            pImpDesc < end   ;
          ++pImpDesc )
    {
      __try
      {
        if ( pImpDesc->Characteristics == 0  )
          break;

        if ( pImpDesc->ForwarderChain != DWORD_MAX ||
             pImpDesc->Name           == 0x0 )
        {
          continue;
        }
      }

      __except (EXCEPTION_EXECUTE_HANDLER)
      {
        continue;
      };


      const auto* szImport =
        reinterpret_cast <const char *> (
          pImgBase + (pImpDesc++)->Name
        );

      //dll_log->Log (L"%hs", szImport);

      size_t hashed_str =
        hash_string_utf8 (szImport, true);


      for (i = 0; i < nCount; i++)
      {
        __try
        {
          if ((! pTests [i].used) && hashes [i] == hashed_str)
          {
            pTests [i].used = true;
                     ++hits;
          }
        }

        __except (EXCEPTION_EXECUTE_HANDLER) { };
      }

      if (hits == nCount)
        break;
    }
  }
  __except (EXCEPTION_EXECUTE_HANDLER) { };

  // We found all of nothing by examining the IAT, so time to try a
  //   different approach...
  if (hits == 0)
  {
    i = 0;

    __try
    {
      // Supplement the import table test with a check for residency,
      //   this may catch games that load graphics APIs dynamically.
      for (i = 0; i < nCount; i++)
      {
        if ( GetModuleHandleExA ( GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    pTests [i].szModuleName,
                                      &hMod ) )
          pTests [i].used = true;
      }
    }

    __except (EXCEPTION_EXECUTE_HANDLER)
    {
      dll_log->Log ( L"[RenderBoot] Exception Code %x Encountered Examining "
                     L"pre-Loaded Render Import DLL (idx = [%li / %li])",
                     GetExceptionCode (), i, nCount );
      return;
    }
  }
}

//
// This prototype is now completely ridiculous, this "design" sucks...
//   FIXME!!
//
void
SK_TestRenderImports ( HMODULE hMod,
                       bool*   gl,
                       bool*   vulkan,
                       bool*   d3d9,
                       bool*   dxgi,
                       bool*   d3d11,
                       bool*   d3d12,
                       bool*   d3d8,
                       bool*   ddraw,
                       bool*   glide )
{
  static sk_import_test_s tests [] = { { "OpenGL32.dll",  false },
                                       { "vulkan-1.dll",  false },
                                       { "d3d9.dll",      false },
                                       //{ "dxgi.dll",     false },
                                       { "d3d11.dll",     false },
                                       { "d3d12.dll",     false },
                                       { "d3d8.dll",      false },
                                       { "ddraw.dll",     false },
                                       { "d3dx11_43.dll", false },

                                       // 32-bit only
                                       { "glide.dll",     false } };

  SK_TestImports (hMod, tests, sizeof (tests) / sizeof (sk_import_test_s));

  *gl     = tests [0].used;
  *vulkan = tests [1].used;
  *d3d9   = tests [2].used;
  *dxgi   = false;
//*dxgi   = tests [3].used;
  *d3d11  = tests [3].used;
  *d3d12  = tests [4].used;
  *dxgi  |= tests [4].used;
  *d3d8   = tests [5].used;
  *ddraw  = tests [6].used;
  *d3d11 |= tests [7].used;
  *dxgi  |= tests [7].used;
  *glide  = tests [8].used;
}

int
SK_Path_wcsicmp (const wchar_t* wszStr1, const wchar_t* wszStr2)
{
  int ret =
    CompareString ( LOCALE_INVARIANT,
                      NORM_IGNORECASE | NORM_IGNOREWIDTH |
                      NORM_IGNORENONSPACE,
                        wszStr1, lstrlenW (wszStr1),
                          wszStr2, lstrlenW (wszStr2) );

  // To make this a drop-in replacement for wcsicmp, subtract
  //   2 from non-zero return values
  return (ret != 0) ?
    (ret - 2) : 0;
}

const wchar_t*
SK_Path_wcsrchr (const wchar_t* wszStr, wchar_t wchr)
{
             int len     = 0;
  const wchar_t* pwszStr = wszStr;

  for (len = 0; len < MAX_PATH; ++len, pwszStr = CharNextW (pwszStr))
  {
    if (*pwszStr == L'\0')
      break;
  }

  const wchar_t* wszSearch = pwszStr;

  while (wszSearch >= wszStr)
  {
    if (*wszSearch == wchr)
      break;

    wszSearch = CharPrevW (wszStr, wszSearch);
  }

  return (wszSearch < wszStr) ?
           nullptr : wszSearch;
}

const wchar_t*
SK_Path_wcsstr (const wchar_t* wszStr, const wchar_t* wszSubStr)
{
#if 0
  int            len     =
    min (lstrlenW (wszSubStr), MAX_PATH);

  const wchar_t* it       = wszStr;
  const wchar_t* wszScan  = it;
  const wchar_t* wszBegin = it;

  int            idx     = 0;

  while (it < (wszStr + MAX_PATH)) {
    bool match = (*wszScan == wszSubStr [idx]);

    if (match) {
      if (++idx == len)
        return wszBegin;

      ++it;
    }

    else {
      if (it > (wszStr + MAX_PATH - len))
        break;

      it  = ++wszBegin;
      idx = 0;
    }
  }

  return (it <= (wszStr + MAX_PATH - len)) ?
           wszBegin : nullptr;
#else
  return StrStrIW (wszStr, wszSubStr);
#endif
}


static HMODULE hModVersion = nullptr;

__forceinline
bool
SK_Import_VersionDLL (void)
{
  if (hModVersion == nullptr)
  {
  //if(!(hModVersion = SK_GetModuleHandle      (L"Version.dll")))
         hModVersion = SK_LoadLibraryW (L"Version.dll");
         // Do not use skModuleRegistry
  }
  //Api-ms-win-core-version-l1-1-0.dll");

  return
    ( hModVersion != nullptr );
}

BOOL
WINAPI
SK_VerQueryValueW (
  _In_                                                                       LPCVOID pBlock,
  _In_                                                                       LPCWSTR lpSubBlock,
  _Outptr_result_buffer_ (_Inexpressible_ ("buffer can be PWSTR or DWORD*")) LPVOID* lplpBuffer,
  _Out_                                                                       PUINT  puLen )
{
  SK_Import_VersionDLL ();

  using VerQueryValueW_pfn = BOOL (WINAPI *)(
    _In_                                                                      LPCVOID  pBlock,
    _In_                                                                      LPCWSTR  lpSubBlock,
    _Outptr_result_buffer_ (_Inexpressible_ ("buffer can be PWSTR or DWORD*")) LPVOID* lplpBuffer,
    _Out_                                                                       PUINT  puLen );

  static auto imp_VerQueryValueW =
    (VerQueryValueW_pfn)
       SK_GetProcAddress (hModVersion, "VerQueryValueW");

  return
    imp_VerQueryValueW ( pBlock, lpSubBlock, lplpBuffer, puLen );
}

BOOL
WINAPI
SK_GetFileVersionInfoExW (_In_                      DWORD   dwFlags,
                          _In_                      LPCWSTR lpwstrFilename,
                          _Reserved_                DWORD   dwHandle,
                          _In_                      DWORD   dwLen,
                          _Out_writes_bytes_(dwLen) LPVOID  lpData)
{
  SK_Import_VersionDLL ();

  using GetFileVersionInfoExW_pfn = BOOL (WINAPI *)(
    _In_                      DWORD   dwFlags,
    _In_                      LPCWSTR lpwstrFilename,
    _Reserved_                DWORD   dwHandle,
    _In_                      DWORD   dwLen,
    _Out_writes_bytes_(dwLen) LPVOID  lpData
  );

  static auto imp_GetFileVersionInfoExW =
    (GetFileVersionInfoExW_pfn)
       SK_GetProcAddress (hModVersion, "GetFileVersionInfoExW");

  return
    imp_GetFileVersionInfoExW ( dwFlags, lpwstrFilename,
                                dwHandle, dwLen, lpData );
}

DWORD
WINAPI
SK_GetFileVersionInfoSizeExW ( _In_  DWORD   dwFlags,
                               _In_  LPCWSTR lpwstrFilename,
                               _Out_ LPDWORD lpdwHandle )
{
  SK_Import_VersionDLL ();

  using GetFileVersionInfoSizeExW_pfn = DWORD (WINAPI *)(
    _In_  DWORD   dwFlags,
    _In_  LPCWSTR lpwstrFilename,
    _Out_ LPDWORD lpdwHandle
  );

  static auto imp_GetFileVersionInfoSizeExW =
    (GetFileVersionInfoSizeExW_pfn)
       SK_GetProcAddress (hModVersion, "GetFileVersionInfoSizeExW");

  return
    imp_GetFileVersionInfoSizeExW ( dwFlags, lpwstrFilename,
                                    lpdwHandle );
}

bool
__stdcall
SK_IsDLLSpecialK (const wchar_t* wszName)
{
  if (! wszName)
    return false;

  if ((! SK_GetModuleHandleW (wszName)) && PathFileExistsW (wszName))
             SK_LoadLibraryW (wszName);

  if (SK_GetProcAddress (SK_GetModuleHandleW (wszName), "SK_GetDLL") != nullptr)
    return true;

  UINT cbTranslatedBytes = 0,
       cbProductBytes    = 0;

  size_t dwSize = 16384;
  uint8_t cbData [16384] = { };

  wchar_t* wszProduct        = nullptr; // Will point somewhere in cbData

  struct LANGANDCODEPAGE {
    WORD wLanguage;
    WORD wCodePage;
  } *lpTranslate = nullptr;

  wchar_t wszFullyQualifiedName [MAX_PATH + 2] = { };

  lstrcatW (wszFullyQualifiedName, SK_GetHostPath ());
  lstrcatW (wszFullyQualifiedName, L"\\");
  lstrcatW (wszFullyQualifiedName, wszName);

  BOOL bRet =
    SK_GetFileVersionInfoExW ( FILE_VER_GET_NEUTRAL |
                               FILE_VER_GET_PREFETCHED,
                                 wszFullyQualifiedName,
                                   0x00,
                  static_cast <DWORD> (dwSize),
                                       cbData );

  if (! bRet) return false;

  if ( SK_VerQueryValueW ( cbData,
                           TEXT ("\\VarFileInfo\\Translation"),
               static_cast_p2p <void> (&lpTranslate),
                                       &cbTranslatedBytes ) && cbTranslatedBytes &&
                                                               lpTranslate )
  {
    wchar_t        wszPropName [64] = { };
    _snwprintf_s ( wszPropName, 63,
                   LR"(\StringFileInfo\%04x%04x\ProductName)",
                     lpTranslate   [0].wLanguage,
                       lpTranslate [0].wCodePage );

    SK_VerQueryValueW ( cbData,
                          wszPropName,
              static_cast_p2p <void> (&wszProduct),
                                      &cbProductBytes );

    return (cbProductBytes && (StrStrIW (wszProduct, L"Special K")));
  }

  return false;
}

std::wstring
__stdcall
SK_GetDLLVersionStr (const wchar_t* wszName)
{
  UINT     cbTranslatedBytes = 0,
           cbProductBytes    = 0,
           cbVersionBytes    = 0;

  uint8_t cbData [16384] = { };
  size_t dwSize = 16384;

  wchar_t* wszFileDescrip = nullptr; // Will point somewhere in cbData
  wchar_t* wszFileVersion = nullptr; // "

  struct LANGANDCODEPAGE {
    WORD wLanguage;
    WORD wCodePage;
  } *lpTranslate = nullptr;

  BOOL bRet =
    SK_GetFileVersionInfoExW ( FILE_VER_GET_NEUTRAL |
                               FILE_VER_GET_PREFETCHED,
                                 wszName,
                                   0x00,
                static_cast <DWORD> (dwSize),
                                       cbData );

  if (! bRet)
    return L"N/A";

  if ( SK_VerQueryValueW ( cbData,
                             TEXT ("\\VarFileInfo\\Translation"),
                 static_cast_p2p <void> (&lpTranslate),
                                         &cbTranslatedBytes ) && cbTranslatedBytes &&
                                                                 lpTranslate )
  {
    wchar_t        wszPropName [64] = { };
    _snwprintf_s ( wszPropName, 63,
                    LR"(\StringFileInfo\%04x%04x\FileDescription)",
                      lpTranslate   [0].wLanguage,
                        lpTranslate [0].wCodePage );

    SK_VerQueryValueW ( cbData,
                          wszPropName,
              static_cast_p2p <void> (&wszFileDescrip),
                                      &cbProductBytes );

    _snwprintf_s ( wszPropName, 63,
                    LR"(\StringFileInfo\%04x%04x\FileVersion)",
                      lpTranslate   [0].wLanguage,
                        lpTranslate [0].wCodePage );

    SK_VerQueryValueW ( cbData,
                          wszPropName,
              static_cast_p2p <void> (&wszFileVersion),
                                      &cbVersionBytes );
  }

  if ( cbTranslatedBytes == 0 ||
         (cbProductBytes == 0 && cbVersionBytes == 0) )
  {
    return L"  ";
  }

  std::wstring ret;

  if (cbProductBytes)
  {
    ret.append (wszFileDescrip);
    ret.append (L"  ");
  }

  if (cbVersionBytes)
    ret.append (wszFileVersion);

  return ret;
}



LPVOID __SK_base_img_addr = nullptr;
LPVOID __SK_end_img_addr  = nullptr;

void*
__stdcall
SK_Scan (const void* pattern, size_t len, const void* mask)
{
  return SK_ScanAligned (pattern, len, mask);
}


void*
__stdcall
SKX_ScanAlignedEx ( const void* pattern, size_t len,   const void* mask,
                          void* after,   int    align,    uint8_t* base_addr,

                          SK_MemScan_Params__v0 params =
                          SK_MemScan_Params__v0 ()       )
{
  MEMORY_BASIC_INFORMATION  minfo = { };
  VirtualQuery (base_addr, &minfo, sizeof (minfo));

           base_addr = static_cast <uint8_t *> (
                                      (void *)SK_GetModuleHandle (nullptr)
                                               );
  uint8_t* end_addr  = static_cast <uint8_t *> (    minfo.BaseAddress    ) +
                                                    minfo.RegionSize;
  size_t pages = 0;

#ifndef _WIN64
  // Account for possible overflow in 32-bit address space in very rare (address randomization) cases
uint8_t* const PAGE_WALK_LIMIT =
  base_addr + static_cast <uintptr_t>(1UL << 27) > base_addr ?
                                                   base_addr + static_cast      <uintptr_t>( 1UL << 27) :
                                                               reinterpret_cast <uint8_t *>(~0UL      );
#else
  // Dark Souls 3 needs this, its address space is completely random to the point
  //   where it may be occupying a range well in excess of 36 bits. Probably a stupid
  //     anti-cheat attempt.
uint8_t* const PAGE_WALK_LIMIT = (base_addr + static_cast <uintptr_t>(1ULL << 36));
#endif

  //
  // For practical purposes, let's just assume that all valid games have less than 256 MiB of
  //   committed executable image data.
  //
  while (VirtualQuery (end_addr, &minfo, sizeof (minfo)) && end_addr < PAGE_WALK_LIMIT)
  {
    if (minfo.Protect & PAGE_NOACCESS || (! (minfo.Type & MEM_IMAGE)))
      break;

    pages += VirtualQuery (end_addr, &minfo, sizeof (minfo));

    end_addr =
      static_cast <uint8_t *> (minfo.BaseAddress) + minfo.RegionSize;
  }

  if (end_addr > PAGE_WALK_LIMIT)
  {
    static bool warned = false;

    if (! warned)
    {
      dll_log->Log ( L"[ Sig Scan ] Module page walk resulted in end addr. out-of-range: %ph",
                       end_addr );
      dll_log->Log ( L"[ Sig Scan ]  >> Restricting to %ph",
                       PAGE_WALK_LIMIT );
      warned = true;
    }

    end_addr =
      PAGE_WALK_LIMIT;
  }

#if 0
  dll_log->Log ( L"[ Sig Scan ] Module image consists of %zu pages, from %ph to %ph",
                  pages,
                    base_addr,
                      end_addr );
#endif

  __SK_base_img_addr = base_addr;
  __SK_end_img_addr  = end_addr;

  uint8_t* begin = std::max (static_cast <uint8_t *> (after) + align, base_addr);
  uint8_t* it    = begin;
  size_t   idx   = 0;

  static MODULEINFO mi_sk = {};

  SK_RunOnce (
    GetModuleInformation ( SK_GetCurrentProcess (),
      SK_GetDLL (),
              &mi_sk,
       sizeof (MODULEINFO) )
  );

  while (it < end_addr)
  {
    VirtualQuery (it, &minfo, sizeof (minfo));

    // Bail-out once we walk into an address range that is not resident, because
    //   it does not belong to the original executable.
    if (minfo.RegionSize == 0)
      break;

    uint8_t* next_rgn =
     (uint8_t *)minfo.BaseAddress + minfo.RegionSize;

    if ( (! (minfo.Type     & MEM_IMAGE))   ||
         (! (minfo.State    & MEM_COMMIT))  ||
             minfo.Protect  & PAGE_NOACCESS ||
             minfo.Protect == 0             ||
         ( ! params.testPrivs (minfo) )     ||

        // It is not a good idea to scan Special K's DLL, since in many cases the pattern
        //   we are scanning for becomes a part of the DLL and makes an exhaustive search
        //     even more exhausting.
        //
        //  * If scanning a Special K DLL is needed, see Kaldaien and we can probably
        //      come up with a more robust solution.
        //
             ( minfo.BaseAddress >=            mi_sk.lpBaseOfDll &&
               it                <= (uint8_t *)mi_sk.lpBaseOfDll + mi_sk.SizeOfImage ) )
    {
      it    = next_rgn;
      idx   = 0;
      begin = it;

      continue;
    }

    // Do not search past the end of the module image!
    if (next_rgn >= end_addr)
      break;

    while (it < next_rgn)
    {
      uint8_t* scan_addr = it;

      uint8_t test_val = static_cast <const uint8_t *> (pattern)[idx];
      uint8_t mask_val = 0x0;

      bool match = false;

      auto orig_se =
      SK_SEH_ApplyTranslator (SK_FilteringStructuredExceptionTranslator (EXCEPTION_ACCESS_VIOLATION));
      try {
        match =
          (*scan_addr == test_val);
      }

      catch (const SK_SEH_IgnoredException&) {
        //continue;
      }
      SK_SEH_RemoveTranslator (orig_se);

      if (mask != nullptr)
      {
        mask_val = static_cast <const uint8_t *> (mask)[idx];

        // Treat portions we do not care about (mask [idx] == 0) as wildcards.
        if (! mask_val)
          match = true;

        else if (match)
        {
          // The more complicated case, where the mask is an actual mask :)
          match = (test_val & mask_val);
        }
      }

      if (match)
      {
        if (++idx == len)
        {
          if ((reinterpret_cast <uintptr_t> (begin) % align) != 0)
          {
            begin += idx;
            begin += align - (reinterpret_cast <uintptr_t> (begin) % align);

            it     = begin;
            idx    = 0;
          }

          else
          {
            return begin;
          }
        }

        else
          ++it;
      }

      else
      {
        // No match?!
        if (it > end_addr - len)
          break;

        begin += idx;
        begin += align - (reinterpret_cast <uintptr_t> (begin) % align);

        it  = begin;
        idx = 0;
      }
    }
  }

  return nullptr;
}

void*
__stdcall
SK_ScanAlignedEx2 ( const void* pattern, size_t len,   const void* mask,
                          void* after,   int    align,    uint8_t* base_addr)
{
  return SKX_ScanAlignedEx ( pattern, len, mask, after, align, base_addr );
}

void*
__stdcall
SKX_ScanAlignedExec (const void* pattern, size_t len, const void* mask, void* after, int align)
{
  auto* base_addr =
    reinterpret_cast <uint8_t *> (SK_GetModuleHandle (nullptr));

  SK_MemScan_Params__v0 params;
                        params.privileges.execute =
                    SK_MemScan_Params__v0::Allowed;

  return SKX_ScanAlignedEx (pattern, len, mask, after, align, base_addr, params);
}

void*
__stdcall
SK_ScanAlignedEx (const void* pattern, size_t len, const void* mask, void* after, int align)
{
  auto* base_addr =
    reinterpret_cast <uint8_t *> (SK_GetModuleHandle (nullptr));

  return SKX_ScanAlignedEx (pattern, len, mask, after, align, base_addr);
}

void*
__stdcall
SK_ScanAligned (const void* pattern, size_t len, const void* mask, int align)
{
  return SK_ScanAlignedEx (pattern, len, mask, nullptr, align);
}

BOOL
__stdcall
SK_InjectMemory ( LPVOID  base_addr,
            const void   *new_data,
                  size_t  data_size,
                  DWORD   permissions,
                  void   *old_data )
{
  BOOL bRet = FALSE;

  auto orig_se =
  SK_SEH_ApplyTranslator (SK_FilteringStructuredExceptionTranslator (EXCEPTION_ACCESS_VIOLATION));
  try
  {
    DWORD dwOld =
      PAGE_NOACCESS;

    if ( VirtualProtect ( base_addr,   data_size,
                          permissions, &dwOld )   )
    {
      if (old_data != nullptr) RtlCopyMemory (old_data, base_addr, data_size);
                               RtlCopyMemory (base_addr, new_data, data_size);

      VirtualProtect ( base_addr, data_size,
                       dwOld,     &dwOld );

      bRet = TRUE;
    }
  }

  catch (const SK_SEH_IgnoredException&)
  {
    assert (false);

    // Bad memory address, just discard the write attempt
    //
    //   This isn't atomic; if we fail, it's possible we wrote part
    //     of the data successfully - consider an undo mechanism.
    //
  }
  SK_SEH_RemoveTranslator (orig_se);

  return bRet;
}

uint64_t
SK_File_GetSize (const wchar_t* wszFile)
{
  WIN32_FILE_ATTRIBUTE_DATA
    file_attrib_data = { };

  if ( GetFileAttributesEx ( wszFile,
                               GetFileExInfoStandard,
                                 &file_attrib_data ) )
  {
    return ULARGE_INTEGER { file_attrib_data.nFileSizeLow,
                            file_attrib_data.nFileSizeHigh }.QuadPart;
  }

  return 0ULL;
}



#if 0
void
__stdcall
SK_wcsrep ( const wchar_t*   wszIn,
                  wchar_t** pwszOut,
            const wchar_t*   wszOld,
            const wchar_t*   wszNew )
{
        wchar_t* wszTemp;
  const wchar_t* wszFound = wcsstr (wszIn, wszOld);

  if (! wszFound) {
    *pwszOut =
      (wchar_t *)malloc (wcslen (wszIn) + 1);

    wcscpy (*pwszOut, wszIn);
    return;
  }

  int idx = wszFound - wszIn;

  *pwszOut =
    (wchar_t *)realloc (  *pwszOut,
                            wcslen (wszIn)  - wcslen (wszOld) +
                            wcslen (wszNew) + 1 );

  wcsncpy ( *pwszOut, wszIn, idx    );
  wcscpy  ( *pwszOut + idx,  wszNew );
  wcscpy  ( *pwszOut + idx + wcslen (wszNew),
               wszIn + idx + wcslen (wszOld) );

  wszTemp =
    (wchar_t *)malloc (idx + wcslen (wszNew) + 1);

  wcsncpy (wszTemp, *pwszOut, idx + wcslen (wszNew));
  wszTemp [idx + wcslen (wszNew)] = '\0';

  SK_wcsrep ( wszFound + wcslen (wszOld),
                pwszOut,
                  wszOld,
                    wszNew );

  wszTemp =
    (wchar_t *)realloc ( wszTemp,
                           wcslen ( wszTemp) +
                           wcslen (*pwszOut) + 1 );

  lstrcatW (wszTemp, *pwszOut);

  free (*pwszOut);

  *pwszOut = wszTemp;
}
#endif

size_t
SK_RemoveTrailingDecimalZeros (wchar_t* wszNum, size_t bufLen)
{
  if (wszNum == nullptr)
    return 0;

  // Remove trailing 0's after the .
  size_t len = bufLen == 0 ?
                  wcslen (wszNum) :
        std::min (wcslen (wszNum), bufLen);

  for (size_t i = (len - 1); i > 1; i--)
  {
    if (wszNum [i] == L'0' && wszNum [i - 1] != L'.')
      len--;

    if (wszNum [i] != L'0' && wszNum [i] != L'\0')
      break;
  }

  wszNum [len] = L'\0';

  return len;
}

size_t
SK_RemoveTrailingDecimalZeros (char* szNum, size_t bufLen)
{
  if (szNum == nullptr)
    return 0;

  // Remove trailing 0's after the .
  size_t len = bufLen == 0 ?
                  strlen (szNum) :
        std::min (strlen (szNum), bufLen);

  for (size_t i = (len - 1); i > 1; i--)
  {
    if (szNum [i] == '0' && szNum [i - 1] != '.')
      len--;

    if (szNum [i] != '0' && szNum [i] != '\0')
      break;
  }

  szNum [len] = '\0';

  return len;
}



struct sk_host_process_s {
  wchar_t wszApp       [MAX_PATH + 2] = { };
  wchar_t wszPath      [MAX_PATH + 2] = { };
  wchar_t wszFullName  [MAX_PATH + 2] = { };
  wchar_t wszBlacklist [MAX_PATH + 2] = { };
  wchar_t wszSystemDir [MAX_PATH + 2] = { };

  std::atomic_bool app                = false;
  std::atomic_bool path               = false;
  std::atomic_bool full_name          = false;
  std::atomic_bool blacklist          = false;
  std::atomic_bool sys_dir            = false;
};

SK_LazyGlobal <sk_host_process_s> host_proc;
SK_LazyGlobal <SK_HostAppUtil>    host_app_util;

bool __SK_RunDLL_Bypass = false;

bool
__cdecl
SK_IsHostAppSKIM (void)
{
  return ( (! __SK_RunDLL_Bypass) &&
      ( StrStrIW (SK_GetHostApp (), L"SKIM") != nullptr ||
        StrStrIW (SK_GetHostApp (), L"SKIF") != nullptr ) );
}

bool
__cdecl
SK_IsRunDLLInvocation (void)
{
  // Allow some commands invoked by RunDLL to function as a normal DLL
  if (__SK_RunDLL_Bypass)
    return false;

  bool rundll_invoked =
    (StrStrIW (SK_GetHostApp (), L"Rundll32") != nullptr);

  if (rundll_invoked)
  {
    // Not all instances of RunDLL32 that load this DLL are Special K ...
    //
    //  The CBT hook may have been triggered by some other software that used
    //    rundll32 and then launched a Win32 application with it.
    //
    // If the command line does not reference our DLL
    if (! StrStrIW (PathGetArgsW (GetCommandLineW ()), L"RunDLL_"))
      rundll_invoked = false;
  }

  return rundll_invoked;
}

bool
__cdecl
SK_IsSuperSpecialK (void)
{
  return ( SK_IsRunDLLInvocation () ||
           SK_IsHostAppSKIM      () );
}


// Using this rather than the Path Shell API stuff due to
//   AppInit_DLL support requirements
void
SK_PathRemoveExtension (wchar_t* wszInOut)
{
  wchar_t *wszEnd  = wszInOut,
          *wszPrev;

  while (*CharNextW (wszEnd) != L'\0')
    wszEnd = CharNextW (wszEnd);

  wszPrev = wszEnd;

  while (  CharPrevW (wszInOut, wszPrev) > wszInOut &&
          *CharPrevW (wszInOut, wszPrev) != L'.' )
    wszPrev = CharPrevW (wszInOut, wszPrev);

  if (CharPrevW (wszInOut, wszPrev) > wszInOut)
  {
    if (*CharPrevW (wszInOut, wszPrev) == L'.')
        *CharPrevW (wszInOut, wszPrev)  = L'\0';
  }
}


const wchar_t*
SK_GetBlacklistFilename (void)
{
  if ( host_proc->blacklist.load () )
    return host_proc->wszBlacklist;

  static volatile
    LONG init = FALSE;

  if (! InterlockedCompareExchangeAcquire (&init, TRUE, FALSE))
  {
    lstrcatW (host_proc->wszBlacklist, SK_GetHostPath ());
    lstrcatW (host_proc->wszBlacklist, L"\\SpecialK.deny.");
    lstrcatW (host_proc->wszBlacklist, SK_GetHostApp  ());

    SK_PathRemoveExtension (host_proc->wszBlacklist);

    host_proc->blacklist.store (true);

    InterlockedIncrementRelease  (&init);
  }

  else
    SK_Thread_SpinUntilAtomicMin (&init, 2);

  return
    host_proc->wszBlacklist;
}

const wchar_t*
SK_GetHostApp (void)
{
  if (     host_proc->app.load () )
  { return host_proc->wszApp;     }

  static volatile
    LONG init = FALSE;

  if (! InterlockedCompareExchangeAcquire (&init, TRUE, FALSE))
  {
    wchar_t       wszFullyQualified       [ MAX_PATH + 2 ] = { };
    wcsncpy_s (   wszFullyQualified,        MAX_PATH,
                SK_GetFullyQualifiedApp (), _TRUNCATE );

    PathStripPathW (wszFullyQualified);

    wcsncpy_s ( host_proc->wszApp, MAX_PATH,
                wszFullyQualified, _TRUNCATE );

    assert (PathFileExistsW (host_proc->wszApp));

    host_proc->app.store (true);

    InterlockedIncrementRelease (&init);
  }

  else
    SK_Thread_SpinUntilAtomicMin (&init, 2);

  return
    host_proc->wszApp;
}

const wchar_t*
SK_GetFullyQualifiedApp (void)
{
  if ( host_proc->full_name.load () )
    return host_proc->wszFullName;

  static volatile
    LONG init = FALSE;

  if (! InterlockedCompareExchangeAcquire (&init, TRUE, FALSE))
  {
    DWORD   dwProcessSize =  MAX_PATH;
    wchar_t wszProcessName [ MAX_PATH + 2 ] = { };

    GetModuleFileNameW ( nullptr,
                           wszProcessName,
                            dwProcessSize );

    wcsncpy_s ( host_proc->wszFullName, MAX_PATH,
                  wszProcessName,       _TRUNCATE );

    assert (PathFileExistsW (host_proc->wszFullName));

    host_proc->full_name.store (true);

    InterlockedIncrementRelease (&init);
  }

  else
    SK_Thread_SpinUntilAtomicMin (&init, 2);

  return
    host_proc->wszFullName;
}

// NOT the working directory, this is the directory that
//   the executable is located in.

const wchar_t*
SK_GetHostPath (void)
{
  if ( host_proc->path.load () )
    return host_proc->wszPath;

  static volatile
    LONG init = FALSE;

  if (! InterlockedCompareExchangeAcquire (&init, TRUE, FALSE))
  {
    wchar_t     wszProcessName [ MAX_PATH + 2 ] = { };
    wcsncpy_s ( wszProcessName,  MAX_PATH,
                  SK_GetFullyQualifiedApp (), _TRUNCATE );

    BOOL bSuccess =
      PathRemoveFileSpecW (wszProcessName);

#ifndef _DEBUG
    UNREFERENCED_PARAMETER (bSuccess);
#endif

    assert (bSuccess != FALSE);

    wcsncpy_s (
      host_proc->wszPath, MAX_PATH,
        wszProcessName,  _TRUNCATE  );

    assert (PathFileExistsW (host_proc->wszPath));

    host_proc->path.store (true);

    InterlockedIncrementRelease (&init);
  }

  else
    SK_Thread_SpinUntilAtomicMin (&init, 2);

  return
    host_proc->wszPath;
}


const wchar_t*
SK_GetSystemDirectory (void)
{
  if ( host_proc->sys_dir.load () )
    return host_proc->wszSystemDir;

  static volatile
    LONG init = FALSE;

  if (! InterlockedCompareExchangeAcquire (&init, TRUE, FALSE))
  {
#ifdef _WIN64
    GetSystemDirectory (host_proc->wszSystemDir, MAX_PATH);
#else
    HANDLE hProc = SK_GetCurrentProcess ();

    BOOL   bWOW64;
    ::IsWow64Process (hProc, &bWOW64);

    if (bWOW64)
      GetSystemWow64Directory (host_proc->wszSystemDir, MAX_PATH);
    else
      GetSystemDirectory      (host_proc->wszSystemDir, MAX_PATH);
#endif

    host_proc->sys_dir.store (true);

    InterlockedIncrementRelease (&init);
  }

  else
    SK_Thread_SpinUntilAtomicMin (&init, 2);

  return
    host_proc->wszSystemDir;
}

LPWSTR
SK_PathCombineW ( _Out_writes_ (MAX_PATH) LPWSTR pszDest,
                                _In_opt_ LPCWSTR pszDir,
                                _In_opt_ LPCWSTR pszFile )
{
  void SK_StripLeadingSlashesW (wchar_t *wszInOut);

  wchar_t                  wszFile [MAX_PATH + 2] = { };
  wcsncpy_s               (wszFile, MAX_PATH, pszFile, _TRUNCATE);
  SK_StripLeadingSlashesW (wszFile);

  return
    PathCombineW          (pszDest, pszDir, wszFile);
}

uint64_t
SK_DeleteTemporaryFiles (const wchar_t* wszPath, const wchar_t* wszPattern)
{
  WIN32_FIND_DATA fd     = {      };
  HANDLE          hFind  = INVALID_HANDLE_VALUE;
  size_t          files  =   0UL;
  LARGE_INTEGER   liSize = { 0ULL };

  wchar_t          wszFindPattern [MAX_PATH + 2] = { };
  SK_PathCombineW (wszFindPattern, wszPath, wszPattern);

  hFind = FindFirstFileW (wszFindPattern, &fd);

  if (hFind != INVALID_HANDLE_VALUE)
  {
    dll_log->LogEx ( true, L"[Clean Mgr.] Cleaning temporary files in '%s'...    ",
                             SK_ConcealUserDir (std::wstring (wszPath).data ()) );

    wchar_t wszFullPath [MAX_PATH + 2] = { };

    do
    {
      if (fd.dwFileAttributes != INVALID_FILE_ATTRIBUTES)
      {
        *wszFullPath = L'\0';

        SK_PathCombineW (wszFullPath, wszPath, fd.cFileName);

        if (DeleteFileW (wszFullPath))
        {
          ++files;

          liSize.QuadPart += LARGE_INTEGER {       fd.nFileSizeLow,
                                             (LONG)fd.nFileSizeHigh }.QuadPart;
        }
      }
    } while (FindNextFileW (hFind, &fd) != 0);

    dll_log->LogEx ( false, L"%zu files deleted\n", files);

    FindClose (hFind);
  }

  return (uint64_t)liSize.QuadPart;
}


bool
SK_FileHasSpaces (const wchar_t* wszLongFileName)
{
  return StrStrIW (wszLongFileName, L" ") != nullptr;
}

BOOL
SK_FileHas8Dot3Name (const wchar_t* wszLongFileName)
{
  wchar_t wszShortPath [MAX_PATH + 2] = { };

  if ((! GetShortPathName   (wszLongFileName, wszShortPath, 1)) ||
         GetFileAttributesW (wszShortPath) == INVALID_FILE_ATTRIBUTES   ||
         StrStrIW           (wszLongFileName, L" "))
  {
    return FALSE;
  }

  return TRUE;
}

HRESULT ModifyPrivilege(
    IN LPCTSTR szPrivilege,
    IN BOOL fEnable)
{
    HRESULT hr = S_OK;
    TOKEN_PRIVILEGES NewState;
    LUID             luid;
    HANDLE hToken    = nullptr;

    // Open the process token for this process.
    if (! OpenProcessToken(SK_GetCurrentProcess (),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                          &hToken ))
    {
        return ERROR_FUNCTION_FAILED;
    }

    // Get the local unique ID for the privilege.
    if ( !LookupPrivilegeValue( nullptr,
                                szPrivilege,
                                &luid ))
    {
        CloseHandle( hToken );
        return ERROR_FUNCTION_FAILED;
    }

    // Assign values to the TOKEN_PRIVILEGE structure.
    NewState.PrivilegeCount            = 1;
    NewState.Privileges [0].Luid       = luid;
    NewState.Privileges [0].Attributes =
              (fEnable ? SE_PRIVILEGE_ENABLED : 0);

    // Adjust the token privilege.
    if (!AdjustTokenPrivileges(hToken,
                               FALSE,
                               &NewState,
                               0,
                               nullptr,
                               nullptr))
    {
        hr = ERROR_FUNCTION_FAILED;
    }

    // Close the handle.
    CloseHandle(hToken);

    return hr;
}


bool
SK_Generate8Dot3 (const wchar_t* wszLongFileName)
{
  wchar_t wszFileName  [MAX_PATH] = { };
  wchar_t wszFileName1 [MAX_PATH] = { };

  PathCombineW (wszFileName, wszLongFileName,  nullptr);
  PathCombineW (wszFileName1, wszLongFileName, nullptr);

  wchar_t  wsz8     [11] = { }; // One non-nul for overflow
  wchar_t  wszDot3  [ 4] = { };
  wchar_t  wsz8Dot3 [14] = { };

  while (SK_FileHasSpaces (wszFileName))
  {
    ModifyPrivilege (SE_RESTORE_NAME, TRUE);
    ModifyPrivilege (SE_BACKUP_NAME,  TRUE);

    SK_AutoHandle hFile (
      CreateFileW ( wszFileName,
                      GENERIC_WRITE      | DELETE,
                        FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          nullptr,
                            OPEN_EXISTING,
                              GetFileAttributes (wszFileName) |
                              FILE_FLAG_BACKUP_SEMANTICS,
                                nullptr ) );

    if ((intptr_t)hFile.m_h <= 0)
    {
      return false;
    }

    DWORD dwAttrib =
      GetFileAttributes (wszFileName);

    if (dwAttrib == INVALID_FILE_ATTRIBUTES)
    {
      return false;
    }

    bool dir = false;

    if (dwAttrib & FILE_ATTRIBUTE_DIRECTORY)
    {
      dir = true;
    }

    else
    {
      dir = false;

      const wchar_t* pwszExt =
        PathFindExtension (wszFileName);

      if (pwszExt != nullptr && *pwszExt == L'.')
      {
        swprintf (wszDot3, L"%3s", CharNextW (pwszExt));
      }

      PathRemoveExtension (wszFileName);
    }

    PathStripPath       (wszFileName);
    PathRemoveBackslash (wszFileName);
    PathRemoveBlanks    (wszFileName);

    wcsncpy_s (wsz8, 11, wszFileName, _TRUNCATE);

    wchar_t idx = 0;

    if (wcslen (wsz8) > 8)
    {
      wsz8 [6] = L'~';
      wsz8 [7] = L'0';
      wsz8 [8] = L'\0';

      swprintf (wsz8Dot3, dir ? L"%s" : L"%s.%s", wsz8, wszDot3);

      while ((! SetFileShortNameW (hFile, wsz8Dot3)) && idx < 9)
      {
        wsz8 [6] = L'~';
        wsz8 [7] = L'0' + idx++;
        wsz8 [8] = L'\0';

        swprintf (wsz8Dot3, dir ? L"%s" : L"%s.%s", wsz8, wszDot3);
      }
    }

    else
    {
      swprintf (wsz8Dot3, dir ? L"%s" : L"%s.%s", wsz8, wszDot3);
    }

    if (idx == 9)
    {
      return false;
    }

    PathRemoveFileSpec (wszFileName1);
    wcsncpy_s          (wszFileName,   MAX_PATH,
                        wszFileName1, _TRUNCATE);
  }

  return true;
}



void
SK_RestartGame (const wchar_t* wszDLL)
{
  wchar_t wszShortPath [MAX_PATH + 2] = { };
  wchar_t wszFullname  [MAX_PATH + 2] = { };

  wcsncpy_s ( wszFullname, MAX_PATH,
             ( wszDLL != nullptr && *wszDLL != L'\0' ) ?
               wszDLL :
              SK_GetModuleFullName ( SK_GetDLL () ).c_str (),
                          _TRUNCATE );

  SK_Generate8Dot3 (wszFullname);
  wcsncpy_s        (wszShortPath, MAX_PATH, wszFullname, _TRUNCATE);

  if (SK_FileHasSpaces (wszFullname))
    GetShortPathName   (wszFullname, wszShortPath, MAX_PATH  );


  if (SK_FileHasSpaces (wszShortPath))
  {
    if (wszDLL != nullptr)
    {
      SK_MessageBox ( L"Your computer is misconfigured; please enable DOS 8.3 filename generation."
                      L"\r\n\r\n\t"
                      L"This is a common problem for non-boot drives, please ensure that the drive your "
                      L"game is installed to has 8.3 filename generation enabled and then re-install "
                      L"the mod.",
                        L"Cannot Automatically Restart Game Because of Bad File system Policy.",
                          MB_OK | MB_SYSTEMMODAL | MB_SETFOREGROUND | MB_ICONASTERISK | MB_TOPMOST );
    }

    else if (SK_HasGlobalInjector ())
    {
      std::wstring global_dll =
        SK_GetDocumentsDir () + LR"(\My Mods\SpecialK\SpecialK)";

#ifdef _WIN64
      global_dll.append (L"64.dll");
#else
      global_dll.append (L"32.dll");
#endif

                wcsncpy_s ( wszFullname, MAX_PATH, global_dll.c_str (), _TRUNCATE );
      GetShortPathName    ( wszFullname, wszShortPath,                   MAX_PATH );
    }

    InterlockedExchange (&__SK_DLL_Ending, 1);

    if (SK_FileHasSpaces (wszShortPath))
      ExitProcess (0x00);
  }

  if (! SK_IsSuperSpecialK ())
  {
    wchar_t wszRunDLLCmd [MAX_PATH * 4] = { };

    swprintf_s ( wszRunDLLCmd, MAX_PATH * 4 - 1,
                 L"RunDll32.exe %s,RunDLL_RestartGame %s",
                   wszShortPath,
                     SK_GetFullyQualifiedApp () );

    STARTUPINFOW        sinfo = { };
    PROCESS_INFORMATION pinfo = { };

    sinfo.cb          = sizeof (STARTUPINFOW);
    sinfo.wShowWindow = SW_HIDE;
    sinfo.dwFlags     = STARTF_USESHOWWINDOW;

    CreateProcess ( nullptr, wszRunDLLCmd,             nullptr, nullptr,
                    FALSE,   CREATE_NEW_PROCESS_GROUP, nullptr, SK_GetHostPath (),
                    &sinfo,  &pinfo );

    CloseHandle (pinfo.hThread);
    CloseHandle (pinfo.hProcess);
  }

  InterlockedExchange (&__SK_DLL_Ending, 1);

  SK_TerminateProcess (0x00);
}


void SK_WinRing0_Unpack (void);
bool SK_WR0_Init        (void);
void SK_WR0_Deinit      (void);

// {3E5FC7F9-9A51-4367-9063-A120244FBEC7}
static const GUID CLSID_SK_ADMINMONIKER =
{ 0x3E5FC7F9, 0x9A51, 0x4367, { 0x90, 0x63, 0xA1, 0x20, 0x24, 0x4F, 0xBE, 0xC7 } };

// {6EDD6D74-C007-4E75-B76A-E5740995E24C}
static const GUID IID_SK_IAdminMoniker =
{ 0x6EDD6D74, 0xC007, 0x4E75, { 0xB7, 0x6A, 0xE5, 0x74, 0x09, 0x95, 0xE2, 0x4C } };

typedef interface SK_IAdminMoniker
                  SK_IAdminMoniker;
typedef struct    SK_IAdminMonikerVtbl
{ BEGIN_INTERFACE

  HRESULT (STDMETHODCALLTYPE *QueryInterface)
                                      (__RPC__in    SK_IAdminMoniker *This,
                                       __RPC__in    REFIID            riid,
                                       _COM_Outptr_ void            **ppvObject);
  ULONG   (STDMETHODCALLTYPE *AddRef )(__RPC__in    SK_IAdminMoniker *This);
  ULONG   (STDMETHODCALLTYPE *Release)(__RPC__in    SK_IAdminMoniker *This);

  HRESULT (STDMETHODCALLTYPE *fn01)(__RPC__in SK_IAdminMoniker *This);
  HRESULT (STDMETHODCALLTYPE *fn02)(__RPC__in SK_IAdminMoniker *This);
  HRESULT (STDMETHODCALLTYPE *fn03)(__RPC__in SK_IAdminMoniker *This);
  HRESULT (STDMETHODCALLTYPE *fn04)(__RPC__in SK_IAdminMoniker *This);
  HRESULT (STDMETHODCALLTYPE *fn05)(__RPC__in SK_IAdminMoniker *This);
  HRESULT (STDMETHODCALLTYPE *fn06)(__RPC__in SK_IAdminMoniker *This);

  HRESULT (STDMETHODCALLTYPE *ShellExec)
      ( __RPC__in SK_IAdminMoniker *This,
        _In_      LPCWSTR           lpFile,
        _In_opt_  LPCTSTR           lpParameters,
        _In_opt_  LPCTSTR           lpDirectory,
        _In_      ULONG             fMask,
        _In_      ULONG             nShow );

  HRESULT (STDMETHODCALLTYPE *SetRegistryStringValue)
      ( __RPC__in SK_IAdminMoniker *This,
         _In_     HKEY              hKey,
         _In_opt_ LPCTSTR           lpSubKey,
         _In_opt_ LPCTSTR           lpValueName,
         _In_     LPCTSTR           lpValueString );

  HRESULT (STDMETHODCALLTYPE *fn09)(__RPC__in SK_IAdminMoniker *This);
  HRESULT (STDMETHODCALLTYPE *fn10)(__RPC__in SK_IAdminMoniker *This);
  HRESULT (STDMETHODCALLTYPE *fn11)(__RPC__in SK_IAdminMoniker *This);
  HRESULT (STDMETHODCALLTYPE *fn12)(__RPC__in SK_IAdminMoniker *This);
  HRESULT (STDMETHODCALLTYPE *fn13)(__RPC__in SK_IAdminMoniker *This);
  HRESULT (STDMETHODCALLTYPE *fn14)(__RPC__in SK_IAdminMoniker *This);
  HRESULT (STDMETHODCALLTYPE *fn15)(__RPC__in SK_IAdminMoniker *This);
  HRESULT (STDMETHODCALLTYPE *fn16)(__RPC__in SK_IAdminMoniker *This);
  HRESULT (STDMETHODCALLTYPE *fn17)(__RPC__in SK_IAdminMoniker *This);
  HRESULT (STDMETHODCALLTYPE *fn18)(__RPC__in SK_IAdminMoniker *This);
  HRESULT (STDMETHODCALLTYPE *fn19)(__RPC__in SK_IAdminMoniker *This);
  HRESULT (STDMETHODCALLTYPE *fn20)(__RPC__in SK_IAdminMoniker *This);

  END_INTERFACE
} *PSK_IAdminMonikerVtbl;

interface SK_IAdminMoniker
{
  CONST_VTBL struct SK_IAdminMonikerVtbl *lpVtbl;
};

HRESULT
SK_COM_CoCreateInstanceAsAdmin ( HWND     hWnd,
                                 REFCLSID rclsid,
                                 REFIID   riid,
                                 void   **ppVoid )
{
  HRESULT    hr        = E_NOT_VALID_STATE;
  wchar_t    wszCLSID       [MAX_PATH + 2]
                       = {};
  wchar_t    wszMonikerName [MAX_PATH + 2]
                       = {};
  BIND_OPTS3 bind_opts =
                   { sizeof (BIND_OPTS3),
                       0x0, 0x0,
                         0, 0,
                     CLSCTX_LOCAL_SERVER,
                       000, nullptr,
                          hWnd           };


   CoInitializeEx ( nullptr,  COINIT_MULTITHREADED );
  StringFromGUID2 ( rclsid,
                      wszCLSID,
                        MAX_PATH + 1 );

  hr =
    StringCchPrintfW ( wszMonikerName, MAX_PATH + 1,
                         L"Elevation:Administrator!new:%ws",
                           wszCLSID );

  if (SUCCEEDED (hr))
  {
    hr =
      CoGetObject ( wszMonikerName, &bind_opts,
                      riid,
                        ppVoid );
  }

  return
    hr;
}

template <class T>
class SK_COM_VtblPtr {
public:
  explicit SK_COM_VtblPtr (T* lp) noexcept
  {
    p = lp;
  }

  ~SK_COM_VtblPtr (void) noexcept
  {
    if ( p != nullptr ) {
         p->lpVtbl->Release (p);
         p = nullptr;   }
  }

  SK_COM_VtblPtr& operator= (_Inout_opt_ T* lp) noexcept
  {
    if ( p != lp &&
         p != nullptr )
    {
      p->lpVtbl->Release (p);
      p = lp;
    }

    return p;
  }

  T* operator-> (void) const noexcept
  {
    return p;
  }

  explicit operator T* (void) const noexcept
  {
    return p;
  }

  T& operator* (void) const
  {
    return *p;
  }

  //Severity	Code	Description	Project	File	Line	Suppression State
  //Warning		Clang : do not overload unary operator&, it is dangerous.[google - runtime - operator]	SpecialK	C : \Users\amcol\source\repos\SpecialK\src\utility.cpp	2873

  //T** operator& (void) noexcept
  //{
  //  return &p;
  //}

  T* p;
};

BOOL
SK_COM_UAC_AdminShellExec ( const wchar_t* wszExecutable,
                            const wchar_t* wszParams,
                            const wchar_t* wszWorkingDir )
{
  SK_COM_VtblPtr
    <SK_IAdminMoniker>
          pAdmin ((SK_IAdminMoniker *)nullptr);
  BOOL      bRet =   FALSE;
  HRESULT     hr =
    SK_COM_CoCreateInstanceAsAdmin ( nullptr,
                                       CLSID_SK_ADMINMONIKER,
                                         IID_SK_IAdminMoniker,
                                           (void **)(&pAdmin) );

  if (SUCCEEDED (hr))
  {
    hr =
      pAdmin->lpVtbl->ShellExec ( (SK_IAdminMoniker *)pAdmin,
                                    wszExecutable,
                                      wszParams,
                                        wszWorkingDir,
                                          0x0,
                                            SW_HIDE );
  }

  if (SUCCEEDED (hr))
    bRet = TRUE;

  return
    bRet;
}

void
CALLBACK
RunDLL_WinRing0 ( HWND  hwnd,        HINSTANCE hInst,
                  LPSTR lpszCmdLine, int       nCmdShow )
{
  UNREFERENCED_PARAMETER (hInst);
  UNREFERENCED_PARAMETER (hwnd);
  UNREFERENCED_PARAMETER (nCmdShow);

  SK_AutoCOMInit auto_com (
    COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE
  );

  if (StrStrA (lpszCmdLine, "Install"))
  {
    wchar_t wszCommand    [MAX_PATH * 4] = { };
    //-----------------------------------------
    wchar_t wszCurrentDir [MAX_PATH + 2] = { };
    wchar_t wszHostDLL    [MAX_PATH + 2] = { };
    wchar_t wszUserDLL    [MAX_PATH + 2] = { };
    wchar_t wszKernelSys  [MAX_PATH + 2] = { };
    wchar_t wszShortDLL   [MAX_PATH + 2] = { };
    wchar_t wszRunDLL32   [MAX_PATH + 2] = { };

    GetSystemDirectoryW  (wszRunDLL32, MAX_PATH);
    GetCurrentDirectoryW (MAX_PATH, wszCurrentDir);
    SK_PathCombineW   ( wszUserDLL, wszCurrentDir,
       SK_RunLHIfBitness ( 64, L"WinRing0x64.dll",
                               L"WinRing0.dll" )
                 );

    SK_PathCombineW ( wszKernelSys, wszCurrentDir,
                                 L"WinRing0x64.sys" ); // 64-bit Drivers Only

    if (! ( PathFileExistsW (wszUserDLL) &&
            PathFileExistsW (wszKernelSys) ) )
    {
      SK_WinRing0_Unpack ();
    }

    if (SK_IsAdmin ())
    {
      SK_WR0_Init ();

      return;
    }

    SK_PathCombineW ( wszHostDLL, wszCurrentDir,
        SK_RunLHIfBitness (64, L"Installer64.dll",
                               L"Installer32.dll")
                 );
    GetShortPathNameW (wszHostDLL, wszShortDLL, MAX_PATH);

    lstrcatW    (wszCommand,                  wszShortDLL);
    lstrcatW    (wszCommand,  L",RunDLL_WinRing0 Install");
    PathAppendW (wszRunDLL32, L"RunDLL32.exe"            );

    if ( SK_COM_UAC_AdminShellExec (
           wszRunDLL32,
             wszCommand,
               wszCurrentDir )
       )
    {
      int tries = 0;

      do {
        SK_Sleep (25UL);
      } while ( GetFileAttributes (wszHostDLL) != INVALID_FILE_ATTRIBUTES &&
                                         tries++ < 32 );
    }

    // Fallback to "runas" if the COM interface above does not function
    //   as designed.
    else
    { SHELLEXECUTEINFO
      sexec_info;
      sexec_info.cbSize       = sizeof (SHELLEXECUTEINFO);
      sexec_info.fMask        = SEE_MASK_NOCLOSEPROCESS;
      sexec_info.hwnd         = nullptr;
      sexec_info.lpVerb       = L"runas";
      sexec_info.lpFile       = wszRunDLL32;
      sexec_info.lpParameters = wszCommand;
      sexec_info.lpDirectory  = wszCurrentDir;
      sexec_info.nShow        = SW_HIDE;
      sexec_info.hInstApp     = nullptr;

      ShellExecuteEx (&sexec_info);

      WaitForSingleObject (sexec_info.hProcess, INFINITE);
      CloseHandle         (sexec_info.hProcess);
    }
  }

  else if (StrStrA (lpszCmdLine, "Uninstall"))
  {
    DWORD   dwTime                       = timeGetTime ();
    wchar_t wszCurrentDir [MAX_PATH + 2] = { };
    wchar_t wszUserDLL    [MAX_PATH + 2] = { };
    wchar_t wszKernelSys  [MAX_PATH + 2] = { };
    wchar_t wszServiceCtl [MAX_PATH + 2] = { };

    GetSystemDirectoryW  (wszServiceCtl, MAX_PATH);
    GetCurrentDirectoryW (MAX_PATH, wszCurrentDir);

    SK_PathCombineW ( wszUserDLL, wszCurrentDir,
       SK_RunLHIfBitness ( 64, L"WinRing0x64.dll",
                               L"WinRing0.dll" )
                 );

    SK_PathCombineW ( wszKernelSys, wszCurrentDir,
                               L"WinRing0x64.sys" ); // 64-bit Drivers Only

    auto wszCommand0 = L"stop WinRing0_1_2_0";
    auto wszCommand1 = L"delete WinRing0_1_2_0";

    PathAppendW (wszServiceCtl, L"sc.exe");

    if ( SK_COM_UAC_AdminShellExec (
           wszServiceCtl,
             wszCommand0,
               wszCurrentDir )
       )
    {
      SK_Sleep (100UL);

      if ( SK_COM_UAC_AdminShellExec (
             wszServiceCtl,
               wszCommand1,
                 wszCurrentDir )
         )
      {
        SK_WR0_Deinit ();

        wchar_t wszTemp [MAX_PATH + 2] = { };

        GetTempFileNameW        ( wszCurrentDir,  L"SKI",
                                  dwTime,         wszTemp );
        SK_File_MoveNoFail      ( wszUserDLL,     wszTemp );
        GetTempFileNameW        ( wszCurrentDir,  L"SKI",
                                  dwTime+1,       wszTemp );
        SK_File_MoveNoFail      ( wszKernelSys,   wszTemp );
        SK_DeleteTemporaryFiles ( wszCurrentDir           );

        return;
      }
    }

    // Fallback to "runas" if the COM interface above does not function
    //   as designed.
    { SHELLEXECUTEINFO
      sexec_info;
      sexec_info.cbSize       = sizeof (SHELLEXECUTEINFO);
      sexec_info.fMask        = SEE_MASK_NOCLOSEPROCESS;
      sexec_info.hwnd         = nullptr;
      sexec_info.lpVerb       = L"runas";
      sexec_info.lpFile       = wszServiceCtl;
      sexec_info.lpParameters = wszCommand0;
      sexec_info.lpDirectory  = wszCurrentDir;
      sexec_info.nShow        = SW_HIDE;
      sexec_info.hInstApp     = nullptr;

      ShellExecuteEx (&sexec_info);

      WaitForSingleObject (sexec_info.hProcess, INFINITE);
      CloseHandle         (sexec_info.hProcess);

      // ------------------

      sexec_info.cbSize       = sizeof (SHELLEXECUTEINFO);
      sexec_info.fMask        = SEE_MASK_NOCLOSEPROCESS;
      sexec_info.hwnd         = nullptr;
      sexec_info.lpVerb       = L"runas";
      sexec_info.lpFile       = wszServiceCtl;
      sexec_info.lpParameters = wszCommand1;
      sexec_info.lpDirectory  = wszCurrentDir;
      sexec_info.nShow        = SW_HIDE;
      sexec_info.hInstApp     = nullptr;

      ShellExecuteEx (&sexec_info);

      WaitForSingleObject (sexec_info.hProcess, INFINITE);
      CloseHandle         (sexec_info.hProcess);

      wchar_t wszTemp [MAX_PATH + 2] = { };

      GetTempFileNameW        ( wszCurrentDir,  L"SKI",
                                dwTime,         wszTemp );
      SK_File_MoveNoFail      ( wszUserDLL,     wszTemp );
      GetTempFileNameW        ( wszCurrentDir,  L"SKI",
                                dwTime+1,       wszTemp );
      SK_File_MoveNoFail      ( wszKernelSys,   wszTemp );
      SK_DeleteTemporaryFiles ( wszCurrentDir           );

      return;
    }
  }
}

void
SK_WinRing0_Uninstall (void)
{
  static std::wstring path_to_driver =
    SK_FormatStringW ( LR"(%ws\My Mods\SpecialK\Drivers\WinRing0\)",
                       SK_GetDocumentsDir ().c_str () );

  static std::wstring kernelmode_driver_path =
    path_to_driver + std::wstring (L"WinRing0x64.sys"); // 64-bit Drivers Only

  static std::wstring installer_path =
    path_to_driver + std::wstring (
                       SK_RunLHIfBitness (64, L"Installer64.dll",
                                              L"Installer32.dll") );

  extern volatile LONG __SK_WR0_Init;
  SK_WR0_Deinit ();

  if (GetFileAttributesW (kernelmode_driver_path.c_str ()) == INVALID_FILE_ATTRIBUTES)
  {
    InterlockedExchange (&__SK_WR0_Init, 0L);
    return;
  }

  std::wstring src_dll =
    SK_GetModuleFullName (skModuleRegistry::Self ());

  wchar_t wszTemp [MAX_PATH + 2] = { };
  DWORD   dwTime                 = timeGetTime ();

  GetTempFileNameW        (path_to_driver.c_str         (), L"SKI",
                           dwTime                         , wszTemp);
  SK_File_MoveNoFail      (kernelmode_driver_path.c_str (), wszTemp);
  SK_DeleteTemporaryFiles (path_to_driver.c_str         ()         );

  if (PathFileExistsW (installer_path.c_str ()))
          DeleteFileW (installer_path.c_str ());

  if (CopyFileW (src_dll.c_str (), installer_path.c_str (), FALSE))
  {
    wchar_t wszRunDLLCmd [MAX_PATH * 4] = { };
    wchar_t wszShortPath [MAX_PATH + 2] = { };
    wchar_t wszFullname  [MAX_PATH + 2] = { };

    wcsncpy_s (wszFullname, MAX_PATH, installer_path.c_str (),
                           _TRUNCATE );

    SK_Generate8Dot3     (wszFullname);
    wcscpy (wszShortPath, wszFullname);

    if (SK_FileHasSpaces (wszFullname))
      GetShortPathName   (wszFullname, wszShortPath, MAX_PATH );

    swprintf_s ( wszRunDLLCmd, MAX_PATH * 4 - 1,
                 L"RunDll32.exe %ws,RunDLL_WinRing0 Uninstall",
                   wszShortPath );

    STARTUPINFOW        sinfo = { };
    PROCESS_INFORMATION pinfo = { };

    sinfo.cb          = sizeof (STARTUPINFOW);
    sinfo.wShowWindow = SW_HIDE;
    sinfo.dwFlags     = STARTF_USESHOWWINDOW;

    CreateProcess ( nullptr, wszRunDLLCmd,             nullptr, nullptr,
                    FALSE,   CREATE_NEW_PROCESS_GROUP, nullptr, path_to_driver.c_str (),
                    &sinfo,  &pinfo );

    DWORD dwWaitState = 1;

    do { if (   WAIT_OBJECT_0 ==
                WaitForSingleObject (pinfo.hProcess, 50UL) )
      {       dwWaitState  = WAIT_OBJECT_0;                }
      else  { dwWaitState++; SK_Sleep (4);                 }
    } while ( dwWaitState < 25 &&
              dwWaitState != WAIT_OBJECT_0 );

    CloseHandle (pinfo.hThread);
    CloseHandle (pinfo.hProcess);

    RtlSecureZeroMemory     (wszTemp, sizeof (wchar_t) * (MAX_PATH + 2));
    GetTempFileNameW        (path_to_driver.c_str         (), L"SKI",
                             dwTime                         , wszTemp);
    SK_File_MoveNoFail      (kernelmode_driver_path.c_str (), wszTemp);
    RtlSecureZeroMemory     (wszTemp, sizeof(wchar_t) * (MAX_PATH + 2));
    GetTempFileNameW        (path_to_driver.c_str         (), L"SKI",
                             dwTime+1                       , wszTemp);
    SK_File_MoveNoFail      (installer_path.c_str         (), wszTemp);
    SK_DeleteTemporaryFiles (path_to_driver.c_str         ()         );

    InterlockedExchange (&__SK_WR0_Init, 0L);
    return;
  }
}

void
SK_WinRing0_Install (void)
{
  SK_WinRing0_Unpack ();

  if (SK_IsAdmin ())
  {
    if (SK_WR0_Init ())
      return;
  }

  static std::wstring path_to_driver =
    SK_FormatStringW ( LR"(%ws\My Mods\SpecialK\Drivers\WinRing0\)",
                       SK_GetDocumentsDir ().c_str () );

  static std::wstring installer_path =
    path_to_driver + std::wstring (
                       SK_RunLHIfBitness (64, L"Installer64.dll",
                                              L"Installer32.dll") );

  if (GetFileAttributesW (installer_path.c_str ()) == INVALID_FILE_ATTRIBUTES)
    SK_CreateDirectories (installer_path.c_str ());

  const std::wstring src_dll =
    SK_GetModuleFullName (skModuleRegistry::Self ());

  if (PathFileExistsW (installer_path.c_str ()))
          DeleteFileW (installer_path.c_str ());

  if (CopyFileW (src_dll.c_str (), installer_path.c_str (), FALSE))
  {
                  wchar_t wszRunDLL32 [MAX_PATH + 2] = { };
    GetSystemDirectoryW  (wszRunDLL32, MAX_PATH);
    PathAppendW          (wszRunDLL32, L"RunDLL32.exe");

    wchar_t wszRunDLLCmd [MAX_PATH * 4] = { };
    wchar_t wszShortPath [MAX_PATH + 2] = { };
    wchar_t wszFullname  [MAX_PATH + 2] = { };

    wcsncpy_s        (wszFullname,  MAX_PATH,
                                  installer_path.c_str (), _TRUNCATE);

    SK_Generate8Dot3 (                        wszFullname);
    wcsncpy_s        (wszShortPath, MAX_PATH, wszFullname, _TRUNCATE);

    if (SK_FileHasSpaces (wszFullname))
      GetShortPathName   (wszFullname, wszShortPath, MAX_PATH );

    swprintf_s ( wszRunDLLCmd, MAX_PATH * 4 - 1,
                 L"%s %s,RunDLL_WinRing0 Install",
                   wszRunDLL32, wszShortPath );

    STARTUPINFOW        sinfo = { };
    PROCESS_INFORMATION pinfo = { };

    sinfo.cb          = sizeof (STARTUPINFOW);
    sinfo.wShowWindow = SW_HIDE;
    sinfo.dwFlags     = STARTF_USESHOWWINDOW;

    CreateProcess ( nullptr, wszRunDLLCmd,             nullptr, nullptr,
                    FALSE,   CREATE_NEW_PROCESS_GROUP, nullptr, path_to_driver.c_str (),
                    &sinfo,  &pinfo );

    DWORD dwWaitState = 1;

    do { if (   WAIT_OBJECT_0 ==
                WaitForSingleObject (pinfo.hProcess, 50UL) )
      {       dwWaitState  = WAIT_OBJECT_0;                }
      else  { dwWaitState++; SK_Sleep (4);                 }
    } while ( dwWaitState < 25 &&
              dwWaitState != WAIT_OBJECT_0 );

    CloseHandle (pinfo.hThread);
    CloseHandle (pinfo.hProcess);

    wchar_t wszTemp [MAX_PATH + 2] = { };

    GetTempFileNameW        (path_to_driver.c_str (), L"SKI",
                             timeGetTime          (), wszTemp);
    SK_File_MoveNoFail      (installer_path.c_str (), wszTemp);
    SK_DeleteTemporaryFiles (path_to_driver.c_str ()         );
  }
}

void
SK_ElevateToAdmin (void)
{
  wchar_t wszRunDLLCmd [MAX_PATH * 4] = { };
  wchar_t wszShortPath [MAX_PATH + 2] = { };
  wchar_t wszFullname  [MAX_PATH + 2] = { };

  wcsncpy_s (wszFullname, MAX_PATH, SK_GetModuleFullName (SK_GetDLL ()).c_str (), _TRUNCATE );

  SK_Generate8Dot3                 (wszFullname);
  wcscpy_s (wszShortPath, MAX_PATH, wszFullname);

  if (SK_FileHasSpaces (wszFullname))
    GetShortPathName   (wszFullname, wszShortPath, MAX_PATH );

  if (SK_FileHasSpaces (wszShortPath))
  {
    SK_MessageBox ( L"Your computer is misconfigured; please enable DOS 8.3 filename generation."
                    L"\r\n\r\n\t"
                    L"This is a common problem for non-boot drives, please ensure that the drive your "
                    L"game is installed to has 8.3 filename generation enabled and then re-install "
                    L"the mod.",
                      L"Cannot Elevate To Admin Because of Bad File system Policy.",
                        MB_OK | MB_SYSTEMMODAL | MB_SETFOREGROUND | MB_ICONASTERISK | MB_TOPMOST );
    ExitProcess   (0x00);
  }

  swprintf_s ( wszRunDLLCmd, MAX_PATH * 4 - 1,
               L"RunDll32.exe %s,RunDLL_ElevateMe %s",
                 wszShortPath,
                   SK_GetFullyQualifiedApp () );

  STARTUPINFOW        sinfo = { };
  PROCESS_INFORMATION pinfo = { };

  sinfo.cb          = sizeof (STARTUPINFOW);
  sinfo.wShowWindow = SW_HIDE;
  sinfo.dwFlags     = STARTF_USESHOWWINDOW;

  CreateProcess ( nullptr, wszRunDLLCmd,             nullptr, nullptr,
                  FALSE,   CREATE_NEW_PROCESS_GROUP, nullptr, SK_GetHostPath (),
                  &sinfo,  &pinfo );

  CloseHandle (pinfo.hThread);
  CloseHandle (pinfo.hProcess);

  SK_TerminateProcess (0x00);
}

std::string
__cdecl
SK_FormatString (char const* const _Format, ...)
{
  size_t len = 0;

  va_list   _ArgList;
  va_start (_ArgList, _Format);
  {
    len =
      vsnprintf ( nullptr, 0, _Format, _ArgList ) + 1;
  }
  va_end   (_ArgList);

  size_t alloc_size =
    sizeof (char) * (len + 2);

  SK_TLS* pTLS =
    nullptr;

  char* pData =
    ( ReadAcquire (&__SK_DLL_Attached) &&
              (pTLS = SK_TLS_Bottom ()) != nullptr )              ?
       (char *)pTLS->scratch_memory->eula.alloc (alloc_size, true) :
       (char *)SK_LocalAlloc (             LPTR, alloc_size      );

  if (! pData)
    return std::string ();

  va_start (_ArgList, _Format);
  {
    len =
      vsnprintf ( pData, len + 1, _Format, _ArgList );
  }
  va_end   (_ArgList);

  return
    pData;
}

int
__cdecl
SK_FormatString (std::string& out, char const* const _Format, ...)
{
  intptr_t len = 0;

  va_list   _ArgList;
  va_start (_ArgList, _Format);
  {
    len =
      vsnprintf (nullptr, 0, _Format, _ArgList);
  }
  va_end (_ArgList);

  if (out.capacity () < (size_t)len)
                    out.resize (len);

  va_start (_ArgList, _Format);
  {
    len =
      vsnprintf (out.data (), len, _Format, _ArgList);
  }
  va_end (_ArgList);

  return
    static_cast <int> (len);
}

std::wstring
__cdecl
SK_FormatStringW (wchar_t const* const _Format, ...)
{
  size_t len = 0;

  va_list   _ArgList;
  va_start (_ArgList, _Format);
  {
    len =
      _vsnwprintf ( nullptr, 0, _Format, _ArgList ) + 1;
  }
  va_end   (_ArgList);

  size_t alloc_size =
    sizeof (wchar_t) * (len + 2);

  SK_TLS* pTLS =
    nullptr;

  wchar_t* pData =
    ( ReadAcquire (&__SK_DLL_Attached) &&
              (pTLS = SK_TLS_Bottom ()) != nullptr )              ?
    (wchar_t *)pTLS->scratch_memory->eula.alloc (alloc_size, true) :
    (wchar_t *)SK_LocalAlloc (              LPTR, alloc_size      );

  if (! pData)
    return std::wstring ();

  va_start (_ArgList, _Format);
  {
    len =
      _vsnwprintf ( (wchar_t *)pData, len + 1, _Format, _ArgList );
  }
  va_end   (_ArgList);

  return
    pData;
}

int
__cdecl
SK_FormatStringW (std::wstring& out, wchar_t const* const _Format, ...)
{
  int len = 0;

  va_list   _ArgList;
  va_start (_ArgList, _Format);
  {
    len =
      _vsnwprintf (nullptr, 0, _Format, _ArgList);
  }
  va_end (_ArgList);

  if (out.capacity () < (size_t)len)
                    out.resize (len);

  va_start (_ArgList, _Format);
  {
    len =
      _vsnwprintf ((wchar_t*)out.data (), len, _Format, _ArgList);
  }
  va_end (_ArgList);

  return
    len;
}

void
SK_StripLeadingSlashesW (wchar_t *wszInOut)
{
  auto IsSlash = [](wchar_t a) -> bool {
    return (a == L'\\' || a == L'/');
  };

  size_t      len = wcslen (wszInOut);
  size_t  new_len = len;

  wchar_t* wszStart = wszInOut;

  while (          *wszStart  != L'\0' &&
        *CharNextW (wszStart) != L'\0' )
  {
    if (IsSlash (*wszStart))
    {
      wszStart =
        CharNextW (wszStart);

      --new_len;
    }

    else
      break;
  }

  if (len != new_len)
  {
    wchar_t *wszOut = wszInOut;
    wchar_t *wszIn  = wszStart;

    for ( size_t i = 0 ; i < new_len ; ++i )
    {
      *wszOut =           *wszIn;

      if (*wszOut == L'\0')
        break;

       wszIn  = CharNextW (wszIn);
       wszOut = CharNextW (wszOut);
    }
  }

  // Else:
}
//
// In-place version of the old code that had to
//   make a copy of the string and then copy-back
//
void
SK_StripTrailingSlashesW (wchar_t* wszInOut)
{
  //wchar_t* wszValidate = wcsdup (wszInOut);

  auto IsSlash = [](wchar_t a) -> bool {
    return (a == L'\\' || a == L'/');
  };

  wchar_t* wszNextUnique = CharNextW (wszInOut);
  wchar_t* wszNext       = wszInOut;

  while (*wszNext != L'\0')
  {
    if (*wszNextUnique == L'\0')
    {
      *CharNextW (wszNext) = L'\0';
      break;
    }

    if (IsSlash (*wszNext))
    {
      if (IsSlash (*wszNextUnique))
      {
        wszNextUnique =
          CharNextW (wszNextUnique);

        continue;
      }
    }

    wszNext = CharNextW (wszNext);
   *wszNext = *wszNextUnique;
    wszNextUnique =
      CharNextW (wszNextUnique);
  }


  //extern void
  //  SK_StripTrailingSlashesW (wchar_t* wszInOut);
  //
  //wchar_t xxx [] = LR"(\\//\/\asda.\\/fasd/ads\d///\asdz/\\/)";
  //wchar_t yyy [] = LR"(a\\)";
  //wchar_t zzz [] = LR"(\\a)";
  //wchar_t yzy [] = LR"(\a/)";
  //wchar_t zzy [] = LR"(\/a)";
  //
  //SK_StripTrailingSlashesW (xxx);
  //SK_StripTrailingSlashesW (yyy);
  //SK_StripTrailingSlashesW (zzz);
  //SK_StripTrailingSlashesW (yzy);
  //SK_StripTrailingSlashesW (zzy);      //extern void
      //  SK_StripTrailingSlashesW (wchar_t* wszInOut);
      //
      //wchar_t xxx [] = LR"(\\//\/\asda.\\/fasd/ads\d///\asdz/\\/)";
      //wchar_t yyy [] = LR"(a\\)";
      //wchar_t zzz [] = LR"(\\a)";
      //wchar_t yzy [] = LR"(\a/)";
      //wchar_t zzy [] = LR"(\/a)";
      //
      //SK_StripTrailingSlashesW (xxx);
      //SK_StripTrailingSlashesW (yyy);
      //SK_StripTrailingSlashesW (zzz);
      //SK_StripTrailingSlashesW (yzy);
      //SK_StripTrailingSlashesW (zzy);

  //SK_StripTrailingSlashesW_ (wszValidate);
  //
  //SK_ReleaseAssert (! wcscmp (wszValidate, wszInOut))
  //
  //dll_log->Log (L"'%ws' and '%ws' are the same", wszValidate, wszInOut);
}

void
SK_FixSlashesW (wchar_t* wszInOut)
{
  if (wszInOut == nullptr)
    return;

  wchar_t* pwszInOut  = wszInOut;
  while ( *pwszInOut != L'\0' )
  {
    if (*pwszInOut == L'/')
        *pwszInOut = L'\\';

    pwszInOut =
      CharNextW (pwszInOut);
  }
}

void
SK_StripTrailingSlashesA (char* szInOut)
{
  auto IsSlash = [](char a) -> bool {
    return (a == '\\' || a == '/');
  };

  char* szNextUnique = szInOut + 1;
  char* szNext       = szInOut;

  while (*szNext != '\0')
  {
    if (*szNextUnique == '\0')
    {
      *CharNextA (szNext) = '\0';
      break;
    }

    if (IsSlash (*szNext))
    {
      if (IsSlash (*szNextUnique))
      {
        szNextUnique =
          CharNextA (szNextUnique);

        continue;
      }
    }

    ++szNext;
     *szNext = *szNextUnique;
                szNextUnique =
     CharNextA (szNextUnique);
  }
}

void
SK_FixSlashesA (char* szInOut)
{
  if (szInOut == nullptr)
    return;

  char*   pszInOut  = szInOut;
  while (*pszInOut != '\0')
  {
    if (*pszInOut == '/')
        *pszInOut = '\\';

    pszInOut =
      CharNextA (pszInOut);
  }
}

_Success_(return != 0)
BOOLEAN
WINAPI
SK_GetUserNameExA (
  _In_                               EXTENDED_NAME_FORMAT  NameFormat,
  _Out_writes_to_opt_(*nSize,*nSize) LPSTR                 lpNameBuffer,
  _Inout_                            PULONG                nSize )
{
  using GetUserNameExA =
    BOOLEAN (WINAPI *)(EXTENDED_NAME_FORMAT, LPSTR, PULONG);

  static auto      hModSecur32 =
    SK_LoadLibraryW (L"Secur32.dll");

  static auto
    _GetUserNameExA =
    (GetUserNameExA)SK_GetProcAddress ( hModSecur32,
    "GetUserNameExA");

  return
    _GetUserNameExA (NameFormat, lpNameBuffer, nSize);
}

_Success_(return != 0)
BOOLEAN
SEC_ENTRY
SK_GetUserNameExW (
    _In_                               EXTENDED_NAME_FORMAT NameFormat,
    _Out_writes_to_opt_(*nSize,*nSize) LPWSTR               lpNameBuffer,
    _Inout_                            PULONG               nSize )
{
  using GetUserNameExW =
    BOOLEAN (WINAPI *)(EXTENDED_NAME_FORMAT, LPWSTR, PULONG);

  static auto      hModSecur32 =
    SK_LoadLibraryW (L"Secur32.dll");

  static auto
    _GetUserNameExW =
    (GetUserNameExW)SK_GetProcAddress ( hModSecur32,
    "GetUserNameExW");

  return
    _GetUserNameExW (NameFormat, lpNameBuffer, nSize);
}

// Doesn't need to be this complicated; it's a string function, might as well optimize it.

static char     szUserName        [MAX_PATH + 2] = { };
static char     szUserNameDisplay [MAX_PATH + 2] = { };
static char     szUserProfile     [MAX_PATH + 2] = { }; // Most likely to match
static wchar_t wszUserName        [MAX_PATH + 2] = { };
static wchar_t wszUserNameDisplay [MAX_PATH + 2] = { };
static wchar_t wszUserProfile     [MAX_PATH + 2] = { }; // Most likely to match

char*
SK_StripUserNameFromPathA (char* szInOut)
{
  if (*szUserProfile == '\0')
  {
                                        uint32_t len = MAX_PATH;
    if (! SK_GetUserProfileDir (wszUserProfile, &len))
      *wszUserProfile = L'?'; // Invalid filesystem char
    else
      PathStripPathW (wszUserProfile);

    strncpy_s ( szUserProfile, MAX_PATH,
                  SK_WideCharToUTF8 (wszUserProfile).c_str (), _TRUNCATE );
  }

  if (*szUserName == '\0')
  {
                                           DWORD dwLen = MAX_PATH;
    SK_GetUserNameExA (NameUnknown, szUserName, &dwLen);

    if (dwLen == 0)
      *szUserName = '?'; // Invalid filesystem char
    else
      PathStripPathA (szUserName);
  }

  if (*szUserNameDisplay == '\0')
  {
                                                  DWORD dwLen = MAX_PATH;
    SK_GetUserNameExA (NameDisplay, szUserNameDisplay, &dwLen);

    if (dwLen == 0)
      *szUserNameDisplay = '?'; // Invalid filesystem char
  }

  char* pszUserNameSubstr =
    StrStrIA (szInOut, szUserProfile);

  if (pszUserNameSubstr != nullptr)
  {
    static const size_t len =
      strlen (szUserProfile);

    for (size_t i = 0; i < len; i++)
    {
      *pszUserNameSubstr = '*';
       pszUserNameSubstr = CharNextA (pszUserNameSubstr);

       if (pszUserNameSubstr == nullptr) break;
    }

    return szInOut;
  }

  pszUserNameSubstr =
    StrStrIA (szInOut, szUserNameDisplay);

  if (pszUserNameSubstr != nullptr)
  {
    static const size_t len =
      strlen (szUserNameDisplay);

    for (size_t i = 0; i < len; i++)
    {
      *pszUserNameSubstr = '*';
       pszUserNameSubstr = CharNextA (pszUserNameSubstr);

       if (pszUserNameSubstr == nullptr) break;
    }

    return szInOut;
  }

  pszUserNameSubstr =
    StrStrIA (szInOut, szUserName);

  if (pszUserNameSubstr != nullptr)
  {
    static const size_t len =
      strlen (szUserName);

    for (size_t i = 0; i < len; i++)
    {
      *pszUserNameSubstr = '*';
       pszUserNameSubstr = CharNextA (pszUserNameSubstr);

       if (pszUserNameSubstr == nullptr) break;
    }

    return szInOut;
  }

  return szInOut;
}

wchar_t*
SK_StripUserNameFromPathW (wchar_t* wszInOut)
{
  if (*wszUserProfile == L'\0')
  {
                                        uint32_t len = MAX_PATH;
    if (! SK_GetUserProfileDir (wszUserProfile, &len))
      *wszUserProfile = L'?'; // Invalid filesystem char
    else
      PathStripPathW (wszUserProfile);
  }

  if (*wszUserName == L'\0')
  {
                                                  DWORD dwLen = MAX_PATH;
    SK_GetUserNameExW (NameSamCompatible, wszUserName, &dwLen);

    if (dwLen == 0)
      *wszUserName = L'?'; // Invalid filesystem char
    else
      PathStripPathW (wszUserName);
  }

  if (*wszUserNameDisplay == L'\0')
  {
                                                   DWORD dwLen = MAX_PATH;
    SK_GetUserNameExW (NameDisplay, wszUserNameDisplay, &dwLen);

    if (dwLen == 0)
      *wszUserNameDisplay = L'?'; // Invalid filesystem char
  }

  if (config.system.log_level > 4)
  {
    SK_RunOnce (
      dll_log->Log ( L"Profile: %ws, User: %ws, Display: %ws",
                       wszUserProfile, wszUserName, wszUserNameDisplay )
    );
  }


  wchar_t* pwszUserNameSubstr =
    StrStrIW (wszInOut, wszUserProfile);

  if (pwszUserNameSubstr != nullptr)
  {
    static const size_t len =
      wcslen (wszUserProfile);

    for (size_t i = 0; i < len; i++)
    {
      *pwszUserNameSubstr = L'*';
       pwszUserNameSubstr = CharNextW (pwszUserNameSubstr);

       if (pwszUserNameSubstr == nullptr) break;
    }

    return wszInOut;
  }

  pwszUserNameSubstr =
    StrStrIW (wszInOut, wszUserNameDisplay);

  if (pwszUserNameSubstr != nullptr)
  {
    static const size_t len =
      wcslen (wszUserNameDisplay);

    for (size_t i = 0; i < len; i++)
    {
      *pwszUserNameSubstr = L'*';
       pwszUserNameSubstr = CharNextW (pwszUserNameSubstr);

       if (pwszUserNameSubstr == nullptr) break;
    }

    return wszInOut;
  }

  pwszUserNameSubstr =
    StrStrIW (wszInOut, wszUserName);

  if (pwszUserNameSubstr != nullptr)
  {
    static const size_t len =
      wcslen (wszUserName);

    for (size_t i = 0; i < len; i++)
    {
      *pwszUserNameSubstr = L'*';
       pwszUserNameSubstr = CharNextW (pwszUserNameSubstr);

       if (pwszUserNameSubstr == nullptr) break;
    }

    return wszInOut;
  }

  return wszInOut;
}

void
SK_DeferCommands (const char** szCommands, int count)
{
  if (szCommands == nullptr || count == 0)
    return;

  static          concurrency::concurrent_queue <std::string> cmds;
  static          HANDLE                                      hNewCmds =
    SK_CreateEvent (nullptr, FALSE, FALSE, nullptr);
  static volatile HANDLE                                      hCommandThread = nullptr;

  for (int i = 0; i < count; i++)
  {
    cmds.push (szCommands [i]);
  }

  SetEvent (hNewCmds);

  // ============================================== //

  if (! InterlockedCompareExchangePointer (&hCommandThread, (LPVOID)1, nullptr))
  {     InterlockedExchangePointer        ((void **)&hCommandThread,

    SK_Thread_CreateEx (
      [](LPVOID) ->
      DWORD
      {
        SetCurrentThreadDescription (                      L"[SK] Async Command Processor" );
        SetThreadPriority           ( SK_GetCurrentThread (), THREAD_PRIORITY_IDLE         );

        static HANDLE wait_objs [] = {
          hNewCmds, __SK_DLL_TeardownEvent
        };

        while (! ReadAcquire (&__SK_DLL_Ending))
        {
          DWORD dwWait =
            WaitForMultipleObjects (2, wait_objs, FALSE, INFINITE);

          if (dwWait == WAIT_OBJECT_0)
          {
            std::string cmd;

            while (cmds.try_pop (cmd))
            {
              SK_GetCommandProcessor ()->ProcessCommandLine (cmd.c_str ());
            }
          }

          // DLL Teardown
          else if (dwWait == WAIT_OBJECT_0 + 1)
          {
            break;
          }
        }

        CloseHandle (hNewCmds);
        SK_Thread_CloseSelf ();

        return 0;
      }
    ) );
  }
};

//
// Issue a command that would deadlock the game if executed synchronously
//   from the render thread.
//
void
SK_DeferCommand (const char* szCommand)
{
  return
    SK_DeferCommands (&szCommand, 1);
};


void
SK_HostAppUtil::init (void)
{
  SK_RunOnce (SKIF     = (StrStrIW ( SK_GetHostApp (), L"SKIF"     ) != nullptr));
  SK_RunOnce (SKIM     = (StrStrIW ( SK_GetHostApp (), L"SKIM"     ) != nullptr));
  SK_RunOnce (RunDll32 = (StrStrIW ( SK_GetHostApp (), L"RunDLL32" ) != nullptr));
}

SK_HostAppUtil*
SK_GetHostAppUtil (void)
{
  // Push the statically initialized value onto the global datastore in the
  //   form of a pointer so that we have an easier time debugging this.
  return host_app_util.getPtr ();
}



const wchar_t*
__stdcall
SK_GetCanonicalDLLForRole (enum DLL_ROLE role)
{
  switch (role)
  {
    case DLL_ROLE::DXGI:
      return L"dxgi.dll";
    case DLL_ROLE::D3D9:
      return L"d3d9.dll";
    case DLL_ROLE::D3D8:
      return L"d3d8.dll";
    case DLL_ROLE::DDraw:
      return L"ddraw.dll";
    case DLL_ROLE::OpenGL:
      return L"OpenGL32.dll";
    case DLL_ROLE::Vulkan:
      return L"vk-1.dll";
    case DLL_ROLE::DInput8:
      return L"dinput8.dll";
    default:
      return SK_RunLHIfBitness ( 64, L"SpecialK64.dll",
                                     L"SpecialK32.dll" );
  }
}

const wchar_t*
SK_DescribeHRESULT (HRESULT hr)
{
  switch (hr)
  {
    /* Generic (SUCCEEDED) */

  case S_OK:
    return L"S_OK";

  case S_FALSE:
    return L"S_FALSE";


#ifndef SK_BUILD__INSTALLER
    /* DXGI */

  case DXGI_ERROR_DEVICE_HUNG:
    return L"DXGI_ERROR_DEVICE_HUNG";

  case DXGI_ERROR_DEVICE_REMOVED:
    return L"DXGI_ERROR_DEVICE_REMOVED";

  case DXGI_ERROR_DEVICE_RESET:
    return L"DXGI_ERROR_DEVICE_RESET";

  case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
    return L"DXGI_ERROR_DRIVER_INTERNAL_ERROR";

  case DXGI_ERROR_FRAME_STATISTICS_DISJOINT:
    return L"DXGI_ERROR_FRAME_STATISTICS_DISJOINT";

  case DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE:
    return L"DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE";

  case DXGI_ERROR_INVALID_CALL:
    return L"DXGI_ERROR_INVALID_CALL";

  case DXGI_ERROR_MORE_DATA:
    return L"DXGI_ERROR_MORE_DATA";

  case DXGI_ERROR_NONEXCLUSIVE:
    return L"DXGI_ERROR_NONEXCLUSIVE";

  case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE:
    return L"DXGI_ERROR_NOT_CURRENTLY_AVAILABLE";

  case DXGI_ERROR_NOT_FOUND:
    return L"DXGI_ERROR_NOT_FOUND";

  case DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED:
    return L"DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED";

  case DXGI_ERROR_REMOTE_OUTOFMEMORY:
    return L"DXGI_ERROR_REMOTE_OUTOFMEMORY";

  case DXGI_ERROR_WAS_STILL_DRAWING:
    return L"DXGI_ERROR_WAS_STILL_DRAWING";

  case DXGI_ERROR_UNSUPPORTED:
    return L"DXGI_ERROR_UNSUPPORTED";

  case DXGI_ERROR_ACCESS_LOST:
    return L"DXGI_ERROR_ACCESS_LOST";

  case DXGI_ERROR_WAIT_TIMEOUT:
    return L"DXGI_ERROR_WAIT_TIMEOUT";

  case DXGI_ERROR_SESSION_DISCONNECTED:
    return L"DXGI_ERROR_SESSION_DISCONNECTED";

  case DXGI_ERROR_RESTRICT_TO_OUTPUT_STALE:
    return L"DXGI_ERROR_RESTRICT_TO_OUTPUT_STALE";

  case DXGI_ERROR_CANNOT_PROTECT_CONTENT:
    return L"DXGI_ERROR_CANNOT_PROTECT_CONTENT";

  case DXGI_ERROR_ACCESS_DENIED:
    return L"DXGI_ERROR_ACCESS_DENIED";

  case DXGI_ERROR_NAME_ALREADY_EXISTS:
    return L"DXGI_ERROR_NAME_ALREADY_EXISTS";

  case DXGI_ERROR_SDK_COMPONENT_MISSING:
    return L"DXGI_ERROR_SDK_COMPONENT_MISSING";

  case DXGI_DDI_ERR_WASSTILLDRAWING:
    return L"DXGI_DDI_ERR_WASSTILLDRAWING";

  case DXGI_DDI_ERR_UNSUPPORTED:
    return L"DXGI_DDI_ERR_UNSUPPORTED";

  case DXGI_DDI_ERR_NONEXCLUSIVE:
    return L"DXGI_DDI_ERR_NONEXCLUSIVE";


    /* DXGI (Status) */
  case DXGI_STATUS_OCCLUDED:
    return L"DXGI_STATUS_OCCLUDED";

  case DXGI_STATUS_UNOCCLUDED:
    return L"DXGI_STATUS_UNOCCLUDED";

  case DXGI_STATUS_CLIPPED:
    return L"DXGI_STATUS_CLIPPED";

  case DXGI_STATUS_NO_REDIRECTION:
    return L"DXGI_STATUS_NO_REDIRECTION";

  case DXGI_STATUS_NO_DESKTOP_ACCESS:
    return L"DXGI_STATUS_NO_DESKTOP_ACCESS";

  case DXGI_STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE:
    return L"DXGI_STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE";

  case DXGI_STATUS_DDA_WAS_STILL_DRAWING:
    return L"DXGI_STATUS_DDA_WAS_STILL_DRAWING";

  case DXGI_STATUS_MODE_CHANGED:
    return L"DXGI_STATUS_MODE_CHANGED";

  case DXGI_STATUS_MODE_CHANGE_IN_PROGRESS:
    return L"DXGI_STATUS_MODE_CHANGE_IN_PROGRESS";


    /* D3D11 */

  case D3D11_ERROR_FILE_NOT_FOUND:
    return L"D3D11_ERROR_FILE_NOT_FOUND";

  case D3D11_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS:
    return L"D3D11_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS";

  case D3D11_ERROR_TOO_MANY_UNIQUE_VIEW_OBJECTS:
    return L"D3D11_ERROR_TOO_MANY_UNIQUE_VIEW_OBJECTS";

  case D3D11_ERROR_DEFERRED_CONTEXT_MAP_WITHOUT_INITIAL_DISCARD:
    return L"D3D11_ERROR_DEFERRED_CONTEXT_MAP_WITHOUT_INITIAL_DISCARD";


    /* D3D9 */

  case D3DERR_WRONGTEXTUREFORMAT:
    return L"D3DERR_WRONGTEXTUREFORMAT";

  case D3DERR_UNSUPPORTEDCOLOROPERATION:
    return L"D3DERR_UNSUPPORTEDCOLOROPERATION";

  case D3DERR_UNSUPPORTEDCOLORARG:
    return L"D3DERR_UNSUPPORTEDCOLORARG";

  case D3DERR_UNSUPPORTEDALPHAOPERATION:
    return L"D3DERR_UNSUPPORTEDALPHAOPERATION";

  case D3DERR_UNSUPPORTEDALPHAARG:
    return L"D3DERR_UNSUPPORTEDALPHAARG";

  case D3DERR_TOOMANYOPERATIONS:
    return L"D3DERR_TOOMANYOPERATIONS";

  case D3DERR_CONFLICTINGTEXTUREFILTER:
    return L"D3DERR_CONFLICTINGTEXTUREFILTER";

  case D3DERR_UNSUPPORTEDFACTORVALUE:
    return L"D3DERR_UNSUPPORTEDFACTORVALUE";

  case D3DERR_CONFLICTINGRENDERSTATE:
    return L"D3DERR_CONFLICTINGRENDERSTATE";

  case D3DERR_UNSUPPORTEDTEXTUREFILTER:
    return L"D3DERR_UNSUPPORTEDTEXTUREFILTER";

  case D3DERR_CONFLICTINGTEXTUREPALETTE:
    return L"D3DERR_CONFLICTINGTEXTUREPALETTE";

  case D3DERR_DRIVERINTERNALERROR:
    return L"D3DERR_DRIVERINTERNALERROR";


  case D3DERR_NOTFOUND:
    return L"D3DERR_NOTFOUND";

  case D3DERR_MOREDATA:
    return L"D3DERR_MOREDATA";

  case D3DERR_DEVICELOST:
    return L"D3DERR_DEVICELOST";

  case D3DERR_DEVICENOTRESET:
    return L"D3DERR_DEVICENOTRESET";

  case D3DERR_NOTAVAILABLE:
    return L"D3DERR_NOTAVAILABLE";

  case D3DERR_OUTOFVIDEOMEMORY:
    return L"D3DERR_OUTOFVIDEOMEMORY";

  case D3DERR_INVALIDDEVICE:
    return L"D3DERR_INVALIDDEVICE";

  case D3DERR_INVALIDCALL:
    return L"D3DERR_INVALIDCALL";

  case D3DERR_DRIVERINVALIDCALL:
    return L"D3DERR_DRIVERINVALIDCALL";

  case D3DERR_WASSTILLDRAWING:
    return L"D3DERR_WASSTILLDRAWING";


  case D3DOK_NOAUTOGEN:
    return L"D3DOK_NOAUTOGEN";


    /* D3D12 */

    //case D3D12_ERROR_FILE_NOT_FOUND:
    //return L"D3D12_ERROR_FILE_NOT_FOUND";

    //case D3D12_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS:
    //return L"D3D12_ERROR_TOO_MANY_UNIQUE_STATE_OBJECTS";

    //case D3D12_ERROR_TOO_MANY_UNIQUE_VIEW_OBJECTS:
    //return L"D3D12_ERROR_TOO_MANY_UNIQUE_VIEW_OBJECTS";
#endif


    /* Generic (FAILED) */

  case E_FAIL:
    return L"E_FAIL";

  case E_INVALIDARG:
    return L"E_INVALIDARG";

  case E_OUTOFMEMORY:
    return L"E_OUTOFMEMORY";

  case E_POINTER:
    return L"E_POINTER";

  case E_ACCESSDENIED:
    return L"E_ACCESSDENIED";

  case E_HANDLE:
    return L"E_HANDLE";

  case E_NOTIMPL:
    return L"E_NOTIMPL";

  case E_NOINTERFACE:
    return L"E_NOINTERFACE";

  case E_ABORT:
    return L"E_ABORT";

  case E_UNEXPECTED:
    return L"E_UNEXPECTED";

  default:
    dll_log->Log (L" *** Encountered unknown HRESULT: (0x%08X)",
      (unsigned long)hr);
    return L"UNKNOWN";
  }
}



UINT
SK_RecursiveMove ( const wchar_t* wszOrigDir,
                   const wchar_t* wszDestDir,
                         bool     replace )
{
  UINT moved = 0;

  wchar_t          wszPath [MAX_PATH + 2] = { };
  SK_PathCombineW (wszPath, wszOrigDir, L"*");

  WIN32_FIND_DATA fd          = {   };
  HANDLE          hFind       =
    FindFirstFileW ( wszPath, &fd);

  if (hFind == INVALID_HANDLE_VALUE) { return 0; }

  do
  {
    if ( wcscmp (fd.cFileName, L".")  == 0 ||
         wcscmp (fd.cFileName, L"..") == 0 )
    {
      continue;
    }

    if (! (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
    {
      wchar_t          wszOld [MAX_PATH + 2];
                      *wszOld = L'\0';
      SK_PathCombineW (wszOld, wszOrigDir, fd.cFileName);

      wchar_t          wszNew [MAX_PATH + 2];
                      *wszNew = L'\0';
      SK_PathCombineW (wszNew, wszDestDir, fd.cFileName);


      bool move = true;

      if (GetFileAttributesW (wszNew) != INVALID_FILE_ATTRIBUTES)
        move = replace; // Only move the file if replacement is desired,
                        //   otherwise just delete the original.


      if (StrStrIW (fd.cFileName, L".log"))
      {
        iSK_Logger* log_file = nullptr;

        if (dll_log->name.find (fd.cFileName) != std::wstring::npos)
        {
          log_file = dll_log.getPtr ();
        }

        else if (steam_log->name.find (fd.cFileName) != std::wstring::npos)
        {
          log_file = steam_log.getPtr ();
        }

        else if (crash_log->name.find (fd.cFileName) != std::wstring::npos)
        {
          log_file = crash_log.getPtr ();
        }

        else if (game_debug->name.find (fd.cFileName) != std::wstring::npos)
        {
          log_file = game_debug.getPtr ();
        }

        else if (tex_log->name.find (fd.cFileName) != std::wstring::npos)
        {
          log_file = tex_log.getPtr ();
        }

        else if (budget_log->name.find (fd.cFileName) != std::wstring::npos)
        {
          log_file = budget_log.getPtr ();
        }

        const bool lock_and_move =
          log_file != nullptr && log_file->fLog;

        if (lock_and_move)
        {
          log_file->lockless = false;
          fflush (log_file->fLog);
          log_file->lock   ();
          fclose (log_file->fLog);
        }

        DeleteFileW                      (wszNew);
        SK_File_MoveNoFail       (wszOld, wszNew);
        SK_File_SetNormalAttribs (wszNew);

        ++moved;

        if (lock_and_move)
        {
          log_file->name = wszNew;
          log_file->fLog = _wfopen (log_file->name.c_str (), L"a");
          log_file->unlock ();
        }
      }

      else
      {
        if (move)
        {
          DeleteFileW                (wszNew);
          SK_File_MoveNoFail (wszOld, wszNew);
          ++moved;
        }
        else
        {
          std::wstring tmp_dest = SK_GetHostPath ();
                       tmp_dest.append (LR"(\SKI0.tmp)");

          SK_File_MoveNoFail (wszOld, tmp_dest.c_str ());
        }
      }
    }

    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    {
      wchar_t               wszDescend0 [MAX_PATH + 2] = { };
      SK_PathCombineW      (wszDescend0, wszOrigDir, fd.cFileName);
         PathAddBackslashW (wszDescend0);

      wchar_t               wszDescend1 [MAX_PATH + 2] = { };
      SK_PathCombineW      (wszDescend1, wszDestDir, fd.cFileName);
         PathAddBackslashW (wszDescend1);

      SK_CreateDirectories (wszDescend1);

      moved +=
        SK_RecursiveMove (wszDescend0, wszDescend1, replace);
    }
  } while (FindNextFile (hFind, &fd));

  FindClose (hFind);

  RemoveDirectoryW (wszOrigDir);

  return moved;
}

PSID
SK_Win32_ReleaseTokenSid (PSID pSid)
{
  if (pSid == nullptr) return pSid;

  if (SK_LocalFree ((HLOCAL)pSid))
    return nullptr;

  assert (pSid == nullptr);

  return pSid;
}

PSID
SK_Win32_GetTokenSid (_TOKEN_INFORMATION_CLASS tic)
{
  PSID   pRet            = nullptr;
  BYTE   token_buf [256] = { };
  LPVOID pTokenBuf       = nullptr;
  DWORD  dwAllocSize     = 0;
  HANDLE hToken          = INVALID_HANDLE_VALUE;

  if (OpenProcessToken (GetCurrentProcess (), TOKEN_QUERY, &hToken))
  {
    if (! GetTokenInformation (hToken, tic, nullptr,
                               0, &dwAllocSize))
    {
      if (GetLastError () == ERROR_INSUFFICIENT_BUFFER)
      {
        if (     tic == TokenUser           && dwAllocSize <= 256)
          pTokenBuf = token_buf;
        else if (tic == TokenIntegrityLevel && dwAllocSize <= 256)
          pTokenBuf = token_buf;

        if (pTokenBuf != nullptr)
        {
          if (GetTokenInformation (hToken, tic, pTokenBuf,
                                   dwAllocSize, &dwAllocSize))
          {
            DWORD dwSidLen =
              GetLengthSid (((SID_AND_ATTRIBUTES *)pTokenBuf)->Sid);

            pRet =
              SK_LocalAlloc ( LPTR, dwSidLen );

            if (pRet != nullptr)
              CopySid (dwSidLen, pRet, ((SID_AND_ATTRIBUTES *)pTokenBuf)->Sid);
          }
        }
      }
    }

    CloseHandle (hToken);
  }

  return pRet;
}


HINSTANCE
WINAPI
SK_ShellExecuteW ( _In_opt_ HWND    hwnd,
                   _In_opt_ LPCWSTR lpOperation,
                   _In_     LPCWSTR lpFile,
                   _In_opt_ LPCWSTR lpParameters,
                   _In_opt_ LPCWSTR lpDirectory,
                   _In_     INT     nShowCmd )
{
  struct exec_args_s {
    HWND    hWnd;
    LPCWSTR lpOperation;
    LPCWSTR lpFile;
    LPCWSTR lpParameters;
    LPCWSTR lpDirectory;
    INT     nShowCmd;

    HANDLE    hThread;
    HINSTANCE hInstance;
    DWORD     tid;
  } args = { hwnd, lpOperation,  lpFile,
                   lpParameters, lpDirectory,
                   nShowCmd,

             nullptr, nullptr, 0x0 };

  args.hThread =
  CreateThread ( nullptr, 0,
  [](LPVOID lpUser)->DWORD
  {
    auto *pArgs =
      (exec_args_s *)lpUser;

    SK_AutoCOMInit _auto_com (
      COINIT_APARTMENTTHREADED |
      COINIT_DISABLE_OLE1DDE
    );

    pArgs->hInstance =
      ShellExecuteW (
        pArgs->hWnd,
          pArgs->lpOperation,  pArgs->lpFile,
          pArgs->lpParameters, pArgs->lpDirectory,
            pArgs->nShowCmd
      );

    return 0;
  }, (LPVOID)&args, 0x0,
             &args.tid );

  if (args.hThread != 0)
  {
    WaitForSingleObject (args.hThread, INFINITE);
    CloseHandle         (args.hThread);
  }

  return
    args.hInstance;
}

HINSTANCE
WINAPI
SK_ShellExecuteA ( _In_opt_ HWND   hwnd,
                   _In_opt_ LPCSTR lpOperation,
                   _In_     LPCSTR lpFile,
                   _In_opt_ LPCSTR lpParameters,
                   _In_opt_ LPCSTR lpDirectory,
                   _In_     INT    nShowCmd )
{
  struct exec_args_s {
    HWND   hWnd;
    LPCSTR lpOperation;
    LPCSTR lpFile;
    LPCSTR lpParameters;
    LPCSTR lpDirectory;
    INT    nShowCmd;

    HANDLE    hThread;
    HINSTANCE hInstance;
    DWORD     tid;
  } args = { hwnd, lpOperation,  lpFile,
                   lpParameters, lpDirectory,
                   nShowCmd,

             nullptr, nullptr, 0x0 };

  args.hThread =
  CreateThread ( nullptr, 0,
  [](LPVOID lpUser)->DWORD
  {
    auto *pArgs =
      (exec_args_s *)lpUser;

    SK_AutoCOMInit _auto_com (
      COINIT_APARTMENTTHREADED |
      COINIT_DISABLE_OLE1DDE
    );

    pArgs->hInstance =
      ShellExecuteA (
        pArgs->hWnd,
          pArgs->lpOperation,  pArgs->lpFile,
          pArgs->lpParameters, pArgs->lpDirectory,
            pArgs->nShowCmd
      );

    return 0;
  }, (LPVOID)&args, 0x0,
             &args.tid );

  if (args.hThread != 0)
  {
    WaitForSingleObject (args.hThread, INFINITE);
    CloseHandle         (args.hThread);
  }

  return
    args.hInstance;
}


template < class T,
           class ...    Args > std::unique_ptr <T>
SK_make_unique_nothrow (Args && ... args) noexcept
(                                         noexcept
(  T ( std::forward   < Args >     (args)   ... ))
)
{
  return
       std::unique_ptr <T> (
  new (std::nothrow)    T  (std::forward
                      < Args >     (args)   ...));
}