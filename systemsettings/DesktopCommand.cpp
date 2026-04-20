

#include "DesktopCommand.h"

#ifndef _ARM
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif // !_ARM

#include <Windows.h>
#include "../globals.h"

extern BOOL isWinXShellAsShell();

// resource.h
#define ID_REFRESH                      1704

class CCoInitialize {
public:
    CCoInitialize() : m_hr(CoInitialize(NULL)) { }
    ~CCoInitialize() { if (SUCCEEDED(m_hr)) CoUninitialize(); }
    operator HRESULT() const { return m_hr; }
    HRESULT m_hr;
};

DesktopCommand::DesktopCommand()
{
    CCoInitialize initCom;
    _pShellView = NULL;
    _pFolderView = NULL;
}

DesktopCommand::DesktopCommand(IShellView * pShellView, IFolderView2 * pFolderView)
{
    _pShellView = pShellView;
    _pFolderView = pFolderView;
}

DesktopCommand::~DesktopCommand()
{
}


void FindDesktopFolderView(REFIID riid, void **ppv)
{
#ifndef _ARM
    if (!ppv)
        return;

    *ppv = NULL;

    ComPtr<IShellWindows> spShellWindows;
    HRESULT hr = ::CoCreateInstance(CLSID_ShellWindows, NULL, CLSCTX_ALL, IID_PPV_ARGS(&spShellWindows));
    if (FAILED(hr) || !spShellWindows)
        return;

    VARIANT vtLoc;
    VariantInit(&vtLoc);
    vtLoc.vt = VT_I4;
    vtLoc.lVal = CSIDL_DESKTOP;

    VARIANT vtEmpty;
    VariantInit(&vtEmpty);

    long lhwnd;
    ComPtr<IDispatch> spdisp;

    hr = spShellWindows->FindWindowSW(
        &vtLoc, &vtEmpty,
        SWC_DESKTOP, &lhwnd, SWFO_NEEDDISPATCH, spdisp.GetAddressOf());

    VariantClear(&vtLoc);
    VariantClear(&vtEmpty);

    if (FAILED(hr) || !spdisp)
        return;

    ComPtr<IServiceProvider> spServiceProvider;
    hr = spdisp.As(&spServiceProvider);
    if (FAILED(hr) || !spServiceProvider)
        return;

    ComPtr<IShellBrowser> spBrowser;
    hr = spServiceProvider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&spBrowser));
    if (FAILED(hr) || !spBrowser)
        return;

    ComPtr<IShellView> spView;
    hr = spBrowser->QueryActiveShellView(&spView);
    if (FAILED(hr) || !spView)
        return;

    spView->QueryInterface(riid, ppv);
#endif
}

void DesktopCommand::Refresh()
{
    if (isWinXShellAsShell()) {
        HWND desktop = FindWindow(TEXT("Progman"), TEXT("Program Manager"));
        SendMessage(desktop, WM_USER + WM_COMMAND, WM_DESKTOP_REFRESH, 0x0);
        return;
    }
#ifndef _ARM
    ComPtr<IShellView> spView;

    FindDesktopFolderView(IID_PPV_ARGS(&spView));
    if (NULL == spView) {
        return;
    }
    spView->Refresh();
#endif
}

void DesktopCommand::SetIconSize(int size)
{
    if (_pShellView) {
        if (_pFolderView) {
            _pFolderView->SetViewModeAndIconSize(FVM_ICON, size);
        }
        return;
    }

    if (isWinXShellAsShell()) {
        HWND desktop = FindWindow(TEXT("Progman"), TEXT("Program Manager"));
        SendMessage(desktop, WM_USER + WM_COMMAND, WM_DESKTOP_SETICONSIZE, size);
        return;
    }
#ifndef _ARM
    ComPtr<IFolderView2> spView;

    FindDesktopFolderView(IID_PPV_ARGS(&spView));
    if (NULL == spView) {
        return;
    }
    spView->SetViewModeAndIconSize(FVM_ICON, size);
#endif
}

void DesktopCommand::SetFolderFlags(DWORD dwMask, int checked)
{
    if (_pFolderView) {
        if (checked) {
            _pFolderView->SetCurrentFolderFlags(dwMask, dwMask);
        } else {
            _pFolderView->SetCurrentFolderFlags(dwMask, 0);
        }
        return;
    }

    if (isWinXShellAsShell()) {
        HWND desktop = FindWindow(TEXT("Progman"), TEXT("Program Manager"));
        SendMessage(desktop, WM_USER + WM_COMMAND, WM_DESKTOP_UNSETFOLDERFLAGS + checked, dwMask);
        return;
    }
#ifndef _ARM
    ComPtr<IFolderView2> spView;
    FindDesktopFolderView(IID_PPV_ARGS(&spView));
    if (NULL == spView) {
        return;
    }

    if (checked) {
        spView->SetCurrentFolderFlags(dwMask, dwMask);
    } else {
        spView->SetCurrentFolderFlags(dwMask, 0);
    }
#endif
}

void DesktopCommand::AutoArrange(int checked)
{
    SetFolderFlags(FWF_AUTOARRANGE, checked);
}

void DesktopCommand::SnapToGrid(int checked)
{
    SetFolderFlags(FWF_SNAPTOGRID, checked);
}

void DesktopCommand::ShowIcons(int checked)
{
    SetFolderFlags(FWF_NOICONS, 1^checked);
}
