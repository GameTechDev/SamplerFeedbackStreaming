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


/*=============================================================================
Usage:

1. Create a TileUpdateManager
2. Use TileUpdateManager::CreateStreamingHeap() to create heaps to be used by 1 or more StreamingResources
3. Use TileUpdateManager::CreateStreamingResource() to create 1 or more StreamingResources
    each StreamingResource resides in a single heap

Draw loop:
1. BeginFrame() with the TileUpdateManager (TUM)
2. Draw your assets using the streaming textures, min-mip-map, and sampler feedback SRVs
    SRVs can be created using StreamingResource methods
3. EndFrame() (with the TUM) returns 2 more command lists: beforeDraw and afterDraw
4. ExecuteCommandLists() with [beforeDraw, yourCommandList, afterDraw] command lists.
=============================================================================*/

#pragma once

#include <d3d12.h>
#include <vector>
#include <memory>
#include <thread>

#include "TileUpdateManagerBase.h"
#include "DataUploader.h"

//=============================================================================
// manager for tiled resources
// contains shared objects, especially the heap
// performs asynchronous copies
// use this to create the StreamingResource
//=============================================================================
namespace Streaming
{
    class TileUpdateManagerSR : public TileUpdateManagerBase
    {
    public:
        ID3D12Device8* GetDevice() const { return m_device.Get(); }

        UINT GetNumSwapBuffers() const { return m_numSwapBuffers; }

        // stop tracking this StreamingResource. Called by its destructor
        void Remove(StreamingResourceBase* in_pResource)
        {
            ASSERT(!GetWithinFrame());
            m_streamingResources.erase(std::remove(m_streamingResources.begin(), m_streamingResources.end(), in_pResource), m_streamingResources.end());
            m_numStreamingResourcesChanged = true;
        }

        UploadBuffer& GetResidencyMap() { return m_residencyMap; }

        Streaming::UpdateList* AllocateUpdateList(StreamingResourceBase* in_pStreamingResource)
        {
            return m_pDataUploader->AllocateUpdateList((Streaming::StreamingResourceDU*)in_pStreamingResource);
        }

        void SubmitUpdateList(Streaming::UpdateList& in_updateList)
        {
            m_pDataUploader->SubmitUpdateList(in_updateList);
        }

        // a fence on the render (direct) queue used to determine when feedback has been written & resolved
        UINT64 GetFrameFenceValue() const { return m_frameFenceValue; }

        void NotifyPackedMips() { m_packedMipTransition = true; } // called when a StreamingResource has recieved its packed mips

        ID3D12CommandQueue* GetMappingQueue() const
        {
            return m_pDataUploader->GetMappingQueue();
        }

        void SetResidencyChanged() { m_residencyChangedFlag.Set(); }
    };
}
