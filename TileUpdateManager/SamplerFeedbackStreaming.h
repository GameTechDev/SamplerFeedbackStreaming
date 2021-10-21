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
3. EndFrame() (with the TUM) returns 2 command lists: beforeDraw and afterDraw
4. ExecuteCommandLists() with [beforeDraw, yourCommandList, afterDraw] command lists.
=============================================================================*/

#pragma once

#include "TileUpdateManager.h"
#include "StreamingResource.h"
#include "StreamingHeap.h"

//==================================================
// a streaming resource is associated with a single heap (in this implementation)
// multiple streaming resources can use the same heap
// TileUpdateManager is used to create these
//==================================================
class StreamingHeap : private Streaming::Heap
{
public:
    virtual ~StreamingHeap() {}

    UINT GetNumTilesAllocated()
    {
        return GetAllocator().GetNumAllocated();
    }
private:
    StreamingHeap() = delete;
    StreamingHeap(const StreamingHeap&) = delete;
    StreamingHeap(StreamingHeap&&) = delete;
    StreamingHeap& operator=(const StreamingHeap&) = delete;
    StreamingHeap& operator=(StreamingHeap&&) = delete;

    // required to cast to base class
    friend class TileUpdateManager;
};

//=============================================================================
// a fine-grained streaming, tiled resource
// TileUpdateManager is used to create these
//=============================================================================
class StreamingResource : private Streaming::StreamingResourceBase
{
public:
    //--------------------------------------------
    // applications need access to the resources to create descriptors
    //--------------------------------------------
    void CreateFeedbackView(ID3D12Device* in_pDevice, D3D12_CPU_DESCRIPTOR_HANDLE in_descriptor);
    void CreateStreamingView(ID3D12Device* in_pDevice, D3D12_CPU_DESCRIPTOR_HANDLE in_descriptor);

    // shader reading min-mip-map buffer will want its dimensions
    UINT GetMinMipMapWidth() const;
    UINT GetMinMipMapHeight() const;

    // shader reading min-mip-map buffer will need an offset into the min-mip-map (residency map)
    // NOTE: all min mip maps are stored in a single buffer. offset into the buffer.
    // this saves a massive amount of GPU memory, since each min mip map is much smaller than 64KB
    UINT GetMinMipMapOffset() const;

    // check if the packed mips are loaded. application likely will not want to use this texture before they have loaded
    bool GetPackedMipsResident() const;

    // if an object isn't visible, set all refcounts to 0
    // this will schedule all tiles to be evicted
    // call any time
    void QueueEviction();

    ID3D12Resource* GetTiledResource() const;

    ID3D12Resource* GetMinMipMap() const;

    // immediately evicts all except packed mips
    // must NOT be called between BeginFrame() and EndFrame()
    void ClearAllocations();

    //--------------------------------------------
    // for visualization
    //--------------------------------------------
    // number of tiles reserved (not necessarily committed) for this resource
    UINT GetNumTilesVirtual() const;
#if RESOLVE_TO_TEXTURE
    ID3D12Resource* GetResolvedFeedback();
#endif
    const D3D12_PACKED_MIP_INFO& GetPackedMipInfo() const;

    virtual ~StreamingResource();
private:
    StreamingResource() = delete;
    StreamingResource(const StreamingResource&) = delete;
    StreamingResource(StreamingResource&&) = delete;
    StreamingResource& operator=(const StreamingResource&) = delete;
    StreamingResource& operator=(StreamingResource&&) = delete;
};

struct TileUpdateManagerDesc
{
    // maximum number of in-flight batches
    UINT m_maxNumCopyBatches{ 128 };

    // limit the number of tile uploads per batch. Multiple batches can be submitted per frame
    UINT m_maxTileCopiesPerBatch{ 32 };

    // affects size of gpu upload buffer, that is, staging between file read and gpu copy
    // uploads should move fast, so it should be hard to hit even a small value.
    // 1024 would become a 64MB upload buffer
    UINT m_maxTileCopiesInFlight{ 512 };

    // the following is product dependent (some HW/drivers seem to have a limit)
    UINT m_maxTileMappingUpdatesPerApiCall{ 512 };

    // need the swap chain count so we can create per-frame upload buffers
    UINT m_swapChainBufferCount{ 2 };

    UINT m_timingNumBatchesToCapture{ 512 };

