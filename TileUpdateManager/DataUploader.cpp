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

#include "Interfaces.h"
#include "FileStreamerReference.h"

//=============================================================================
// Internal class that uploads texture data into a reserved resource
// Takes a streamer. creates a HeapAllocator sized to match texture atlas
//=============================================================================
Streaming::DataUploader::DataUploader(
    ID3D12Device* in_pDevice,
    UINT in_maxCopyBatches,                 // maximum number of batches
    UINT in_maxTileCopiesPerBatch,          // batch size. a small number, like 32
    UINT in_maxTileCopiesInFlight,          // upload buffer size. 1024 would become a 64MB upload buffer
    UINT in_maxTileMappingUpdatesPerApiCall,// some HW/drivers seem to have a limit
    UINT in_timingNumBatchesToCapture       // number of UpdateList timings to save
) :
    m_updateLists(in_maxCopyBatches)
    , m_maxTileCopiesInFlight(in_maxTileCopiesInFlight)
    , m_maxBatchSize(in_maxTileCopiesPerBatch)
    , m_updateListFreeCount(in_maxCopyBatches)
    , m_gpuTimer(in_pDevice, in_maxCopyBatches, D3D12GpuTimer::TimerType::Copy)
    , m_streamingTimes(in_timingNumBatchesToCapture)
    , m_mappingUpdater(in_maxTileMappingUpdatesPerApiCall)
    , m_device(in_pDevice)
{
    for (auto& u : m_updateLists)
    {
        u.Init(in_maxTileCopiesPerBatch);
    }

    // copy queue just for UpdateTileMappings() on reserved resources
    {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
        ThrowIfFailed(in_pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_mappingCommandQueue)));
        m_mappingCommandQueue->SetName(L"DataUploader::m_mappingCommandQueue");

        // fence exclusively for mapping command queue
        ThrowIfFailed(in_pDevice->CreateFence(m_mappingFenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_mappingFence)));
        m_mappingFence->SetName(L"DataUploader::m_mappingFence");
        m_mappingFenceValue++;
    }

    //NOTE: TileUpdateManager must call SetStreamer() to start streaming
    //SetStreamer(StreamerType::Reference);
}

Streaming::DataUploader::~DataUploader()
{
    // stop updating. all StreamingResources must have been destroyed already, presumably.
    // don't risk trying to notify anyone.

    FlushCommands();
    StopThreads();
}

