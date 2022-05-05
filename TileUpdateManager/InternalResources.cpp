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

#include "InternalResources.h"
#include "XeTexture.h"

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
Streaming::InternalResources::InternalResources(
    ID3D12Device8* in_pDevice,
    XeTexture* m_pTextureFileInfo,
    // need the swap chain count so we can create per-frame upload buffers
    UINT in_swapChainBufferCount) :
    m_packedMipInfo{}, m_tileShape{}, m_numTilesTotal(0)
{
    // create reserved resource
    {
        D3D12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Tex2D(
            m_pTextureFileInfo->GetFormat(),
            m_pTextureFileInfo->GetImageWidth(),
            m_pTextureFileInfo->GetImageHeight(), 1,
            (UINT16)m_pTextureFileInfo->GetMipCount()
        );

        // Layout must be D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE when creating reserved resources
        rd.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;

        ThrowIfFailed(in_pDevice->CreateReservedResource(
            &rd,
            // application is allowed to use before packed mips are loaded, but it's really a copy dest
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&m_tiledResource)));

        NameStreamingTexture();
    }

    // query the reserved resource for its tile properties
    // allocate data structure according to tile properties
    {
        UINT subresourceCount = GetTiledResource()->GetDesc().MipLevels;
        m_tiling.resize(subresourceCount);
        in_pDevice->GetResourceTiling(GetTiledResource(), &m_numTilesTotal, &m_packedMipInfo, &m_tileShape, &subresourceCount, 0, &m_tiling[0]);
    }

    // create the feedback map
    // the dimensions of the feedback map must match the size of the streaming texture
    {
        auto desc = m_tiledResource->GetDesc();
        D3D12_RESOURCE_DESC1 sfbDesc = CD3DX12_RESOURCE_DESC1::Tex2D(
            DXGI_FORMAT_SAMPLER_FEEDBACK_MIN_MIP_OPAQUE,
            desc.Width, desc.Height, desc.DepthOrArraySize, desc.MipLevels);
        sfbDesc.SamplerFeedbackMipRegion = D3D12_MIP_REGION{
            GetTileTexelWidth(), GetTileTexelHeight(), 1 };

        // the feedback texture must be in the unordered state to be written, then transitioned to RESOLVE_SOURCE
        sfbDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

        const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        ThrowIfFailed(in_pDevice->CreateCommittedResource2(
            &heapProperties, D3D12_HEAP_FLAG_NONE,
            &sfbDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr, // not a render target, so optimized clear value illegal. That's ok, clear value is ignored on feedback maps
            nullptr, IID_PPV_ARGS(&m_feedbackResource)));
        m_feedbackResource->SetName(L"m_feedbackResource");
    }

    // CPU heap used for ClearUnorderedAccessView on feedback map
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 1; // only need the one for the single feedback map
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(in_pDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_clearUavHeap)));
        m_clearUavHeap->SetName(L"m_clearUavHeap");
    }

    // now that both feedback map and paired texture have been created,
    // can create the sampler feedback view
    {
        in_pDevice->CreateSamplerFeedbackUnorderedAccessView(
            m_tiledResource.Get(),
            m_feedbackResource.Get(),
            m_clearUavHeap->GetCPUDescriptorHandleForHeapStart());
    }

#if RESOLVE_TO_TEXTURE
    // create gpu-side resolve destination
    {
        D3D12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(GetNumTilesWidth() * GetNumTilesHeight());
        const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

        const auto textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8_UINT, GetNumTilesWidth(), GetNumTilesHeight(), 1, 1);

        ThrowIfFailed(in_pDevice->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &textureDesc,
            // NOTE: though used as RESOLVE_DEST, it is also copied to the CPU
            // start in the copy_source state to align with transition barrier logic in TileUpdateManager
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            nullptr,
            IID_PPV_ARGS(&m_resolvedResource)));
        m_resolvedResource->SetName(L"m_resolvedResource");
    }
#endif

    {
        D3D12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Buffer(GetNumTilesWidth() * GetNumTilesHeight());
        const auto resolvedHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);

#if RESOLVE_TO_TEXTURE
        // CopyTextureRegion requires pitch multiple of D3D12_TEXTURE_DATA_PITCH_ALIGNMENT = 256
        UINT pitch = GetNumTilesWidth();
        pitch = (pitch + 0x0ff) & ~0x0ff;
        rd.Width = pitch * GetNumTilesHeight();
