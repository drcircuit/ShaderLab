#pragma once

#include <memory>
#include <string>

namespace ShaderLab::RuntimeStartupPolicy {

#ifndef SHADERLAB_TINY_PLAYER
#define SHADERLAB_TINY_PLAYER 0
#endif

struct HandleCloser {
    void operator()(void* handle) const;
};

using UniqueHandle = std::unique_ptr<void, HandleCloser>;

UniqueHandle CreateSingleInstanceMutex(bool& alreadyExists);
void AppendPlayerErrorLogLine(const std::string& message);
#if !SHADERLAB_TINY_PLAYER
void EmitRuntimeError(const char* code, const char* shortText = nullptr);
#endif
void HideRuntimeCursor(bool& runtimeCursorHidden);
void RestoreRuntimeCursor(bool& runtimeCursorHidden);
void EnableConsoleLogging();

} // namespace ShaderLab::RuntimeStartupPolicy
