#include "ShaderLab/UI/ShaderLabIDE.h"
#include "ShaderLab/UI/ShaderLabIDECore/ActionWidgets.h"
#include "ShaderLab/UI/OpenFontIcons.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace ShaderLab {

namespace {
using EditorActionWidgets::LabeledActionButton;

}

void ShaderLabIDE::ShowSnippetBin() {
    if (!ImGui::Begin("Scene: Snippets")) {
        ImGui::End();
        return;
    }

    namespace fs = std::filesystem;
    ImFont* snippetButtonFont = m_fontMenuSmall
        ? m_fontMenuSmall
        : (m_fontOrbitronText ? m_fontOrbitronText : ImGui::GetFont());
    float snippetButtonFontSize = 0.0f;
    if (m_fontCodeSizes[static_cast<int>(CodeFontSize::XS)]) {
        snippetButtonFontSize = m_fontCodeSizes[static_cast<int>(CodeFontSize::XS)]->LegacySize;
    } else if (snippetButtonFont) {
        snippetButtonFontSize = snippetButtonFont->LegacySize;
    }
    auto SnippetActionButton = [&](const char* id, uint32_t iconCodepoint, const char* label, const char* tooltip, const ImVec2& size) {
        if (snippetButtonFont && snippetButtonFontSize > 0.0f) {
            ImGui::PushFont(snippetButtonFont, snippetButtonFontSize);
        }
        const bool pressed = LabeledActionButton(id, iconCodepoint, label, tooltip, size);
        if (snippetButtonFont && snippetButtonFontSize > 0.0f) {
            ImGui::PopFont();
        }
        return pressed;
    };

    ImGui::Text("Reusable Snippets");
    ImGui::Separator();

    if (m_snippetFolders.empty()) {
        ShaderSnippetFolder folder;
        folder.name = "General";
        if (!m_snippetsDirectoryPath.empty()) {
            folder.filePath = (fs::path(m_snippetsDirectoryPath) / "General.json").string();
        }
        m_snippetFolders.push_back(std::move(folder));
        m_selectedSnippetFolderIndex = 0;
    }

    if (m_selectedSnippetFolderIndex < 0 || m_selectedSnippetFolderIndex >= (int)m_snippetFolders.size()) {
        m_selectedSnippetFolderIndex = 0;
    }

    std::vector<const char*> folderNames;
    folderNames.reserve(m_snippetFolders.size());
    for (auto& folder : m_snippetFolders) {
        folderNames.push_back(folder.name.c_str());
    }

    ImGui::TextUnformatted("Folder");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::Combo("##SnippetFolder", &m_selectedSnippetFolderIndex, folderNames.data(), (int)folderNames.size());

    auto& activeFolder = m_snippetFolders[m_selectedSnippetFolderIndex];
    static char folderNameBuffer[128] = {};
    static int folderNameIndex = -1;
    if (folderNameIndex != m_selectedSnippetFolderIndex) {
        folderNameIndex = m_selectedSnippetFolderIndex;
        std::snprintf(folderNameBuffer, sizeof(folderNameBuffer), "%s", activeFolder.name.c_str());
    }

    ImGui::TextUnformatted("Folder Name");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##SnippetFolderName", folderNameBuffer, sizeof(folderNameBuffer));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        std::string newFolderName = folderNameBuffer;
        if (newFolderName.empty()) {
            newFolderName = "Folder " + std::to_string(m_selectedSnippetFolderIndex + 1);
            std::snprintf(folderNameBuffer, sizeof(folderNameBuffer), "%s", newFolderName.c_str());
        }
        if (activeFolder.name != newFolderName) {
            auto sanitize = [](const std::string& value) {
                std::string out;
                out.reserve(value.size());
                for (char c : value) {
                    const bool ok =
                        (c >= 'a' && c <= 'z') ||
                        (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') ||
                        c == '_' || c == '-';
                    out.push_back(ok ? c : '_');
                }
                if (out.empty()) {
                    out = "Folder";
                }
                return out;
            };

            const fs::path oldFilePath = activeFolder.filePath.empty() ? fs::path() : fs::path(activeFolder.filePath);
            fs::path dir = m_snippetsDirectoryPath.empty() ? fs::current_path() : fs::path(m_snippetsDirectoryPath);
            std::string stem = sanitize(newFolderName);
            fs::path newFilePath = dir / (stem + ".json");

            auto isPathUsedByAnotherFolder = [&](const fs::path& candidate) {
                if (candidate.empty()) {
                    return false;
                }
                const fs::path normalizedCandidate = candidate.lexically_normal();
                for (size_t i = 0; i < m_snippetFolders.size(); ++i) {
                    if (static_cast<int>(i) == m_selectedSnippetFolderIndex) {
                        continue;
                    }
                    const auto& other = m_snippetFolders[i];
                    if (other.filePath.empty()) {
                        continue;
                    }
                    if (fs::path(other.filePath).lexically_normal() == normalizedCandidate) {
                        return true;
                    }
                }
                return false;
            };

            int suffix = 2;
            while ((fs::exists(newFilePath) && (!oldFilePath.empty() && newFilePath.lexically_normal() != oldFilePath.lexically_normal())) ||
                   isPathUsedByAnotherFolder(newFilePath)) {
                newFilePath = dir / (stem + "_" + std::to_string(suffix) + ".json");
                ++suffix;
            }

            activeFolder.name = std::move(newFolderName);
            activeFolder.filePath = newFilePath.string();
            SaveGlobalSnippets();

            if (!oldFilePath.empty()) {
                std::error_code ec;
                if (oldFilePath.lexically_normal() != newFilePath.lexically_normal()) {
                    fs::remove(oldFilePath, ec);
                }
            }
        }
    }

    static char newFolderName[64] = "";
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##NewSnippetFolder", newFolderName, sizeof(newFolderName));

    if (SnippetActionButton("SnippetAddFolder", OpenFontIcons::kPlus, "Add Folder", "Add folder", ImVec2(140.0f, 0.0f))) {
        std::string folderName = newFolderName;
        if (folderName.empty()) {
            folderName = "Folder " + std::to_string((int)m_snippetFolders.size() + 1);
        }

        auto sanitize = [](const std::string& value) {
            std::string out;
            out.reserve(value.size());
            for (char c : value) {
                const bool ok =
                    (c >= 'a' && c <= 'z') ||
                    (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') ||
                    c == '_' || c == '-';
                out.push_back(ok ? c : '_');
            }
            if (out.empty()) {
                out = "Folder";
            }
            return out;
        };

        fs::path dir = m_snippetsDirectoryPath.empty() ? fs::current_path() : fs::path(m_snippetsDirectoryPath);
        std::string stem = sanitize(folderName);
        fs::path filePath = dir / (stem + ".json");
        int suffix = 2;
        while (fs::exists(filePath)) {
            filePath = dir / (stem + "_" + std::to_string(suffix) + ".json");
            ++suffix;
        }

        ShaderSnippetFolder folder;
        folder.name = folderName;
        folder.filePath = filePath.string();
        m_snippetFolders.push_back(std::move(folder));
        m_selectedSnippetFolderIndex = (int)m_snippetFolders.size() - 1;
        m_selectedSnippetIndex = -1;
        SaveGlobalSnippets();
        newFolderName[0] = '\0';
    }
    ImGui::SameLine();
    if (SnippetActionButton("SnippetDeleteFolder", OpenFontIcons::kTrash2, "Delete Folder", "Delete folder", ImVec2(160.0f, 0.0f)) && m_snippetFolders.size() > 1) {
        fs::path fileToDelete = m_snippetFolders[m_selectedSnippetFolderIndex].filePath;
        m_snippetFolders.erase(m_snippetFolders.begin() + m_selectedSnippetFolderIndex);
        if (m_selectedSnippetFolderIndex >= (int)m_snippetFolders.size()) {
            m_selectedSnippetFolderIndex = (int)m_snippetFolders.size() - 1;
        }
        m_selectedSnippetIndex = -1;
        if (!fileToDelete.empty()) {
            std::error_code ec;
            fs::remove(fileToDelete, ec);
        }
        SaveGlobalSnippets();
    }

    auto& snippets = activeFolder.snippets;

    ImGui::Separator();

    if (ImGui::BeginTable("SnippetCreateActions", 1, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("CreateA", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextColumn();
        if (SnippetActionButton("SnippetAddEmpty", OpenFontIcons::kFilePlus, "Add Empty", "Add empty snippet", ImVec2(-FLT_MIN, 0.0f))) {
            ShaderSnippet snippet;
            snippet.name = "Snippet " + std::to_string(m_nextSnippetId++);
            snippet.code = "float4 SnippetFunc(float2 uv, float t) {\n    return float4(uv, sin(t), 1.0);\n}\n";
            snippets.push_back(std::move(snippet));
            m_selectedSnippetIndex = (int)snippets.size() - 1;
            m_snippetDraftFolderIndex = m_selectedSnippetFolderIndex;
            m_snippetDraftIndex = m_selectedSnippetIndex;
            m_snippetDraftCode = snippets[m_selectedSnippetIndex].code;
            m_snippetDraftDirty = false;
            m_snippetTextEditor.SetText(m_snippetDraftCode);
            SaveGlobalSnippets();
        }
        ImGui::EndTable();
    }

    ImGui::Separator();

    if (snippets.empty()) {
        ImGui::TextDisabled("No snippets yet.");
        ImGui::TextDisabled("Create one with Add Empty.");
        ImGui::End();
        return;
    }

    if (m_selectedSnippetIndex < 0 || m_selectedSnippetIndex >= (int)snippets.size()) {
        m_selectedSnippetIndex = 0;
    }

    std::vector<const char*> snippetNames;
    snippetNames.reserve(snippets.size());
    for (const auto& snippet : snippets) {
        snippetNames.push_back(snippet.name.c_str());
    }

    ImGui::TextUnformatted("Snippet");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::Combo("##SnippetSelector", &m_selectedSnippetIndex, snippetNames.data(), (int)snippetNames.size());

    auto& selected = snippets[m_selectedSnippetIndex];

    static char snippetNameBuffer[128] = {};
    static int snippetNameFolderIndex = -1;
    static int snippetNameIndex = -1;

    if (snippetNameFolderIndex != m_selectedSnippetFolderIndex ||
        snippetNameIndex != m_selectedSnippetIndex) {
        snippetNameFolderIndex = m_selectedSnippetFolderIndex;
        snippetNameIndex = m_selectedSnippetIndex;
        std::snprintf(snippetNameBuffer, sizeof(snippetNameBuffer), "%s", selected.name.c_str());
    }

    ImGui::TextUnformatted("Snippet Name");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("##SnippetName", snippetNameBuffer, sizeof(snippetNameBuffer));
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        std::string newName = snippetNameBuffer;
        if (newName.empty()) {
            newName = "Snippet " + std::to_string(m_nextSnippetId++);
            std::snprintf(snippetNameBuffer, sizeof(snippetNameBuffer), "%s", newName.c_str());
        }
        if (selected.name != newName) {
            selected.name = std::move(newName);
            SaveGlobalSnippets();
        }
    }

    if (m_snippetDraftFolderIndex != m_selectedSnippetFolderIndex ||
        m_snippetDraftIndex != m_selectedSnippetIndex) {
        m_snippetDraftFolderIndex = m_selectedSnippetFolderIndex;
        m_snippetDraftIndex = m_selectedSnippetIndex;
        m_snippetDraftCode = selected.code;
        m_snippetDraftDirty = false;
        m_snippetTextEditor.SetText(m_snippetDraftCode);
    }

    if (ImGui::BeginTable("SnippetActions", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("SnipActA", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("SnipActB", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextColumn();
        if (SnippetActionButton("SnippetInsert", OpenFontIcons::kInsert, "Insert", "Insert at cursor", ImVec2(-FLT_MIN, 0.0f))) {
            InsertSnippetIntoEditor(selected.code);
        }

        ImGui::TableNextColumn();
        if (SnippetActionButton("SnippetDelete", OpenFontIcons::kTrash2, "Delete", "Delete snippet", ImVec2(-FLT_MIN, 0.0f))) {
            snippets.erase(snippets.begin() + m_selectedSnippetIndex);
            if (m_selectedSnippetIndex >= (int)snippets.size()) {
                m_selectedSnippetIndex = (int)snippets.size() - 1;
            }
            SaveGlobalSnippets();
            ImGui::EndTable();
            ImGui::End();
            return;
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Snippet Code");
    ImGui::SameLine();
    if (SnippetActionButton("SnippetEditMode", m_snippetCodeLocked ? OpenFontIcons::kUnlock : OpenFontIcons::kLock,
                            m_snippetCodeLocked ? "Edit / Unlock" : "Relock",
                            m_snippetCodeLocked ? "Open snippet editor" : "Lock and close editor",
                            ImVec2(120.0f, 0.0f))) {
        if (m_snippetCodeLocked) {
            m_snippetCodeLocked = false;
            m_snippetDraftCode = selected.code;
            m_snippetTextEditor.SetText(m_snippetDraftCode);
            m_snippetDraftDirty = false;
            ImGui::OpenPopup("Snippet Editor");
        } else {
            m_snippetCodeLocked = true;
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled(m_snippetCodeLocked ? "Locked" : "Editing in popup");

    if (m_snippetCodeLocked) {
        // Only sync the TextEditor when the draft content has actually changed
        // (e.g. after folder/snippet selection change).  Avoid the per-frame
        // GetText() → O(n) string comparison that was killing performance.
        static int s_syncedFolder = -1;
        static int s_syncedSnippet = -1;
        if (s_syncedFolder != m_snippetDraftFolderIndex ||
            s_syncedSnippet != m_snippetDraftIndex) {
            s_syncedFolder = m_snippetDraftFolderIndex;
            s_syncedSnippet = m_snippetDraftIndex;
            m_snippetTextEditor.SetText(m_snippetDraftCode);
        }
        m_snippetTextEditor.SetReadOnly(true);
        m_snippetTextEditor.SetHandleMouseInputs(true);
        m_snippetTextEditor.SetHandleKeyboardInputs(true);
        float codeEditorHeight = (std::max)(180.0f, ImGui::GetContentRegionAvail().y);
        ImFont* activeSnippetCodeFont = ResolveCodeEditorFont(-1);
        if (activeSnippetCodeFont) {
            SyncEditorCommentFont(m_snippetTextEditor, activeSnippetCodeFont);
            ImGui::PushFont(activeSnippetCodeFont);
        }
        m_snippetTextEditor.Render("##SnippetCodeEditor", ImVec2(-1.0f, codeEditorHeight), true);
        if (activeSnippetCodeFont) {
            ImGui::PopFont();
        }
    }
    if (!m_snippetCodeLocked) {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport) {
            ImGui::SetNextWindowPos(
                ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f),
                ImGuiCond_Appearing,
                ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(viewport->Size.x * 0.72f, viewport->Size.y * 0.72f), ImGuiCond_Appearing);
        }
        bool popupOpen = true;
        if (ImGui::BeginPopupModal("Snippet Editor", &popupOpen, ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::TextUnformatted(selected.name.c_str());
            ImGui::Separator();

            m_snippetTextEditor.SetReadOnly(false);
            m_snippetTextEditor.SetHandleMouseInputs(true);
            m_snippetTextEditor.SetHandleKeyboardInputs(true);
            if (ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
            }
            ImFont* popupSnippetFont = ResolveCodeEditorFont(-1);
            if (popupSnippetFont) {
                SyncEditorCommentFont(m_snippetTextEditor, popupSnippetFont);
                ImGui::PushFont(popupSnippetFont);
            }
            const float popupEditorHeight = (std::max)(240.0f, ImGui::GetContentRegionAvail().y - (ImGui::GetFrameHeightWithSpacing() * 2.5f));
            m_snippetTextEditor.Render("##SnippetCodePopupEditor", ImVec2(-1.0f, popupEditorHeight), true);
            if (popupSnippetFont) {
                ImGui::PopFont();
            }

            if (m_snippetTextEditor.IsTextChanged()) {
                m_snippetDraftCode = m_snippetTextEditor.GetText();
                m_snippetDraftDirty = (m_snippetDraftCode != selected.code);
            }

            if (SnippetActionButton("SnippetSaveDraftPopup", OpenFontIcons::kSave, "Save Edits", "Save snippet edits", ImVec2(130.0f, 0.0f))) {
                selected.code = m_snippetDraftCode;
                m_snippetDraftDirty = false;
                SaveGlobalSnippets();
            }
            ImGui::SameLine();
            if (SnippetActionButton("SnippetRevertDraftPopup", OpenFontIcons::kRefresh, "Revert", "Revert unsaved edits", ImVec2(120.0f, 0.0f))) {
                m_snippetDraftCode = selected.code;
                m_snippetTextEditor.SetText(m_snippetDraftCode);
                m_snippetDraftDirty = false;
            }
            ImGui::SameLine();
            if (SnippetActionButton("SnippetCloseDraftPopup", OpenFontIcons::kX, "Done", "Close editor", ImVec2(100.0f, 0.0f))) {
                m_snippetCodeLocked = true;
                ImGui::CloseCurrentPopup();
            }

            if (m_snippetDraftDirty) {
                ImGui::TextDisabled("Unsaved snippet edits");
            }
            ImGui::EndPopup();
        }

        if (!popupOpen) {
            m_snippetCodeLocked = true;
        }
    }

    ImGui::End();
}

}
