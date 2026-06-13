// RetroRec v1.1 — Universal Ghost Screen Recorder
// "Every screen deserves to be recorded."
// Built by MaxRBLX1
// MJPEG Recording | x264 Post-Convert | Zero Drops | Auto-CRF Matrix

#include <windows.h>
#include <shellapi.h>
#include <string>
#include <atomic>
#include <mutex>
#include <ctime>
#include <sstream>
#include <shlobj.h>
#include <chrono>
#include <vector>

#pragma comment(lib, "comctl32.lib")

#define RETROREC_VERSION "1.1.0"
#define ID_BTN_RECORD 1001
#define ID_HOTKEY_F12 1
#define ID_TIMER_UPDATE 2001

struct RetroRec {
    std::atomic<bool> recording{false};
    HWND hwnd = nullptr;
    HWND btnRecord = nullptr;
    UINT recordHotkey = VK_F12;
    UINT hotkeyModifiers = MOD_NOREPEAT;
    HWND lblStatus = nullptr;
    HWND progressBar = nullptr;
    PROCESS_INFORMATION ffmpegProcess = {0};
    std::string finalFile;
    std::string tempFile;
    std::string ffmpegPath;
    std::string outputDir;
    int screenWidth = 1920;
    int screenHeight = 1080;
    int cpuCoreCount = 4;
    int dynamicThreads = 2;
    int crf = 28;
    int maxrate = 4000;
    std::mutex mtx;
    std::chrono::steady_clock::time_point recStart;
    int sessions = 0;
    long long totalBytes = 0;
    int lastRecordingDurationMs = 0;
    int convertPreset = 0;
} app;

std::string GetExeDir() {
    char path[MAX_PATH]; GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string s(path); auto pos = s.find_last_of("\\/");
    return (pos != std::string::npos) ? s.substr(0, pos) : ".";
}
bool FileExists(const std::string& p) {
    DWORD a = GetFileAttributesA(p.c_str());
    return (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY));
}
long long GetFileSize(const std::string& p) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (GetFileAttributesExA(p.c_str(), GetFileExInfoStandard, &d)) {
        LARGE_INTEGER s; s.HighPart = d.nFileSizeHigh; s.LowPart = d.nFileSizeLow;
        return s.QuadPart;
    }
    return -1;
}
std::string FormatSize(long long b) {
    if (b < 0) return "Unknown";
    if (b < 1024) return std::to_string(b) + " B";
    double kb = b / 1024.0;
    if (kb < 1048576) { char x[32]; sprintf_s(x, "%.1f KB", kb); return x; }
    double mb = kb / 1024.0;
    if (mb < 1024) { char x[32]; sprintf_s(x, "%.1f MB", mb); return x; }
    char x[32]; sprintf_s(x, "%.2f GB", mb / 1024.0); return x;
}
std::string FormatTime(int s) {
    char b[32]; sprintf_s(b, "%02d:%02d", s/60, s%60); return b;
}
std::string Timestamp() {
    time_t n = time(nullptr); tm t; localtime_s(&t, &n);
    char b[64]; strftime(b, sizeof(b), "%Y%m%d_%H%M%S", &t);
    return "RetroRec_" + std::string(b);
}
std::string GetVideosFolder() {
    char p[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_MYVIDEO, nullptr, 0, p))) {
        std::string d = std::string(p) + "\\RetroRec";
        CreateDirectoryA(d.c_str(), nullptr);
        return d;
    }
    return GetExeDir();
}
bool FindFFmpeg() {
    std::string ucrt = "D:/msys2/ucrt64/bin/ffmpeg.exe";
    if (FileExists(ucrt)) { app.ffmpegPath = ucrt; return true; }
    std::string l = GetExeDir() + "\\ffmpeg.exe";
    if (FileExists(l)) { app.ffmpegPath = l; return true; }
    char b[MAX_PATH];
    if (SearchPathA(nullptr, "ffmpeg.exe", nullptr, MAX_PATH, b, nullptr) > 0) {
        app.ffmpegPath = b; return true;
    }
    return false;
}