    // false: use internal file streaming system. true: use Microsoft DirectStorage
    bool m_useDirectStorage{ false };
};

class TileUpdateManager : private Streaming::TileUpdateManagerBase
{
public:
    TileUpdateManager(
        // query resource for tiling properties. use its device to create internal resources
        ID3D12Device8* in_pDevice,

        // the Direct command queue the application is using to render, which TUM monitors to know when new feedback is ready
        ID3D12CommandQueue* in_pDirectCommandQueue,

        const TileUpdateManagerDesc& in_desc);

    virtual ~TileUpdateManager();

    //--------------------------------------------
    // Create a heap used by 1 or more StreamingResources
    // parameter is number of 64KB tiles to manage
    //--------------------------------------------
    StreamingHeap* CreateStreamingHeap(UINT in_maxNumTilesHeap);

    //--------------------------------------------
    // Create StreamingResources using a common TileUpdateManager
    //--------------------------------------------
    StreamingResource* CreateStreamingResource(const std::wstring& in_filename, StreamingHeap* in_pHeap);

    //--------------------------------------------
    // Call BeginFrame() first,
    // once for all TileUpdateManagers that share heap/upload buffers
    // descriptor heap is used per ProcessFeedback() to clear the feedback buffer
    // the shader resource view for the min mip map will be updated if necessary
    //    (which only happens if StreamingResources are created/destroyed)
    // NOTE: the root signature should set the associated descriptor range as descriptor and data volatile
    //--------------------------------------------
    void BeginFrame(ID3D12DescriptorHeap* in_pDescriptorHeap, D3D12_CPU_DESCRIPTOR_HANDLE in_minmipmapDescriptorHandle);

    // application must explicitly request feedback for each resource each frame
    // this allows the application to limit how much time is spent on feedback, or stop processing e.g. for off-screen objects
    // descriptor required to create Clear() and Resolve() commands
    void QueueFeedback(StreamingResource* in_pResource, D3D12_GPU_DESCRIPTOR_HANDLE in_gpuDescriptor);

    //--------------------------------------------
    // Call EndFrame() last, paired with each BeginFrame() and after all draw commands
    // returns two command lists:
    //   m_beforeDrawCommands: must be called /before/ any draw commands
    //   m_afterDrawCommands: must be called /after/ any draw commands
    // e.g.
    //    auto commandLists = pTileUpdateManager->EndFrame();
    //    ID3D12CommandList* pCommandLists[] = { commandLists.m_beforeDrawCommands, myCommandList, commandLists.m_afterDrawCommands };
    //    m_commandQueue->ExecuteCommandLists(_countof(pCommandLists), pCommandLists);
    //--------------------------------------------
    struct CommandLists
    {
        ID3D12CommandList* m_beforeDrawCommands;
        ID3D12CommandList* m_afterDrawCommands;
    };
    CommandLists EndFrame();

    //--------------------------------------------
    // force all outstanding commands to complete.
    // NOTE: unlike EndFrame(), Finish() waits for all outstanding work to complete and stops (joins) all internal threads.
    // Typically never have to call this explicitly.
    //--------------------------------------------
    void Finish();

    //--------------------------------------------
    // choose DirectStorage vs. manual tile loading
    //--------------------------------------------
    void UseDirectStorage(bool in_useDS);

    //--------------------------------------------
    // are we between BeginFrame and EndFrame? useful for debugging
    //--------------------------------------------
    bool GetWithinFrame() const;

    //--------------------------------------------
    // GPU time for resolving feedback buffers last frame
    // use this to time-limit gpu feedback processing
    // to determine per-resolve time, divide this time by the number of QueueFeedback() calls during the frame
    //--------------------------------------------
    float GetGpuTime() const;

    //--------------------------------------------
    // for visualization
    //--------------------------------------------
    float GetGpuStreamingTime() const;
    float GetCpuProcessFeedbackTime(); // returns time since last query. expected usage is once per frame.

    UINT GetTotalNumUploads() const;
    UINT GetTotalNumEvictions() const;
    void SetVisualizationMode(UINT in_mode);

    const Streaming::BatchTimes& GetBatchTimes() const;
private:
    TileUpdateManager(const TileUpdateManager&) = delete;
    TileUpdateManager(TileUpdateManager&&) = delete;
    TileUpdateManager& operator=(const TileUpdateManager&) = delete;
    TileUpdateManager& operator=(TileUpdateManager&&) = delete;
};
