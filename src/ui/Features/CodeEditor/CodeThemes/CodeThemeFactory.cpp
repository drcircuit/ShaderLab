#include "CodeThemeFactory.h"

#include <string>

namespace ShaderLab::CodeThemes {

TextEditor::Palette BuildCodeThemePalette(std::string_view themeName) {
    const std::string name(themeName);
    if (name == "ShaderPunk") {
        return MakeShaderPunkPalette();
    }
    return MakeDarkDefaultPalette();
}

} // namespace ShaderLab::CodeThemes
