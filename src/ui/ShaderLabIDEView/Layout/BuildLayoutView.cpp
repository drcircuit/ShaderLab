#include "ShaderLab/UI/ShaderLabIDE.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace ShaderLab {

void ShaderLabIDE::BuildLayout(UIMode mode) {
    ImGuiID dockspace_id = ImGui::GetID("MainDockspace");

    // Clear existing layout
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

    if (mode == UIMode::Demo) {
        // Demoscene Layout
        // Top: Transport (Small)
        // Left: Library (25%)
        // Center: Tracker (Top 50%), Preview (Bottom 50%)

        ImGuiID dock_main = dockspace_id;
        ImGuiID dock_up = 0, dock_down = 0;
        ImGuiID dock_left = 0, dock_right = 0;
        ImGuiID dock_right_top = 0, dock_right_bottom = 0;
        ImGuiID dock_left_top = 0, dock_left_bottom = 0;

        // 1. Top Transport
        ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Up, 0.08f, &dock_up, &dock_down);

        // 2. Left Library (from remaining space)
        ImGui::DockBuilderSplitNode(dock_down, ImGuiDir_Left, 0.25f, &dock_left, &dock_right);

        // 3. Right: Split into Tracker (Top) and Preview (Bottom)
        ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Up, 0.50f, &dock_right_top, &dock_right_bottom);

        // 4. Split Left Stack into Metadata / Scene / Audio / Log
        ImGuiID dock_left_mid = 0;
        ImGuiID dock_left_upper = 0;
        ImGui::DockBuilderSplitNode(dock_left, ImGuiDir_Down, 0.33f, &dock_left_bottom, &dock_left);
        ImGui::DockBuilderSplitNode(dock_left, ImGuiDir_Down, 0.50f, &dock_left_mid, &dock_left_top);
        ImGui::DockBuilderSplitNode(dock_left_top, ImGuiDir_Down, 0.60f, &dock_left_top, &dock_left_upper);

        ImGui::DockBuilderDockWindow("Transport", dock_up);
        ImGui::DockBuilderDockWindow("Demo: Playlist", dock_right_top);
        ImGui::DockBuilderDockWindow("Preview", dock_right_bottom);

        ImGui::DockBuilderDockWindow("Demo: Metadata", dock_left_top);
        ImGui::DockBuilderDockWindow("Demo: Scene Library", dock_left_upper);
        ImGui::DockBuilderDockWindow("Audio Library", dock_left_mid);
        ImGui::DockBuilderDockWindow("Demo: Runtime Log", dock_left_bottom);

    } else if (mode == UIMode::PostFX) {
        // Post FX Layout
        ImGuiID dock_main = dockspace_id;
        ImGuiID dock_up = 0, dock_down = 0;
        ImGuiID dock_left = 0, dock_center = 0, dock_right = 0;

        // 1. Top Transport
        ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Up, 0.08f, &dock_up, &dock_down);

        // 2. Left column (Library/Source)
        ImGui::DockBuilderSplitNode(dock_down, ImGuiDir_Left, 0.22f, &dock_left, &dock_down);

        // 3. Right column (Preview/Diagnostics)
        ImGui::DockBuilderSplitNode(dock_down, ImGuiDir_Right, 0.28f, &dock_right, &dock_center);

        // Split left stack
        ImGuiID dock_left_top = 0, dock_left_bot = 0;
        ImGui::DockBuilderSplitNode(dock_left, ImGuiDir_Up, 0.65f, &dock_left_top, &dock_left_bot);

        // Split center stack
        ImGuiID dock_center_top = 0, dock_center_bot = 0;
        ImGui::DockBuilderSplitNode(dock_center, ImGuiDir_Up, 0.28f, &dock_center_top, &dock_center_bot);

        // Split right stack
        ImGuiID dock_right_top = 0, dock_right_bot = 0;
        ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Up, 0.60f, &dock_right_top, &dock_right_bot);

        ImGui::DockBuilderDockWindow("Transport", dock_up);
        ImGui::DockBuilderDockWindow("FX: Library", dock_left_top);
        ImGui::DockBuilderDockWindow("FX: Source", dock_left_bot);
        ImGui::DockBuilderDockWindow("FX: Chain", dock_center_top);
        ImGui::DockBuilderDockWindow("Palette Generator", dock_center_top);
        ImGui::DockBuilderDockWindow("Shader Editor", dock_center_bot);
        ImGui::DockBuilderDockWindow("Preview", dock_right_top);
        ImGui::DockBuilderDockWindow("Diagnostics", dock_right_bot);
    } else {
        // Scene Mode Layout
        ImGuiID dock_main = dockspace_id;
        ImGuiID dock_up = 0, dock_down = 0;
        ImGuiID dock_left = 0, dock_center = 0, dock_right = 0;

        // 1. Top Transport
        ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Up, 0.08f, &dock_up, &dock_down);

        // 2. Split Left (Library)
        ImGui::DockBuilderSplitNode(dock_down, ImGuiDir_Left, 0.24f, &dock_left, &dock_down);

        // 3. Split Right (Preview/Diag)
        ImGui::DockBuilderSplitNode(dock_down, ImGuiDir_Right, 0.27f, &dock_right, &dock_center);

        // Split Right Stack
        ImGuiID dock_right_top = 0, dock_right_bot = 0;
        ImGui::DockBuilderSplitNode(dock_right, ImGuiDir_Up, 0.62f, &dock_right_top, &dock_right_bot);

        // Split Left Stack
        ImGuiID dock_left_top = 0, dock_left_bot = 0;
        ImGui::DockBuilderSplitNode(dock_left, ImGuiDir_Up, 0.40f, &dock_left_top, &dock_left_bot);

        ImGui::DockBuilderDockWindow("Transport", dock_up);
        ImGui::DockBuilderDockWindow("Scene: Library", dock_left_top);
        ImGui::DockBuilderDockWindow("Scene: Snippets", dock_left_bot);
        ImGui::DockBuilderDockWindow("Scene: Post Stack", dock_left_bot);
        ImGui::DockBuilderDockWindow("Palette Generator", dock_left_bot);
        ImGui::DockBuilderDockWindow("Shader Editor", dock_center);
        ImGui::DockBuilderDockWindow("Preview", dock_right_top);
        ImGui::DockBuilderDockWindow("Diagnostics", dock_right_bot);
        ImGui::DockBuilderDockWindow("Scene: Textures & Channels", dock_right_bot);
    }

    ImGui::DockBuilderFinish(dockspace_id);
}

} // namespace ShaderLab
