// RetroRec v1.0 — Universal Ghost Screen Recorder
// "Every screen deserves to be recorded."
// Hidden Console Signals | Zero Pipe | Zero Corruption

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

#define RETROREC_VERSION "1.0.0"
#define ID_BTN_RECORD 1001
#define ID_HOTKEY_F12 1
#define ID_TIMER_UPDATE 2001

struct RetroRec {
    std::atomic<bool> recording{false};
    HWND hwnd = nullptr;
    HWND btnRecord = nullptr;
    HWND lblStatus = nullptr;
    HWND progressBar = nullptr;
    PROCESS_INFORMATION ffmpegProcess = {0};
    std::string finalFile;
    std::string ffmpegPath;
    std::string outputDir;
    int screenWidth = 1920;
    int screenHeight = 1080;
    int cpuCoreCount = 4;
    int dynamicThreads = 2;
    std::mutex mtx;
    std::chrono::steady_clock::time_point recStart;
    int sessions = 0;
    long long totalBytes = 0;
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
    std::string l = GetExeDir() + "\\ffmpeg.exe";
    if (FileExists(l)) { app.ffmpegPath = l; return true; }
    char b[MAX_PATH];
    if (SearchPathA(nullptr, "ffmpeg.exe", nullptr, MAX_PATH, b, nullptr) > 0) {
        app.ffmpegPath = b; return true;
    }
    return false;
}
void ConfigureUniversalPipeline() {
    SYSTEM_INFO si; GetSystemInfo(&si);
    app.cpuCoreCount = si.dwNumberOfProcessors;
    if (app.cpuCoreCount <= 2) app.dynamicThreads = 1;
    else if (app.cpuCoreCount <= 8) app.dynamicThreads = 2;
    else app.dynamicThreads = 3;
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
void UpdateUI() {
    if (app.recording) {
        auto e = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - app.recStart).count();
        SetButton("■ STOP (F12)");
        SetStatus("● REC " + FormatTime((int)e));
        SendMessage(app.progressBar, PBM_SETMARQUEE, 1, 0);
    } else {
        SetButton("▶ START (F12)");
        std::string s = "✓ Ready — F12 to record\r\n\r\n";
        s += "Cores: " + std::to_string(app.cpuCoreCount);
        s += " | Threads: " + std::to_string(app.dynamicThreads);
        s += "\r\n" + std::to_string(app.screenWidth) + "x" + std::to_string(app.screenHeight);
        s += "\r\nMKV | DDAGrab | VFR";
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
    ConfigureUniversalPipeline();
    std::string ts = Timestamp();
    app.finalFile = app.outputDir + "\\" + ts + ".mkv";
    int mr = (app.screenWidth >= 1920) ? 8000 : 6000;
    
    // Wrap your working Holy Grail command inside a hidden cmd.exe execution pipeline
    std::ostringstream c;
    c << "cmd.exe /c \"" 
      << "\"" << app.ffmpegPath << "\" -y -rtbufsize 2000M"
      << " -f lavfi -i ddagrab="
      << "framerate=60:video_size=" << app.screenWidth << "x" << app.screenHeight
      << ":draw_mouse=1:output_idx=0:output_fmt=bgra:allow_fallback=1:dup_frames=0"
      << " -max_muxing_queue_size 2048 -thread_queue_size 2048 -fps_mode vfr"
      << " -vf \"hwdownload,format=bgra,format=yuv420p\""
      << " -c:v libx264 -preset ultrafast -crf 25 -maxrate " << mr << "k -bufsize " << mr << "k"
      << " -pix_fmt yuv420p -g 600 -threads " << app.dynamicThreads
      << " -x264-params \"sliced-threads=0:sync-lookahead=0:rc-lookahead=0:cabac=0:deblock=0:partitions=none\""
      << " -f matroska \"" << app.finalFile << "\""
      << "\""; // Closing quote for the cmd.exe wrapper
    
    // Launch through our RunFFmpeg function using standard priority definitions
    if (!RunFFmpeg(c.str(), &app.ffmpegProcess, CREATE_NEW_PROCESS_GROUP | NORMAL_PRIORITY_CLASS)) {
        SetStatus("✗ Failed"); return;
    }
    
    SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
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
    SetStatus("⏳ Finalizing video file...");
    
    if (app.ffmpegProcess.hProcess) {
        // Temporarily instruct our main application to ignore console signals
        SetConsoleCtrlHandler(nullptr, TRUE);
        
        // Signal the hidden CMD process tree group to cleanly wrap up execution
        GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, app.ffmpegProcess.dwProcessId);
        
        // Wait for CMD and FFmpeg to naturally close down and write headers
        DWORD wr = WaitForSingleObject(app.ffmpegProcess.hProcess, 6000);
        if (wr == WAIT_TIMEOUT) {
            TerminateProcess(app.ffmpegProcess.hProcess, 0);
        }
        
        // Restore default console handling to RetroRec
        SetConsoleCtrlHandler(nullptr, FALSE);
        
        CloseHandle(app.ffmpegProcess.hProcess);
        CloseHandle(app.ffmpegProcess.hThread);
        app.ffmpegProcess.hProcess = nullptr;
        app.ffmpegProcess.hThread = nullptr;
    }
    
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
    app.recording = false;
    Sleep(800); // Storage buffer flush buffer wait
    
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
    case WM_CREATE:
        app.btnRecord = CreateWindowA("BUTTON", "▶ START (F12)",
            WS_VISIBLE|WS_CHILD|BS_DEFPUSHBUTTON, 20,20,340,50, h, (HMENU)ID_BTN_RECORD, nullptr, nullptr);
        app.lblStatus = CreateWindowA("STATIC", "",
            WS_VISIBLE|WS_CHILD|SS_LEFT, 20,90,340,120, h, nullptr, nullptr, nullptr);
        app.progressBar = CreateWindowA("msctls_progress32", "",
            WS_VISIBLE|WS_CHILD|PBS_MARQUEE, 20,225,340,15, h, nullptr, nullptr, nullptr);
        RegisterHotKey(h, ID_HOTKEY_F12, MOD_NOREPEAT, VK_F12);
        UpdateUI();
        return 0;
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
    app.screenWidth = GetSystemMetrics(SM_CXSCREEN);
    app.screenHeight = GetSystemMetrics(SM_CYSCREEN);
    app.outputDir = GetVideosFolder();
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
    int w = 395, h = 295;
    app.hwnd = CreateWindowExA(WS_EX_TOPMOST, "RetroRecWnd",
        "RetroRec v" RETROREC_VERSION,
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        (GetSystemMetrics(SM_CXSCREEN)-w)/2, (GetSystemMetrics(SM_CYSCREEN)-h)/2,
        w, h, nullptr, nullptr, hInst, nullptr);
    ShowWindow(app.hwnd, nCmdShow);
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return 0;
}