//-----------------------------------------------------------------------------
// releases ownership of and returns the old streamer
// calling function may need to delete some other resources before deleting the streamer
//-----------------------------------------------------------------------------
Streaming::FileStreamer* Streaming::DataUploader::SetStreamer(StreamerType in_streamerType)
{
    FlushCommands();
    StopThreads();

    ComPtr<ID3D12Device> device;
    m_mappingCommandQueue->GetDevice(IID_PPV_ARGS(&device));

    Streaming::FileStreamer* pOldStreamer = m_pFileStreamer.release();

    if (StreamerType::Reference == in_streamerType)
    {
        // the streamer must support least one fully loaded updatelist, or a full updatelist will never be able to complete
        // it's really a user error for max in flight to be less than the max number in 1 update list
        UINT minNumUploads = std::max(m_maxTileCopiesInFlight, m_maxBatchSize);

        m_pFileStreamer = std::make_unique<Streaming::FileStreamerReference>(device.Get(),
            (UINT)m_updateLists.size(), m_maxBatchSize, minNumUploads);
    }
    else
    {
        //m_pFileStreamer = std::make_unique<Streaming::FileStreamerDS>(device.Get());
    }

    StartThreads();

    return pOldStreamer;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Streaming::DataUploader::StartThreads()
{
    // launch notify thread
    ASSERT(false == m_threadsRunning);
    m_threadsRunning = true;

    m_submitThread = std::thread([&]
        {
            DebugPrint(L"Created Submit Thread\n");
            while (m_threadsRunning)
            {
                m_submitFlag.Wait();
                SubmitThread();
            }
            DebugPrint(L"Destroyed Submit Thread\n");
        });

    // launch thread to monitor fences
    m_fenceMonitorThread = std::thread([&]
        {
            DebugPrint(L"Created Fence Monitor Thread\n");
            while (m_threadsRunning)
            {
                FenceMonitorThread();

                // check constructed this way so we can wake the thread to allow for exit
                if (m_updateLists.size() == m_updateListFreeCount)
                {
                    m_monitorFenceFlag.Wait();
                }
            }
            DebugPrint(L"Destroyed Fence Monitor Thread\n");
        });
}

void Streaming::DataUploader::StopThreads()
{
    if (m_threadsRunning)
    {
        m_threadsRunning = false;

        // wake up threads so they can exit
        m_submitFlag.Set();
        m_monitorFenceFlag.Set();

        if (m_fenceMonitorThread.joinable())
        {
            m_fenceMonitorThread.join();
            DebugPrint(L"JOINED Fence Monitor Thread\n");
        }

        if (m_submitThread.joinable())
        {
            m_submitThread.join();
            DebugPrint(L"JOINED Submit Thread\n");
        }
    }
}

//-----------------------------------------------------------------------------
// wait for all pending commands to complete, at which point all queues will be drained
//-----------------------------------------------------------------------------
void Streaming::DataUploader::FlushCommands()
{
    DebugPrint(m_updateListFreeCount.load(), " DU flush\n");
    while (m_updateListFreeCount < m_updateLists.size())
    {
        _mm_pause();
    }

#ifdef _DEBUG
    for (auto& u : m_updateLists)
    {
        ASSERT(UpdateList::State::STATE_FREE == u.m_executionState);
    }
#endif

    // NOTE: all copy and mapping queues must be empty if the UpdateLists have notified
}

//-----------------------------------------------------------------------------
// tries to find an available UpdateList, may return null
//-----------------------------------------------------------------------------
Streaming::UpdateList* Streaming::DataUploader::AllocateUpdateList(StreamingResource* in_pStreamingResource)
{
    UpdateList* pUpdateList = nullptr;

    // early out if there are none available
    if (m_updateListFreeCount)
    {
        // Idea: consider allocating in order, that is index 0, then 1, etc.
        //       eventually will loop around. the most likely available index after the last index is index 0.
        //       that is, the next index is likely available because has had the longest time to execute
        //       in testing, rarely needed a few more iterations
        UINT numLists = (UINT)m_updateLists.size();
        for (UINT i = 0; i < numLists; i++)
        {
            m_updateListAllocIndex = (m_updateListAllocIndex + 1) % numLists;
            auto& p = m_updateLists[m_updateListAllocIndex];

            UpdateList::State expected = UpdateList::State::STATE_FREE;
            if (p.m_executionState.compare_exchange_weak(expected, UpdateList::State::STATE_ALLOCATED))
            {
                pUpdateList = &p;
                // it is only safe to clear the state within the allocating thread
                p.Reset((Streaming::StreamingResourceDU*)in_pStreamingResource);
                ASSERT(m_updateListFreeCount);
                m_updateListFreeCount--;

                // start fence polling thread now
                m_monitorFenceFlag.Set();
                break;
            }
        }
        ASSERT(pUpdateList);
    }
    return pUpdateList;
}

//-----------------------------------------------------------------------------
// return UpdateList to free state
//-----------------------------------------------------------------------------
void Streaming::DataUploader::FreeUpdateList(Streaming::UpdateList& in_updateList)
{
    // NOTE: updatelist is deliberately not cleared until after allocation
    // otherwise there can be a race with the mapping thread
    in_updateList.m_executionState = UpdateList::State::STATE_FREE;
    m_updateListFreeCount++;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Streaming::DataUploader::SubmitUpdateList(Streaming::UpdateList& in_updateList)
{
    ASSERT(UpdateList::State::STATE_ALLOCATED == in_updateList.m_executionState);

    if (in_updateList.GetNumStandardUpdates())
    {
        m_pFileStreamer->StreamTexture(in_updateList);
    }

    in_updateList.m_executionState = UpdateList::State::STATE_SUBMITTED;

    m_submitFlag.Set();
}

//-----------------------------------------------------------------------------
// Allow StreamingResource to free empty update lists that it allocates
//-----------------------------------------------------------------------------
void Streaming::DataUploader::FreeEmptyUpdateList(Streaming::UpdateList& in_updateList)
{
    ASSERT(0 == in_updateList.GetNumStandardUpdates());
    ASSERT(0 == in_updateList.GetNumPackedUpdates());
    ASSERT(0 == in_updateList.m_evictCoords.size());

    in_updateList.m_executionState = UpdateList::State::STATE_FREE;
    m_updateListFreeCount++;
    ASSERT(m_updateListFreeCount.load() <= m_updateLists.size());
}

//-----------------------------------------------------------------------------
// check necessary fences to determine completion status
// possibilities:
// 1. packed tiles, submitted state, mapping done, move to uploading state
// 2. packed tiles, copy pending state, copy complete
// 3. standard tiles, copy pending state, mapping started and complete, copy complete
// 4. no tiles, mapping started and complete
// all cases: state > allocated
//-----------------------------------------------------------------------------
void Streaming::DataUploader::FenceMonitorThread()
{
    bool signalUpload = false;
    for (auto& updateList : m_updateLists)
    {
        switch (updateList.m_executionState)
        {

        case UpdateList::State::STATE_PACKED_MAPPING:
            ASSERT(updateList.GetNumPackedUpdates());
            // wait for mapping complete before streaming packed tiles
            if (updateList.m_mappingFenceValue <= m_mappingFence->GetCompletedValue())
            {
                updateList.m_executionState = UpdateList::State::STATE_UPLOADING;
                m_pFileStreamer->StreamPackedMips(updateList);
            }
            break;

        case UpdateList::State::STATE_UPLOADING:
            if (updateList.m_copyFenceValid)
            {
                signalUpload = true;
                updateList.m_executionState = UpdateList::State::STATE_COPY_PENDING;
            }
            break;

        case UpdateList::State::STATE_COPY_PENDING:
        {
            // standard updates? check if copy complete
            if (updateList.GetNumStandardUpdates())
            {
                if (!m_pFileStreamer->GetCompleted(updateList))
                {
                    // copy hasn't completed
                    break;
                }
            }

            // standard updates or mapping only? check if mapping complete
            if (0 == updateList.GetNumPackedUpdates())
            {
                // when there are copies, if copies are complete mapping is almost certainly complete
                if (updateList.m_mappingFenceValue > m_mappingFence->GetCompletedValue())
                {
                    break;
                }
            }
            else // packed updates? check if copy complete
            {
                if (!m_pFileStreamer->GetCompleted(updateList))
                {
                    break;
                }
            }

            // The UpdateList is complete
            // notify all tiles, evictions, and packed mips

            auto& timings = m_streamingTimes[m_streamingTimeIndex];
            m_streamingTimeIndex = (m_streamingTimeIndex + 1) % m_streamingTimes.size();

            // record total time time since sumbission
            timings.m_totalTime = m_cpuTimer.GetSecondsSince(updateList.m_startTime);
            timings.m_mappingTime = updateList.m_mappingTime;
            timings.m_numTilesUnMapped = updateList.GetNumEvictions();
            timings.m_copyTime = 0;
            timings.m_numTilesCopied = (UINT)updateList.GetNumStandardUpdates();

            // notify evictions
            if (updateList.GetNumEvictions())
            {
                m_numTotalEvictions.fetch_add(updateList.GetNumEvictions(), std::memory_order_relaxed);

                updateList.m_pStreamingResource->NotifyEvicted(updateList.m_evictCoords);
            }

            // notify regular tiles
            if (updateList.GetNumStandardUpdates())
            {
                timings.m_copyTime = updateList.m_copyTime;
                // a gpu copy has completed, so we can update the corresponding timer
                //timings.m_gpuTime = m_gpuTimer.MapReadBack(in_updateList.m_streamingTimeIndex);
                m_numTotalUploads.fetch_add(updateList.GetNumStandardUpdates(), std::memory_order_relaxed);

                updateList.m_pStreamingResource->NotifyCopyComplete(updateList.m_coords);
            }

            // notify packed mips
            if (updateList.GetNumPackedUpdates())
            {
                ASSERT(0 == updateList.GetNumStandardUpdates());
                ASSERT(0 == updateList.GetNumEvictions());

                updateList.m_pStreamingResource->NotifyPackedMips();
            }
            FreeUpdateList(updateList);
        }
        break;

        default:
            break;
        }
    } // end loop over updatelists

    // signal filestreamer that it should submit work (if it hasn't already)
    if (signalUpload)
    {
        m_pFileStreamer->Signal();
    }
}

//-----------------------------------------------------------------------------
// Submit Thread
// On submission, all updatelists need mapping
// then set state as appropriate depending on the task
// FIXME: QueryPerformanceCounter() needs to be called from the same CPU for values to be compared (deltas)
// capture start time here
//-----------------------------------------------------------------------------
void Streaming::DataUploader::SubmitThread()
{
    bool signalMap = false;

    for (auto& updateList : m_updateLists)
    {
        switch (updateList.m_executionState)
        {
            //----------------------------------------
            // STATE_SUBMITTED
            // statistics: get start time
            //----------------------------------------
        case UpdateList::State::STATE_SUBMITTED:
        {
            // all UpdateLists require mapping
            signalMap = true;
            updateList.m_mappingFenceValue = m_mappingFenceValue;

            // WARNING: UpdateTileMappings performance is an issue on some hardware
            // throughput will degrade if UpdateTileMappings isn't ~free

            // record initial discovery time
            updateList.m_startTime = m_cpuTimer.GetTime();

            // unmap tiles that are being evicted
            if (updateList.GetNumEvictions())
            {
                m_mappingUpdater.UnMap(GetMappingQueue(), updateList.m_pStreamingResource->GetTiledResource(), updateList.m_evictCoords);
            }

            // map standard tiles
            if (updateList.GetNumStandardUpdates())
            {
                updateList.m_executionState = UpdateList::State::STATE_UPLOADING;

                m_mappingUpdater.Map(GetMappingQueue(),
                    updateList.m_pStreamingResource->GetTiledResource(),
                    updateList.m_pStreamingResource->GetHeap()->GetHeap(),
                    updateList.m_coords, updateList.m_heapIndices);
            }
            else if (0 == updateList.GetNumPackedUpdates())
            {
                // if no uploads, skip the uploading state
                updateList.m_executionState = UpdateList::State::STATE_COPY_PENDING;
            }
            else
            {
                updateList.m_pStreamingResource->MapPackedMips(GetMappingQueue());
                // special state for packed mips: mapping must happen before copying
                updateList.m_executionState = UpdateList::State::STATE_PACKED_MAPPING;
            }

            // note: packed tile mapping has previously been submitted, but mapping may not be complete

            updateList.m_mappingTime = m_cpuTimer.GetSecondsSince(updateList.m_startTime);
        }
        break; // end STATE_SUBMITTED

        default:
            break;

        }
    }

    if (signalMap)
    {
        m_mappingCommandQueue->Signal(m_mappingFence.Get(), m_mappingFenceValue);
        m_mappingFenceValue++;
    }
}
