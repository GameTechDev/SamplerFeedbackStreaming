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

#include "UpdateList.h"
#include "MappingUpdater.h"
#include "FileStreamer.h"
#include "D3D12GpuTimer.h"
#include "Timer.h"

#include "TileUpdateManager.h"

class StreamingResource;

//==================================================
// UploadBuffer keeps an upload buffer per swapchain backbuffer
// and tracks occupancy of current buffer
// intended to allow multiple objects to share the same upload buffers
// NOTE: requires a "reset" per frame
//==================================================
namespace Streaming
{
    class DataUploader
    {
    public:
        DataUploader(
            ID3D12Device* in_pDevice,
            UINT in_maxCopyBatches,                     // maximum number of batches
            UINT in_maxTileCopiesPerBatch,              // batch size. a small number, like 32
            UINT in_maxTileCopiesInFlight,              // upload buffer size. 1024 would become a 64MB upload buffer
            UINT in_maxTileMappingUpdatesPerApiCall,    // some HW/drivers seem to have a limit
            UINT in_timingNumBatchesToCapture           // number of UpdateList timings to save for statistics gathering
        );
        ~DataUploader();

        FileStreamer::FileHandle* OpenFile(const std::wstring& in_path) const { return m_pFileStreamer->OpenFile(in_path); }
 
        // wait for all outstanding commands to complete. 
        void FlushCommands();

        ID3D12CommandQueue* GetMappingQueue() const { return m_mappingCommandQueue.Get(); }

        // may return null. called by StreamingResource.
        UpdateList* AllocateUpdateList(StreamingResource* in_pStreamingResource);

        void SubmitUpdateList(Streaming::UpdateList& in_updateList);

        // Streaming resource may find it can't use an updatelist
        void FreeEmptyUpdateList(Streaming::UpdateList& in_updateList);

        enum class StreamerType
        {
            Reference,
            DirectStorage
        };
        Streaming::FileStreamer* SetStreamer(StreamerType in_streamerType);

        //----------------------------------
        // statistics and visualization
        //----------------------------------
        const TileUpdateManager::BatchTimes& GetStreamingTimes() const { return m_streamingTimes; }

        float GetGpuStreamingTime() const { return m_gpuTimer.GetTimes()[0].first; }

        UINT GetTotalNumUploads() const { return m_numTotalUploads; }
        UINT GetTotalNumEvictions() const { return m_numTotalEvictions; }

        void SetVisualizationMode(UINT in_mode) { m_pFileStreamer->SetVisualizationMode(in_mode); }
    private:
        // free updatelist after processing
        void FreeUpdateList(Streaming::UpdateList& in_updateList);

        // affects upload buffer size. 1024 would become a 64MB upload buffer
        const UINT m_maxTileCopiesInFlight{ 0 };
        const UINT m_maxBatchSize{ 0 };

        ComPtr<ID3D12Device> m_device;
        D3D12GpuTimer m_gpuTimer;
        RawCpuTimer m_cpuTimer;

        // fence to monitor forward progress of the mapping queue. independent of the frame queue
        ComPtr<ID3D12Fence> m_mappingFence;
        UINT64 m_mappingFenceValue{ 0 };
        // copy queue just for mapping UpdateTileMappings() on reserved resource
        ComPtr<ID3D12CommandQueue> m_mappingCommandQueue;

        // pool of all updatelists
        // copy thread loops over these
        std::vector<UpdateList> m_updateLists;

        // early out: don't bother trying to allocate if nothing is available
        // that is, it's O(1) to determine there are none available
        std::atomic<UINT> m_updateListFreeCount;

        // pointer to next address to attempt allocation from
        UINT m_updateListAllocIndex{ 0 };

        UINT m_streamingTimeIndex{ 0 }; // index into cpu or gpu streaming history arrays
        TileUpdateManager::BatchTimes m_streamingTimes;

        // object that performs UpdateTileMappings() requests
        Streaming::MappingUpdater m_mappingUpdater;

        // object that knows how to take data from disk and upload to gpu
        std::unique_ptr<Streaming::FileStreamer> m_pFileStreamer;

        // thread to handle UpdateList submissions
        void SubmitThread();
        std::thread m_submitThread;
        Streaming::SynchronizationFlag m_submitFlag;

        // thread to poll copy and mapping fences
        // this thread could have been designed using WaitForMultipleObjects, but it was found that SetEventOnCompletion() was expensive in a tight thread loop
        // compromise solution is to keep this thread awake so long as there are live UpdateLists.
        void FenceMonitorThread();
        std::thread m_fenceMonitorThread;
        Streaming::SynchronizationFlag m_monitorFenceFlag;

        void StartThreads();
        void StopThreads();
        std::atomic<bool> m_threadsRunning{ false };

        //-------------------------------------------
        // statistics
        //-------------------------------------------
        UINT m_numTotalEvictions{ 0 };
        UINT m_numTotalUploads{ 0 };
    };
}
