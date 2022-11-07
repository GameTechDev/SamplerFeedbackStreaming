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
#include "CommandLineArgs.h"
#include "SamplerFeedbackStreaming.h"
#include "CreateSphere.h"

class AssetUploader;

namespace SceneObjects
{
    enum class Descriptors
    {
        HeapOffsetTexture = 0,
        HeapOffsetFeedback,
        NumEntries
    };

    enum class RootSigParams
    {
        ParamObjectTextures = 0,
        ParamSharedTextures,
        ParamConstantBuffers,
        Param32BitConstants,
        ParamSamplers,
        NumParams
    };

    struct DrawParams
    {
        D3D12_GPU_DESCRIPTOR_HANDLE m_srvBaseGPU;
        D3D12_GPU_DESCRIPTOR_HANDLE m_sharedMinMipMap;
        D3D12_GPU_DESCRIPTOR_HANDLE m_constantBuffers;
        D3D12_GPU_DESCRIPTOR_HANDLE m_samplers;
        DirectX::XMMATRIX m_projection;
        DirectX::XMMATRIX m_view;
        DirectX::XMMATRIX m_viewInverse;
    };

    class BaseObject
    {
    public:
        virtual ~BaseObject()
        {
            m_pStreamingResource->Destroy();
        }

        bool GetPackedMipsPresent() const { return m_pStreamingResource->GetPackedMipsResident(); }

        virtual void Draw(ID3D12GraphicsCommandList1* in_pCommandList, const DrawParams& in_drawParams);

        DirectX::XMMATRIX& GetModelMatrix() { return m_matrix; }
        DirectX::XMMATRIX& GetCombinedMatrix() { return m_combinedMatrix; }

        void Spin(float in_radians); // spin this object around its desired axis

        // for visualization
        ID3D12Resource* GetTiledResource() const { return m_pStreamingResource->GetTiledResource(); }
        ID3D12Resource* GetMinMipMap() const { return m_pStreamingResource->GetMinMipMap(); }

#if RESOLVE_TO_TEXTURE
        ID3D12Resource* GetResolvedFeedback() const { return m_pStreamingResource->GetResolvedFeedback(); }
#endif

        StreamingResource* GetStreamingResource() const { return m_pStreamingResource; }

        void CopyGeometry(const BaseObject* in_pObjectForSharedHeap);

        void SetGeometry(ID3D12Resource* in_pVertexBuffer, UINT in_vertexSize,
            ID3D12Resource* in_pIndexBuffer, UINT in_lod = 0);

        void SetFeedbackEnabled(bool in_value) { m_feedbackEnabled = in_value; }

        void SetAxis(DirectX::XMVECTOR in_vector) { m_axis.v = in_vector; }
    protected:
        // pass in a location in a descriptor heap where this can write 3 descriptors
        BaseObject(
            const std::wstring& in_filename, // this class takes ownership and deletes in destructor
            TileUpdateManager* in_pTileUpdateManager,
            StreamingHeap* in_pStreamingHeap,
            ID3D12Device* in_pDevice,
            D3D12_CPU_DESCRIPTOR_HANDLE in_srvBaseCPU,
            BaseObject* in_pSharedObject);  // to share root sig, etc.

        template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

        bool m_feedbackEnabled{ true };
        TileUpdateManager* m_pTileUpdateManager{ nullptr };

        DirectX::XMMATRIX m_matrix{ DirectX::XMMatrixIdentity() };
        DirectX::XMMATRIX m_combinedMatrix{ DirectX::XMMatrixIdentity() };

        struct ModelConstantData
        {
            DirectX::XMMATRIX g_combinedTransform;
            DirectX::XMMATRIX g_worldTransform;

            int g_minmipmapWidth;
            int g_minmipmapHeight;
            int g_minmipmapOffset;
        };

        virtual void SetModelConstants(ModelConstantData& out_modelConstantData,
            const DirectX::XMMATRIX& in_projection,
            const DirectX::XMMATRIX& in_view);

        StreamingResource* m_pStreamingResource{ nullptr };

        void CreatePipelineState(
            const wchar_t* in_ps, const wchar_t* in_psFB, const wchar_t* in_vs,
            ID3D12Device* in_pDevice, UINT in_sampleCount,
            const D3D12_RASTERIZER_DESC& in_rasterizerDesc,
            const D3D12_DEPTH_STENCIL_DESC& in_depthStencilDesc);

