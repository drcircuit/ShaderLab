#include "ShaderLab/UI/ShaderLabIDE.h"
#include "ShaderLab/UI/UISystemDemoUtils.h"

#include "ShaderLab/Graphics/Swapchain.h"
#include "ShaderLab/Graphics/PreviewRenderer.h"

#include <imgui.h>
#include <imgui_impl_dx12.h>

namespace ShaderLab {

void ShaderLabIDE::Render(ID3D12GraphicsCommandList* commandList) {
    bool previewRendered = false;
    if (m_previewRenderer && m_swapchainRef && m_deviceRef) {
        if (m_showAbout) {
            RenderAboutLogo(commandList);
        }
#if SHADERLAB_DEBUG
        previewRendered = m_dbgEnableRenderPreview ? RenderPreviewTexture(commandList) : false;
#else
        previewRendered = RenderPreviewTexture(commandList);
#endif
        RenderTutsPreviewTexture(commandList);

        if (previewRendered && m_previewVideoExportActive && !m_previewVideoExportPendingReadback &&
            m_previewVideoExportCapturedFrames < m_previewVideoExportTotalFrames) {
            QueuePreviewVideoCapture(commandList);
        }

        if (previewRendered) {
            D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_swapchainRef->GetCurrentRTV();
            commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

            D3D12_VIEWPORT viewport{};
            viewport.Width = static_cast<float>(m_swapchainRef->GetWidth());
            viewport.Height = static_cast<float>(m_swapchainRef->GetHeight());
            viewport.MinDepth = 0.0f;
            viewport.MaxDepth = 1.0f;
            commandList->RSSetViewports(1, &viewport);

            D3D12_RECT scissor{};
            scissor.right = static_cast<LONG>(m_swapchainRef->GetWidth());
            scissor.bottom = static_cast<LONG>(m_swapchainRef->GetHeight());
            commandList->RSSetScissorRects(1, &scissor);
        }
    }

    if (m_srvHeap) {
        ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
        commandList->SetDescriptorHeaps(1, heaps);
    }

    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
}

} // namespace ShaderLab
