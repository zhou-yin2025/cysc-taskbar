// CYSC 任务栏 0.2 —— 卸载程序
//
// 纯 Win32，不依赖 Qt：本机就算 Qt 运行库被删了一半也能跑起来。
//
// 做四件事：
//   1. 结束正在运行的程序
//   2. 删开机自启项（HKCU\...\Run 下的 "CYSC TaskBar"）
//   3. 删设置（HKCU\Software\CYSC\CYSC TaskBar）
//   4. 删程序文件；用户勾选时连任务数据一起删
//
// 自我删除的做法：先把自身复制到 %TEMP% 再运行，让临时副本去删安装目录，
// 最后临时副本再删自己。否则 exe 正在运行、文件被占用，删不掉。
//
// 用法：卸载.exe            正常卸载
//       卸载.exe /dryrun    只写日志、不真删（自测用）

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <string>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")

static const wchar_t *kAppTitle = L"卸载 CYSC 任务栏 0.2";
static const wchar_t *kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t *kRunValue = L"CYSC TaskBar";
static const wchar_t *kSettingsKey = L"Software\\CYSC\\CYSC TaskBar";
static const wchar_t *kInstallExe = L"CYSC任务管理.exe";

static bool g_dryRun = false;
static bool g_silent = false;   // 跳过所有弹窗（自测 / 无人值守）
static std::wstring g_logPath;
static int g_deletedFiles = 0;

static void Log(const std::wstring &line) {
    std::wstring full = line + L"\r\n";
    HANDLE h = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    int bytes = WideCharToMultiByte(CP_UTF8, 0, full.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::vector<char> buf(bytes > 0 ? bytes : 0);
    WideCharToMultiByte(CP_UTF8, 0, full.c_str(), -1, buf.data(), bytes, nullptr, nullptr);
    if (bytes > 1) SetFilePointer(h, 0, nullptr, FILE_END), WriteFile(h, buf.data(), bytes - 1, &written, nullptr);
    CloseHandle(h);
}

static bool Exists(const std::wstring &path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

// 结束进程（按映像名匹配）
static void KillIfRunning(const wchar_t *exeName) {
    std::vector<DWORD> pids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe{sizeof(pe)};
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName) == 0) pids.push_back(pe.th32ProcessID);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    for (DWORD pid : pids) {
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (!h) continue;
        if (g_dryRun) Log(L"[dry] 结束进程 " + std::wstring(exeName));
        else TerminateProcess(h, 0);
        CloseHandle(h);
        WaitForSingleObject(h, 500);
    }
    if (!pids.empty()) Sleep(300);
}

