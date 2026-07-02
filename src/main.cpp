#include <windows.h>
#include <shellapi.h>
#include <psapi.h>
#include <dwmapi.h>

#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <string>

namespace {

constexpr UINT WM_TRAYICON = WM_APP + 1;
constexpr UINT WM_RECOVER = WM_APP + 2;
constexpr UINT WM_RECOVERY_DONE = WM_APP + 3;
constexpr UINT ID_TRAY_EXIT = 1001;
constexpr UINT ID_TRAY_STATUS = 1002;
constexpr UINT ID_TRAY_MANUAL_RECOVER = 1003;
constexpr UINT ID_HOLD_TIMER = 2001;
constexpr UINT ID_WATCHDOG_TIMER = 2002;
constexpr wchar_t kClassName[] = L"ForceUnfreezeTrayWindow";
constexpr wchar_t kTooltipBase[] = L"ForceUnfreeze - Active (F1 x5 or hold)";
constexpr wchar_t kLogFileName[] = L"ForceUnfreeze.log";

HINSTANCE g_instance = nullptr;
HWND g_hwnd = nullptr;
HHOOK g_keyboardHook = nullptr;
NOTIFYICONDATAW g_tray = {};
std::deque<DWORD> g_f1Presses;
DWORD g_f1DownTick = 0;
bool g_f1IsDown = false;
DWORD g_lastTriggerTick = 0;
DWORD g_recoveryCount = 0;
DWORD g_lastRecoveryDoneTick = 0;
bool g_hookInstalled = false;
UINT g_taskbarCreatedMessage = 0;
std::atomic_bool g_recoveryRunning{false};
std::atomic_bool g_logEnabled{true};
std::atomic_bool g_escalatedRecovery{false};

using IsHungAppWindowFn = BOOL(WINAPI *)(HWND);
IsHungAppWindowFn g_isHungAppWindow = nullptr;

void RemoveTrayIcon();

std::wstring g_logPath;

void InitializeLog() {
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring path(exePath);
    auto pos = path.find_last_of(L'\\');
    if (pos != std::wstring::npos) {
        path = path.substr(0, pos + 1);
    }
    path += kLogFileName;
    g_logPath = path;
}

void LogMessage(const wchar_t* message) {
    if (!g_logEnabled.load() || g_logPath.empty()) {
        return;
    }
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    struct tm tm_buf{};
    localtime_s(&tm_buf, &time_t);
    wchar_t timestamp[32]{};
    swprintf_s(timestamp, L"%02d:%02d:%02d", tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
    wchar_t logLine[512]{};
    swprintf_s(logLine, L"[%s] %s\r\n", timestamp, message);
    HANDLE hFile = CreateFileW(g_logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        SetFilePointer(hFile, 0, nullptr, FILE_END);
        char utf8[2048]{};
        int bytes = WideCharToMultiByte(CP_UTF8, 0, logLine, -1, utf8, static_cast<int>(sizeof(utf8)), nullptr, nullptr);
        if (bytes > 1) {
            DWORD written = 0;
            WriteFile(hFile, utf8, static_cast<DWORD>(bytes - 1), &written, nullptr);
        }
        CloseHandle(hFile);
    }
}

void LogRecoveryStart() {
    LogMessage(L"--- Recovery pass started ---");
}

void LogRecoveryStep(const wchar_t* step, bool success) {
    wchar_t msg[256]{};
    swprintf_s(msg, L"  [%s] %s", success ? L"OK" : L"SKIP", step);
    LogMessage(msg);
}

void LogRecoveryEnd() {
    wchar_t msg[128]{};
    swprintf_s(msg, L"--- Recovery pass completed (total: %lu) ---", g_recoveryCount);
    LogMessage(msg);
}

DWORD NowTick() {
    return GetTickCount();
}

bool CooldownElapsed(DWORD now) {
    if (g_lastTriggerTick == 0) {
        return true;
    }
    DWORD elapsed = now - g_lastTriggerTick;
    return elapsed > 4000;
}

bool CommandLineHas(const wchar_t* needle) {
    std::wstring commandLine = GetCommandLineW();
    return commandLine.find(needle) != std::wstring::npos;
}

void UpdateTrayTooltip() {
    wchar_t tip[128]{};
    if (g_recoveryCount > 0) {
        swprintf_s(tip, L"%s (%lu recoveries)", kTooltipBase, g_recoveryCount);
    } else {
        swprintf_s(tip, L"%s", kTooltipBase);
    }
    wcsncpy_s(g_tray.szTip, tip, _TRUNCATE);
    g_tray.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_tray);
}

void ShowTrayInfo(const wchar_t* title, const wchar_t* text) {
    if (g_tray.cbSize == 0) {
        return;
    }
    g_tray.uFlags = NIF_INFO;
    wcsncpy_s(g_tray.szInfoTitle, title, _TRUNCATE);
    wcsncpy_s(g_tray.szInfo, text, _TRUNCATE);
    g_tray.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &g_tray);
}

