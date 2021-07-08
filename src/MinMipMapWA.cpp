#include "pch.h"
#include "MinMipMapWA.h"
#include "DXSampleHelper.h"

// Interlocked* only works on int or uint 32b types

struct WAConstants
{
    UINT32 g_numLevels;
    UINT32 g_feedbackWidth;
    UINT32 g_feedbackHeight;
    float g_renderWidth;
    float g_renderHeight;
};

ComPtr<ID3DBlob> MinMipMapWA::g_vertexShader;
ComPtr<ID3DBlob> MinMipMapWA::g_pixelShader;

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
MinMipMapWA::MinMipMapWA(ID3D12Device* in_pDevice, UINT64 in_imageWidth, UINT in_imageHeight)
{

    UINT64 resolveWidth = UINT64(std::ceil(float(in_imageWidth) / float(m_hardwareRegionDim)));
    UINT resolveHeight = UINT(std::ceil(float(in_imageHeight) / float(m_hardwareRegionDim)));

    //---------------------------------------------
    // Create root signature
    //---------------------------------------------
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

        // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

        D3D12_DESCRIPTOR_RANGE1 ranges[2];
        ranges[0] = CD3DX12_DESCRIPTOR_RANGE1(
            D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0,
            D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE, 0);
        ranges[1] = CD3DX12_DESCRIPTOR_RANGE1(
            D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 0,
            D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 1);

        CD3DX12_ROOT_PARAMETER1 rootParameters[3] = {};
        UINT num32BitValues = sizeof(WAConstants) / 4;
        rootParameters[0].InitAsConstants(num32BitValues, 0, D3D12_ROOT_DESCRIPTOR_FLAG_NONE, D3D12_SHADER_VISIBILITY_PIXEL);
        rootParameters[1].InitAsDescriptorTable(_countof(ranges), ranges, D3D12_SHADER_VISIBILITY_PIXEL);

        D3D12_STATIC_SAMPLER_DESC staticSampler = {};
        staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
        staticSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
        staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        staticSampler.ShaderRegister = 0;
        staticSampler.MaxLOD = 0xff;

        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
        rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters,
            1, &staticSampler,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
        ThrowIfFailed(in_pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
    }

    //---------------------------------------------
    // Create shaders & pso
    //---------------------------------------------
    {
        const char shader[] = "\
        struct VS_OUT                              \
        {                                          \
            float4 pos : SV_POSITION;              \
            float2 uv : TEXCOORD0;                 \
        };                                         \
                                                   \
        cbuffer cb0 : register(b0)                 \
        {                                          \
            uint g_numLevels;                      \
            uint2 g_feedbackDim;                   \
            float g_renderWidth;                   \
            float g_renderHeight;                  \
        };                                         \
                                                   \
        VS_OUT vs(uint vertexID : SV_VertexID)     \
        {                                          \
            VS_OUT output;                         \
            float2 grid = float2((vertexID & 1) << 1, vertexID & 2);     \
            output.pos = float4(grid + float2(-1.0f, -1.0), 0.0f, 1.0f); \
            output.uv.xy = 0.5f * grid;            \
            output.uv.y = 1-output.uv.y;           \
            return output;                         \
        }                                          \
        Texture2D feedback : register(t0);         \
        RWTexture2D<uint> minmip : register(u1);   \
        SamplerState g_samPoint  : register(s0);   \
        uint psTypeConvert(VS_OUT input) : SV_Target \
        {                                          \
            uint2 uv = input.uv * g_feedbackDim;   \
            return minmip[uv];                     \
        }                                          \
        void ps(VS_OUT input)                      \
        {                                          \
            float2 uv = input.uv;                                            \
            uint2 destTilePos = uv * g_feedbackDim;                          \
            uint2 srcTilePos = destTilePos;                                  \
            uint2 screenDim = uint2(g_renderWidth, g_renderHeight);          \
            uint2 screenPos = uv * screenDim;                                \
            uint2 regionDim = screenDim / g_feedbackDim;                     \
            uint2 offset = screenPos - (srcTilePos * regionDim);             \
            for (uint i = 0; i < g_numLevels; i++) {                         \
                if (0!=feedback.SampleLevel(g_samPoint, uv, i).x)            \
                    { InterlockedMin(minmip[destTilePos], i); return;}       \
                srcTilePos >>= 1; screenDim /= 2;                            \
                screenPos = (srcTilePos * regionDim) + offset;               \
                uv = float2(screenPos) / screenDim;                          \
            }                                                                \
        }";
        // screenpos is say [0..255]
        // feedbackDim is say 8
        // regionDim then is 256/8 = 32, which is # texels per tile
        // tilePos will be 0,1,2,3,4,5,6,7
        // (tilePos * regionDim) + offset should equal screenpos for mip0
        // offset should be [0..31]

        // Load and compile shaders.
        ID3DBlob* pErrorMsgs = 0;
        HRESULT hr = 0;

        if (nullptr == g_vertexShader)
        {
            hr = D3DCompile(shader, strlen(shader), NULL, NULL, NULL, "vs", "vs_5_0", 0, 0, &g_vertexShader, &pErrorMsgs);
            if (FAILED(hr) && (pErrorMsgs))
            {
                char* pMsgs = (char*)pErrorMsgs->GetBufferPointer();
                pErrorMsgs->Release();
            }
            hr = D3DCompile(shader, strlen(shader), NULL, NULL, NULL, "ps", "ps_5_0", 0, 0, &g_pixelShader, &pErrorMsgs);
            if (FAILED(hr) && (pErrorMsgs))
            {
                char* pMsgs = (char*)pErrorMsgs->GetBufferPointer();
                pErrorMsgs->Release();
            }
        }

        D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
        {
            { "SV_Position", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        // Describe and create the graphics pipeline state object (PSO).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};

        psoDesc.pRootSignature = m_rootSignature.Get();
        psoDesc.VS = CD3DX12_SHADER_BYTECODE(g_vertexShader.Get());
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(g_pixelShader.Get());
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

        CD3DX12_DEPTH_STENCIL_DESC depthStencilDesc(D3D12_DEFAULT);
        depthStencilDesc.DepthEnable = FALSE;
        depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_NEVER;
        psoDesc.DepthStencilState = depthStencilDesc;

        psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 0;
        psoDesc.SampleDesc.Count = 1;

        ThrowIfFailed(in_pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));

        ComPtr<ID3DBlob> psTypeConvert;
        hr = D3DCompile(shader, strlen(shader), NULL, NULL, NULL, "psTypeConvert", "ps_5_0", 0, 0, &psTypeConvert, &pErrorMsgs);
        if (FAILED(hr) && (pErrorMsgs))
        {
            char* pMsgs = (char*)pErrorMsgs->GetBufferPointer();
            pErrorMsgs->Release();
        }
        psoDesc.PS = CD3DX12_SHADER_BYTECODE(psTypeConvert.Get());
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = DXGI_FORMAT_R8_UINT;
        ThrowIfFailed(in_pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_convertPSO)));
    }

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 2;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(in_pDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_srvHeap)));
    m_srvHeap->SetName(L"m_srvHeap");

    ThrowIfFailed(in_pDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8_UINT, resolveWidth, resolveHeight),
        D3D12_RESOURCE_STATE_RESOLVE_DEST, nullptr,
        IID_PPV_ARGS(&m_manualMinMipResolve)));
    m_manualMinMipResolve->SetName(L"m_manualMinMipResolve");

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = -1;
    // Can't sample UINT, so use unorm. only care about 0 or not 0
    srvDesc.Format = DXGI_FORMAT_R8_UNORM;
    srvDesc.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0, 0, 0, 0);
    in_pDevice->CreateShaderResourceView(m_manualMinMipResolve.Get(), &srvDesc,
        m_srvHeap->GetCPUDescriptorHandleForHeapStart());

    // CPU heap used for ClearUnorderedAccessView on feedback map
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 1; // only need the one for the single feedback map
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(in_pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_clearUavHeap)));
        m_clearUavHeap->SetName(L"SFB m_clearUavHeap");
    }

    // will render to final min mip map (type conversion only)
    {
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = 1;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        ThrowIfFailed(in_pDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));
    }
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void MinMipMapWA::WA(ID3D12Resource* out_pMinMipMap, ID3D12GraphicsCommandList* in_pCmdList)
{
    {
        ComPtr<ID3D12Device> device;
        in_pCmdList->GetDevice(IID_PPV_ARGS(&device));

        if (m_firstFrame)
        {
            m_firstFrame = false;

            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
            device->CreateRenderTargetView(out_pMinMipMap, nullptr, rtvHandle);

            auto desc = out_pMinMipMap->GetDesc();
            desc.Format = DXGI_FORMAT_R32_UINT;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            ThrowIfFailed(device->CreateCommittedResource(
                &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                D3D12_HEAP_FLAG_NONE,
                &desc,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                IID_PPV_ARGS(&m_mipResolve32b)));
            m_mipResolve32b->SetName(L"m_mipResolve32b");

            D3D12_UNORDERED_ACCESS_VIEW_DESC vd = {};
            vd.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
            vd.Format = m_mipResolve32b->GetDesc().Format;

            D3D12_CPU_DESCRIPTOR_HANDLE secondHeapCPU = m_clearUavHeap->GetCPUDescriptorHandleForHeapStart();

            device->CreateUnorderedAccessView(m_mipResolve32b.Get(), nullptr, &vd, secondHeapCPU);

            device->CopyDescriptorsSimple(1,
                CD3DX12_CPU_DESCRIPTOR_HANDLE(
                    m_srvHeap->GetCPUDescriptorHandleForHeapStart(),
                    1, device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)),
                secondHeapCPU, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }

        UINT clearValue[4]{ UINT_MAX, UINT_MAX, UINT_MAX, UINT_MAX };
        in_pCmdList->ClearUnorderedAccessViewUint(
            CD3DX12_GPU_DESCRIPTOR_HANDLE(m_srvHeap->GetGPUDescriptorHandleForHeapStart(),
                1, device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)),
            m_clearUavHeap->GetCPUDescriptorHandleForHeapStart(),
            m_mipResolve32b.Get(),
            clearValue, 0, nullptr);
    }

    in_pCmdList->SetGraphicsRootSignature(m_rootSignature.Get());

    ID3D12DescriptorHeap* ppHeaps[] = { m_srvHeap.Get() };
    in_pCmdList->SetDescriptorHeaps(_countof(ppHeaps), ppHeaps);

    D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(m_manualMinMipResolve.Get());
    D3D12_RECT scissor = CD3DX12_RECT(0, 0, (LONG)m_manualMinMipResolve->GetDesc().Width, m_manualMinMipResolve->GetDesc().Height);

    UINT num32BitValues = sizeof(WAConstants) / 4;
    auto desc = out_pMinMipMap->GetDesc();
    WAConstants waConstants{ m_manualMinMipResolve->GetDesc().MipLevels,
        (UINT32)desc.Width, desc.Height, viewport.Width, viewport.Height };
    in_pCmdList->SetGraphicsRoot32BitConstants(0, num32BitValues, &waConstants, 0);

    in_pCmdList->SetGraphicsRootDescriptorTable(1, m_srvHeap->GetGPUDescriptorHandleForHeapStart());

    in_pCmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    in_pCmdList->RSSetViewports(1, &viewport);
    in_pCmdList->RSSetScissorRects(1, &scissor);
    in_pCmdList->IASetIndexBuffer(nullptr);
    in_pCmdList->IASetVertexBuffers(0, 0, nullptr);

    in_pCmdList->SetPipelineState(m_pipelineState.Get());
    in_pCmdList->DrawInstanced(4, 1, 0, 0);

    {
        viewport = CD3DX12_VIEWPORT(out_pMinMipMap);
        scissor = CD3DX12_RECT(0, 0, (LONG)viewport.Width, (LONG)viewport.Height);
        in_pCmdList->RSSetViewports(1, &viewport);
        in_pCmdList->RSSetScissorRects(1, &scissor);

        in_pCmdList->SetPipelineState(m_convertPSO.Get());
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        in_pCmdList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
        in_pCmdList->DrawInstanced(4, 1, 0, 0);
    }
}
