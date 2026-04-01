#include <windows.h>
#include <DbgHelp.h>

#include <cstring>
#include <string>
#include <vector>

#include "ShaderLab/App/PlayerApp.h"

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

bool IsSwitch(const std::wstring& arg, const wchar_t* name) {
    if (arg.empty()) {
        return false;
    }
    if (arg[0] != L'/' && arg[0] != L'-') {
        return false;
    }

    std::wstring value = arg.substr(1);
    const size_t colonPos = value.find(L':');
    if (colonPos != std::wstring::npos) {
        value = value.substr(0, colonPos);
    }
    return _wcsicmp(value.c_str(), name) == 0;
}

} // namespace

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    SetUnhandledExceptionFilter(WriteCrashMinidump);

    const auto args = GetCommandLineArgs();

    bool configMode = false;
    bool previewMode = false;
    bool runMode = true;
    bool debugConsole = false;
    bool loopPlayback = true;

    for (size_t i = 1; i < args.size(); ++i) {
        if (IsSwitch(args[i], L"c")) {
            configMode = true;
            runMode = false;
        } else if (IsSwitch(args[i], L"p")) {
            previewMode = true;
            runMode = false;
        } else if (IsSwitch(args[i], L"s")) {
            runMode = true;
        } else if (_wcsicmp(args[i].c_str(), L"-d") == 0 || _wcsicmp(args[i].c_str(), L"--debug") == 0) {
            debugConsole = true;
        } else if (_wcsicmp(args[i].c_str(), L"--no-loop") == 0 || _wcsicmp(args[i].c_str(), L"-noloop") == 0) {
            loopPlayback = false;
        }
    }

    if (configMode) {
        MessageBoxA(nullptr, "This ShaderLab screen saver has no configurable options.", "ShaderLab Screen Saver", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    if (previewMode) {
        return 0;
    }

    if (!runMode) {
        return 0;
    }

    ShaderLab::PlayerLaunchOptions options{};
    options.debugConsole = debugConsole;
    options.loopPlayback = loopPlayback;
    options.screenSaverMode = true;

    return ShaderLab::RunPlayerApp(hInstance, options);
}
