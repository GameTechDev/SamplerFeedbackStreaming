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

#include "pch.h"

#include <filesystem>
#include <DirectXMath.h>
#include <wrl.h>
#include <string>

#include <shlwapi.h> // for PathFindExtension
#pragma comment(lib, "Shlwapi.lib")

#include "SceneObject.h"
#include "SharedConstants.h"
#include "Scene.h"
#include "TerrainGenerator.h"
#include "AssetUploader.h"

//-------------------------------------------------------------------------
// constructor
//-------------------------------------------------------------------------
SceneObjects::BaseObject::BaseObject(
    const std::wstring& in_filename,
    TileUpdateManager* in_pTileUpdateManager,
    StreamingHeap* in_pStreamingHeap,
    ID3D12Device* in_pDevice,
    D3D12_CPU_DESCRIPTOR_HANDLE in_srvBaseCPU,
    BaseObject* in_pSharedObject) :
    m_matrix(DirectX::XMMatrixIdentity()),m_srvUavCbvDescriptorSize(0)
    , m_pTileUpdateManager(in_pTileUpdateManager)
    , m_combinedMatrix(DirectX::XMMatrixIdentity())
{
    //---------------------------------------
    // create root signature
    //---------------------------------------
    if (in_pSharedObject)
    {
        m_rootSignature = in_pSharedObject->m_rootSignature;
        m_rootSignatureFB = in_pSharedObject->m_rootSignatureFB;
        m_pipelineState = in_pSharedObject->m_pipelineState;
        m_pipelineStateFB = in_pSharedObject->m_pipelineStateFB;
    }
    else
    {
        //--------------------------------------------
        // uav and srvs for streaming texture
        //--------------------------------------------
        std::vector<CD3DX12_DESCRIPTOR_RANGE1> ranges;

        // these three descriptors are expected to be consecutive in the descriptor heap:
        // t0: streaming/reserved/paired texture
        // t1: min mip map
        // u0: feedback map

        // t0: streaming/reserved/paired texture
        ranges.push_back(CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE,
            UINT(Descriptors::HeapOffsetTexture)));

        // t1: min mip map
        // the min mip view descriptor is "volatile" because it changes if the # of objects changes.
        std::vector<CD3DX12_DESCRIPTOR_RANGE1> sharedRanges;
        sharedRanges.push_back(CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0));

        // b0: constant buffers
        std::vector<CD3DX12_DESCRIPTOR_RANGE1> cbvRanges;
        cbvRanges.push_back(CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE, 0));

        // s0: sampler
        std::vector<CD3DX12_DESCRIPTOR_RANGE1> samplerRanges;
        samplerRanges.push_back(CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE, 0));

        std::vector<CD3DX12_ROOT_PARAMETER1> rootParameters;
        rootParameters.resize((UINT)RootSigParams::NumParams);

        //-----------------------------
        // Root Signature Descriptor Tables & 32-bit constants
        //-----------------------------

        // per-object textures & UAVs
        CD3DX12_ROOT_PARAMETER1 rootParam;
        rootParam.InitAsDescriptorTable((UINT)ranges.size(), ranges.data(), D3D12_SHADER_VISIBILITY_PIXEL);
        rootParameters[(UINT)RootSigParams::ParamObjectTextures] = rootParam;

        // shared textures
        rootParam.InitAsDescriptorTable((UINT)sharedRanges.size(), sharedRanges.data(), D3D12_SHADER_VISIBILITY_PIXEL);
        rootParameters[(UINT)RootSigParams::ParamSharedTextures] = rootParam;

        // constant buffers used by both vertex & pixel shaders
        rootParam.InitAsDescriptorTable((UINT)cbvRanges.size(), cbvRanges.data(), D3D12_SHADER_VISIBILITY_ALL);
        rootParameters[(UINT)RootSigParams::ParamConstantBuffers] = rootParam;

        // samplers
        rootParam.InitAsDescriptorTable((UINT)samplerRanges.size(), samplerRanges.data(), D3D12_SHADER_VISIBILITY_PIXEL);
        rootParameters[(UINT)RootSigParams::ParamSamplers] = rootParam;

        UINT num32BitValues = sizeof(ModelConstantData) / sizeof(UINT32);
        rootParam.InitAsConstants(num32BitValues, 1, 0, D3D12_SHADER_VISIBILITY_ALL);
        rootParameters[(UINT)RootSigParams::Param32BitConstants] = rootParam;

        // root sig without feedback map bound
        {
            CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
            rootSignatureDesc.Init_1_1((UINT)rootParameters.size(), rootParameters.data(), 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
            ComPtr<ID3DBlob> signature;
            ComPtr<ID3DBlob> error;
            ThrowIfFailed(D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &signature, &error));
            ThrowIfFailed(in_pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
        }

        // root sig with feedback map bound
        {
            // add a UAV for the feedback map
            ranges.push_back(CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE,
                UINT(Descriptors::HeapOffsetFeedback)));

            // add uav range to previous root param
            rootParam.InitAsDescriptorTable((UINT)ranges.size(), ranges.data(), D3D12_SHADER_VISIBILITY_PIXEL);
            rootParameters[(UINT)RootSigParams::ParamObjectTextures] = rootParam;

            // re-serialize
            CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
            rootSignatureDesc.Init_1_1((UINT)rootParameters.size(), rootParameters.data(), 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

            ComPtr<ID3DBlob> signature;
            ComPtr<ID3DBlob> error;
            ThrowIfFailed(D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &signature, &error));
            ThrowIfFailed(in_pDevice->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignatureFB)));
        }
    }

    //---------------------------------------
    // create streaming resources
    //---------------------------------------
    {
        m_srvUavCbvDescriptorSize = in_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // The tile update manager queries the streaming texture for its tile dimensions
        // The feedback resource will be allocated with a mip region size matching the tile size
        m_pStreamingResource = in_pTileUpdateManager->CreateStreamingResource(in_filename, in_pStreamingHeap);

        // sampler feedback view
        CD3DX12_CPU_DESCRIPTOR_HANDLE feedbackHandle(in_srvBaseCPU, (UINT)Descriptors::HeapOffsetFeedback, m_srvUavCbvDescriptorSize);
        m_pStreamingResource->CreateFeedbackView(in_pDevice, feedbackHandle);

        // texture view
        CD3DX12_CPU_DESCRIPTOR_HANDLE textureHandle(in_srvBaseCPU, (UINT)Descriptors::HeapOffsetTexture, m_srvUavCbvDescriptorSize);
        m_pStreamingResource->CreateStreamingView(in_pDevice, textureHandle);
    }
}