#endif

        m_resolvedReadback.resize(in_swapChainBufferCount);

        for (auto& b : m_resolvedReadback)
        {
            ThrowIfFailed(in_pDevice->CreateCommittedResource(
                &resolvedHeapProperties,
                D3D12_HEAP_FLAG_NONE,
                &rd,
#if RESOLVE_TO_TEXTURE
                // start in the copy_source state to align with transition barrier logic in TileUpdateManager
                D3D12_RESOURCE_STATE_COPY_DEST,
#else
                D3D12_RESOURCE_STATE_RESOLVE_DEST,
#endif
                nullptr,
                IID_PPV_ARGS(&b)));
        }
    }
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Streaming::InternalResources::ClearFeedback(
    ID3D12GraphicsCommandList* out_pCmdList,
    const D3D12_GPU_DESCRIPTOR_HANDLE in_gpuDescriptor)
{
    // CPU descriptor corresponding to separate CPU heap /not/ bound to command list
    D3D12_CPU_DESCRIPTOR_HANDLE secondHeapCPU = m_clearUavHeap->GetCPUDescriptorHandleForHeapStart();

    // note clear value is ignored when clearing feedback maps
    UINT clearValue[4]{};
    out_pCmdList->ClearUnorderedAccessViewUint(
        in_gpuDescriptor,
        secondHeapCPU,
        m_feedbackResource.Get(),
        clearValue, 0, nullptr);
}

//-----------------------------------------------------------------------------
// write command to resolve the opaque feedback to a min-mip feedback map
//-----------------------------------------------------------------------------
#if RESOLVE_TO_TEXTURE
void Streaming::InternalResources::ResolveFeedback(ID3D12GraphicsCommandList1* out_pCmdList, UINT)
{
    auto resolveDest = m_resolvedResource.Get();
#else
void Streaming::InternalResources::ResolveFeedback(ID3D12GraphicsCommandList1 * out_pCmdList, UINT in_index)
{
    auto resolveDest = m_resolvedReadback[in_index].Get();
#endif

    // resolve the min mip map
    // can resolve directly to a host readable buffer
    out_pCmdList->ResolveSubresourceRegion(
        resolveDest,
        0,                   // decode target only has 1 layer (or is a buffer)
        0, 0,
        m_feedbackResource.Get(),
        UINT_MAX,            // decode SrcSubresource must be UINT_MAX
        nullptr,             // src rect is not supported for min mip maps
        DXGI_FORMAT_R8_UINT, // decode format must be R8_UINT
        D3D12_RESOLVE_MODE_DECODE_SAMPLER_FEEDBACK
    );

}

#if RESOLVE_TO_TEXTURE
//-----------------------------------------------------------------------------
// write command to copy GPU resolved feedback to CPU readable readback buffer
//-----------------------------------------------------------------------------
void Streaming::InternalResources::ReadbackFeedback(ID3D12GraphicsCommandList* out_pCmdList, UINT in_index)
{
    ID3D12Resource* pResolvedReadback = m_resolvedReadback[in_index].Get();
    auto srcDesc = m_resolvedResource->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{ 0,
        {srcDesc.Format, (UINT)srcDesc.Width, srcDesc.Height, 1, (UINT)srcDesc.Width } };
    layout.Footprint.RowPitch = (layout.Footprint.RowPitch + 0x0ff) & ~0x0ff;

    D3D12_TEXTURE_COPY_LOCATION srcLocation = CD3DX12_TEXTURE_COPY_LOCATION(m_resolvedResource.Get(), 0);
    D3D12_TEXTURE_COPY_LOCATION dstLocation = CD3DX12_TEXTURE_COPY_LOCATION(pResolvedReadback, layout);

    out_pCmdList->CopyTextureRegion(
        &dstLocation,
        0, 0, 0,
        &srcLocation,
        nullptr);
}
#endif

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Streaming::InternalResources::NameStreamingTexture()
{
    static UINT m_streamingResourceID = 0;

    std::wstringstream name;
    name << "m_streamingTexture" << m_streamingResourceID++;
    m_tiledResource->SetName(name.str().c_str());
}