UINT ReadHotkeyFromConfig() {
    std::string iniPath = GetExeDir() + "\\RetroRec.ini";
    char buf[32];
    GetPrivateProfileStringA("Settings", "Hotkey", "F12", buf, sizeof(buf), iniPath.c_str());
    if (strcmp(buf, "F1") == 0)  return VK_F1;
    if (strcmp(buf, "F2") == 0)  return VK_F2;
    if (strcmp(buf, "F3") == 0)  return VK_F3;
    if (strcmp(buf, "F4") == 0)  return VK_F4;
    if (strcmp(buf, "F5") == 0)  return VK_F5;
    if (strcmp(buf, "F6") == 0)  return VK_F6;
    if (strcmp(buf, "F7") == 0)  return VK_F7;
    if (strcmp(buf, "F8") == 0)  return VK_F8;
    if (strcmp(buf, "F9") == 0)  return VK_F9;
    if (strcmp(buf, "F10") == 0) return VK_F10;
    if (strcmp(buf, "F11") == 0) return VK_F11;
    return VK_F12;
}

void DetectCurrentResolution() {
    app.screenWidth = GetSystemMetrics(SM_CXSCREEN);
    app.screenHeight = GetSystemMetrics(SM_CYSCREEN);
}

void ConfigureUniversalPipeline() {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int cores = si.dwNumberOfProcessors;
    app.cpuCoreCount = cores;
    if (cores <= 2) {
        app.crf = 30; app.maxrate = 3000; app.dynamicThreads = 1;
    } else if (cores <= 4) {
        app.crf = 28; app.maxrate = 4000; app.dynamicThreads = 1;
    } else if (cores <= 8) {
        app.crf = 25; app.maxrate = 6000; app.dynamicThreads = 4;
    } else {
        app.crf = 20; app.maxrate = 12000; app.dynamicThreads = 8;
    }
}

bool RunFFmpeg(const std::string& cmd, PROCESS_INFORMATION* pi, DWORD flags) {
    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    std::vector<char> cl(cmd.begin(), cmd.end()); cl.push_back('\0');
    return CreateProcessA(nullptr, cl.data(), nullptr, nullptr, FALSE,
        flags, nullptr, nullptr, &si, pi);
}

void SetStatus(const std::string& t) {
    if (app.lblStatus && IsWindow(app.lblStatus)) SetWindowTextA(app.lblStatus, t.c_str());
}
void SetButton(const std::string& t) {
    if (app.btnRecord && IsWindow(app.btnRecord)) SetWindowTextA(app.btnRecord, t.c_str());
}

std::string GetHotkeyName() {
    if (app.recordHotkey >= VK_F1 && app.recordHotkey <= VK_F12)
        return "F" + std::to_string(app.recordHotkey - VK_F1 + 1);
    return "F12";
}

void UpdateUI() {
    std::string hotkey = GetHotkeyName();
    if (app.recording) {
        auto e = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - app.recStart).count();
        SetButton("■ STOP (" + hotkey + ")");
        std::string status = "● REC " + FormatTime((int)e);
        status += "\r\nCRF: " + std::to_string(app.crf);
        status += " | " + std::to_string(app.maxrate) + "k";
        SetStatus(status);
        SendMessage(app.progressBar, PBM_SETMARQUEE, 1, 0);
    } else {
        SetButton("▶ START (" + hotkey + ")");
        std::string s = "✓ Ready — " + hotkey + " to record\r\n\r\n";
        s += "Cores: " + std::to_string(app.cpuCoreCount);
        s += " | Threads: " + std::to_string(app.dynamicThreads);
        s += "\r\nCRF: " + std::to_string(app.crf);
        s += " | Bitrate: " + std::to_string(app.maxrate) + "k";
        s += "\r\n" + std::to_string(app.screenWidth) + "x" + std::to_string(app.screenHeight);
        s += "\r\nMJPEG | DDAGrab | VFR";
        s += "\r\nBuilt by MaxRBLX1";
        if (app.sessions > 0) s += "\r\nSessions: " + std::to_string(app.sessions);
        SetStatus(s);
        SendMessage(app.progressBar, PBM_SETPOS, 0, 0);
    }
}