//-------------------------------------------------------------------------
//-------------------------------------------------------------------------
void SceneObjects::BaseObject::CreatePipelineState(
    const wchar_t* in_ps, const wchar_t* in_psFB, const wchar_t* in_vs,
    ID3D12Device* in_pDevice, UINT in_sampleCount,
    const D3D12_RASTERIZER_DESC& in_rasterizerDesc,
    const D3D12_DEPTH_STENCIL_DESC& in_depthStencilDesc)
{
    // Define the vertex input layout
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        { "POS",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEX",    0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    std::wstring firstPassVsPath = GetAssetFullPath(in_vs);
    std::wstring firstPassPsPath = GetAssetFullPath(in_ps);

    std::ifstream vsFile(firstPassVsPath, std::fstream::binary);
    std::vector<char> vsBytes = std::vector<char>((std::istreambuf_iterator<char>(vsFile)), std::istreambuf_iterator<char>());
    std::ifstream psFile(firstPassPsPath, std::fstream::binary);
    std::vector<char> psBytes = std::vector<char>((std::istreambuf_iterator<char>(psFile)), std::istreambuf_iterator<char>());

    // Describe and create the graphics pipeline state object (PSO).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS.BytecodeLength = vsBytes.size();
    psoDesc.VS.pShaderBytecode = vsBytes.data();
    psoDesc.PS.BytecodeLength = psBytes.size();
    psoDesc.PS.pShaderBytecode = psBytes.data();
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState = in_rasterizerDesc;
    psoDesc.DepthStencilState = in_depthStencilDesc;
    psoDesc.InputLayout = { inputElementDescs, _countof(inputElementDescs) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = SharedConstants::SWAP_CHAIN_FORMAT;
    psoDesc.DSVFormat = SharedConstants::DEPTH_FORMAT;
    psoDesc.SampleDesc.Count = in_sampleCount;

    ThrowIfFailed(in_pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));

    // create a second PSO that writes to a feedback map using our root signature with the feedback map UAV bound
    {
        psoDesc.pRootSignature = m_rootSignatureFB.Get();

        firstPassPsPath = GetAssetFullPath(in_psFB);
        std::ifstream psFeedback(firstPassPsPath, std::fstream::binary);
        std::vector<char> psFeedbackBytes = std::vector<char>((std::istreambuf_iterator<char>(psFeedback)), std::istreambuf_iterator<char>());

        psoDesc.PS.BytecodeLength = psFeedbackBytes.size();
        psoDesc.PS.pShaderBytecode = psFeedbackBytes.data();

        ThrowIfFailed(in_pDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineStateFB)));
    }
}