void AddTrayIcon(HWND hwnd) {
    RemoveTrayIcon();
    g_tray = {};
    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = hwnd;
    g_tray.uID = 1;
    g_tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_tray.uCallbackMessage = WM_TRAYICON;
    g_tray.hIcon = LoadIconW(g_instance, MAKEINTRESOURCEW(101));
    Shell_NotifyIconW(NIM_ADD, &g_tray);
    UpdateTrayTooltip();
}

void RemoveTrayIcon() {
    if (g_tray.cbSize != 0) {
        Shell_NotifyIconW(NIM_DELETE, &g_tray);
    }
}

void ShowTrayMenu(HWND hwnd) {
    POINT pt{};
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();

    wchar_t statusText[128]{};
    if (g_recoveryRunning.load()) {
        wcscpy_s(statusText, L"Status: Recovery running");
    } else if (g_recoveryCount > 0) {
        DWORD elapsed = g_lastRecoveryDoneTick ? (NowTick() - g_lastRecoveryDoneTick) / 1000 : 0;
        swprintf_s(statusText, L"Status: Active  |  %lu recovery(ies), last %lus ago", g_recoveryCount, elapsed);
    } else {
        wcscpy_s(statusText, L"Status: Active  |  No recoveries yet");
    }
    AppendMenuW(menu, MF_STRING | MF_GRAYED, ID_TRAY_STATUS, statusText);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, g_recoveryRunning.load() ? MF_STRING | MF_GRAYED : MF_STRING, ID_TRAY_MANUAL_RECOVER, L"Trigger Recovery");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TRAY_EXIT, L"Exit");
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
}

void PressKey(WORD vk, bool down) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(input));
}

bool LaunchDetached(const wchar_t* commandLine) {
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::wstring cmd = commandLine;
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi)) {
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

void BroadcastResponsivenessNudges() {
    DWORD_PTR ignored = 0;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, 0, SMTO_ABORTIFHUNG, 120, &ignored);
    SendNotifyMessageW(HWND_BROADCAST, WM_NULL, 0, 0);
}

BOOL CALLBACK NudgeWindowProc(HWND hwnd, LPARAM) {
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == GetCurrentProcessId()) {
        return TRUE;
    }

    SendMessageTimeoutW(hwnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_NOTIMEOUTIFNOTHUNG, 80, nullptr);
    if (g_isHungAppWindow && g_isHungAppWindow(hwnd)) {
        PostMessageW(hwnd, WM_CANCELMODE, 0, 0);
        PostMessageW(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        Sleep(50);
        PostMessageW(hwnd, WM_SYSCOMMAND, SC_RESTORE, 0);
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
    return TRUE;
}

void EmptyProcessWorkingSets() {
    DWORD bytesNeeded = 0;
    std::array<DWORD, 4096> pids{};
    if (!EnumProcesses(pids.data(), static_cast<DWORD>(pids.size() * sizeof(DWORD)), &bytesNeeded)) {
        return;
    }

    const DWORD count = bytesNeeded / sizeof(DWORD);
    for (DWORD i = 0; i < count; ++i) {
        if (pids[i] == 0 || pids[i] == GetCurrentProcessId()) {
            continue;
        }
        HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pids[i]);
        if (process) {
            EmptyWorkingSet(process);
            CloseHandle(process);
        }
    }
}

