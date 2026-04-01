#include <windows.h>
#include <DbgHelp.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "ShaderLab/App/PlayerApp.h"

#ifndef SHADERLAB_RUNTIME_DEBUG_LOG
#define SHADERLAB_RUNTIME_DEBUG_LOG 0
#endif

namespace {

using MiniDumpWriteDumpFn = BOOL (WINAPI*)(
    HANDLE,
    DWORD,
    HANDLE,
    MINIDUMP_TYPE,
    PMINIDUMP_EXCEPTION_INFORMATION,
    PMINIDUMP_USER_STREAM_INFORMATION,
    PMINIDUMP_CALLBACK_INFORMATION);

LONG WINAPI WriteCrashMinidump(EXCEPTION_POINTERS* exceptionInfo) {
    char exePath[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) == 0) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    char dumpPath[MAX_PATH] = {};
    lstrcpynA(dumpPath, exePath, MAX_PATH);
    char* lastDot = strrchr(dumpPath, '.');
    if (lastDot) {
        *lastDot = '\0';
    }

    SYSTEMTIME st = {};
    GetLocalTime(&st);
    char suffix[96] = {};
    wsprintfA(
        suffix,
        "_crash_%04u%02u%02u_%02u%02u%02u_pid%lu.dmp",
        st.wYear,
        st.wMonth,
        st.wDay,
        st.wHour,
        st.wMinute,
        st.wSecond,
        GetCurrentProcessId());
    lstrcatA(dumpPath, suffix);

    HMODULE dbghelp = LoadLibraryA("Dbghelp.dll");
    if (!dbghelp) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    auto miniDumpWriteDump = reinterpret_cast<MiniDumpWriteDumpFn>(
        GetProcAddress(dbghelp, "MiniDumpWriteDump"));
    if (!miniDumpWriteDump) {
        FreeLibrary(dbghelp);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    HANDLE dumpFile = CreateFileA(
        dumpPath,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (dumpFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION dumpExceptionInfo = {};
        dumpExceptionInfo.ThreadId = GetCurrentThreadId();
        dumpExceptionInfo.ExceptionPointers = exceptionInfo;
        dumpExceptionInfo.ClientPointers = FALSE;

        miniDumpWriteDump(
            GetCurrentProcess(),
            GetCurrentProcessId(),
            dumpFile,
            static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory),
            exceptionInfo ? &dumpExceptionInfo : nullptr,
            nullptr,
            nullptr);
        CloseHandle(dumpFile);
    }

    FreeLibrary(dbghelp);
    return EXCEPTION_EXECUTE_HANDLER;
}

std::vector<std::wstring> GetCommandLineArgs() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    if (!argv) {
        return args;
    }

    args.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    LocalFree(argv);
    return args;
}

bool HasAnyFlag(const std::vector<std::wstring>& args, const std::vector<std::wstring>& flags) {
    for (size_t i = 1; i < args.size(); ++i) {
        for (const auto& flag : flags) {
            if (_wcsicmp(args[i].c_str(), flag.c_str()) == 0) {
                return true;
            }
        }
    }
    return false;
}

bool IsArg(const std::wstring& arg, const wchar_t* value) {
    return _wcsicmp(arg.c_str(), value) == 0;
}

} // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    SetUnhandledExceptionFilter(WriteCrashMinidump);

    const auto args = GetCommandLineArgs();
#if SHADERLAB_RUNTIME_DEBUG_LOG
    bool debugConsole = HasAnyFlag(args, {L"-d", L"--debug"});
#else
    bool debugConsole = false;
#endif
    bool loopPlayback = !HasAnyFlag(args, {L"--no-loop", L"-noloop"});
    bool vsyncEnabled = true;
    bool startFullscreen = true;
    if (HasAnyFlag(args, {L"--loop", L"-loop"})) {
        loopPlayback = true;
    }

    for (size_t i = 1; i < args.size(); ++i) {
        if (IsArg(args[i], L"--no-vsync") || IsArg(args[i], L"--vsync-off") || IsArg(args[i], L"--unlimited-fps") || IsArg(args[i], L"--fps-unlimited")) {
            vsyncEnabled = false;
        } else if (IsArg(args[i], L"--vsync") || IsArg(args[i], L"--vsync-on")) {
            vsyncEnabled = true;
        } else if (IsArg(args[i], L"--windowed") || IsArg(args[i], L"-windowed")) {
            startFullscreen = false;
        } else if (IsArg(args[i], L"--fullscreen") || IsArg(args[i], L"-fullscreen")) {
            startFullscreen = true;
        }
    }

    ShaderLab::PlayerLaunchOptions options{};
    options.debugConsole = debugConsole;
    options.loopPlayback = loopPlayback;
    options.screenSaverMode = false;
    options.vsyncEnabled = vsyncEnabled;
    options.startFullscreen = startFullscreen;

    return ShaderLab::RunPlayerApp(hInstance, options);
}