//-------------------------------------------------------------------------
//-------------------------------------------------------------------------
std::wstring SceneObjects::BaseObject::GetAssetFullPath(const std::wstring& in_filename)
{
    WCHAR buffer[MAX_PATH];
    GetModuleFileName(nullptr, buffer, MAX_PATH);
    std::filesystem::path exePath(buffer);
    return exePath.remove_filename().append(in_filename);
}

//-------------------------------------------------------------------------
//-------------------------------------------------------------------------
void SceneObjects::BaseObject::SetModelConstants(ModelConstantData& out_modelConstantData,
    const DirectX::XMMATRIX&, const DirectX::XMMATRIX&)
{
    out_modelConstantData.g_combinedTransform = m_combinedMatrix;

    out_modelConstantData.g_worldTransform = m_matrix;

    out_modelConstantData.g_minmipmapWidth = m_pStreamingResource->GetMinMipMapWidth();
    out_modelConstantData.g_minmipmapHeight = m_pStreamingResource->GetMinMipMapHeight();
    out_modelConstantData.g_minmipmapOffset = m_pStreamingResource->GetMinMipMapOffset();
}

//-------------------------------------------------------------------------
//-------------------------------------------------------------------------
void SceneObjects::BaseObject::CopyGeometry(const BaseObject* in_pObjectForSharedHeap)
{
    m_lods.resize(in_pObjectForSharedHeap->m_lods.size());
    for (UINT i = 0; i < m_lods.size(); i++)
    {
        m_lods[i].m_numIndices = in_pObjectForSharedHeap->m_lods[i].m_numIndices;
        m_lods[i].m_indexBufferView = in_pObjectForSharedHeap->m_lods[i].m_indexBufferView;
        m_lods[i].m_vertexBufferView = in_pObjectForSharedHeap->m_lods[i].m_vertexBufferView;
    }
}

//-------------------------------------------------------------------------
//-------------------------------------------------------------------------
void SceneObjects::BaseObject::SetGeometry(ID3D12Resource* in_pVertexBuffer, UINT in_numVertices, UINT in_vertexSize,
    ID3D12Resource* in_pIndexBuffer, UINT in_numIndices, UINT in_lod)
{
    if (in_lod >= m_lods.size())
    {
        m_lods.resize(in_lod + 1);
    }

    auto& lod = m_lods[in_lod];

    lod.m_vertexBuffer = in_pVertexBuffer;
    lod.m_indexBuffer = in_pIndexBuffer;

    in_pVertexBuffer->Release();
    in_pIndexBuffer->Release();

    lod.m_numIndices = in_numIndices;
    lod.m_vertexBufferView = { lod.m_vertexBuffer->GetGPUVirtualAddress(), in_numVertices * in_vertexSize,  in_vertexSize };
    lod.m_indexBufferView = { lod.m_indexBuffer->GetGPUVirtualAddress(), UINT(in_numIndices * sizeof(UINT32)), DXGI_FORMAT_R32_UINT };
}

