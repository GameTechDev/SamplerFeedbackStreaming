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

#include <vector>

#include "Streaming.h"

// FIXME: resolve to buffer only supported in Win11 and some insider versions of Win10
// When resolving to texture, must copy to cpu-readable buffer from gpu texture (which cannot be in the readback heap)
// Buffer mode resolves directly to cpu-readable buffer
#define RESOLVE_TO_TEXTURE 1

namespace Streaming
{
    class InternalResources
    {
    public:
        InternalResources(ID3D12Device8* in_pDevice, class XeTexture* m_pTextureFileInfo,
            // need the swap chain count so we can create per-frame upload buffers
            UINT in_swapChainBufferCount);

        ID3D12Resource* GetTiledResource() const { return m_tiledResource.Get(); }

        ID3D12Resource* GetResolvedReadback(UINT in_index) const { return m_resolvedReadback[in_index].Get(); }
#if RESOLVE_TO_TEXTURE
        // for visualization
        ID3D12Resource* GetResolvedFeedback() const { return m_resolvedResource.Get(); }
#endif

        ID3D12Resource* GetOpaqueFeedback() const { return m_feedbackResource.Get(); }
        ID3D12DescriptorHeap* GetClearUavHeap() const { return m_clearUavHeap.Get(); }

        UINT GetNumTilesWidth() const { return m_tiling[0].WidthInTiles; }
        UINT GetNumTilesHeight() const { return m_tiling[0].HeightInTiles; }
        UINT GetTileTexelWidth() const { return m_tileShape.WidthInTexels; }
        UINT GetTileTexelHeight() const { return m_tileShape.HeightInTexels; }
        const D3D12_PACKED_MIP_INFO& GetPackedMipInfo() const { return m_packedMipInfo; }
        const D3D12_SUBRESOURCE_TILING* GetTiling() const { return m_tiling.data(); }
        UINT GetNumTilesVirtual() const { return m_numTilesTotal; }

        void ClearFeedback(ID3D12GraphicsCommandList* out_pCmdList, const D3D12_GPU_DESCRIPTOR_HANDLE in_gpuDescriptor);

        void ResolveFeedback(ID3D12GraphicsCommandList1* out_pCmdList, UINT in_index);
#if RESOLVE_TO_TEXTURE
        void ReadbackFeedback(ID3D12GraphicsCommandList* out_pCmdList, UINT in_index);
#endif

    private:
        ComPtr<ID3D12Resource> m_tiledResource;
        ComPtr<ID3D12Resource> m_tiledResourceAlloc;     // notify thread creates new tiled resource, to be swapped in later
        ComPtr<ID3D12Resource> m_tiledResourceDelete;    // resource in pergatory is moved here to be deleted by notify thread

        ComPtr<ID3D12Resource2> m_feedbackResource;
        ComPtr<ID3D12DescriptorHeap> m_clearUavHeap; // CPU heap to clear the feedback

#if RESOLVE_TO_TEXTURE
        // feedback resolved on gpu for visualization
        ComPtr<ID3D12Resource> m_resolvedResource;
#endif
        // per-swap-buffer cpu readable resolved feedback
        std::vector<ComPtr<ID3D12Resource>> m_resolvedReadback;

        D3D12_PACKED_MIP_INFO m_packedMipInfo; // last n mips may be packed into a single tile
        D3D12_TILE_SHAPE m_tileShape;          // e.g. a 64K tile may contain 128x128 texels @ 4B/pixel
        UINT m_numTilesTotal;
        std::vector<D3D12_SUBRESOURCE_TILING> m_tiling;

        void NameStreamingTexture();
    };
}