// 递归删除目录内容（不删目录本身）
static void DeleteContents(const std::wstring &dir, const std::wstring &skipFullPath) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring full = dir + L"\\" + fd.cFileName;
        if (!skipFullPath.empty() && _wcsicmp(full.c_str(), skipFullPath.c_str()) == 0) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            DeleteContents(full, skipFullPath);
            if (g_dryRun) Log(L"[dry] 删目录 " + full);
            else RemoveDirectoryW(full.c_str());
        } else {
            SetFileAttributesW(full.c_str(), FILE_ATTRIBUTE_NORMAL);
            if (g_dryRun) Log(L"[dry] 删文件 " + full);
            else DeleteFileW(full.c_str());
            ++g_deletedFiles;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static void DeleteTree(const std::wstring &dir) {
    if (!Exists(dir)) return;
    DeleteContents(dir, L"");
    if (g_dryRun) Log(L"[dry] 删目录 " + dir);
    else RemoveDirectoryW(dir.c_str());
}

static std::wstring DirOf(const std::wstring &path) {
    size_t p = path.find_last_of(L'\\');
    return p == std::wstring::npos ? L"" : path.substr(0, p);
}

static std::wstring BaseName(const std::wstring &path) {
    size_t p = path.find_last_of(L'\\');
    return p == std::wstring::npos ? path : path.substr(p + 1);
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"/dryrun") == 0) g_dryRun = true;
        if (_wcsicmp(argv[i], L"/silent") == 0) g_silent = true;
    }

    wchar_t self[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring selfPath = self;

    wchar_t temp[MAX_PATH] = {0};
    GetTempPathW(MAX_PATH, temp);
    g_logPath = std::wstring(temp) + L"cysc-uninstall.log";

    // 判断自己是不是临时副本：是的话，参数里带着真正的安装目录
    bool staged = (argc > 1 && argv[1][0] != L'/' && wcsstr(argv[1], L":\\") != nullptr);

    if (!staged && !g_dryRun && !g_silent) {
        // 第一次运行：把自己复制到 %TEMP%，让副本去删安装目录
        std::wstring stageDir = std::wstring(temp) + L"cysc_uninstall";
        CreateDirectoryW(stageDir.c_str(), nullptr);
        std::wstring stageExe = stageDir + L"\\cysc-uninstall.exe";
        if (!CopyFileW(selfPath.c_str(), stageExe.c_str(), FALSE)) {
            MessageBoxW(nullptr, L"卸载程序无法复制到临时目录，可能是权限或杀毒软件拦截。\n"
                                 L"请把整个文件夹删掉，并手动删除 %APPDATA%\\CYSC。",
                        kAppTitle, MB_ICONERROR);
            return 1;
        }
        std::wstring cmd = L"\"" + stageExe + L"\" \"" + DirOf(selfPath) + L"\"";
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, const_cast<wchar_t *>(cmd.c_str()), nullptr, nullptr,
                           FALSE, 0, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        return 0;
    }

    std::wstring installDir = staged ? std::wstring(argv[1]) : DirOf(selfPath);
    Log(L"==== 卸载开始 ====");
    Log(L"安装目录: " + installDir);
    if (g_dryRun) Log(L"** dryrun 模式，不会真的删除 **");

    // 安全检查：目录里得真有这个程序的 exe，防止被挪到别处误删
    if (!Exists(installDir + L"\\" + kInstallExe) && !Exists(installDir + L"\\cysc-taskbar.exe")) {
        MessageBoxW(nullptr,
                    (L"这个文件夹里没有找到 CYSC 任务栏的程序文件：\n" + installDir +
                     L"\n\n为避免误删，卸载已取消。").c_str(),
                    kAppTitle, MB_ICONERROR);
        return 1;
    }

    // 1) 确认
    std::wstring msg = L"确定要卸载「任务栏 0.2」吗？\n\n将删除程序文件夹：\n" + installDir;
    if (!g_silent && MessageBoxW(nullptr, msg.c_str(), kAppTitle, MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
        return 0;

    // 2) 是否删数据
    int dataChoice = g_silent ? IDYES : MessageBoxW(
        nullptr,
        L"是否同时删除你的任务数据？\n\n"
        L"「是」= 连数据一起删（不可恢复）\n"
        L"「否」= 保留数据，只删程序\n\n"
        L"数据位置：%APPDATA%\\CYSC 与程序目录\\data",
        kAppTitle, MB_YESNO | MB_ICONWARNING);

    // 3) 结束进程
    KillIfRunning(kInstallExe);
    KillIfRunning(L"cysc-taskbar.exe");

    // 4) 开机自启
    HKEY runKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &runKey) == ERROR_SUCCESS) {
        LSTATUS st = RegDeleteValueW(runKey, kRunValue);
        Log(st == ERROR_SUCCESS ? L"已删除开机自启项" : L"开机自启项不存在或删除失败");
        RegCloseKey(runKey);
    }

    // 5) 设置
    LSTATUS sk = RegDeleteKeyW(HKEY_CURRENT_USER, kSettingsKey);
    Log(sk == ERROR_SUCCESS ? L"已删除注册表设置项" : L"注册表设置项不存在");

    // 6) 数据
    if (dataChoice == IDYES) {
        wchar_t appdata[MAX_PATH] = {0};
        if (SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata) == S_OK) {
            DeleteTree(std::wstring(appdata) + L"\\CYSC");
            Log(L"已删除 %APPDATA%\\CYSC");
        }
        wchar_t tempPath[MAX_PATH] = {0};
        GetTempPathW(MAX_PATH, tempPath);
        DeleteTree(std::wstring(tempPath) + L"CYSC");
    } else {
        Log(L"按用户选择保留任务数据");
    }
    DeleteTree(installDir + L"\\data");

    // 7) 程序文件（跳过正在运行的临时副本）
    DeleteContents(installDir, selfPath.c_str());
    if (g_dryRun) Log(L"[dry] 删目录 " + installDir);
    else RemoveDirectoryW(installDir.c_str());

    Log(L"共处理文件: " + std::to_wstring(g_deletedFiles));

    std::wstring done = L"卸载完成。\n\n";
    if (dataChoice == IDYES) done += L"任务数据已删除。\n";
    else done += L"任务数据已保留（%APPDATA%\\CYSC）。\n";
    done += L"\n日志：" + g_logPath;
    if (!g_silent) MessageBoxW(nullptr, done.c_str(), kAppTitle, MB_ICONINFORMATION);

    // 8) 删掉临时副本自己
    if (!g_dryRun) {
        std::wstring cmd = L"cmd.exe /c ping -n 3 127.0.0.1 >nul & del /f /q \"" + selfPath + L"\"";
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, const_cast<wchar_t *>(cmd.c_str()), nullptr, nullptr,
                           FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }
    return 0;
}