//-------------------------------------------------------------------------
//-------------------------------------------------------------------------
void SceneObjects::BaseObject::Draw(ID3D12GraphicsCommandList1* in_pCommandList, const SceneObjects::DrawParams& in_drawParams)
{
    //-------------------------------------------
    // draw object to multi-sampled rendertarget
    //-------------------------------------------
    if (m_pStreamingResource->GetPackedMipsResident())
    {
        // choose LoD if applicable
        UINT lod = 0;
        if (m_lods.size() > 1)
        {
            DirectX::XMVECTOR eye = in_drawParams.m_viewInverse.r[3];
            DirectX::XMVECTOR pos = m_matrix.r[3];
            DirectX::XMVECTOR distance = DirectX::XMVectorSubtract(pos, eye);
            distance = DirectX::XMVector3LengthEst(distance);

            // FIXME? could store bounding sphere diameter in object
            DirectX::XMVECTOR scale = DirectX::XMVector3LengthEst(m_matrix.r[0]);
            float objectSize = DirectX::XMVectorGetX(scale);

            float distanceToEye = DirectX::XMVectorGetX(distance);
            // slow lod progression with distance
            distanceToEye = std::powf(distanceToEye, 0.95f);

            float lodBias = 0.1f * (float)SharedConstants::SPHERE_LOD_BIAS;
            float ratio = distanceToEye / (objectSize * lodBias);

            // clamp to the number of lods
            lod = std::min((UINT)m_lods.size() - 1, UINT(ratio));
        }
        const auto& geometry = m_lods[lod];

        // if feedback is enabled, 2 things:
        // 1. tell the tile update manager to queue a readback of the resolved feedback
        // 2. draw the object with a shader that calls WriteSamplerFeedback()
        if (m_feedbackEnabled)
        {
            auto feedbackDescriptor = CD3DX12_GPU_DESCRIPTOR_HANDLE(in_drawParams.m_srvBaseGPU,
                UINT(SceneObjects::Descriptors::HeapOffsetFeedback), m_srvUavCbvDescriptorSize);
            m_pTileUpdateManager->QueueFeedback(GetStreamingResource(), feedbackDescriptor);
            SetRootSigPsoFB(in_pCommandList);
        }
        else
        {
            SetRootSigPso(in_pCommandList);
        }

        // uavs and srvs
        in_pCommandList->SetGraphicsRootDescriptorTable((UINT)RootSigParams::ParamObjectTextures, in_drawParams.m_srvBaseGPU);

        // shared min mip map resource
        in_pCommandList->SetGraphicsRootDescriptorTable((UINT)SceneObjects::RootSigParams::ParamSharedTextures, in_drawParams.m_sharedMinMipMap);

        // constant buffers
        in_pCommandList->SetGraphicsRootDescriptorTable((UINT)SceneObjects::RootSigParams::ParamConstantBuffers, in_drawParams.m_constantBuffers);

        // samplers
        in_pCommandList->SetGraphicsRootDescriptorTable((UINT)SceneObjects::RootSigParams::ParamSamplers, in_drawParams.m_samplers);

        ModelConstantData modelConstantData{};
        SetModelConstants(modelConstantData, in_drawParams.m_projection, in_drawParams.m_view);
        UINT num32BitValues = sizeof(ModelConstantData) / sizeof(UINT32);
        in_pCommandList->SetGraphicsRoot32BitConstants((UINT)RootSigParams::Param32BitConstants, num32BitValues, &modelConstantData, 0);

        in_pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        in_pCommandList->IASetIndexBuffer(&geometry.m_indexBufferView);
        in_pCommandList->IASetVertexBuffers(0, 1, &geometry.m_vertexBufferView);
        in_pCommandList->DrawIndexedInstanced(geometry.m_numIndices, 1, 0, 0, 0);
    }
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void SceneObjects::CreateSphereResources(
    ID3D12Resource** out_ppVertexBuffer, ID3D12Resource** out_ppIndexBuffer,
    ID3D12Device* in_pDevice, const SphereGen::Properties& in_sphereProperties,
    AssetUploader& in_assetUploader)
{
    std::vector<SphereGen::Vertex> sphereVerts;
    std::vector<UINT32> sphereIndices;

    SphereGen::Create(sphereVerts, sphereIndices, &in_sphereProperties);

    // build vertex buffer
    {
        UINT vertexBufferSize = UINT(sphereVerts.size()) * sizeof(sphereVerts[0]);

        const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
        ThrowIfFailed(in_pDevice->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(out_ppVertexBuffer)));

        in_assetUploader.SubmitRequest(*out_ppVertexBuffer, sphereVerts.data(), vertexBufferSize,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    }

    // build index buffer
    {
        UINT indexBufferSize = UINT(sphereIndices.size()) * sizeof(sphereIndices[0]);

        const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(indexBufferSize);
        ThrowIfFailed(in_pDevice->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(out_ppIndexBuffer)));

        in_assetUploader.SubmitRequest(*out_ppIndexBuffer, sphereIndices.data(), indexBufferSize,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    }
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void SceneObjects::CreateSphere(SceneObjects::BaseObject* out_pObject,
    ID3D12Device* in_pDevice, AssetUploader& in_assetUploader,
    const SphereGen::Properties& in_sphereProperties,
    UINT in_numLods)
{
    const float lodStepFactor = 1.0f / in_numLods;
    float lodScaleFactor = 1.0f;

    SphereGen::Properties sphereProperties = in_sphereProperties;

    for (UINT lod = 0; lod < in_numLods; lod++)
    {
        sphereProperties.m_numLat = UINT(in_sphereProperties.m_numLat * lodScaleFactor);
        sphereProperties.m_numLong = UINT(in_sphereProperties.m_numLong * lodScaleFactor);
        lodScaleFactor -= lodStepFactor;

        std::vector<SphereGen::Vertex> sphereVerts;
        std::vector<UINT32> sphereIndices;
        SphereGen::Create(sphereVerts, sphereIndices, &sphereProperties);

        ID3D12Resource* pVertexBuffer{ nullptr };
        ID3D12Resource* pIndexBuffer{ nullptr };
        CreateSphereResources(&pVertexBuffer, &pIndexBuffer, in_pDevice, sphereProperties, in_assetUploader);

        out_pObject->SetGeometry(
            pVertexBuffer, (UINT)sphereVerts.size(), (UINT)sizeof(SphereGen::Vertex),
            pIndexBuffer, (UINT)sphereIndices.size(), lod);
    }
}

//=========================================================================
//=========================================================================
SceneObjects::Terrain::Terrain(const std::wstring& in_filename,
    TileUpdateManager* in_pTileUpdateManager,
    StreamingHeap* in_pStreamingHeap,
    ID3D12Device* in_pDevice,
    UINT in_sampleCount,
    D3D12_CPU_DESCRIPTOR_HANDLE in_srvBaseCPU,
    const CommandLineArgs& in_args,
    AssetUploader& in_assetUploader) :
    BaseObject(in_filename, in_pTileUpdateManager, in_pStreamingHeap,
        in_pDevice, in_srvBaseCPU, nullptr)
{
    D3D12_RASTERIZER_DESC rasterizerDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    CreatePipelineState(L"terrainPS.cso", L"terrainPS-FB.cso", L"terrainVS.cso", in_pDevice, in_sampleCount, rasterizerDesc, depthStencilDesc);

    D3D12_INDEX_BUFFER_VIEW indexBufferView{};
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};

    ID3D12Resource* pVertexBuffer{ nullptr };
    ID3D12Resource* pIndexBuffer{ nullptr };

    TerrainGenerator mesh(in_args.m_terrainParams);

    // build vertex buffer
    {
        auto& vertices = mesh.GetVertices();

        UINT vertexBufferSize = UINT(vertices.size() * sizeof(vertices[0]));
        const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize);
        ThrowIfFailed(in_pDevice->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&pVertexBuffer)));

        in_assetUploader.SubmitRequest(pVertexBuffer, mesh.GetVertices().data(), vertexBufferSize,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
    }

    // build index buffer
    {
        const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(mesh.GetIndexBufferSize());
        ThrowIfFailed(in_pDevice->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&pIndexBuffer)));

        std::vector<BYTE> indices(mesh.GetIndexBufferSize());
        mesh.GenerateIndices((UINT*)indices.data());
        
        in_assetUploader.SubmitRequest(pIndexBuffer, indices.data(), indices.size(),
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_INDEX_BUFFER);
    }

    SetGeometry(pVertexBuffer, (UINT)mesh.GetVertices().size(), (UINT)sizeof(TerrainGenerator::Vertex),
        pIndexBuffer, mesh.GetNumIndices());
}

