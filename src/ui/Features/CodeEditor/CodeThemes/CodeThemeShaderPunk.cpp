#include "CodeThemeFactory.h"

namespace ShaderLab::CodeThemes {

TextEditor::Palette MakeShaderPunkPalette() {
    auto palette = TextEditor::GetDarkPalette();
    palette[(int)TextEditor::PaletteIndex::Keyword] = 0xffd69c56;
    palette[(int)TextEditor::PaletteIndex::KnownIdentifier] = 0xffb0c94e;
    palette[(int)TextEditor::PaletteIndex::Number] = 0xffa8ceb5;
    palette[(int)TextEditor::PaletteIndex::String] = 0xff7891ce;
    palette[(int)TextEditor::PaletteIndex::Comment] = 0xff55996a;
    palette[(int)TextEditor::PaletteIndex::MultiLineComment] = 0xff55996a;
    palette[(int)TextEditor::PaletteIndex::Identifier] = 0xffdcdcdc;
    palette[(int)TextEditor::PaletteIndex::Punctuation] = 0xffdcdcdc;
    palette[(int)TextEditor::PaletteIndex::Preprocessor] = 0xff9b9b9b;
    return palette;
}

} // namespace ShaderLab::CodeThemes
