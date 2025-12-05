#include <windows.h>
#include <wininet.h>
#include <stdio.h>
#include "resource.h"

#pragma comment(lib, "wininet.lib")

static HWND g_hDlg = NULL;
static HANDLE g_hProcess = NULL;
static DWORD g_dwProcessId = 0;

#define APP_NAME L"SingboxManager"
#define SINGBOX_EXE L"sing-box.exe"
#define CONFIG_FILE L"config.json"
#define URL_FILE L"subscription.txt"

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

void UpdateSubscription(void) {
    wchar_t url[2048];
    GetDlgItemTextW(g_hDlg, IDC_EDIT_URL, url, 2048);

    if (wcslen(url) == 0) {
        UpdateStatus(L"状态: 请输入订阅链接");
        return;
    }

    SaveUrl();
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
            wchar_t path[MAX_PATH];
            GetModuleFileNameW(NULL, path, MAX_PATH);
            RegSetValueExW(hKey, APP_NAME, 0, REG_SZ, (BYTE*)path, (wcslen(path) + 1) * sizeof(wchar_t));
        } else {
            RegDeleteValueW(hKey, APP_NAME);
        }
        RegCloseKey(hKey);
    }
}

INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG:
        g_hDlg = hDlg;
        SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_ICON)));
        CheckDlgButton(hDlg, IDC_CHK_AUTO, IsAutoStartEnabled() ? BST_CHECKED : BST_UNCHECKED);
        LoadUrl();
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_BTN_TOGGLE:
            if (IsSingboxRunning()) StopSingbox();
            else StartSingbox();
            break;
        case IDC_BTN_RESTART:
            RestartSingbox();
            break;
        case IDC_BTN_UPDATE:
            UpdateSubscription();
            break;
        case IDC_CHK_AUTO:
            SetAutoStart(IsDlgButtonChecked(hDlg, IDC_CHK_AUTO) == BST_CHECKED);
            break;
        }
        return TRUE;

    case WM_CLOSE:
        SaveUrl();
        StopSingbox();
        EndDialog(hDlg, 0);
        return TRUE;
    }
    return FALSE;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;
    DialogBoxW(hInstance, MAKEINTRESOURCEW(IDD_MAIN), NULL, DlgProc);
    return 0;
}
