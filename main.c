#include <windows.h>
#include <wininet.h>
#include <stdio.h>
#include "resource.h"

#pragma comment(lib, "wininet.lib")

static HWND g_hDlg = NULL;
static HANDLE g_hProcess = NULL;
static DWORD g_dwProcessId = 0;
static NOTIFYICONDATAW g_nid = {0};
static wchar_t g_exeDir[MAX_PATH] = {0};

#define APP_NAME L"SingboxManager"
#define SINGBOX_EXE L"sing-box.exe"
#define CONFIG_FILE L"config.json"
#define URL_FILE L"subscription.txt"
#define BAK_DIR L"bak"
#define WM_TRAYICON (WM_USER + 1)

static BOOL g_startMinimized = FALSE;

void UpdateStatus(const wchar_t* status) {
    SetDlgItemTextW(g_hDlg, IDC_STATUS, status);
}

void SaveUrl(void) {
    wchar_t url[2048];
    GetDlgItemTextW(g_hDlg, IDC_EDIT_URL, url, 2048);
    FILE* f = _wfopen(URL_FILE, L"w, ccs=UTF-8");
    if (f) { fwprintf(f, L"%s", url); fclose(f); }
}

void LoadUrl(void) {
    FILE* f = _wfopen(URL_FILE, L"r, ccs=UTF-8");
    if (f) {
        wchar_t url[2048] = {0};
        fgetws(url, 2048, f);
        fclose(f);
        size_t len = wcslen(url);
        if (len > 0 && url[len-1] == L'\n') url[len-1] = 0;
        SetDlgItemTextW(g_hDlg, IDC_EDIT_URL, url);
    }
}

BOOL IsSingboxRunning(void) {
    if (g_hProcess) {
        DWORD exitCode;
        if (GetExitCodeProcess(g_hProcess, &exitCode) && exitCode == STILL_ACTIVE)
            return TRUE;
        CloseHandle(g_hProcess);
        g_hProcess = NULL;
    }
    return FALSE;
}

void StartSingbox(void) {
    if (IsSingboxRunning()) return;

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {0};

    wchar_t cmd[512];
    swprintf(cmd, 512, L"%s run -c %s", SINGBOX_EXE, CONFIG_FILE);

    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        g_hProcess = pi.hProcess;
        g_dwProcessId = pi.dwProcessId;
        CloseHandle(pi.hThread);
        SetDlgItemTextW(g_hDlg, IDC_BTN_TOGGLE, L"停止");
        UpdateStatus(L"状态: 运行中");
    } else {
        UpdateStatus(L"状态: 启动失败");
    }
}

void StopSingbox(void) {
    if (g_hProcess) {
        TerminateProcess(g_hProcess, 0);
        WaitForSingleObject(g_hProcess, 2000);
        CloseHandle(g_hProcess);
        g_hProcess = NULL;
    }
    SetDlgItemTextW(g_hDlg, IDC_BTN_TOGGLE, L"启动");
    UpdateStatus(L"状态: 已停止");
}

void RestartSingbox(void) {
    StopSingbox();
    Sleep(500);
    StartSingbox();
}

BOOL DownloadFile(const wchar_t* url, const wchar_t* filename) {
    BOOL result = FALSE;
    HINTERNET hInternet = InternetOpenW(L"SingboxManager/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return FALSE;

    HINTERNET hUrl = InternetOpenUrlW(hInternet, url, NULL, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (hUrl) {
        FILE* f = _wfopen(filename, L"wb");
        if (f) {
            char buffer[4096];
            DWORD bytesRead;
            while (InternetReadFile(hUrl, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
                fwrite(buffer, 1, bytesRead, f);
            }
            fclose(f);
            result = TRUE;
        }
        InternetCloseHandle(hUrl);
    }
    InternetCloseHandle(hInternet);
    return result;
}

void BackupConfig(void) {
    if (GetFileAttributesW(CONFIG_FILE) != INVALID_FILE_ATTRIBUTES) {
        CreateDirectoryW(BAK_DIR, NULL);
        CopyFileW(CONFIG_FILE, BAK_DIR L"\\config.json", FALSE);
    }
}

void UpdateSubscription(void) {
    wchar_t url[2048];
    GetDlgItemTextW(g_hDlg, IDC_EDIT_URL, url, 2048);

    if (wcslen(url) == 0) {
        UpdateStatus(L"状态: 请输入订阅链接");
        return;
    }

    SaveUrl();
    BackupConfig();
    UpdateStatus(L"状态: 正在更新订阅...");

    if (DownloadFile(url, CONFIG_FILE)) {
        UpdateStatus(L"状态: 订阅更新成功，正在重启...");
        RestartSingbox();
    } else {
        UpdateStatus(L"状态: 订阅更新失败");
    }
}

BOOL IsAutoStartEnabled(void) {
    HKEY hKey;
    BOOL result = FALSE;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        result = (RegQueryValueExW(hKey, APP_NAME, NULL, NULL, NULL, NULL) == ERROR_SUCCESS);
        RegCloseKey(hKey);
    }
    return result;
}

void SetAutoStart(BOOL enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            wchar_t path[MAX_PATH], cmd[MAX_PATH + 20];
            GetModuleFileNameW(NULL, path, MAX_PATH);
            swprintf(cmd, MAX_PATH + 20, L"\"%s\" -minimized", path);
            RegSetValueExW(hKey, APP_NAME, 0, REG_SZ, (BYTE*)cmd, (wcslen(cmd) + 1) * sizeof(wchar_t));
        } else {
            RegDeleteValueW(hKey, APP_NAME);
        }
        RegCloseKey(hKey);
    }
}

