/*
 * Copyright 2003, 2004, 2005 Martin Fuchs
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */


//
// Explorer clone
//
// utility.cpp
//
// Martin Fuchs, 23.07.2003
//


#include <precomp.h>

//#include <shellapi.h>

#include <time.h>
#include <sstream>

DWORD PASCAL ReadKernelVersion(DWORD *wdVers);


DWORD WINAPI Thread::ThreadProc(void *para)
{
    Thread *pThis = (Thread *) para;

    int ret = pThis->Run();

    pThis->_alive = false;

    return ret;
}


void CenterWindow(HWND hwnd)
{
    RECT rt, prt;
    GetWindowRect(hwnd, &rt);

    DWORD style;
    HWND owner = 0;

    for (HWND wh = hwnd; (wh = GetWindow(wh, GW_OWNER)) != 0;)
        if (((style = GetWindowStyle(wh))&WS_VISIBLE) && !(style & WS_MINIMIZE))
        {owner = wh; break;}

    if (owner)
        GetWindowRect(owner, &prt);
    else
        SystemParametersInfo(SPI_GETWORKAREA, 0, &prt, 0);  //@@ GetDesktopWindow() w�re auch hilfreich.

    SetWindowPos(hwnd, 0, (prt.left + prt.right + rt.left - rt.right) / 2,
                 (prt.top + prt.bottom + rt.top - rt.bottom) / 2, 0, 0, SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOZORDER);

    MoveVisible(hwnd);
}

void MoveVisible(HWND hwnd)
{
    RECT rc;
    GetWindowRect(hwnd, &rc);
    int left = rc.left, top = rc.top;

    int xmax = GetSystemMetrics(SM_CXSCREEN);
    int ymax = GetSystemMetrics(SM_CYSCREEN);

    if (rc.left < 0)
        rc.left = 0;
    else if (rc.right > xmax)
        if ((rc.left -= rc.right - xmax) < 0)
            rc.left = 0;

    if (rc.top < 0)
        rc.top = 0;
    else if (rc.bottom > ymax)
        if ((rc.top -= rc.bottom - ymax) < 0)
            rc.top = 0;

    if (rc.left != left || rc.top != top)
        SetWindowPos(hwnd, 0, rc.left, rc.top, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}


void display_error(HWND hwnd, DWORD error)  //@@ CONTEXT mit ausgeben -> display_error(HWND hwnd, const Exception& e)
{
    PTSTR msg;

    if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                      0, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (PTSTR)&msg, 0, NULL)) {
        LOG(FmtString(TEXT("display_error(%#x): %s"), error, msg));

        SetLastError(0);
        MessageBox(hwnd, msg, TEXT("WinXShell"), MB_OK);

        if (GetLastError() == ERROR_INVALID_WINDOW_HANDLE)
            MessageBox(0, msg, TEXT("WinXShell"), MB_OK);
    } else {
        LOG(FmtString(TEXT("Unknown Error %#x"), error));

        FmtString msg(TEXT("Unknown Error %#x"), error);

        SetLastError(0);
        MessageBox(hwnd, msg, TEXT("WinXShell"), MB_OK);

        if (GetLastError() == ERROR_INVALID_WINDOW_HANDLE)
            MessageBox(0, msg, TEXT("WinXShell"), MB_OK);
    }

    LocalFree(msg);
}


Context Context::s_main("-NO-CONTEXT-");
Context *Context::s_current = &Context::s_main;

String Context::toString() const
{
    String str = _ctx;

    if (!_obj.empty())
        str.appendf(TEXT("\nObject: %s"), (LPCTSTR)_obj);

    return str;
}

String Context::getStackTrace() const
{
    ostringstream str;

    str << "Context Trace:\n";

    for (const Context *p = this; p && p != &s_main; p = p->_last) {
        str << "- " << p->_ctx;

        if (!p->_obj.empty())
            str << " obj=" << ANS(p->_obj);

        str << '\n';
    }

    return str.str();
}


BOOL time_to_filetime(const time_t *t, FILETIME *ftime)
{
#if defined(__STDC_WANT_SECURE_LIB__) && defined(_MS_VER)
    SYSTEMTIME stime;
    struct tm tm_;
    struct tm *tm = &tm_;

    if (gmtime_s(tm, t) != 0)
        return FALSE;
#else
    struct tm *tm = gmtime(t);
    SYSTEMTIME stime;

    if (!tm)
        return FALSE;
#endif

    stime.wYear = tm->tm_year + 1900;
    stime.wMonth = tm->tm_mon + 1;
    stime.wDayOfWeek = (WORD) - 1;
    stime.wDay = tm->tm_mday;
    stime.wHour = tm->tm_hour;
    stime.wMinute = tm->tm_min;
    stime.wSecond = tm->tm_sec;
    stime.wMilliseconds = 0;

    return SystemTimeToFileTime(&stime, ftime);
}


static BOOL launch_file_shell_execute(HWND hwnd, LPCTSTR cmd, UINT nCmdShow, LPCTSTR parameters)
{
    HINSTANCE hinst = ShellExecute(hwnd, NULL/*operation*/, cmd, parameters, NULL/*dir*/, nCmdShow);

    if ((INT_PTR)hinst <= 32) {
        display_error(hwnd, GetLastError());
        return FALSE;
    }

    return TRUE;
}

