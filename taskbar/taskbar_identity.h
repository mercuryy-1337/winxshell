#pragma once

#include <propkey.h>
#include <shobjidl.h>

namespace taskbar_identity {

inline String ToLowerString(LPCTSTR value)
{
    if (!value || !*value)
        return String();

    String lower = value;
    CharLower(lower.str());
    return lower;
}

inline String MakePathKey(LPCTSTR path)
{
    if (!path || !*path)
        return String();

    TCHAR full_path[MAX_PATH] = { 0 };
    if (!GetFullPathName(path, COUNTOF(full_path), full_path, NULL) || !full_path[0])
        lstrcpyn(full_path, path, COUNTOF(full_path));

    String key = TEXT("path:");
    key += ToLowerString(full_path);
    return key;
}

inline String MakeAppIdKey(LPCTSTR app_id)
{
    if (!app_id || !*app_id)
        return String();

    String key = TEXT("appid:");
    key += ToLowerString(app_id);
    return key;
}

inline bool PathMatchesFileName(LPCTSTR path, LPCTSTR file_name)
{
    if (!path || !*path || !file_name || !*file_name)
        return false;

    LPCTSTR base_name = PathFindFileName(path);
    return base_name && !_tcsicmp(base_name, file_name);
}

inline String ReadAppIdFromPropertyStore(IPropertyStore *store)
{
    if (!store)
        return String();

    PROPVARIANT value;
    PropVariantInit(&value);

    String result;
    HRESULT hr = store->GetValue(PKEY_AppUserModel_ID, &value);
    if (SUCCEEDED(hr) && value.vt == VT_LPWSTR && value.pwszVal && value.pwszVal[0])
        result = MakeAppIdKey(value.pwszVal);

    PropVariantClear(&value);
    return result;
}

inline bool IsExplorerProcessPath(LPCTSTR path)
{
    if (!path || !*path)
        return false;

    TCHAR windows_dir[MAX_PATH] = { 0 };
    GetWindowsDirectory(windows_dir, COUNTOF(windows_dir));

    String explorer_path = windows_dir;
    explorer_path += TEXT("\\explorer.exe");
    return !_tcsicmp(explorer_path.c_str(), path);
}

inline bool IsHostedProcessPath(LPCTSTR path)
{
    return PathMatchesFileName(path, TEXT("ApplicationFrameHost.exe")) ||
        PathMatchesFileName(path, TEXT("WWAHost.exe")) ||
        PathMatchesFileName(path, TEXT("ShellExperienceHost.exe")) ||
        PathMatchesFileName(path, TEXT("StartMenuExperienceHost.exe")) ||
        PathMatchesFileName(path, TEXT("SearchHost.exe"));
}

inline String ReadShortcutAppId(LPCTSTR path)
{
    if (!path || !*path)
        return String();

    IPropertyStore *store = NULL;
    HRESULT hr = SHGetPropertyStoreFromParsingName(path, NULL, GPS_DEFAULT, IID_PPV_ARGS(&store));
    if (FAILED(hr) || !store)
        return String();

    String app_id = ReadAppIdFromPropertyStore(store);
    store->Release();
    return app_id;
}

inline bool GetWindowProcessPath(HWND hwnd, TCHAR *path, size_t path_count)
{
    if (!path || path_count == 0)
        return false;

    path[0] = 0;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid)
        return false;

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process)
        return false;

    DWORD length = (DWORD)path_count;
    BOOL ok = QueryFullProcessImageName(process, 0, path, &length);
    CloseHandle(process);
    return ok && path[0] != 0;
}

inline String GetWindowAppKey(HWND hwnd)
{
    TCHAR process_path[MAX_PATH] = { 0 };
    bool has_process_path = GetWindowProcessPath(hwnd, process_path, COUNTOF(process_path));

    String app_id;
    IPropertyStore *store = NULL;
    HRESULT hr = SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&store));
    if (SUCCEEDED(hr) && store) {
        app_id = ReadAppIdFromPropertyStore(store);
        store->Release();
    }

    if (has_process_path) {
        if (IsExplorerProcessPath(process_path)) {
            if (!app_id.empty())
                return app_id;

            return MakePathKey(process_path);
        }

        if (!app_id.empty() && IsHostedProcessPath(process_path))
            return app_id;

        return MakePathKey(process_path);
    }

    if (!app_id.empty())
        return app_id;

    // Last resort: class name
    TCHAR class_name[128] = { 0 };
    if (GetClassName(hwnd, class_name, COUNTOF(class_name))) {
        String fallback = TEXT("class:");
        fallback += ToLowerString(class_name);
        return fallback;
    }

    return String();
}

inline String GetExplorerAppKey()
{
    TCHAR windows_dir[MAX_PATH] = { 0 };
    GetWindowsDirectory(windows_dir, COUNTOF(windows_dir));

    String explorer_path = windows_dir;
    explorer_path += TEXT("\\explorer.exe");
    return MakePathKey(explorer_path.c_str());
}

inline String GetShortcutAppKey(LPCTSTR path)
{
    if (!path || !*path)
        return String();

    if (PathMatchSpec(path, TEXT("*.lnk"))) {
        String app_id = ReadShortcutAppId(path);

        TCHAR target_path[MAX_PATH] = { 0 };
        GetShortcutPath(path, target_path, COUNTOF(target_path));

        if (target_path[0]) {
            if (!app_id.empty() && (IsExplorerProcessPath(target_path) || IsHostedProcessPath(target_path)))
                return app_id;

            return MakePathKey(target_path);
        }

        if (!app_id.empty())
            return app_id;
    }

    return MakePathKey(path);
}

} // namespace taskbar_identity