void ShowTrayIcon(HWND hDlg) {
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hDlg;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_ICON));
    wcscpy(g_nid.szTip, APP_NAME);
    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

void HideToTray(HWND hDlg) {
    ShowWindow(hDlg, SW_HIDE);
}

void ShowFromTray(HWND hDlg) {
    ShowWindow(hDlg, SW_SHOW);
    SetForegroundWindow(hDlg);
}

void ShowTrayMenu(HWND hDlg) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_SHOW, L"显示窗口");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_TOGGLE, IsSingboxRunning() ? L"停止" : L"启动");
    AppendMenuW(hMenu, MF_STRING, IDM_RESTART, L"重启");
    AppendMenuW(hMenu, MF_STRING, IDM_UPDATE, L"更新订阅");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"退出");
    SetForegroundWindow(hDlg);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hDlg, NULL);
    DestroyMenu(hMenu);
}

INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        g_hDlg = hDlg;
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_ICON)));
        CheckDlgButton(hDlg, IDC_CHK_AUTO, IsAutoStartEnabled() ? BST_CHECKED : BST_UNCHECKED);
        LoadUrl();
        ShowTrayIcon(hDlg);
        if (g_startMinimized) {
            HideToTray(hDlg);
            StartSingbox();
        }
        return TRUE;

    case WM_TRAYICON:
        if (lParam == WM_LBUTTONDBLCLK) {
            ShowFromTray(hDlg);
        } else if (lParam == WM_RBUTTONUP) {
            ShowTrayMenu(hDlg);
        }
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BTN_TOGGLE:
        case IDM_TOGGLE:
            if (IsSingboxRunning()) StopSingbox();
            else StartSingbox();
            break;
        case IDC_BTN_RESTART:
        case IDM_RESTART:
            RestartSingbox();
            break;
        case IDC_BTN_UPDATE:
        case IDM_UPDATE:
            UpdateSubscription();
            break;
        case IDC_CHK_AUTO:
            SetAutoStart(IsDlgButtonChecked(hDlg, IDC_CHK_AUTO) == BST_CHECKED);
            break;
        case IDM_SHOW:
            ShowFromTray(hDlg);
            break;
        case IDM_EXIT:
            DestroyWindow(hDlg);
            break;
        }
        return TRUE;

    case WM_CLOSE:
        SaveUrl();
        HideToTray(hDlg);
        return TRUE;

    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        StopSingbox();
        PostQuitMessage(0);
        return TRUE;
    }
    return FALSE;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)nCmdShow;

    // 检查命令行参数
    if (lpCmdLine && wcsstr(lpCmdLine, L"-minimized"))
        g_startMinimized = TRUE;

    // 设置工作目录为exe所在目录
    GetModuleFileNameW(NULL, g_exeDir, MAX_PATH);
    wchar_t* lastSlash = wcsrchr(g_exeDir, L'\\');
    if (lastSlash) *lastSlash = L'\0';
    SetCurrentDirectoryW(g_exeDir);

    DialogBoxW(hInstance, MAKEINTRESOURCEW(IDD_MAIN), NULL, DlgProc);
    return 0;
}