static bool IsPeaZipArchivePath(LPCTSTR path)
{
    return path && *path && PathMatchSpec(path,
        TEXT("*.zip;*.7z;*.rar;*.tar;*.gz;*.tgz;*.bz2;*.tbz;*.xz;*.txz;*.cab;*.iso"));
}

static bool TryGetModuleDirectory(TCHAR *module_path, size_t path_count)
{
    if (!module_path || !path_count)
        return false;

    module_path[0] = TEXT('\0');

    String configured_module_path = JVAR("JVAR_MODULEPATH").ToString();
    if (!configured_module_path.empty()) {
        lstrcpyn(module_path, configured_module_path.c_str(), (int)path_count);
        return true;
    }

    if (!GetModuleFileName(NULL, module_path, (DWORD)path_count) || !module_path[0])
        return false;

    PathRemoveFileSpec(module_path);
    return module_path[0] != TEXT('\0');
}

BOOL TryGetPeaZipPath(PTSTR peazip_path, size_t path_count)
{
    if (!peazip_path || !path_count)
        return FALSE;

    peazip_path[0] = TEXT('\0');

    TCHAR module_path[MAX_PATH] = { 0 };
    if (!TryGetModuleDirectory(module_path, COUNTOF(module_path)))
        return FALSE;

    TCHAR candidate[MAX_PATH] = { 0 };
    if (!PathCombine(candidate, module_path, TEXT("..\\Explorer\\peazip.exe")))
        return FALSE;

    TCHAR canonical[MAX_PATH] = { 0 };
    LPCTSTR resolved = candidate;
    if (PathCanonicalize(canonical, candidate))
        resolved = canonical;

    if (!PathFileExists(resolved))
        return FALSE;

    lstrcpyn(peazip_path, resolved, (int)path_count);
    return TRUE;
}

static bool SetRegistryStringValue(HKEY root, LPCTSTR subkey, LPCTSTR value_name, LPCTSTR value_data)
{
    if (!subkey || !*subkey || !value_data)
        return false;

    HKEY hkey = NULL;
    LONG status = RegCreateKeyEx(root,
        subkey,
        0,
        NULL,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE,
        NULL,
        &hkey,
        NULL);
    if (status != ERROR_SUCCESS)
        return false;

    status = RegSetValueEx(hkey,
        value_name,
        0,
        REG_SZ,
        (const BYTE *)value_data,
        (DWORD)((_tcslen(value_data) + 1) * sizeof(TCHAR)));
    RegCloseKey(hkey);
    return status == ERROR_SUCCESS;
}

static bool SetRegistryNoneValue(HKEY root, LPCTSTR subkey, LPCTSTR value_name)
{
    if (!subkey || !*subkey)
        return false;

    HKEY hkey = NULL;
    LONG status = RegCreateKeyEx(root,
        subkey,
        0,
        NULL,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE,
        NULL,
        &hkey,
        NULL);
    if (status != ERROR_SUCCESS)
        return false;

    status = RegSetValueEx(hkey, value_name, 0, REG_NONE, NULL, 0);
    RegCloseKey(hkey);
    return status == ERROR_SUCCESS;
}

static bool QueryRegistryStringValue(HKEY root, LPCTSTR subkey, LPCTSTR value_name, PTSTR value_data, DWORD value_count)
{
    if (!subkey || !*subkey || !value_data || value_count == 0)
        return false;

    value_data[0] = TEXT('\0');

    HKEY hkey = NULL;
    LONG status = RegOpenKeyEx(root, subkey, 0, KEY_QUERY_VALUE, &hkey);
    if (status != ERROR_SUCCESS)
        return false;

    DWORD type = REG_NONE;
    DWORD byte_count = value_count * sizeof(TCHAR);
    status = RegQueryValueEx(hkey, value_name, 0, &type, (LPBYTE)value_data, &byte_count);
    RegCloseKey(hkey);

    if (status != ERROR_SUCCESS)
        return false;

    if (type != REG_SZ && type != REG_EXPAND_SZ)
        return false;

    value_data[value_count - 1] = TEXT('\0');
    return value_data[0] != TEXT('\0');
}

static bool DeleteRegistryTreeIfPresent(HKEY root, LPCTSTR subkey)
{
    if (!subkey || !*subkey)
        return false;

    LONG status = SHDeleteKey(root, subkey);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND;
}

static bool DeleteRegistryTreeBestEffort(HKEY root, LPCTSTR subkey)
{
    if (!subkey || !*subkey)
        return false;

    LONG status = SHDeleteKey(root, subkey);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND || status == ERROR_ACCESS_DENIED;
}

