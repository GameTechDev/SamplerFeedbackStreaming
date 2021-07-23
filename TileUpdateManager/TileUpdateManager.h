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

#include "StreamingResource.h"  // TileUpdateManager is used to create these
#include "StreamingHeap.h"      // TileUpdateManager is used to create these

#include "D3D12GpuTimer.h"
#include "Timer.h"

namespace Streaming
{
    class StreamingResourceTUM;
    class DataUploader;
}

//=============================================================================
// manager for tiled resources
// contains shared objects, especially the heap
// performs asynchronous copies
// use this to create the StreamingResource
//=============================================================================
class TileUpdateManager
{
public:
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

    TileUpdateManager(
        // query resource for tiling properties. use its device to create internal resources
        ID3D12Device8* in_pDevice,

        // the Direct command queue the application is using to render, which TUM monitors to know when new feedback is ready
        ID3D12CommandQueue* in_pDirectCommandQueue,

        const TileUpdateManagerDesc& in_desc);

    ~TileUpdateManager();

    //--------------------------------------------
    // Create a heap used by 1 or more StreamingResources
    // parameter is number of 64KB tiles to manage
    //--------------------------------------------
    class Streaming::Heap* CreateStreamingHeap(UINT in_maxNumTilesHeap);

    //--------------------------------------------
    // Create StreamingResources using a common TileUpdateManager
    //--------------------------------------------
    class StreamingResource* CreateStreamingResource(const std::wstring& in_filename, Streaming::Heap* in_pHeap);

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
    // NOTE: unlike EndFrame(), Finish() waits for all outstanding copies to complete and halts the copy thread.
    // use when you need everthing to drain, e.g. to delete or create a StreamingResource
    //--------------------------------------------
    void Finish();

    void UseDirectStorage(bool in_useDS);

    //--------------------------------------------
    // are we between BeginFrame and EndFrame? useful for debugging
    //--------------------------------------------
    bool GetWithinFrame() const { return m_withinFrame; }

    //--------------------------------------------
    // for visualization
    //--------------------------------------------
    float GetGpuStreamingTime() const;
    float GetProcessFeedbackTime(); // returns time since last query. expected usage is once per frame.

    // feedback resolve + readback
    float GetGpuTime() const;
    UINT GetTotalNumUploads() const;
    UINT GetTotalNumEvictions() const;
    void SetVisualizationMode(UINT in_mode);

    struct BatchTiming
    {
        float m_copyTime{ -1 };    // from before ReadFile() to after gpu copy complete
        float m_mappingTime{ -1 }; // time for mapping these tiles
        float m_totalTime{ -1 };   // from just before starting to stream from disk to after mapping has completed

        float m_cpuTime{ -1 };
        float m_gpuTime{ -1 };
        UINT m_numTilesCopied{ 0 };
        UINT m_numTilesUnMapped{ 0 };
    };
    using BatchTimes = std::vector<BatchTiming>;
    const BatchTimes& GetBatchTimes() const;
protected:
    template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    ComPtr<ID3D12Device8> m_device;

    // direct queue is used to monitor progress of render frames so we know when feedback buffers are ready to be used
    ComPtr<ID3D12CommandQueue> m_directCommandQueue;

    // the frame fence is used to optimize readback of feedback by StreamingResource
    // only read back the feedback after the frame that writes to it has completed
    ComPtr<ID3D12Fence> m_frameFence;
    UINT64 m_frameFenceValue{ 0 };

    const UINT m_numSwapBuffers;
    const UINT m_maxTileMappingUpdatesPerApiCall;
    const UINT m_maxTileCopiesPerBatch;

    struct FeedbackReadback
    {
        Streaming::StreamingResourceTUM* m_pStreamingResource;
        D3D12_GPU_DESCRIPTOR_HANDLE m_gpuDescriptor;
    };
    std::vector<FeedbackReadback> m_feedbackReadbacks;

    std::unique_ptr<Streaming::DataUploader> m_pDataUploader;

    // packed-mip transition barriers
    Streaming::BarrierList m_packedMipTransitionBarriers;

