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

#include "TextureViewer.h"
#include "DXSampleHelper.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
enum RootSignatureSlots
{
    ROOT_SIG_CONSTANT_BUFFER = 0,
    ROOT_SIG_SRV_TABLE,

    ROOT_SIG_NUM_DESCRIPTORS
};

struct ConstantBuffer
{
    float x, y, width, height;
    float gap;
    float visBaseMip;
    UINT32 vertical;
};

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
inline std::wstring GetAssetFullPath(const std::wstring in_filename)
{
    constexpr size_t PATHBUFFERSIZE = MAX_PATH * 4;
    TCHAR buffer[PATHBUFFERSIZE];
    GetCurrentDirectory(_countof(buffer), buffer);
    std::wstring directory = buffer;
    return directory + L"\\\\" + in_filename;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void TextureViewer::CreateResources(
    ID3D12Resource* in_pResource, D3D12_SHADER_RESOURCE_VIEW_DESC& in_desc,
    const DXGI_FORMAT in_swapChainFormat,
    ID3D12DescriptorHeap* in_pDescriptorHeap, INT in_descriptorOffset,
    const wchar_t* in_pShaderFileName, const char* in_psEntryPoint)
{
    in_pResource->GetDevice(IID_PPV_ARGS(&m_device));
    m_descriptorOffset = in_descriptorOffset;

    //-------------------------------------------------------------------------
    // Create root signature
    //-------------------------------------------------------------------------
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

        // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

        CD3DX12_DESCRIPTOR_RANGE1 ranges[1] = {};
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0,
            D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE,
            m_descriptorOffset);

        CD3DX12_ROOT_PARAMETER1 rootParameters[ROOT_SIG_NUM_DESCRIPTORS] = {};

        rootParameters[ROOT_SIG_CONSTANT_BUFFER].InitAsConstants((UINT)m_constants.size(), 0, 0, D3D12_SHADER_VISIBILITY_ALL);
        rootParameters[ROOT_SIG_SRV_TABLE].InitAsDescriptorTable(_countof(ranges), ranges, D3D12_SHADER_VISIBILITY_PIXEL);

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        D3D12_STATIC_SAMPLER_DESC staticSampler = {};
        staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        staticSampler.ShaderRegister = 0;
        staticSampler.MaxLOD = 0xff;
        rootSignatureDesc.Desc_1_1.NumStaticSamplers = 1;
        rootSignatureDesc.Desc_1_1.pStaticSamplers = &staticSampler;

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
        ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
    }

    //-------------------------------------------------------------------------
    // Create descriptor heap
    //-------------------------------------------------------------------------
    {
        if (in_pDescriptorHeap)
        {
            m_descriptorHeap = in_pDescriptorHeap;
        }
        else
        {
            // if creating its own descriptor heap, it also creates its own view in the first offset into the heap
            m_descriptorOffset = 0;

            D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc = { };
            descriptorHeapDesc.NumDescriptors = 1;
            descriptorHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            descriptorHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            ThrowIfFailed(m_device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&m_descriptorHeap)));
            m_descriptorHeap->SetName(L"TextureViewer::m_descriptorHeap");

            CD3DX12_CPU_DESCRIPTOR_HANDLE descriptorHandle(m_descriptorHeap->GetCPUDescriptorHandleForHeapStart(), m_descriptorOffset,
                m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
            m_device->CreateShaderResourceView(in_pResource, &in_desc, descriptorHandle);
        }
    }

    //-------------------------------------------------------------------------
    // Create shaders & pso
    //-------------------------------------------------------------------------
    {
        ComPtr<ID3DBlob> vertexShader;
        ComPtr<ID3DBlob> pixelShader;

#ifdef _DEBUG
        // Enable better shader debugging with the graphics debugging tools.
        UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
        UINT compileFlags = 0;
#endif

        // Load and compile shaders.
        ID3DBlob* pErrorMsgs = 0;

        HRESULT hr = 0;
        hr = D3DCompileFromFile(GetAssetFullPath(in_pShaderFileName).c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "vs", "vs_5_0", compileFlags, 0, &vertexShader, &pErrorMsgs);
        if (FAILED(hr) && (pErrorMsgs))
        {
            char* pMsgs = (char*)pErrorMsgs->GetBufferPointer();
            MessageBoxA(0, pMsgs, "error", MB_OK);
            pErrorMsgs->Release();
        }
        hr = D3DCompileFromFile(GetAssetFullPath(in_pShaderFileName).c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, in_psEntryPoint, "ps_5_0", compileFlags, 0, &pixelShader, &pErrorMsgs);
        if (FAILED(hr) && (pErrorMsgs))
        {
            char* pMsgs = (char*)pErrorMsgs->GetBufferPointer();
            MessageBoxA(0, pMsgs, "error", MB_OK);
            pErrorMsgs->Release();
        }

        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            { "SV_Position", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEX", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };


        CD3DX12_DEPTH_STENCIL_DESC depthStencilDesc(D3D12_DEFAULT);
        depthStencilDesc.DepthEnable = FALSE;
        depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_NEVER;

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
        psoDesc.pRootSignature = m_rootSignature.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState = depthStencilDesc;
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = in_swapChainFormat;
        psoDesc.SampleDesc.Count = 1;

        hr = m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState));
    }
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
TextureViewer::TextureViewer(
    ID3D12Resource* in_pResource,
    const DXGI_FORMAT in_swapChainFormat,
    ID3D12DescriptorHeap* in_pDescriptorHeap, INT in_descriptorOffset)
{
    D3D12_RESOURCE_DESC resourceDesc = in_pResource->GetDesc();
    m_numMips = resourceDesc.MipLevels;

    // create a view of the texture
    D3D12_SHADER_RESOURCE_VIEW_DESC textureViewDesc{};
    textureViewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    textureViewDesc.Format = resourceDesc.Format;
    textureViewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    textureViewDesc.Texture2D.MipLevels = resourceDesc.MipLevels;

    m_constants.resize(sizeof(ConstantBuffer) / sizeof(UINT32));

    CreateResources(
        in_pResource, textureViewDesc,
        in_swapChainFormat,
        in_pDescriptorHeap, in_descriptorOffset,
        L"TextureViewer.hlsl");
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
TextureViewer::~TextureViewer()
{
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void TextureViewer::DrawWindows(ID3D12GraphicsCommandList* in_pCL, D3D12_VIEWPORT in_viewPort,
    UINT in_numWindows)
{
    in_pCL->RSSetViewports(1, &in_viewPort);
    D3D12_RECT scissorRect =
    {
        LONG(in_viewPort.TopLeftX), LONG(in_viewPort.TopLeftY),
        LONG(in_viewPort.TopLeftX + in_viewPort.Width), LONG(in_viewPort.TopLeftY + in_viewPort.Height) };
    in_pCL->RSSetScissorRects(1, &scissorRect);

    in_pCL->SetGraphicsRootSignature(m_rootSignature.Get());
    in_pCL->SetPipelineState(m_pipelineState.Get());

    in_pCL->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    ID3D12DescriptorHeap* ppHeaps[] = { m_descriptorHeap.Get() };
    in_pCL->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    in_pCL->IASetIndexBuffer(nullptr);
    in_pCL->IASetVertexBuffers(0, 0, nullptr);

    in_pCL->SetGraphicsRoot32BitConstants(ROOT_SIG_CONSTANT_BUFFER, (UINT)m_constants.size(), m_constants.data(), 0);
    in_pCL->SetGraphicsRootDescriptorTable(ROOT_SIG_SRV_TABLE, (m_descriptorHeap->GetGPUDescriptorHandleForHeapStart()));

    in_pCL->DrawInstanced(4 * in_numWindows, 1, 0, 0);
}

//-----------------------------------------------------------------------------
// draw the rectangle
//-----------------------------------------------------------------------------
void TextureViewer::Draw(ID3D12GraphicsCommandList* in_pCL,
    DirectX::XMFLOAT2 in_position, DirectX::XMFLOAT2 in_windowDim,
    D3D12_VIEWPORT in_viewPort, int in_visualizationBaseMip, int in_numMips,
    bool in_vertical)
{
    if ((in_windowDim.x < MIN_WINDOW_DIM) || (in_windowDim.y < MIN_WINDOW_DIM))
    {
        return;
    }

    // adjust the base mip to visualize if user scrolled past the end
    if ((in_numMips + in_visualizationBaseMip) > m_numMips)
    {
        in_visualizationBaseMip = m_numMips - in_numMips;
    }

    ConstantBuffer* pConstants = (ConstantBuffer*)m_constants.data();
    pConstants->x = float(in_position.x) / in_viewPort.Width;
    pConstants->y = float(in_position.y) / in_viewPort.Height;

    pConstants->width = in_windowDim.x / in_viewPort.Width;
    pConstants->height = in_windowDim.y / in_viewPort.Height;
    pConstants->gap = 2.0f / in_viewPort.Height;

    pConstants->visBaseMip = (float)in_visualizationBaseMip;
    pConstants->vertical = in_vertical ? 1 : 0;

    DrawWindows(in_pCL, in_viewPort, in_numMips);
}
