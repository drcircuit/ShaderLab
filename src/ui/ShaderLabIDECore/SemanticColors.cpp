#include "ShaderLab/UI/ShaderLabIDECore/SemanticColors.h"

namespace ShaderLab::EditorCore {

ImVec4 SemanticSuccessColor() {
    return ImVec4(0.2f, 0.85f, 0.4f, 1.0f);
}

ImVec4 SemanticWarningColor() {
    return ImVec4(0.95f, 0.70f, 0.25f, 1.0f);
}

ImVec4 SemanticErrorColor() {
    return ImVec4(0.95f, 0.35f, 0.35f, 1.0f);
}

ImVec4 SemanticInfoColor() {
    return ImVec4(0.35f, 0.72f, 0.95f, 1.0f);
}

}
