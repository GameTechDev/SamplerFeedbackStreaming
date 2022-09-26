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

#include "SamplerFeedbackStreaming.h"
#include "D3D12GpuTimer.h"
#include "Timer.h"
#include "Streaming.h" // for ComPtr

#define COPY_RESIDENCY_MAPS 0

//=============================================================================
// manager for tiled resources
// contains shared objects, especially the heap
// performs asynchronous copies
// use this to create the StreamingResource
//=============================================================================
namespace Streaming
{
    class StreamingResourceBase;
    class DataUploader;
    class Heap;
    struct UpdateList;

    class TileUpdateManagerBase : public ::TileUpdateManager
    {
    public:
        //-----------------------------------------------------------------
        // external APIs
        //-----------------------------------------------------------------
        virtual void Destroy() override;
        virtual StreamingHeap* CreateStreamingHeap(UINT in_maxNumTilesHeap) override;
        virtual StreamingResource* CreateStreamingResource(const std::wstring& in_filename, StreamingHeap* in_pHeap) override;
        virtual void BeginFrame(ID3D12DescriptorHeap* in_pDescriptorHeap, D3D12_CPU_DESCRIPTOR_HANDLE in_minmipmapDescriptorHandle) override;
        virtual void QueueFeedback(StreamingResource* in_pResource, D3D12_GPU_DESCRIPTOR_HANDLE in_gpuDescriptor) override;
        virtual CommandLists EndFrame() override;
        virtual void UseDirectStorage(bool in_useDS) override;
        virtual bool GetWithinFrame() const  override { return m_withinFrame; }
        virtual float GetGpuTime() const override;
        virtual void SetVisualizationMode(UINT in_mode) override;
        virtual float GetGpuStreamingTime() const override;
        virtual float GetCpuProcessFeedbackTime() override;
        virtual UINT GetTotalNumUploads() const override;
        virtual UINT GetTotalNumEvictions() const override;
        virtual float GetTotalTileCopyLatency() const override;
        //-----------------------------------------------------------------
        // end external APIs
        //-----------------------------------------------------------------

        //--------------------------------------------
        // force all outstanding commands to complete.
        // used internally when everthing must drain, e.g. to delete or create a StreamingResource
        //--------------------------------------------
        void Finish();

        TileUpdateManagerBase(
            // query resource for tiling properties. use its device to create internal resources
            ID3D12Device8* in_pDevice,

            // the Direct command queue the application is using to render, which TUM monitors to know when new feedback is ready
            ID3D12CommandQueue* in_pDirectCommandQueue,

            const struct TileUpdateManagerDesc& in_desc);

        virtual ~TileUpdateManagerBase();

    protected:
        ComPtr<ID3D12Device8> m_device;

        const UINT m_numSwapBuffers;

        // track the objects that this resource created
        // used to discover which resources have been updated within a frame
        std::vector<StreamingResourceBase*> m_streamingResources;
        UINT64 m_frameFenceValue{ 0 };

        std::unique_ptr<Streaming::DataUploader> m_pDataUploader;
        UINT m_updateListWatermark{ 1 }; // heuristic to prevent "storms" of small batch submissions

        // each StreamingResource writes current uploaded tile state to min mip map, separate data for each frame
        // internally, use a single buffer containing all the residency maps
        Streaming::UploadBuffer m_residencyMap;

        Streaming::SynchronizationFlag m_residencyChangedFlag;

        // allocating/deallocating StreamingResources requires reallocation of shared resources
        bool m_numStreamingResourcesChanged{ false };

        std::atomic<bool> m_packedMipTransition{ false }; // flag that we need to transition a resource due to packed mips

    private:
        // direct queue is used to monitor progress of render frames so we know when feedback buffers are ready to be used
        ComPtr<ID3D12CommandQueue> m_directCommandQueue;

        // the frame fence is used to optimize readback of feedback by StreamingResource
        // only read back the feedback after the frame that writes to it has completed
        ComPtr<ID3D12Fence> m_frameFence;

        struct FeedbackReadback
        {
            StreamingResourceBase* m_pStreamingResource;
            D3D12_GPU_DESCRIPTOR_HANDLE m_gpuDescriptor;
        };
        std::vector<FeedbackReadback> m_feedbackReadbacks;

        // packed-mip transition barriers
        Streaming::BarrierList m_packedMipTransitionBarriers;

        ComPtr<ID3D12Resource> m_residencyMapLocal; // GPU copy of residency state

        Streaming::SynchronizationFlag m_processFeedbackFlag;

        std::atomic<bool> m_havePackedMipsToLoad{ false };

        void StartThreads();
        void ProcessFeedbackThread();

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
        ID3D12GraphicsCommandList1* GetCommandList(CommandListName in_name) { return m_commandLists[UINT(in_name)].m_commandList.Get(); }

        Streaming::BarrierList m_barrierUavToResolveSrc; // also copy source to resolve dest
        Streaming::BarrierList m_barrierResolveSrcToUav; // also resolve dest to copy source

        Streaming::BarrierList m_aliasingBarriers; // optional barrier for performance analysis only
        bool m_addAliasingBarriers{ false };

        UINT m_renderFrameIndex{ 0 };

        D3D12GpuTimer m_gpuTimerResolve; // time for feedback resolve

        RawCpuTimer m_cpuTimer;
        std::atomic<INT64> m_processFeedbackTime{ 0 }; // sum of cpu timer times since start
        INT64 m_previousFeedbackTime{ 0 }; // m_processFeedbackTime at time of last query
        float m_processFeedbackFrameTime{ 0 }; // cpu time spent processing feedback for the most recent frame

        // are we between BeginFrame and EndFrame? useful for debugging
        std::atomic<bool> m_withinFrame{ false };

        void AllocateResidencyMap(D3D12_CPU_DESCRIPTOR_HANDLE in_descriptorHandle);

        struct CommandList
        {
            ComPtr<ID3D12GraphicsCommandList1> m_commandList;
            std::vector<ComPtr<ID3D12CommandAllocator>> m_allocators;
        };
        std::vector<CommandList> m_commandLists;

        const UINT m_maxTileMappingUpdatesPerApiCall;

        std::atomic<bool> m_threadsRunning{ false };

        const int m_threadPriority{ 0 };

        // a thread to process feedback (when available) and queue tile loads / evictions to datauploader
        std::thread m_processFeedbackThread;

        // UpdateResidency thread's lifetime is bound to m_processFeedbackThread
        std::thread m_updateResidencyThread;

        // the min mip map is shared. it must be created (at least) every time a StreamingResource is created/destroyed
        void CreateMinMipMapView(D3D12_CPU_DESCRIPTOR_HANDLE in_descriptor);

        std::vector<UINT> m_residencyMapOffsets; // one for each StreamingResource sized for numswapbuffers min mip maps each
    };
}
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

    // unnecessary when resolving directly to cpu:
    barriers for resolved feedback transition RESOLVE_DEST->COPY_SOURCE
    resolved resource readback copies (to cpu)
    barriers for resolved feedback transition COPY_SOURCE->RESOLVE_DEST
*/
