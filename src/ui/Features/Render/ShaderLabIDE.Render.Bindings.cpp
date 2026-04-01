#include "ShaderLab/UI/ShaderLabIDE.h"

#include "ShaderLab/Graphics/Device.h"

namespace ShaderLab {

void ShaderLabIDE::PopulateSceneBindingDescriptors(int sceneIndex, Scene& scene) {
    if (!scene.srvHeap || !m_deviceRef) {
        return;
    }

    auto device = m_deviceRef->GetDevice();
    auto handleStep = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    auto startHandle = scene.srvHeap->GetCPUDescriptorHandleForHeapStart();

    for (int i = 0; i < 8; ++i) {
        D3D12_CPU_DESCRIPTOR_HANDLE dest = startHandle;
        dest.ptr += i * handleStep;

        bool bound = false;
        for (const auto& b : scene.bindings) {
            if (b.channelIndex == i && b.enabled) {
                if (b.bindingType == BindingType::Scene) {
                    if (b.sourceSceneIndex != -1 && b.sourceSceneIndex != sceneIndex) {
                        if (b.sourceSceneIndex < 0 || b.sourceSceneIndex >= static_cast<int>(m_scenes.size())) {
                            continue;
                        }

                        auto& srcScene = m_scenes[b.sourceSceneIndex];
                        bool compatible = false;
                        if (srcScene.texture) {
                            D3D12_RESOURCE_DESC desc = srcScene.texture->GetDesc();

                            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                            srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

                            if (b.type == TextureType::TextureCube) {
                                if (desc.DepthOrArraySize == 6 && desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
                                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                                    srvDesc.TextureCube.MipLevels = 1;
                                    srvDesc.TextureCube.MostDetailedMip = 0;
                                    srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
                                    compatible = true;
                                }
                            } else if (b.type == TextureType::Texture3D) {
                                if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
                                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
                                    srvDesc.Texture3D.MipLevels = 1;
                                    srvDesc.Texture3D.MostDetailedMip = 0;
                                    srvDesc.Texture3D.ResourceMinLODClamp = 0.0f;
                                    compatible = true;
                                }
                            } else {
                                if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && desc.DepthOrArraySize == 1) {
                                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                                    srvDesc.Texture2D.MipLevels = 1;
                                    srvDesc.Texture2D.MostDetailedMip = 0;
                                    srvDesc.Texture2D.PlaneSlice = 0;
                                    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
                                    compatible = true;
                                }
                            }

                            if (compatible) {
                                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                                device->CreateShaderResourceView(srcScene.texture.Get(), &srvDesc, dest);
                                bound = true;
                            }
                        }
                    }
                } else if (b.bindingType == BindingType::File) {
                    if (b.fileTextureValid && b.textureResource) {
                        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                        srvDesc.Texture2D.MipLevels = 1;
                        srvDesc.Texture2D.MostDetailedMip = 0;
                        srvDesc.Texture2D.PlaneSlice = 0;
                        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

                        device->CreateShaderResourceView(b.textureResource.Get(), &srvDesc, dest);
                        bound = true;
                    }
                }
            }
        }

        if (!bound) {
            TextureType type = TextureType::Texture2D;
            for (const auto& b : scene.bindings) {
                if (b.channelIndex == i) {
                    type = b.type;
                    break;
                }
            }

            if (type == TextureType::TextureCube && m_dummySrvHeapCube) {
                device->CopyDescriptorsSimple(1, dest, m_dummySrvHeapCube->GetCPUDescriptorHandleForHeapStart(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            } else if (type == TextureType::Texture3D && m_dummySrvHeap3D) {
                device->CopyDescriptorsSimple(1, dest, m_dummySrvHeap3D->GetCPUDescriptorHandleForHeapStart(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            } else if (m_dummySrvHeap) {
                device->CopyDescriptorsSimple(1, dest, m_dummySrvHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }
        }
    }
}

}