void BoostForegroundResponsiveness() {
    HWND fg = GetForegroundWindow();
    if (!fg) {
        return;
    }
    DWORD pid = 0;
    DWORD tid = GetWindowThreadProcessId(fg, &pid);
    if (tid) {
        PostThreadMessageW(tid, WM_NULL, 0, 0);
    }
    if (pid && pid != GetCurrentProcessId()) {
        HANDLE process = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (process) {
            SetPriorityClass(process, ABOVE_NORMAL_PRIORITY_CLASS);
            EmptyWorkingSet(process);
            CloseHandle(process);
        }
    }
}

bool RestartProcessByName(const wchar_t* exeName, const wchar_t* launchCommand) {
    DWORD bytesNeeded = 0;
    std::array<DWORD, 4096> pids{};
    if (!EnumProcesses(pids.data(), static_cast<DWORD>(pids.size() * sizeof(DWORD)), &bytesNeeded)) {
        LogMessage(L"EnumProcesses failed");
        return false;
    }

    bool restarted = false;
    const DWORD count = bytesNeeded / sizeof(DWORD);
    DWORD currentSession = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &currentSession);
    for (DWORD i = 0; i < count; ++i) {
        if (pids[i] == 0 || pids[i] == GetCurrentProcessId()) {
            continue;
        }
        DWORD processSession = 0;
        if (!ProcessIdToSessionId(pids[i], &processSession) || processSession != currentSession) {
            continue;
        }
        HANDLE process = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pids[i]);
        if (!process) {
            continue;
        }
        wchar_t path[MAX_PATH]{};
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(process, 0, path, &size)) {
            const wchar_t* base = wcsrchr(path, L'\\');
            base = base ? base + 1 : path;
            if (_wcsicmp(base, exeName) == 0) {
                TerminateProcess(process, 1);
                restarted = true;
            }
        }
        CloseHandle(process);
    }

    if (restarted && launchCommand) {
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        std::wstring cmd = launchCommand;
        if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
    return restarted;
}

void RecoverShellIfHung() {
    HWND shell = GetShellWindow();
    HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    bool shellHung = !shell;
    if (shell && g_isHungAppWindow) {
        shellHung = g_isHungAppWindow(shell) != FALSE;
    }
    if (taskbar && g_isHungAppWindow) {
        shellHung = shellHung || g_isHungAppWindow(taskbar) != FALSE;
    }
    if (shellHung) {
        LogMessage(L"Shell hung or missing, restarting explorer");
        RestartProcessByName(L"explorer.exe", L"explorer.exe");
    }
}

void ClearForegroundLock() {
    AllowSetForegroundWindow(ASFW_ANY);
    UINT timeout = 0;
    SystemParametersInfoW(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, &timeout, SPIF_SENDCHANGE);
}