        // pipeline state that does not capture sampler feedback
        void SetRootSigPso(ID3D12GraphicsCommandList1* in_pCommandList)
        {
            in_pCommandList->SetGraphicsRootSignature(m_rootSignature.Get());
            in_pCommandList->SetPipelineState(m_pipelineState.Get());
        }

        // pipeline state with pixel shader that calls WriteSamplerFeedback()
        void SetRootSigPsoFB(ID3D12GraphicsCommandList1* in_pCommandList)
        {
            in_pCommandList->SetGraphicsRootSignature(m_rootSignatureFB.Get());
            in_pCommandList->SetPipelineState(m_pipelineStateFB.Get());
        }

        ID3D12Device* GetDevice();
        DirectX::XMVECTORF32 m_axis{ { { 0.0f, 1.0f, 0.0f, 0.0f } } };
    private:

        struct Geometry
        {
            UINT m_numIndices{ 0 };
            D3D12_INDEX_BUFFER_VIEW m_indexBufferView;
            D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;
            ComPtr<ID3D12Resource> m_indexBuffer;
            ComPtr<ID3D12Resource> m_vertexBuffer;
        };

        std::vector<Geometry> m_lods;

        ComPtr<ID3D12RootSignature> m_rootSignature;
        ComPtr<ID3D12PipelineState> m_pipelineState;

        ComPtr<ID3D12RootSignature> m_rootSignatureFB;
        ComPtr<ID3D12PipelineState> m_pipelineStateFB;

        //-----------------------------------
        // tile memory management
        //-----------------------------------

        std::wstring GetAssetFullPath(const std::wstring& in_filename);

        UINT m_srvUavCbvDescriptorSize{ 0 };
    };

    void CreateSphere(SceneObjects::BaseObject* out_pObject,
        ID3D12Device* in_pDevice, AssetUploader& in_assetUploader,
        const SphereGen::Properties& in_sphereProperties, UINT in_numLods = 1);

    void CreateSphereResources(ID3D12Resource** out_ppVertexBuffer, ID3D12Resource** out_ppIndexBuffer,
        ID3D12Device* in_pDevice, const SphereGen::Properties& in_sphereProperties,
        AssetUploader& in_assetUploader);

    class Terrain : public BaseObject
    {
    public:
        Terrain(const std::wstring& in_filename,
            TileUpdateManager* in_pTileUpdateManager,
            StreamingHeap* in_pStreamingHeap,
            ID3D12Device* in_pDevice,
            UINT in_sampleCount,
            D3D12_CPU_DESCRIPTOR_HANDLE in_srvBaseCPU,
            const CommandLineArgs& in_args,
            AssetUploader& in_assetUploader);
    };

    class Planet : public BaseObject
    {
    public:
        Planet(const std::wstring& in_filename,
            TileUpdateManager* in_pTileUpdateManager,
            StreamingHeap* in_pStreamingHeap,
            ID3D12Device* in_pDevice, AssetUploader& in_assetUploader,
            UINT in_sampleCount,
            D3D12_CPU_DESCRIPTOR_HANDLE in_srvBaseCPU,
            const SphereGen::Properties& in_properties);

        Planet(const std::wstring& in_filename,
            StreamingHeap* in_pStreamingHeap,
            D3D12_CPU_DESCRIPTOR_HANDLE in_srvBaseCPU,
            Planet* in_pSharedObject);
    };

    // special render state (front face cull)
    // lower triangle count
    class Sky : public BaseObject
    {
    public:
        Sky(const std::wstring& in_filename,
            TileUpdateManager* in_pTileUpdateManager,
            StreamingHeap* in_pStreamingHeap,
            ID3D12Device* in_pDevice, AssetUploader& in_assetUploader,
            UINT in_sampleCount,
            D3D12_CPU_DESCRIPTOR_HANDLE in_srvBaseCPU);

        virtual void SetModelConstants(ModelConstantData& out_modelConstantData,
            const DirectX::XMMATRIX& in_projection,
            const DirectX::XMMATRIX& in_view) override;
    };
}