//=========================================================================
// planets have multiple LoDs
// Texture Coordinates may optionally be mirrored in U
//=========================================================================
SceneObjects::Planet::Planet(const std::wstring& in_filename,
    TileUpdateManager* in_pTileUpdateManager,
    StreamingHeap* in_pStreamingHeap,
    ID3D12Device* in_pDevice, AssetUploader& in_assetUploader,
    UINT in_sampleCount,
    D3D12_CPU_DESCRIPTOR_HANDLE in_srvBaseCPU,
    const SphereGen::Properties& in_sphereProperties) :
    BaseObject(in_filename, in_pTileUpdateManager, in_pStreamingHeap,
        in_pDevice, in_srvBaseCPU, nullptr)
{
    D3D12_RASTERIZER_DESC rasterizerDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    CreatePipelineState(L"terrainPS.cso", L"terrainPS-FB.cso", L"terrainVS.cso", in_pDevice, in_sampleCount, rasterizerDesc, depthStencilDesc);

    const UINT numLevelsOfDetail = SharedConstants::NUM_SPHERE_LEVELS_OF_DETAIL;
    CreateSphere(this, in_pDevice, in_assetUploader, in_sphereProperties, numLevelsOfDetail);

    float d = SharedConstants::SPHERE_SCALE * 200;
    GetModelMatrix() = DirectX::XMMatrixScaling(d, d, d);
}