void ForceForegroundWindow() {
    HWND fg = GetForegroundWindow();
    if (fg) {
        DWORD currentThread = GetCurrentThreadId();
        DWORD targetThread = GetWindowThreadProcessId(fg, nullptr);
        if (targetThread && targetThread != currentThread) {
            AttachThreadInput(currentThread, targetThread, TRUE);
        }
        SetForegroundWindow(fg);
        SetWindowPos(fg, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        RedrawWindow(fg, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        if (targetThread && targetThread != currentThread) {
            AttachThreadInput(currentThread, targetThread, FALSE);
        }
    }
}

void FlushDesktopComposition() {
    DwmFlush();
}

void RefreshDesktopWindowsOnly() {
    HWND desktop = GetDesktopWindow();
    if (desktop) {
        RedrawWindow(desktop, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
}

void EnsureTaskManagerAvailable() {
    if (!FindWindowW(nullptr, L"Task Manager")) {
        LogMessage(L"Task Manager not found, launching");
        LaunchDetached(L"taskmgr.exe");
    }
}

void RecoveryPass() {
    LogRecoveryStart();
    const bool escalated = g_escalatedRecovery.exchange(false);
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    UpdateTrayTooltip();

    ClearForegroundLock();
    LogRecoveryStep(L"ClearForegroundLock", true);

    ForceForegroundWindow();
    LogRecoveryStep(L"ForceForegroundWindow", true);

    BroadcastResponsivenessNudges();
    LogRecoveryStep(L"BroadcastResponsivenessNudges", true);

    BoostForegroundResponsiveness();
    LogRecoveryStep(L"BoostForegroundResponsiveness", true);

    EnumWindows(NudgeWindowProc, 0);
    LogRecoveryStep(L"EnumWindows/NudgeWindowProc", true);

    EmptyProcessWorkingSets();
    LogRecoveryStep(L"EmptyProcessWorkingSets", true);

    RecoverShellIfHung();
    LogRecoveryStep(L"RecoverShellIfHung", true);

    if (escalated) {
        LogMessage(L"Escalated recovery requested, restarting Explorer");
        RestartProcessByName(L"explorer.exe", L"explorer.exe");
        LogRecoveryStep(L"EscalatedExplorerRestart", true);
    }

    FlushDesktopComposition();
    LogRecoveryStep(L"FlushDesktopComposition", true);

    RefreshDesktopWindowsOnly();
    LogRecoveryStep(L"RefreshDesktopWindowsOnly", true);

    EnsureTaskManagerAvailable();
    LogRecoveryStep(L"EnsureTaskManagerAvailable", true);

    DwmFlush();
    SetThreadExecutionState(ES_CONTINUOUS);
    LogRecoveryEnd();
}

DWORD WINAPI RecoveryThreadProc(void*) {
    RecoveryPass();
    PostMessageW(g_hwnd, WM_RECOVERY_DONE, 0, 0);
    return 0;
}

void TriggerRecovery() {
    const DWORD now = NowTick();
    if (!CooldownElapsed(now)) {
        return;
    }
    if (g_lastRecoveryDoneTick != 0 && now - g_lastRecoveryDoneTick < 60000) {
        g_escalatedRecovery.store(true);
        LogMessage(L"Recovery escalation armed");
    }
    g_lastTriggerTick = now;
    LogMessage(L"Recovery triggered");
    PostMessageW(g_hwnd, WM_RECOVER, 0, 0);
}

LRESULT CALLBACK KeyboardHookProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HC_ACTION) {
        const auto* info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        if (info->vkCode == VK_F1) {
            const DWORD now = NowTick();
            const bool keyDown = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
            const bool keyUp = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;

            if (keyDown && !g_f1IsDown) {
                g_f1IsDown = true;
                g_f1DownTick = now;
                SetTimer(g_hwnd, ID_HOLD_TIMER, 100, nullptr);
                g_f1Presses.push_back(now);
                while (!g_f1Presses.empty() && now - g_f1Presses.front() > 2000) {
                    g_f1Presses.pop_front();
                }
                if (g_f1Presses.size() >= 5) {
                    g_f1Presses.clear();
                    TriggerRecovery();
                }
            } else if (keyDown && g_f1IsDown && g_f1DownTick != 0 && now - g_f1DownTick >= 3000) {
                TriggerRecovery();
                g_f1DownTick = now;
            } else if (keyUp) {
                g_f1IsDown = false;
                g_f1DownTick = 0;
                KillTimer(g_hwnd, ID_HOLD_TIMER);
            }
        }
    }
    return CallNextHookEx(g_keyboardHook, code, wParam, lParam);
}

void InstallHook() {
    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHookProc, g_instance, 0);
    if (!g_keyboardHook) {
        g_hookInstalled = false;
        LogMessage(L"Failed to install keyboard hook");
    } else {
        g_hookInstalled = true;
        LogMessage(L"Keyboard hook installed");
    }
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
}

void UninstallHook() {
    if (g_keyboardHook) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }
    g_hookInstalled = false;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == g_taskbarCreatedMessage) {
        AddTrayIcon(hwnd);
        LogMessage(L"TaskbarCreated received, tray icon restored");
        return 0;
    }

    switch (msg) {
    case WM_CREATE:
        AddTrayIcon(hwnd);
        InstallHook();
        SetTimer(hwnd, ID_WATCHDOG_TIMER, 30000, nullptr);
        return 0;
    case WM_TRAYICON:
        if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU) {
            ShowTrayMenu(hwnd);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TRAY_EXIT) {
            DestroyWindow(hwnd);
        } else if (LOWORD(wParam) == ID_TRAY_MANUAL_RECOVER) {
            TriggerRecovery();
        }
        return 0;
    case WM_RECOVER:
        {
            if (g_recoveryRunning.exchange(true)) {
                return 0;
            }
            HANDLE thread = CreateThread(nullptr, 0, RecoveryThreadProc, nullptr, 0, nullptr);
            if (thread) {
                SetThreadPriority(thread, THREAD_PRIORITY_HIGHEST);
                LogMessage(L"Recovery thread created");
                CloseHandle(thread);
            } else {
                LogMessage(L"Failed to create recovery thread");
                g_recoveryRunning.store(false);
            }
        }
        return 0;
    case WM_RECOVERY_DONE:
        ++g_recoveryCount;
        g_lastRecoveryDoneTick = NowTick();
        g_recoveryRunning.store(false);
        UpdateTrayTooltip();
        ShowTrayInfo(L"ForceUnfreeze", L"Recovery pass completed.");
        return 0;
    case WM_TIMER:
        if (wParam == ID_HOLD_TIMER && g_f1IsDown && g_f1DownTick != 0 && NowTick() - g_f1DownTick >= 3000) {
            KillTimer(hwnd, ID_HOLD_TIMER);
            g_f1DownTick = NowTick();
            TriggerRecovery();
        } else if (wParam == ID_WATCHDOG_TIMER && !g_keyboardHook) {
            LogMessage(L"Watchdog noticed missing keyboard hook, reinstalling");
            InstallHook();
            UpdateTrayTooltip();
        }
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, ID_HOLD_TIMER);
        KillTimer(hwnd, ID_WATCHDOG_TIMER);
        UninstallHook();
        RemoveTrayIcon();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