void StartRecording() {
    std::lock_guard<std::mutex> l(app.mtx);
    if (app.recording) return;
    if (app.ffmpegPath.empty() && !FindFFmpeg()) {
        MessageBoxA(app.hwnd, "ffmpeg.exe not found!", "RetroRec", MB_OK);
        return;
    }
    DetectCurrentResolution();
    ConfigureUniversalPipeline();
    std::string ts = Timestamp();
    app.tempFile = app.outputDir + "\\" + ts + "_temp.mkv";
    app.finalFile = app.outputDir + "\\" + ts + ".mkv";

    std::ostringstream c;
    c << "cmd.exe /c \"" 
      << "\"" << app.ffmpegPath << "\" -y -rtbufsize 2000M"
      << " -f lavfi -i ddagrab="
      << "framerate=60:video_size=" << app.screenWidth << "x" << app.screenHeight
      << ":draw_mouse=1:output_idx=0:output_fmt=bgra:allow_fallback=1:dup_frames=0"
      << " -max_muxing_queue_size 2048 -thread_queue_size 2048 -fps_mode vfr"
      << " -vf \"hwdownload,format=bgra,format=yuv420p\""
      << " -c:v mjpeg -q:v 5"
      << " -threads " << app.dynamicThreads
      << " -f matroska \"" << app.tempFile << "\""
      << "\"";

    if (!RunFFmpeg(c.str(), &app.ffmpegProcess, CREATE_NEW_PROCESS_GROUP | NORMAL_PRIORITY_CLASS)) {
        SetStatus("✗ Failed"); return;
    }

    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    app.recording = true;
    app.recStart = std::chrono::steady_clock::now();
    app.sessions++;
    SetTimer(app.hwnd, ID_TIMER_UPDATE, 1000, nullptr);
    UpdateUI();
}