static bool ConfigureExplorerOpenWithForExtension(LPCTSTR extension, LPCTSTR progid, LPCTSTR app_name)
{
    if (!extension || !*extension || !progid || !*progid || !app_name || !*app_name)
        return false;

    String base_key = FmtString(TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\%s"), extension);
    String open_with_progids_key = base_key + TEXT("\\OpenWithProgids");
    String open_with_list_key = base_key + TEXT("\\OpenWithList");

    bool success = true;
    success = success && SetRegistryNoneValue(HKEY_CURRENT_USER, open_with_progids_key.c_str(), progid);
    success = success && SetRegistryStringValue(HKEY_CURRENT_USER, open_with_list_key.c_str(), TEXT("a"), app_name);
    success = success && SetRegistryStringValue(HKEY_CURRENT_USER, open_with_list_key.c_str(), TEXT("MRUList"), TEXT("a"));

    // Best effort: if UserChoice is removable, fallback resolution can use our per-user class mapping.
    String user_choice_key = base_key + TEXT("\\UserChoice");
    success = success && DeleteRegistryTreeBestEffort(HKEY_CURRENT_USER, user_choice_key.c_str());
    return success;
}

static bool ClearExplorerAssociationStateForExtension(LPCTSTR extension)
{
    if (!extension || !*extension)
        return false;

    String base_key = FmtString(TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\%s"), extension);
    bool success = true;
    success = success && DeleteRegistryTreeBestEffort(HKEY_CURRENT_USER, (base_key + TEXT("\\UserChoice")).c_str());
    success = success && DeleteRegistryTreeIfPresent(HKEY_CURRENT_USER, (base_key + TEXT("\\OpenWithProgids")).c_str());
    success = success && DeleteRegistryTreeIfPresent(HKEY_CURRENT_USER, (base_key + TEXT("\\OpenWithList")).c_str());
    return success;
}

static bool CommandReferencesExecutable(LPCTSTR command, LPCTSTR executable_path)
{
    if (!command || !*command || !executable_path || !*executable_path)
        return false;

    return StrStrI(command, executable_path) != NULL;
}

BOOL IsPeaZipDefaultArchiveAssociation()
{
    static const TCHAR *kZipExtKey = TEXT("Software\\Classes\\.zip");
    static const TCHAR *kRarExtKey = TEXT("Software\\Classes\\.rar");
    static const TCHAR *kZipProgId = TEXT("PeaZip.zip");
    static const TCHAR *kRarProgId = TEXT("PeaZip.rar");
    static const TCHAR *kZipCommandKey = TEXT("Software\\Classes\\PeaZip.zip\\shell\\open\\command");
    static const TCHAR *kRarCommandKey = TEXT("Software\\Classes\\PeaZip.rar\\shell\\open\\command");
    static const TCHAR *kZipUserChoiceKey = TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.zip\\UserChoice");
    static const TCHAR *kRarUserChoiceKey = TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.rar\\UserChoice");

    TCHAR peazip_path[MAX_PATH] = { 0 };
    if (!TryGetPeaZipPath(peazip_path, COUNTOF(peazip_path)))
        return FALSE;

    TCHAR zip_progid[128] = { 0 };
    TCHAR rar_progid[128] = { 0 };
    if (!QueryRegistryStringValue(HKEY_CURRENT_USER, kZipExtKey, NULL, zip_progid, COUNTOF(zip_progid)) ||
        !QueryRegistryStringValue(HKEY_CURRENT_USER, kRarExtKey, NULL, rar_progid, COUNTOF(rar_progid))) {
        return FALSE;
    }

    if (_tcsicmp(zip_progid, kZipProgId) != 0 || _tcsicmp(rar_progid, kRarProgId) != 0)
        return FALSE;

    TCHAR zip_command[MAX_PATH * 2] = { 0 };
    TCHAR rar_command[MAX_PATH * 2] = { 0 };
    if (!QueryRegistryStringValue(HKEY_CURRENT_USER, kZipCommandKey, NULL, zip_command, COUNTOF(zip_command)) ||
        !QueryRegistryStringValue(HKEY_CURRENT_USER, kRarCommandKey, NULL, rar_command, COUNTOF(rar_command))) {
        return FALSE;
    }

    if (!CommandReferencesExecutable(zip_command, peazip_path) || !CommandReferencesExecutable(rar_command, peazip_path))
        return FALSE;

    TCHAR zip_userchoice_progid[128] = { 0 };
    if (QueryRegistryStringValue(HKEY_CURRENT_USER, kZipUserChoiceKey, TEXT("ProgId"), zip_userchoice_progid, COUNTOF(zip_userchoice_progid)) &&
        _tcsicmp(zip_userchoice_progid, kZipProgId) != 0) {
        return FALSE;
    }

    TCHAR rar_userchoice_progid[128] = { 0 };
    if (QueryRegistryStringValue(HKEY_CURRENT_USER, kRarUserChoiceKey, TEXT("ProgId"), rar_userchoice_progid, COUNTOF(rar_userchoice_progid)) &&
        _tcsicmp(rar_userchoice_progid, kRarProgId) != 0) {
        return FALSE;
    }

    return TRUE;
}

BOOL SetPeaZipDefaultArchiveAssociation(BOOL enabled)
{
    static const TCHAR *kZipExtKey = TEXT("Software\\Classes\\.zip");
    static const TCHAR *kRarExtKey = TEXT("Software\\Classes\\.rar");
    static const TCHAR *kZipOpenWithProgidsKey = TEXT("Software\\Classes\\.zip\\OpenWithProgids");
    static const TCHAR *kRarOpenWithProgidsKey = TEXT("Software\\Classes\\.rar\\OpenWithProgids");
    static const TCHAR *kZipProgIdBaseKey = TEXT("Software\\Classes\\PeaZip.zip");
    static const TCHAR *kRarProgIdBaseKey = TEXT("Software\\Classes\\PeaZip.rar");
    static const TCHAR *kZipCommandKey = TEXT("Software\\Classes\\PeaZip.zip\\shell\\open\\command");
    static const TCHAR *kRarCommandKey = TEXT("Software\\Classes\\PeaZip.rar\\shell\\open\\command");
    static const TCHAR *kZipIconKey = TEXT("Software\\Classes\\PeaZip.zip\\DefaultIcon");
    static const TCHAR *kRarIconKey = TEXT("Software\\Classes\\PeaZip.rar\\DefaultIcon");
    static const TCHAR *kPeaZipAppBaseKey = TEXT("Software\\Classes\\Applications\\peazip.exe");
    static const TCHAR *kPeaZipAppCommandKey = TEXT("Software\\Classes\\Applications\\peazip.exe\\shell\\open\\command");
    static const TCHAR *kPeaZipSupportedTypesKey = TEXT("Software\\Classes\\Applications\\peazip.exe\\SupportedTypes");
    static const TCHAR *kZipProgId = TEXT("PeaZip.zip");
    static const TCHAR *kRarProgId = TEXT("PeaZip.rar");

    bool success = true;

    if (enabled) {
        TCHAR peazip_path[MAX_PATH] = { 0 };
        if (!TryGetPeaZipPath(peazip_path, COUNTOF(peazip_path)))
            return FALSE;

        TCHAR open_command[MAX_PATH * 2] = { 0 };
        _sntprintf(open_command, COUNTOF(open_command), TEXT("\"%s\" \"%%1\""), peazip_path);
        open_command[COUNTOF(open_command) - 1] = TEXT('\0');

        TCHAR icon_command[MAX_PATH * 2] = { 0 };
        _sntprintf(icon_command, COUNTOF(icon_command), TEXT("\"%s\",0"), peazip_path);
        icon_command[COUNTOF(icon_command) - 1] = TEXT('\0');

        success = success && SetRegistryStringValue(HKEY_CURRENT_USER, kZipProgIdBaseKey, NULL, TEXT("ZIP Archive"));
        success = success && SetRegistryStringValue(HKEY_CURRENT_USER, kRarProgIdBaseKey, NULL, TEXT("RAR Archive"));
        success = success && SetRegistryStringValue(HKEY_CURRENT_USER, kZipCommandKey, NULL, open_command);
        success = success && SetRegistryStringValue(HKEY_CURRENT_USER, kRarCommandKey, NULL, open_command);
        success = success && SetRegistryStringValue(HKEY_CURRENT_USER, kZipIconKey, NULL, icon_command);
        success = success && SetRegistryStringValue(HKEY_CURRENT_USER, kRarIconKey, NULL, icon_command);
        success = success && SetRegistryStringValue(HKEY_CURRENT_USER, kZipExtKey, NULL, kZipProgId);
        success = success && SetRegistryStringValue(HKEY_CURRENT_USER, kRarExtKey, NULL, kRarProgId);
        success = success && SetRegistryNoneValue(HKEY_CURRENT_USER, kZipOpenWithProgidsKey, kZipProgId);
        success = success && SetRegistryNoneValue(HKEY_CURRENT_USER, kRarOpenWithProgidsKey, kRarProgId);
        success = success && SetRegistryStringValue(HKEY_CURRENT_USER, kPeaZipAppBaseKey, NULL, TEXT("PeaZip"));
        success = success && SetRegistryStringValue(HKEY_CURRENT_USER, kPeaZipAppCommandKey, NULL, open_command);
        success = success && SetRegistryNoneValue(HKEY_CURRENT_USER, kPeaZipSupportedTypesKey, TEXT(".zip"));
        success = success && SetRegistryNoneValue(HKEY_CURRENT_USER, kPeaZipSupportedTypesKey, TEXT(".rar"));
        success = success && ConfigureExplorerOpenWithForExtension(TEXT(".zip"), kZipProgId, TEXT("peazip.exe"));
        success = success && ConfigureExplorerOpenWithForExtension(TEXT(".rar"), kRarProgId, TEXT("peazip.exe"));
    } else {
        success = success && ClearExplorerAssociationStateForExtension(TEXT(".zip"));
        success = success && ClearExplorerAssociationStateForExtension(TEXT(".rar"));
        success = success && DeleteRegistryTreeIfPresent(HKEY_CURRENT_USER, kZipExtKey);
        success = success && DeleteRegistryTreeIfPresent(HKEY_CURRENT_USER, kRarExtKey);
        success = success && DeleteRegistryTreeIfPresent(HKEY_CURRENT_USER, kZipProgIdBaseKey);
        success = success && DeleteRegistryTreeIfPresent(HKEY_CURRENT_USER, kRarProgIdBaseKey);
        success = success && DeleteRegistryTreeIfPresent(HKEY_CURRENT_USER, kPeaZipAppBaseKey);
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    if (enabled && success && !IsPeaZipDefaultArchiveAssociation())
        return FALSE;
    return success ? TRUE : FALSE;
}

static bool TryGetPeaZipLaunchTarget(LPCTSTR cmd, LPCTSTR parameters, String &target_path)
{
    target_path.erase();

    if (!cmd || !*cmd)
        return false;

    if (parameters && *parameters)
        return false;

    TCHAR expanded_cmd[MAX_PATH] = { 0 };
    LPCTSTR resolved_cmd = cmd;
    DWORD expanded_length = ExpandEnvironmentStrings(cmd, expanded_cmd, COUNTOF(expanded_cmd));
    if (expanded_length > 0 && expanded_length < COUNTOF(expanded_cmd))
        resolved_cmd = expanded_cmd;

    TCHAR normalized_cmd[MAX_PATH] = { 0 };
    lstrcpyn(normalized_cmd, resolved_cmd, COUNTOF(normalized_cmd));
    PathUnquoteSpaces(normalized_cmd);

    if (PathIsDirectory(normalized_cmd)) {
        target_path = normalized_cmd;
        return true;
    }

    if (PathFileExists(normalized_cmd) && IsPeaZipArchivePath(normalized_cmd)) {
        target_path = normalized_cmd;
        return true;
    }

    if (PathMatchSpec(normalized_cmd, TEXT("*.lnk"))) {
        TCHAR shortcut_target[MAX_PATH] = { 0 };
        GetShortcutPath(normalized_cmd, shortcut_target, COUNTOF(shortcut_target));
        if (shortcut_target[0] && (PathIsDirectory(shortcut_target) ||
            (PathFileExists(shortcut_target) && IsPeaZipArchivePath(shortcut_target)))) {
            target_path = shortcut_target;
            return true;
        }
    }

    return false;
}

BOOL launch_folder_with_peazip(HWND hwnd, LPCTSTR folder_path, UINT nCmdShow)
{
    if (!folder_path || !*folder_path)
        return FALSE;

    TCHAR peazip_path[MAX_PATH] = { 0 };
    if (TryGetPeaZipPath(peazip_path, COUNTOF(peazip_path))) {
        String peazip_parameters = FmtString(TEXT("\"%s\""), folder_path);
        return launch_file_shell_execute(hwnd, peazip_path, nCmdShow, peazip_parameters.c_str());
    }

    return launch_file_shell_execute(hwnd, folder_path, nCmdShow, NULL);
}


BOOL launch_file(HWND hwnd, LPCTSTR cmd, UINT nCmdShow, LPCTSTR parameters)
{
    CONTEXT("launch_file()");

    String peazip_target;
    if (TryGetPeaZipLaunchTarget(cmd, parameters, peazip_target))
        return launch_folder_with_peazip(hwnd, peazip_target.c_str(), nCmdShow);

    return launch_file_shell_execute(hwnd, cmd, nCmdShow, parameters);
}

#ifdef UNICODE
BOOL launch_fileA(HWND hwnd, LPSTR cmd, UINT nCmdShow, LPCSTR parameters)
{
    HINSTANCE hinst = ShellExecuteA(hwnd, NULL/*operation*/, cmd, parameters, NULL/*dir*/, nCmdShow);

    if ((int)hinst <= 32) {
        display_error(hwnd, GetLastError());
        return FALSE;
    }

    return TRUE;
}
#endif

void GetShortcutPath(const TCHAR *lnk, TCHAR *path, DWORD cchBuffer)
{
    IShellLink *psl = NULL;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (LPVOID*)&psl);
    if (SUCCEEDED(hr)) {
        IPersistFile *ppf = NULL;
        hr = psl->QueryInterface(IID_IPersistFile, (LPVOID *)&ppf);
        if (SUCCEEDED(hr)) {
            hr = ppf->Load(lnk, STGM_READ);
            if (SUCCEEDED(hr)) {
                WIN32_FIND_DATA wfd;
                psl->GetPath(path, cchBuffer, &wfd, SLGP_UNCPRIORITY | SLGP_RAWPATH);
            }
            ppf->Release();
        }
        psl->Release();
    }
}

#include "UNIBASE.h"

TCHAR *CompletePath(TCHAR *target, TCHAR *out)
{
    TCHAR buff[MAX_PATH] = { 0 };
    ExpandEnvironmentStrings(target, out, MAX_PATH);
    if (PathFileExists(out)) return out;
    StrCpy(buff, out);
    if (SearchPath(NULL, buff, NULL, MAX_PATH, out, NULL)) {
        return out;
     }
     return NULL;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
// Create shortcut
HRESULT CreateShortcut(PTSTR lnk, PTSTR target,
    PTSTR param = NULL, PTSTR icon = NULL,
    int iIcon = 0, int iShowCmd = SW_SHOWNORMAL)
{
    if (target == NULL) {
        return ERROR_PATH_NOT_FOUND;
    }

    // Search target
    TCHAR tzTarget[MAX_PATH];
    target = CompletePath(target, tzTarget);
    if (!target) return ERROR_PATH_NOT_FOUND;

    // Create shortcut
    IShellLink *pLink = NULL;
    CoInitialize(NULL);
    HRESULT hResult = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLink, (PVOID *)&pLink);
    if (hResult == S_OK) {
        IPersistFile *pFile = NULL;
        hResult = pLink->QueryInterface(IID_IPersistFile, (PVOID *)&pFile);
        if (hResult == S_OK) {
            // Shortcut settings
            if (iShowCmd > SW_SHOWNORMAL) {
                if (iShowCmd == SW_SHOWMINIMIZED) iShowCmd = SW_SHOWMINNOACTIVE;
                hResult = pLink->SetShowCmd(iShowCmd);
            }

            hResult = pLink->SetPath(target);
            hResult = pLink->SetArguments(param);
            hResult = pLink->SetIconLocation(icon, iIcon);

            if (DirSplitPath(target) != target) {
                hResult = pLink->SetWorkingDirectory(target);
            }

            // Save link
            TCHAR tzLink[MAX_PATH];
            ExpandEnvironmentStrings(lnk, tzLink, MAX_PATH);
            DirCreate(tzLink);
            hResult = pFile->Save(tzLink, FALSE);
            pFile->Release();
        }
        pLink->Release();
    }
    CoUninitialize();
    return hResult;
}
//////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static BOOL ShellExecuteWithSEInfo(SHELLEXECUTEINFO &ei, TCHAR *cmd)
{
    TCHAR paramSep = _T(' ');
    TCHAR *cp = cmd;
    if (cmd[0] == _T('\"') || cmd[0] == _T('\'')) {
        cp = cmd + 1;
        paramSep = cmd[0];
    }

    {
        while (*cp != _T('\0') && *cp != paramSep) {
            if (paramSep != _T(' ')) *(cp - 1) = *cp;
            cp++;
        }
        if (paramSep != _T(' ')) {
            *(cp - 1) = _T('\0');
            cp++;
        } else {
            *cp = _T('\0');
        }
        ei.lpParameters = cp + 1;
    }
    return ShellExecuteEx(&ei);
}

// Execute command
DWORD Exec(PTSTR ptzCmd, BOOL bWait, INT iShowCmd, PTSTR ptzVerb)
{
    HANDLE hProcess = NULL;
    HANDLE hThread = NULL;
    DWORD dwExitCode = 0;
    DWORD dwCreationFlags = 0;
    TCHAR tzExpandCmd[MAX_PATH * 10];
    ExpandEnvironmentStrings(ptzCmd, tzExpandCmd, MAX_PATH * 10);

    BOOL bResult = FALSE;
    if (ptzVerb && ptzVerb[0] != _T('\0')) {
        SHELLEXECUTEINFO ei = { sizeof(ei) };
        ei.fMask = SEE_MASK_INVOKEIDLIST;
        ei.hwnd = NULL;
        ei.nShow = iShowCmd;
        ei.lpVerb = ptzVerb;
        ei.lpFile = tzExpandCmd;

        bResult = ShellExecuteWithSEInfo(ei, tzExpandCmd);
        if (!bResult) return S_FALSE;

        hProcess = ei.hProcess;
    } else {
        STARTUPINFO si = { 0 };
        PROCESS_INFORMATION pi;
        si.cb = sizeof(STARTUPINFO);
        si.lpDesktop = TEXT("WinSta0\\Default");
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = iShowCmd;
        if (iShowCmd == SW_HIDE) {
            // si.lpDesktop = NULL;
            dwCreationFlags = CREATE_NO_WINDOW;
        }
        bResult = CreateProcess(NULL, tzExpandCmd, NULL, NULL, FALSE, dwCreationFlags, NULL, NULL, &si, &pi);
        if (!bResult) return S_FALSE;

        hProcess = pi.hProcess;
        hThread = pi.hThread;
    }

    if (bWait && hProcess) {
        WaitForSingleObject(hProcess, INFINITE);
        GetExitCodeProcess(hProcess, &dwExitCode);
    }

    if (hThread) CloseHandle(hThread);
    if (hProcess) CloseHandle(hProcess);
    return dwExitCode;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int CommandHook(HWND hwnd, const TCHAR *act, const TCHAR *sect)
{
    String cmd = TEXT("");
    INT showflags = SW_SHOWNORMAL;
    String parameters = TEXT("");
    if (!hwnd) return 0;
    cmd = JCFG_CMDW(sect, act, "command", TEXT("")).ToString();
    if (cmd != TEXT("")) {
        showflags = JCFG_CMDW(sect, act, "showflags", showflags).ToInt();
        parameters = JCFG_CMDW(sect, act, "parameters", TEXT("")).ToString();
        launch_file(hwnd, cmd, showflags, parameters);
        return 1;
    }
    return 0;
}

BOOL HandleEnvChangeBroadcast(LPARAM lparam)
{
    static BOOL(WINAPI *RegenerateUserEnvironment)(void **, BOOL) = NULL;
    //
    // Check if the user's environment variables have changed, if so
    // regenerate the environment, so that new apps started from
    // taskman will have the latest environment.
    //
    if (lparam && (!lstrcmpi((LPTSTR)lparam, (LPTSTR)TEXT("Environment")))) {
        PVOID pEnv;
        if (!RegenerateUserEnvironment) {
            RegenerateUserEnvironment = (BOOL(WINAPI *)(void **, BOOL))
                GetProcAddress(GetModuleHandle(TEXT("SHELL32")), "RegenerateUserEnvironment");
        }
        if (RegenerateUserEnvironment) {
            RegenerateUserEnvironment(&pEnv, TRUE);
        }
        return TRUE;
    }
    return FALSE;
}

/* search for already running instance */

static int g_foundPrevInstance = 0;

static BOOL CALLBACK EnumWndProc(HWND hwnd, LPARAM lparam)
{
    TCHAR cls[128];

    GetClassName(hwnd, cls, 128);

    if (!lstrcmp(cls, (LPCTSTR)lparam)) {
        g_foundPrevInstance++;
        return FALSE;
    }

    return TRUE;
}

/* search for window of given class name to allow only one running instance */
int find_window_class(LPCTSTR classname)
{
    EnumWindows(EnumWndProc, (LPARAM)classname);

    if (g_foundPrevInstance)
        return 1;

    return 0;
}


String get_windows_version_str()
{
    DWORD wdVers[4] = {0};
    ReadKernelVersion(wdVers);

    DWORD major = wdVers[0];
    DWORD minor = wdVers[1];
    DWORD build = wdVers[2];

    if (major == 0 && minor == 0)
        return TEXT("???");

    String str;
    if (major >= 10 && build >= 22000)
        str = TEXT("Microsoft Windows 11");
    else if (major >= 10)
        str = TEXT("Microsoft Windows 10");
    else if (major == 6 && minor == 3)
        str = TEXT("Microsoft Windows 8.1");
    else if (major == 6 && minor == 2)
        str = TEXT("Microsoft Windows 8");
    else if (major == 6 && minor == 1)
        str = TEXT("Microsoft Windows 7");
    else if (major == 6 && minor == 0)
        str = TEXT("Microsoft Windows Vista");
    else if (major == 5 && minor == 1)
        str = TEXT("Microsoft Windows XP");
    else
        str = TEXT("Microsoft Windows");

    String vstr;
    vstr.printf(TEXT(" Version %lu.%lu (Build %lu)"), major, minor, build);
    return str + vstr;
}


typedef void (WINAPI *RUNDLLPROC)(HWND hwnd, HINSTANCE hinst, LPCTSTR cmdline, DWORD nCmdShow);

BOOL RunDLL(HWND hwnd, LPCTSTR dllname, LPCSTR procname, LPCTSTR cmdline, UINT nCmdShow)
{
    HMODULE hmod = LoadLibrary(dllname);
    if (!hmod)
        return FALSE;

    /*TODO
        <Windows NT/2000>
        It is possible to create a Unicode version of the function.
        Rundll32 first tries to find a function named EntryPointW.
        If it cannot find this function, it tries EntryPointA, then EntryPoint.
        To create a DLL that supports ANSI on Windows 95/98/Me and Unicode otherwise,
        export two functions: EntryPointW and EntryPoint.
    */
    RUNDLLPROC proc = (RUNDLLPROC)GetProcAddress(hmod, procname);
    if (!proc) {
        FreeLibrary(hmod);
        return FALSE;
    }

    proc(hwnd, hmod, cmdline, nCmdShow);

    FreeLibrary(hmod);

    return TRUE;
}


#ifdef UNICODE
#define CONTROL_RUNDLL "Control_RunDLLW"
#else
#define CONTROL_RUNDLL "Control_RunDLLA"
#endif

BOOL launch_cpanel(HWND hwnd, LPCTSTR applet)
{
    TCHAR parameters[MAX_PATH];

    _tcscpy(parameters, TEXT("shell32.dll,Control_RunDLL "));
    _tcscat(parameters, applet);

    return ((int)ShellExecute(hwnd, TEXT("open"), TEXT("rundll32.exe"), parameters, NULL, SW_SHOWDEFAULT) > 32);
}


BOOL RecursiveCreateDirectory(LPCTSTR path_in)
{
    TCHAR path[MAX_PATH], hole_path[MAX_PATH];

    _tcscpy(hole_path, path_in);

    int drv_len = 0;
    LPCTSTR d;

    for (d = hole_path; *d && *d != '/' && *d != '\\'; ++d) {
        ++drv_len;

        if (*d == ':')
            break;
    }

    LPTSTR dir = hole_path + drv_len;

    int l;
    LPTSTR p = hole_path + (l = (int)_tcslen(hole_path));

    while (--p >= hole_path && (*p == '/' || *p == '\\'))
        *p = '\0';

    WIN32_FIND_DATA w32fd;

    HANDLE hFind = FindFirstFile(hole_path, &w32fd);

    if (hFind == INVALID_HANDLE_VALUE) {
        _tcsncpy(path, hole_path, drv_len);
        int i = drv_len;

        for (p = dir; *p == '/' || *p == '\\'; p++)
            path[i++] = *p++;

        for (; i < l; i++) {
            memcpy(path, hole_path, i * sizeof(TCHAR));

            for (; hole_path[i] && hole_path[i] != '/' && hole_path[i] != '\\'; i++)
                path[i] = hole_path[i];

            path[i] = '\0';

            hFind = FindFirstFile(path, &w32fd);

            if (hFind != INVALID_HANDLE_VALUE)
                FindClose(hFind);
            else {
                LOG(FmtString(TEXT("CreateDirectory(\"%s\")"), path));

                if (!CreateDirectory(path, 0))
                    return FALSE;
            }
        }
    } else
        FindClose(hFind);

    return TRUE;
}


DWORD RegGetDWORDValue(HKEY root, LPCTSTR path, LPCTSTR valueName, DWORD def)
{
    HKEY hkey;
    DWORD ret;

    if (!RegOpenKey(root, path, &hkey)) {
        DWORD len = sizeof(ret);

        if (RegQueryValueEx(hkey, valueName, 0, NULL, (LPBYTE)&ret, &len))
            ret = def;

        RegCloseKey(hkey);

        return ret;
    } else
        return def;
}


BOOL RegSetDWORDValue(HKEY root, LPCTSTR path, LPCTSTR valueName, DWORD value)
{
    HKEY hkey;
    BOOL ret = FALSE;

    if (!RegOpenKey(root, path, &hkey)) {
        ret = RegSetValueEx(hkey, valueName, 0, REG_DWORD, (LPBYTE)&value, sizeof(value));

        RegCloseKey(hkey);
    }

    return ret;
}


BOOL exists_path(LPCTSTR path)
{
    WIN32_FIND_DATA fd;

    HANDLE hfind = FindFirstFile(path, &fd);

    if (hfind != INVALID_HANDLE_VALUE) {
        FindClose(hfind);

        return TRUE;
    } else
        return FALSE;
}


bool SplitFileSysURL(LPCTSTR url, String &dir_out, String &fname_out)
{
    if (!_tcsnicmp(url, TEXT("file://"), 7)) {
        url += 7;

        // remove third slash in front of drive characters
        if (*url == '/')
            ++url;
    }

    if (exists_path(url)) {
        TCHAR path[_MAX_PATH];

        // convert slashes to back slashes
        GetFullPathName(url, COUNTOF(path), path, NULL);

        if (GetFileAttributes(path) & FILE_ATTRIBUTE_DIRECTORY)
            fname_out.erase();
        else {
            TCHAR drv[_MAX_DRIVE], dir[_MAX_DIR], fname[_MAX_FNAME], ext[_MAX_EXT];

            _tsplitpath_s(path, drv, COUNTOF(drv), dir, COUNTOF(dir), fname, COUNTOF(fname), ext, COUNTOF(ext));
            _stprintf(path, TEXT("%s%s"), drv, dir);

            fname_out.printf(TEXT("%s%s"), fname, ext);
        }

        dir_out = path;

        return true;
    } else
        return false;
}


static char *getmsgstr(UINT msgid)
{
    char *msg = NULL;
    static char buff[200];
    switch (msgid) {
    case WM_COMMAND:msg = ("WM_COMMAND"); break;
    case WM_NOTIFY:msg = ("WM_NOTIFY"); break;
    case WM_CONTEXTMENU: msg = ("WM_CONTEXTMENU"); break;
    case WM_INITDIALOG: msg = ("WM_INITDIALOG"); break;
    case WM_ACTIVATEAPP: msg = ("WM_ACTIVATEAPP"); break;
    case WM_STYLECHANGING: msg = ("WM_STYLECHANGING"); break;
    case WM_STYLECHANGED: msg = ("WM_STYLECHANGED"); break;
    case WM_NCPAINT: msg = ("WM_NCPAINT"); break;
    case WM_NCACTIVATE: msg = ("WM_NCACTIVATE"); break;
    case WM_CHANGEUISTATE: msg = ("WM_CHANGEUISTATE"); break;
    case WM_ACTIVATE: msg = ("WM_ACTIVATE"); break;
    case WM_SHOWWINDOW: msg = ("WM_SHOWWINDOW"); break;
    case WM_CTLCOLORDLG: msg = ("WM_CTLCOLORDLG"); break;
    case WM_PRINTCLIENT: msg = ("WM_PRINTCLIENT"); break;
    case WM_SETCURSOR: msg = ("WM_SETCURSOR"); break;
    case WM_LBUTTONUP: msg = ("WM_LBUTTONUP"); break;
    case WM_LBUTTONDBLCLK: msg = ("WM_LBUTTONDBLCLK"); break;
    case WM_RBUTTONDOWN: msg = ("WM_RBUTTONDOWN"); break;
    case WM_RBUTTONUP: msg = ("WM_RBUTTONUP"); break;
    case WM_RBUTTONDBLCLK: msg = ("WM_RBUTTONDBLCLK"); break;
    case WM_MBUTTONDOWN: msg = ("WM_MBUTTONDOWN"); break;
    case WM_MBUTTONUP: msg = ("WM_MBUTTONUP"); break;
    case WM_NCHITTEST: msg = ("WM_NCHITTEST"); break;
    default:
        sprintf_s(buff, 200, "0x%x", msgid);
        return buff;
    }
    return msg;
}

#ifndef LOGA
extern void _logA_(LPCSTR txt);

#define LOGA(txt) _logA_(txt)
#endif
void PrintMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    char buff[200];
    if (message != WM_SETCURSOR && message != WM_NCMOUSEMOVE && message != WM_MOUSEMOVE) {
        sprintf_s(buff, 200, "hWnd:0x%x %s 0x%x 0x%x\r\n", hWnd, getmsgstr(message), wParam, lParam);
        LOGA(buff);
    }
}

