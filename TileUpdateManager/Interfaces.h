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

#include "TileUpdateManager.h"
#include "DataUploader.h"

// Internal interfaces
namespace Streaming
{
    //-----------------------------------------------------------------
    // custom TUM interface for StreamingResource
    //-----------------------------------------------------------------
    class TileUpdateManagerSR : private TileUpdateManager
    {
    public:
        ID3D12Device8* GetDevice() const { return m_device.Get(); }

        UINT GetNumSwapBuffers() const { return m_numSwapBuffers; }
        UINT GetMaxTileCopiesPerBatch() const { return m_maxTileCopiesPerBatch; }

        // stop tracking this StreamingResource. Called by its destructor
        void Remove(StreamingResource* in_pResource)
        {
            ASSERT(!GetWithinFrame());
            m_streamingResources.erase(std::remove(m_streamingResources.begin(), m_streamingResources.end(), in_pResource), m_streamingResources.end());
            m_numStreamingResourcesChanged = true;
        }
        UploadBuffer& GetResidencyMap() { return m_residencyMap; }
        Streaming::UpdateList* AllocateUpdateList(StreamingResource* in_pStreamingResource) { return m_pDataUploader->AllocateUpdateList(in_pStreamingResource); }
        void SubmitUpdateList(Streaming::UpdateList& in_updateList) { m_pDataUploader->SubmitUpdateList(in_updateList); }
        void FreeEmptyUpdateList(Streaming::UpdateList& in_updateList) { m_pDataUploader->FreeEmptyUpdateList(in_updateList); }

        // a fence on the render (direct) queue used to determine when feedback has been written & resolved
        UINT64 GetFrameFenceValue() const { return m_frameFenceValue; }

        void Finish() { TileUpdateManager::Finish(); }

        bool GetWithinFrame() const { return TileUpdateManager::GetWithinFrame(); }

        void NotifyNotifyPackedMips() { m_packedMipTransition = true; } // called when a StreamingResource has recieved its packed mips

        ID3D12CommandQueue* GetMappingQueue() const { return m_pDataUploader->GetMappingQueue(); }

        void SetResidencyChanged() { SetEvent(m_residencyChangeEvent); }
    };

    //-----------------------------------------------------------------
    // custom StreamingResource interface for DataUploader
    //-----------------------------------------------------------------
    class StreamingResourceDU : private StreamingResource
    {
    public:
        XeTexture* GetTextureStreamer() const { return m_pTextureStreamer.get(); }
        Streaming::Heap* GetHeap() const { return m_pHeap; }

        // just for packed mips
        const D3D12_PACKED_MIP_INFO& GetPackedMipInfo() const { return m_resources->GetPackedMipInfo(); }

        void NotifyCopyComplete(const std::vector<D3D12_TILED_RESOURCE_COORDINATE>& in_coords);
        void NotifyPackedMips();
        void NotifyEvicted(const std::vector<D3D12_TILED_RESOURCE_COORDINATE>& in_coords);

        ID3D12Resource* GetTiledResource() const { return StreamingResource::GetTiledResource(); }

        const Streaming::FileStreamer::FileHandle* GetFileHandle() const { return m_pFileHandle.get(); }

        const BYTE* GetPaddedPackedMips(UINT& out_numBytes) const { out_numBytes = (UINT)m_paddedPackedMips.size(); return m_paddedPackedMips.data(); }
    };

    //-----------------------------------------------------------------
    // custom StreamingResource interface for TUM
    //-----------------------------------------------------------------
    class StreamingResourceTUM : public StreamingResource
    {
    public:
        // share heap & other resources with another TileUpdateManager
        StreamingResourceTUM(
            // method that will fill a tile-worth of bits, for streaming
            const std::wstring& in_filename,
            Streaming::FileStreamer::FileHandle* in_pFileHandle,
            // share heap and upload buffers with other InternalResources
            Streaming::TileUpdateManagerSR* in_pTileUpdateManager,
            Heap* in_pHeap) : StreamingResource(in_filename, in_pFileHandle, in_pTileUpdateManager, in_pHeap) {}

        // called whenever a new StreamingResource is created - even one other than "this"
        void SetResidencyMapOffsetBase(UINT in_residencyMapOffsetBase) { StreamingResource::SetResidencyMapOffsetBase(in_residencyMapOffsetBase); }

        // called when creating/changing FileStreamer
        void SetFileHandle(const DataUploader* in_pDataUploader) { m_pFileHandle.reset(in_pDataUploader->OpenFile(m_filename)); }

        //-------------------------------------
        // begin called by TUM::EndFrame()
        // note: that is, called once per frame
        //-------------------------------------

        // Called on every object every frame
        // exits fast if tile residency has not changed (due to addmap or decmap)
        void UpdateMinMipMap() { StreamingResource::UpdateMinMipMap(); }

        // returns true if packed mips are loaded
        // NOTE: this query will only return true one time
        bool GetPackedMipsNeedTransition();

        // the following are called only if the application made a feedback request for the object:

        // called before draw to clear the feedback map
        void ClearFeedback(ID3D12GraphicsCommandList* in_pCmdList, const D3D12_GPU_DESCRIPTOR_HANDLE in_gpuDescriptor) { m_resources->ClearFeedback(in_pCmdList, in_gpuDescriptor); }
 
        ID3D12Resource* GetOpaqueFeedback() { return m_resources->GetOpaqueFeedback(); }

        // call after drawing to get feedback
        void ResolveFeedback(ID3D12GraphicsCommandList1* out_pCmdList);
#if RESOLVE_TO_TEXTURE
        // call after resolving to read back to CPU
        void ReadbackFeedback(ID3D12GraphicsCommandList* out_pCmdList);
#endif

        //-------------------------------------
        // end called by TUM::EndFrame()
        //-------------------------------------

        //-------------------------------------
        // called by TUM::ProcessFeedbackThread
        //-------------------------------------

        // indicate the render frame has advanced
        // Useful in particular for preventing evictions of in-flight data
        void NextFrame() { m_pendingEvictions.NextFrame(); }

        // if a feedback buffer is ready, process it to generate lists of tiles to load/evict
        void ProcessFeedback(UINT64 in_frameFenceCompletedValue) { StreamingResource::ProcessFeedback(in_frameFenceCompletedValue); }

        // try to load/evict tiles. only queue evictions once per frame.
        void QueueTiles() { StreamingResource::QueueTiles(); }

        bool IsStale()
        {
            return (m_pendingTileLoads.size() || m_pendingEvictions.GetReadyToEvict().size());
        }

        //-------------------------------------
        // end called by TUM::ProcessFeedbackThread
        //-------------------------------------
    };
}