    // track the objects that this resource created
    // used to discover which resources have been updated within a frame
    std::vector<Streaming::StreamingResourceTUM*> m_streamingResources;

    // each StreamingResource writes current uploaded tile state to min mip map, separate data for each frame
    // internally, use a single buffer containing all the residency maps
    Streaming::UploadBuffer m_residencyMap;
    ComPtr<ID3D12Resource> m_residencyMapLocal; // GPU copy of residency state
    // allocating/deallocating StreamingResources requires reallocation of shared resources
    bool m_numStreamingResourcesChanged{ false };

    std::atomic<bool> m_packedMipTransition{ false }; // flag that we need to transition a resource due to packed mips

    HANDLE m_residencyChangeEvent{ nullptr };
    HANDLE m_processFeedbackEvent{ nullptr };
private:
    TileUpdateManager(const TileUpdateManager&) = delete;
    TileUpdateManager(TileUpdateManager&&) = delete;
    TileUpdateManager& operator=(const TileUpdateManager&) = delete;
    TileUpdateManager& operator=(TileUpdateManager&&) = delete;

    // the min mip map is shared. it must be created (at least) every time a StreamingResource is created/destroyed
    void CreateMinMipMapView(D3D12_CPU_DESCRIPTOR_HANDLE in_descriptor);

    UINT m_renderFrameIndex;

    D3D12GpuTimer m_gpuTimerResolve; // time for feedback resolve

    Streaming::BarrierList m_barrierUavToResolveSrc; // also copy source to resolve dest
    Streaming::BarrierList m_barrierResolveSrcToUav; // also resolve dest to copy source

    //---------------------------------------------------------------------------
    // TUM creates 2 command lists to be executed Before & After application draw
    // these clear & resolve feedback buffers, coalescing all their barriers
    //---------------------------------------------------------------------------
    enum class CommandListName
    {
        Before,    // before any draw calls: clear feedback, transition packed mips
        After,    // after all draw calls: resolve feedback
        Num
    };

    struct CommandList
    {
        ComPtr<ID3D12GraphicsCommandList1> m_commandList;
        std::vector<ComPtr<ID3D12CommandAllocator>> m_allocators;
    };
    std::vector<CommandList> m_commandLists;
    ID3D12GraphicsCommandList1* GetCommandList(CommandListName in_name) { return m_commandLists[UINT(in_name)].m_commandList.Get(); }

    // are we between BeginFrame and EndFrame? useful for debugging
    std::atomic<bool> m_withinFrame{ false };

    std::vector<UINT> m_residencyMapOffsets; // one for each StreamingResource sized for numswapbuffers min mip maps each
    void AllocateResidencyMap(D3D12_CPU_DESCRIPTOR_HANDLE in_descriptorHandle);

    std::atomic<bool> m_threadsRunning{ false };
    void StartThreads();

    // a thread to process feedback (when available) and queue tile loads / evictions to datauploader
    std::thread m_processFeedbackThread;
    RawCpuTimer m_cpuTimer;
    std::atomic<INT64> m_processFeedbackTime{ 0 }; // sum of cpu timer times since start
    INT64 m_previousFeedbackTime{ 0 }; // m_processFeedbackTime at time of last query

    // UpdateResidency thread's lifetime is bound to m_processFeedbackThread
    std::thread m_updateResidencyThread;
};

/*
NOTE:

Because there are several small per-object operations, there can be a lot of barriers if there are many objects.
These have all been coalesced into 2 command lists per frame:

before draw commands:
    feedback clears
    barriers for aliasing with the texture atlas
    barriers for packed mips

after draw commands:
    barriers for opaque feedback transition UAV->RESOLVE_SOURCE
    feedback resolves
    barriers for opaque feedback transition RESOLVE_SOURCE->UAV
    barriers for resolved feedback transition RESOLVE_DEST->COPY_SOURCE
    resolved resource readback copies (to cpu)
    barriers for resolved feedback transition COPY_SOURCE->RESOLVE_DEST
*/
