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

#include "pch.h"
#include "FrustumViewer.h"
#include "DebugHelper.h"
#include "AssetUploader.h"

using namespace DirectX;

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
FrustumViewer::FrustumViewer(ID3D12Device* in_pDevice,
    const DXGI_FORMAT in_swapChainFormat,
    const DXGI_FORMAT in_depthFormat,
    UINT in_sampleCount,
    AssetUploader& in_assetUploader) :
    m_world(XMMatrixIdentity())
    , m_frustumConstants{}
    , m_numIndices(0)
{

    //-------------------------------------------------------------------------
    // Create Vertex Buffer
    //-------------------------------------------------------------------------
    {
        // vertices are just position. will draw with constant color.
        struct Vertex
        {
            float x, y, z;
        };

        // pad to avoid debug messages
        Vertex vertices[]
        {
            {0, 0, 0},
            {-1, -1, 1},
            {-1, 1, 1},

            {1, -1, 1},
            {1, 1, 1}
        };

        UINT vertexBufferSize = sizeof(vertices);

        const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
        ThrowIfFailed(in_pDevice->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_vertexBuffer)));

        in_assetUploader.SubmitRequest(m_vertexBuffer.Get(), vertices, vertexBufferSize,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

        m_vertexBufferView.SizeInBytes = vertexBufferSize;
        m_vertexBufferView.StrideInBytes = sizeof(Vertex);
        m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    }

    //-------------------------------------------------------------------------
    // Create Index Buffer
    //-------------------------------------------------------------------------
    {
        UINT16 indices[]
        {
            0, 4, 2, // top
            0, 1, 3,  // bottom
            0, 1, 2, // left
            0, 4, 3, // right
        };

        UINT indexBufferSize = sizeof(indices);
        m_numIndices = _countof(indices);

        const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize);
        ThrowIfFailed(in_pDevice->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_indexBuffer)));

        in_assetUploader.SubmitRequest(m_indexBuffer.Get(), indices, indexBufferSize,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER);

        m_indexBufferView.SizeInBytes = indexBufferSize;
        m_indexBufferView.Format = DXGI_FORMAT_R16_UINT;
        m_indexBufferView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
    }

    //-------------------------------------------------------------------------
    // Create root signature
    //-------------------------------------------------------------------------
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

        // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

        CD3DX12_ROOT_PARAMETER1 rootParameters[1] = {};
        UINT num32BitValues = sizeof(m_frustumConstants) / 4;
        rootParameters[0].InitAsConstants(num32BitValues, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
        ThrowIfFailed(in_pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
    }

    //-------------------------------------------------------------------------
    // Create shaders & pso
    //-------------------------------------------------------------------------
    {
        const char shader[] = "                    \
        struct VS_IN                               \
        {                                          \
            float3 pos : POS;                      \
        };                                         \
                                                   \
        struct VS_OUT                              \
        {                                          \
            float4 pos : SV_POSITION;              \
float c : color;\
        };                                         \
                                                   \
        cbuffer cb0 : register(b0)                 \
        {                                          \
            float4x4 m_combinedTransform;          \
            float m_fov;                           \
            float m_aspectRatio;                   \
            float m_scale;                         \
        };                                         \
                                                   \
        VS_OUT vs(VS_IN input)                     \
        {                                          \
            VS_OUT output;                         \
            output.c = (input.pos.y+1) / 2.0f;     \
            float fov = sin(m_fov);                \
            input.pos.x *= fov;                    \
            input.pos.y *= fov/m_aspectRatio;      \
            output.pos = mul(m_combinedTransform,  \
            float4(m_scale*input.pos, 1.0f));      \
                                                   \
            return output;                         \
        }                                          \
                                                   \
        float4 ps(VS_OUT input) : SV_Target        \
        {                                          \
            return float4(                         \
            0.1f,                                  \
            0.8f*input.c,                          \
            0.8f + (0.2f*input.c), 0.5f);          \
        }";

        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;

        // Load and compile shaders.
        ID3DBlob* pErrorMsgs = 0;
        HRESULT hr = 0;

        hr = D3DCompile(shader, strlen(shader), NULL, NULL, NULL, "vs", "vs_5_0", 0, 0, &vertexShader, &pErrorMsgs);
        if (FAILED(hr) && (pErrorMsgs))
        {
            //char* pMsgs = (char*)pErrorMsgs->GetBufferPointer();
            pErrorMsgs->Release();
        }
        hr = D3DCompile(shader, strlen(shader), NULL, NULL, NULL, "ps", "ps_5_0", 0, 0, &pixelShader, &pErrorMsgs);
        if (FAILED(hr) && (pErrorMsgs))
        {
            //char* pMsgs = (char*)pErrorMsgs->GetBufferPointer();
            pErrorMsgs->Release();
        }

        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            { "POS", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};

        psoDesc.pRootSignature = m_rootSignature.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
        {
            psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            D3D12_RENDER_TARGET_BLEND_DESC& blendDesc = psoDesc.BlendState.RenderTarget[0];
            blendDesc.BlendEnable = TRUE;
            blendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
            blendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            blendDesc.BlendOp = D3D12_BLEND_OP_ADD;
        }
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

        psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = in_swapChainFormat;
        psoDesc.DSVFormat = in_depthFormat;
        psoDesc.SampleDesc.Count = in_sampleCount;

        hr = in_pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineStateFill));

        psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        hr = in_pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineStateWireframe));

        ThrowIfFailed(hr);
    }

}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void FrustumViewer::Draw(ID3D12GraphicsCommandList* in_pCL,
    const DirectX::XMMATRIX& in_combinedTransform,
    float in_fov, float in_aspectRatio)
{
    m_frustumConstants.m_combinedTransform = m_world * in_combinedTransform;
    m_frustumConstants.m_fov = in_fov;
    m_frustumConstants.m_aspectRatio = in_aspectRatio;

    in_pCL->SetGraphicsRootSignature(m_rootSignature.Get());
    UINT num32BitValues = sizeof(m_frustumConstants) / 4;
    in_pCL->SetGraphicsRoot32BitConstants(0, num32BitValues, &m_frustumConstants, 0);

    in_pCL->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    in_pCL->IASetIndexBuffer(&m_indexBufferView);
    in_pCL->IASetVertexBuffers(0, 1, &m_vertexBufferView);

    in_pCL->SetPipelineState(m_pipelineStateFill.Get());
    in_pCL->DrawIndexedInstanced(m_numIndices, 1, 0, 0, 0);

    in_pCL->SetPipelineState(m_pipelineStateWireframe.Get());
    in_pCL->DrawIndexedInstanced(m_numIndices, 1, 0, 0, 0);
}
