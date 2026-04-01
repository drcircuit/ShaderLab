#include "ShaderLab/Runtime/RuntimeStartupPolicy.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

#ifndef SHADERLAB_TINY_PLAYER
#define SHADERLAB_TINY_PLAYER 0
#endif

#if !SHADERLAB_TINY_PLAYER
#include <iostream>
#endif

#include <windows.h>

namespace ShaderLab::RuntimeStartupPolicy {

namespace {

std::string BuildSingleInstanceMutexName() {
    char exePath[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) == 0) {
        return "Local\\ShaderLabPlayerSingleInstance";
    }

    std::string path(exePath);
    std::replace(path.begin(), path.end(), '\\', '/');
    const size_t hashValue = std::hash<std::string>{}(path);
    return "Local\\ShaderLabPlayerSingleInstance_" + std::to_string(hashValue);
}

} // namespace

void HandleCloser::operator()(void* handle) const {
    if (handle) {
        CloseHandle(static_cast<HANDLE>(handle));
    }
}

UniqueHandle CreateSingleInstanceMutex(bool& alreadyExists) {
    alreadyExists = false;
    const std::string mutexName = BuildSingleInstanceMutexName();
    UniqueHandle handle(CreateMutexA(nullptr, FALSE, mutexName.c_str()));
    alreadyExists = handle && GetLastError() == ERROR_ALREADY_EXISTS;
    return handle;
}

void AppendPlayerErrorLogLine(const std::string& message) {
    char exePath[MAX_PATH] = {};
    std::string logPath = "runtime_error.log";
    const DWORD length = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        std::string exe(exePath, length);
        const size_t slash = exe.find_last_of("\\/");
        if (slash != std::string::npos) {
            logPath = exe.substr(0, slash + 1) + "runtime_error.log";
        }
    }

    std::ofstream logFile(logPath, std::ios::out | std::ios::app);
    if (!logFile.is_open()) {
        return;
    }
    logFile << message << "\n";
}

 #if !SHADERLAB_TINY_PLAYER
void EmitRuntimeError(const char* code, const char* shortText) {
    if (!code || !*code) {
        return;
    }

    std::string line(code);
    if (shortText && *shortText) {
        line += " ";
        line += shortText;
    }
    std::fputs(line.c_str(), stderr);
    std::fputc('\n', stderr);
    AppendPlayerErrorLogLine(line);
}
#endif

void HideRuntimeCursor(bool& runtimeCursorHidden) {
    if (runtimeCursorHidden) {
        return;
    }
    while (ShowCursor(FALSE) >= 0) {
    }
    runtimeCursorHidden = true;
}

void RestoreRuntimeCursor(bool& runtimeCursorHidden) {
    if (!runtimeCursorHidden) {
        return;
    }
    while (ShowCursor(TRUE) < 0) {
    }
    runtimeCursorHidden = false;
}

void EnableConsoleLogging() {
#if !SHADERLAB_TINY_PLAYER
    if (!AllocConsole()) {
        return;
    }
    FILE* ignored = nullptr;
    freopen_s(&ignored, "CONOUT$", "w", stdout);
    freopen_s(&ignored, "CONOUT$", "w", stderr);
    std::ios::sync_with_stdio(true);
    std::cout.clear();
    std::cerr.clear();
#endif
}

} // namespace ShaderLab::RuntimeStartupPolicy
