//*********************************************************
//
// Copyright 2020 Intel Corporation 
//
// Permission is hereby granted, free of charge, to any 
// person obtaining a copy of this software and associated 
// documentation files(the "Software"), to deal in the Software 
// without restriction, including without limitation the rights 
// to use, copy, modify, merge, publish, distribute, sublicense, 
// and/or sell copies of the Software, and to permit persons to 
// whom the Software is furnished to do so, subject to the 
// following conditions :
// The above copyright notice and this permission notice shall 
// be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF 
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT 
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
// WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
// DEALINGS IN THE SOFTWARE.
//
//*********************************************************

#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <functional>
#include <DirectXMath.h>

class FrustumViewer
{
public:
    FrustumViewer(ID3D12Device* in_pDevice,
        const DXGI_FORMAT in_swapChainFormat,
        const DXGI_FORMAT in_depthFormat,
        UINT in_sampleCount,
        class AssetUploader& in_assetUploader);

    void Draw(ID3D12GraphicsCommandList* in_pCL,
        const DirectX::XMMATRIX& in_combinedTransform,
        float in_fov, float in_aspectRatio);

    void SetView(const DirectX::XMMATRIX& in_viewInverse, float in_scale)
    {
        m_world = in_viewInverse;
        m_frustumConstants.m_scale = in_scale;
    }
private:
    template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    ComPtr<ID3D12Resource> m_indexBuffer;
    ComPtr<ID3D12Resource> m_vertexBuffer;

    D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
    D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;

    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineStateFill;
    ComPtr<ID3D12PipelineState> m_pipelineStateWireframe;
    ComPtr<ID3D12Resource> m_constantBuffer;

    struct FrustumConstants
    {
        DirectX::XMMATRIX m_combinedTransform;
        float m_fov;
        float m_aspectRatio;
        float m_scale;
    };
    FrustumConstants m_frustumConstants;

    UINT m_numIndices;

    DirectX::XMMATRIX m_world;
};