bool RegisterWindowClass() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_instance;
    wc.lpszClassName = kClassName;
    wc.hIcon = LoadIconW(g_instance, MAKEINTRESOURCEW(101));
    wc.hIconSm = LoadIconW(g_instance, MAKEINTRESOURCEW(101));
    return RegisterClassExW(&wc) != 0;
}

bool CreateHiddenWindow() {
    g_hwnd = CreateWindowExW(0, kClassName, L"ForceUnfreeze", 0, 0, 0, 0, 0, nullptr, nullptr, g_instance, nullptr);
    return g_hwnd != nullptr;
}

void LoadOptionalApis() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        g_isHungAppWindow = reinterpret_cast<IsHungAppWindowFn>(GetProcAddress(user32, "IsHungAppWindow"));
    }
}

void HardenSelfRuntime() {
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    SetProcessShutdownParameters(0x100, SHUTDOWN_NORETRY);
    RegisterApplicationRestart(nullptr, 0);

    PROCESS_POWER_THROTTLING_STATE throttling{};
    throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    throttling.StateMask = 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &throttling, sizeof(throttling));
    LogMessage(L"Runtime hardening applied");
}

} // namespace

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    g_instance = hInstance;
    InitializeLog();
    LogMessage(L"ForceUnfreeze starting");
    g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    LoadOptionalApis();
    HardenSelfRuntime();

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Global\\ForceUnfreeze.SingleInstance");
    if (mutex && GetLastError() != ERROR_ALREADY_EXISTS && CommandLineHas(L"--exit")) {
        LogMessage(L"Exit requested but no existing instance was running");
        ReleaseMutex(mutex);
        CloseHandle(mutex);
        return 0;
    }
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(kClassName, L"ForceUnfreeze");
        if (existing) {
            if (CommandLineHas(L"--exit")) {
                PostMessageW(existing, WM_CLOSE, 0, 0);
                LogMessage(L"Another instance is running, requested clean exit from existing instance");
            } else {
                PostMessageW(existing, WM_RECOVER, 0, 0);
                LogMessage(L"Another instance is running, requested recovery from existing instance");
            }
        } else {
            LogMessage(L"Another instance is already running but window was not found");
        }
        CloseHandle(mutex);
        return 0;
    }

    if (!RegisterWindowClass() || !CreateHiddenWindow()) {
        LogMessage(L"Failed to register window class or create hidden window");
        return 1;
    }
    LogMessage(L"ForceUnfreeze initialized successfully");

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return 0;
}