void StopRecording() {
    std::lock_guard<std::mutex> l(app.mtx);
    if (!app.recording) return;
    KillTimer(app.hwnd, ID_TIMER_UPDATE);
    SendMessage(app.progressBar, PBM_SETMARQUEE, 0, 0);
    SetStatus("⏳ Finalizing...");

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - app.recStart).count();
    app.lastRecordingDurationMs = (int)elapsed;
    app.recording = false;

    if (app.ffmpegProcess.hProcess) {
        SetConsoleCtrlHandler(nullptr, TRUE);
        GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, app.ffmpegProcess.dwProcessId);
        DWORD wr = WaitForSingleObject(app.ffmpegProcess.hProcess, 6000);
        if (wr == WAIT_TIMEOUT) TerminateProcess(app.ffmpegProcess.hProcess, 0);
        SetConsoleCtrlHandler(nullptr, FALSE);
        CloseHandle(app.ffmpegProcess.hProcess);
        CloseHandle(app.ffmpegProcess.hThread);
        app.ffmpegProcess.hProcess = nullptr;
        app.ffmpegProcess.hThread = nullptr;
    }

    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
    Sleep(500);

    if (FileExists(app.tempFile) && GetFileSize(app.tempFile) > 2048) {
        std::string presetName = (app.convertPreset == 0) ? "fast" : "medium";
        SetStatus("⏳ Converting (" + presetName + ")...");
        SendMessage(app.progressBar, PBM_SETPOS, 0, 0);

        HANDLE hRead, hWrite;
        SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
        CreatePipe(&hRead, &hWrite, &sa, 0);
        SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

        SYSTEM_INFO siCpu;
        GetSystemInfo(&siCpu);
        int conversionThreads = siCpu.dwNumberOfProcessors;

        char cmdLine[4096];
        sprintf_s(cmdLine, sizeof(cmdLine),
            "cmd.exe /c \"\"%s\" -y -progress pipe:1 -loglevel error -i \"%s\" -c:v libx264 -preset %s -crf 23 -pix_fmt yuv420p -threads %d \"%s\"\"",
            app.ffmpegPath.c_str(),
            app.tempFile.c_str(),
            presetName.c_str(),
            conversionThreads,
            app.finalFile.c_str()
        );

        STARTUPINFOA siConv = { sizeof(siConv) };
        siConv.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        siConv.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
        siConv.hStdOutput = hWrite;
        siConv.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
        siConv.wShowWindow = SW_HIDE;

        PROCESS_INFORMATION convertPI;

        if (CreateProcessA(nullptr, cmdLine, nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS,
            nullptr, nullptr, &siConv, &convertPI)) {

            CloseHandle(hWrite);

            char buf[512];
            std::string lineBuffer;
            DWORD bytesRead;
            long long totalDurationUs = (long long)app.lastRecordingDurationMs * 1000;

            while (ReadFile(hRead, buf, sizeof(buf) - 1, &bytesRead, NULL) && bytesRead > 0) {
                buf[bytesRead] = '\0';
                lineBuffer += buf;
                size_t pos;
                while ((pos = lineBuffer.find('\n')) != std::string::npos) {
                    std::string oneLine = lineBuffer.substr(0, pos);
                    lineBuffer = lineBuffer.substr(pos + 1);
                    if (!oneLine.empty() && oneLine.back() == '\r') oneLine.pop_back();
                    if (oneLine.find("out_time_ms=") == 0) {
                        try {
                            long long timeUs = std::stoll(oneLine.substr(12));
                            int percent = (totalDurationUs > 0) ? (int)((timeUs * 100) / totalDurationUs) : 0;
                            if (percent > 100) percent = 100;
                            SendMessage(app.progressBar, PBM_SETPOS, percent, 0);
                            SetStatus("⏳ Converting (" + presetName + ")... " + std::to_string(percent) + "%");
                        } catch (...) {}
                    }
                }
            }
            CloseHandle(hRead);
            WaitForSingleObject(convertPI.hProcess, INFINITE);
            CloseHandle(convertPI.hProcess);
            CloseHandle(convertPI.hThread);
            DeleteFileA(app.tempFile.c_str());
        } else {
            CloseHandle(hRead);
            CloseHandle(hWrite);
            MoveFileA(app.tempFile.c_str(), app.finalFile.c_str());
        }
    }

    long long fs = GetFileSize(app.finalFile);
    if (fs > 2048) {
        app.totalBytes += fs;
        SetStatus("✓ " + FormatSize(fs));
        std::string exp = "explorer /select,\"" + app.finalFile + "\"";
        WinExec(exp.c_str(), SW_SHOW);
    } else {
        SetStatus("✗ File Error: Corrupted or Empty");
    }
    UpdateUI();
}

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        app.btnRecord = CreateWindowA("BUTTON", "▶ START (F12)",
            WS_VISIBLE|WS_CHILD|BS_DEFPUSHBUTTON, 20,20,340,50, h, (HMENU)ID_BTN_RECORD, nullptr, nullptr);
        app.lblStatus = CreateWindowA("STATIC", "",
            WS_VISIBLE|WS_CHILD|SS_LEFT, 20,90,340,130, h, nullptr, nullptr, nullptr);
        app.progressBar = CreateWindowA("msctls_progress32", "",
            WS_VISIBLE|WS_CHILD|PBS_MARQUEE, 20,235,340,15, h, nullptr, nullptr, nullptr);
        RegisterHotKey(h, ID_HOTKEY_F12, app.hotkeyModifiers, app.recordHotkey);
        std::string btnText = "▶ START (" + GetHotkeyName() + ")";
        SetButton(btnText);
        UpdateUI();
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(w) == ID_BTN_RECORD) { if (app.recording) StopRecording(); else StartRecording(); }
        return 0;
    case WM_HOTKEY:
        if (w == ID_HOTKEY_F12) { if (app.recording) StopRecording(); else StartRecording(); }
        return 0;
    case WM_TIMER:
        if (w == ID_TIMER_UPDATE) UpdateUI();
        return 0;
    case WM_DESTROY:
        if (app.recording) StopRecording();
        UnregisterHotKey(h, ID_HOTKEY_F12);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    app.outputDir = GetVideosFolder();
    app.recordHotkey = ReadHotkeyFromConfig();
    if (!FindFFmpeg()) {
        MessageBoxA(nullptr, "Place ffmpeg.exe next to RetroRec.exe", "RetroRec", MB_OK);
        return 0;
    }
    WNDCLASSEXA wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc; wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE+1);
    wc.lpszClassName = "RetroRecWnd";
    RegisterClassExA(&wc);
    int w = 395, h = 310;
    app.hwnd = CreateWindowExA(WS_EX_TOPMOST, "RetroRecWnd",
        "RetroRec v" RETROREC_VERSION,
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        (GetSystemMetrics(SM_CXSCREEN)-w)/2, (GetSystemMetrics(SM_CYSCREEN)-h)/2,
        w, h, nullptr, nullptr, hInst, nullptr);
    if (!app.hwnd) return 1;
    ShowWindow(app.hwnd, nCmdShow);
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return 0;
}