//-------------------------------------------------------------------------
//-------------------------------------------------------------------------
SceneObjects::Planet::Planet(const std::wstring& in_filename,
    TileUpdateManager* in_pTileUpdateManager,
    StreamingHeap* in_pStreamingHeap,
    ID3D12Device* in_pDevice,
    D3D12_CPU_DESCRIPTOR_HANDLE in_srvBaseCPU,
    BaseObject* in_pSharedObject) :
    BaseObject(in_filename, in_pTileUpdateManager, in_pStreamingHeap,
        in_pDevice, in_srvBaseCPU, in_pSharedObject)
{
    CopyGeometry(in_pSharedObject);
}

//=============================================================================
// sky is like a planet with front culling instead of backface culling
// it is always positioned around 0,0,0. The camera only rotates relative to the sky
// it has only 1 LoD
// shading is much simpler: no lighting
//=============================================================================
SceneObjects::Sky::Sky(const std::wstring& in_filename,
    TileUpdateManager* in_pTileUpdateManager,
    StreamingHeap* in_pStreamingHeap,
    ID3D12Device* in_pDevice, AssetUploader& in_assetUploader,
    UINT in_sampleCount,
    D3D12_CPU_DESCRIPTOR_HANDLE in_srvBaseCPU) :
    BaseObject(in_filename, in_pTileUpdateManager, in_pStreamingHeap,
        in_pDevice, in_srvBaseCPU, nullptr)
{
    D3D12_RASTERIZER_DESC rasterizerDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    rasterizerDesc.CullMode = D3D12_CULL_MODE_FRONT;
    D3D12_DEPTH_STENCIL_DESC depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depthStencilDesc.DepthEnable = false;

    CreatePipelineState(L"skyPS.cso", L"skyPS-FB.cso", L"skyVS.cso", in_pDevice, in_sampleCount, rasterizerDesc, depthStencilDesc);

    SphereGen::Properties sphereProperties;
    sphereProperties.m_numLong = 80;
    sphereProperties.m_numLat = 81;
    sphereProperties.m_mirrorU = true;
    CreateSphere(this, in_pDevice, in_assetUploader, sphereProperties);
    float d = SharedConstants::SPHERE_SCALE * 200;
    GetModelMatrix() = DirectX::XMMatrixScaling(d, d, d);
}

//-------------------------------------------------------------------------
//-------------------------------------------------------------------------
void SceneObjects::Sky::SetModelConstants(ModelConstantData& out_modelConstantData,
    const DirectX::XMMATRIX& in_projection, const DirectX::XMMATRIX& in_view)
{
    DirectX::XMMATRIX view = in_view;
    view.r[3] = DirectX::XMVectorSet(0, 0, 0, 1);

    out_modelConstantData.g_combinedTransform = m_matrix * view * in_projection;

    out_modelConstantData.g_worldTransform = DirectX::XMMatrixIdentity();

    out_modelConstantData.g_minmipmapWidth = m_pStreamingResource->GetMinMipMapWidth();
    out_modelConstantData.g_minmipmapHeight = m_pStreamingResource->GetMinMipMapHeight();
    out_modelConstantData.g_minmipmapOffset = m_pStreamingResource->GetMinMipMapOffset();
}
