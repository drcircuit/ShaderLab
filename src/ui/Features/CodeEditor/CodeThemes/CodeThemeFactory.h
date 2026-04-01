#pragma once

#include <string_view>

#include "TextEditor.h"

namespace ShaderLab::CodeThemes {

TextEditor::Palette MakeDarkDefaultPalette();
TextEditor::Palette MakeShaderPunkPalette();
TextEditor::Palette BuildCodeThemePalette(std::string_view themeName);

} // namespace ShaderLab::CodeThemes
