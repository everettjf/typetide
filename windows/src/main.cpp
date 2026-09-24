//
//  main.cpp — 入口：单实例、COM/控件初始化、消息循环。
//  `--selftest` / `--selftest-translate` 走内建自检（stdout 输出）。
//
#include <windows.h>
#include "App.h"
#include "BuiltInModel.h"
#include "CrashDump.h"
#include "SelfTest.h"
#include "Util.h"
#include <objbase.h>
#include <shellapi.h>
#include <cstdio>
#include <atomic>
#include <string>

namespace {

int runSelfTest(bool live) {
    // stdout 已被重定向（管道/文件）就直接用；否则挂到父进程控制台
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == nullptr || out == INVALID_HANDLE_VALUE) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        SetConsoleOutputCP(CP_UTF8);
    }
    int failures = RunSelfTests(live);
    fflush(stdout);
    return failures;
}

int runBuiltInSelfTest() {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == nullptr || out == INVALID_HANDLE_VALUE) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
        FILE* stream = nullptr;
        freopen_s(&stream, "CONOUT$", "w", stdout);
        SetConsoleOutputCP(CP_UTF8);
    }
    std::atomic<bool> cancel{false};
    std::wstring error;
    int lastPercent = -1;
    if (!builtin::Download(cancel, [&](double fraction, const std::wstring&) {
        const int percent = int(fraction * 100);
        if (percent >= lastPercent + 10) {
            lastPercent = percent;
            printf("Model download: %d%%\n", percent);
            fflush(stdout);
        }
    }, &error)) {
        printf("Built-in model download failed: %s\n", util::Narrow(error).c_str());
        return 1;
    }
    for (const TranslationRequest& request : {
        TranslationRequest{L"你好世界，今天天气很好。", Language::Chinese, Language::English, RewriteStyle::Faithful},
        TranslationRequest{L"Please save the document before closing the window.", Language::English, Language::Chinese, RewriteStyle::Faithful}
    }) {
        std::wstring full;
        if (!builtin::Stream(request, 1, [](uint64_t, const std::wstring&) {}, cancel, &full, &error) ||
            util::Trim(full).empty()) {
            printf("Built-in translation failed: %s\n", util::Narrow(error).c_str());
            builtin::Shutdown();
            return 1;
        }
        printf("Built-in translation: %s\n", util::Narrow(full).c_str());
    }
    builtin::Shutdown();
    fflush(stdout);
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR cmdLine, int) {
    std::wstring args = cmdLine ? cmdLine : L"";

    crashdump::Install();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    if (args.find(L"--selftest-builtin") != std::wstring::npos)
        return runBuiltInSelfTest();
    if (args.find(L"--selftest") != std::wstring::npos) {
        const int result = runSelfTest(args.find(L"--selftest-translate") != std::wstring::npos);
        builtin::Shutdown();
        return result;
    }

    // 单实例：已在运行则让它打开设置窗口
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\TypeTideSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(kAppWindowClass, nullptr))
            PostMessageW(existing, WM_APP_OPEN_SETTINGS, 0, 0);
        return 0;
    }

    if (!App::shared().init(inst)) return 1;

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    builtin::Shutdown();

    if (mutex) CloseHandle(mutex);
    CoUninitialize();
    return (int)msg.wParam;
}
