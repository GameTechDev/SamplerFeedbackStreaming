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

    // mapping thread sleeps unless there is mapping work to be done
    m_mapRequestedEvent = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_mapRequestedEvent == nullptr)
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }

    // launch mapping thread
    ASSERT(false == m_mappingThreadRunning);
    m_mappingThreadRunning = true;
    m_mappingThread = std::thread([&]
        {
            DebugPrint(L"Created Mapping Thread\n");
            while (m_mappingThreadRunning)
            {
                WaitForSingleObject(m_mapRequestedEvent, INFINITE);
                UpdateMapping();
            }
            DebugPrint(L"Destroyed Mapping Thread\n");
        });

    //NOTE: TileUpdateManager must call SetStreamer() to start streaming
    //SetStreamer(StreamerType::Reference);
}

Streaming::DataUploader::~DataUploader()
{
    // stop updating. all StreamingResources must have been destroyed already, presumably.
    // don't risk trying to notify anyone.
    ASSERT(m_updateLists.size() == m_updateListFreeCount);

    FlushCommands();

    ::SetEvent(m_mapRequestedEvent);

    m_mappingThreadRunning = false;
    if (m_mappingThread.joinable())
    {
        m_mappingThread.join();
    }

    m_notifyThreadRunning = false;
    if (m_notifyThread.joinable())
    {
        m_notifyThread.join();
    }

    ::CloseHandle(m_mapRequestedEvent);
}

//-----------------------------------------------------------------------------
// releases ownership of and returns the old streamer
// calling function may need to delete some other resources before deleting the streamer
//-----------------------------------------------------------------------------
Streaming::FileStreamer* Streaming::DataUploader::SetStreamer(StreamerType in_streamerType)
{
    FlushCommands();

    m_notifyThreadRunning = false;
    if (m_notifyThread.joinable())
    {
        m_notifyThread.join();
    }

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

    CreateNotifyThread();

    return pOldStreamer;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Streaming::DataUploader::CreateNotifyThread()
{
    // launch notify thread
    ASSERT(false == m_notifyThreadRunning);
    m_notifyThreadRunning = true;
    m_notifyThread = std::thread([&]
        {
            DebugPrint(L"Created Notify Thread\n");
            while (m_notifyThreadRunning)
            {
                Notify();
            }
            DebugPrint(L"Destroyed Notify Thread\n");
        });
}

//-----------------------------------------------------------------------------
// wait for all pending commands to complete, at which point all queues will be drained
//-----------------------------------------------------------------------------
void Streaming::DataUploader::FlushCommands()
{
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
                m_updateListFreeCount--;
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
    m_numTotalUploads += (UINT)in_updateList.GetNumStandardUpdates();
    m_numTotalEvictions += (UINT)in_updateList.GetNumEvictions();

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

    in_updateList.m_executionState = UpdateList::State::STATE_SUBMITTED;

    ::SetEvent(m_mapRequestedEvent);
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
}

//-----------------------------------------------------------------------------
// NotifyThread also serves as a statistics thread
// QueryPerformanceCounter() needs to be called from the same CPU for values to be compared (deltas)
// overall start time, copy completion time, 
//-----------------------------------------------------------------------------
void Streaming::DataUploader::Notify()
{
    bool signalUpload = false;
    for (auto& updateList : m_updateLists)
    {
        switch (updateList.m_executionState)
        {
        default:
            break;

        //----------------------------------------
        // STATE_SUBMITTED
        // statistics: get start time
        //----------------------------------------
        case UpdateList::State::STATE_SUBMITTED:
        {
            // record initial discovery time
            updateList.m_totalTime = m_cpuTimer.GetTime();

            if (updateList.GetNumStandardUpdates())
            {
                updateList.m_executionState = UpdateList::State::STATE_UPLOADING;
                m_pFileStreamer->StreamTexture(updateList);
                signalUpload = true;
            }
            // UpdateList contains only evictions
            else if (0 == updateList.GetNumPackedUpdates())
            {
                updateList.m_executionState = UpdateList::State::STATE_NOTIFY_MAP;
            }
            // UpdateList contains only packed mips
            // Unlike other uploads, the copies cannot start until after synchronizing around a mapping fence
            else if (updateList.m_mappingStarted && (updateList.m_mappingFenceValue <= m_mappingFence->GetCompletedValue()))
            {
                updateList.m_executionState = UpdateList::State::STATE_UPLOADING;
                m_pFileStreamer->StreamPackedMips(updateList);
                signalUpload = true;
            }
        }
        break; // end STATE_SUBMITTED

        //----------------------------------------
        // poll transfer completion fence
        //----------------------------------------
        case UpdateList::State::STATE_NOTIFY:
            if (!m_pFileStreamer->GetCompleted(updateList))
            {
                break;
            }
            // else
            updateList.m_executionState = UpdateList::State::STATE_NOTIFY_MAP;
            [[fallthrough]]; // fallthrough is explicit

        //----------------------------------------
        // STATE_NOTIFY_MAP
        //
        // gather statistics for this UpdateList, including total time and mapping time
        //----------------------------------------
        case UpdateList::State::STATE_NOTIFY_MAP:
        {
            if (updateList.m_mappingStarted && (updateList.m_mappingFenceValue <= m_mappingFence->GetCompletedValue()))
            {
                auto& timings = m_streamingTimes[m_streamingTimeIndex];
                m_streamingTimeIndex = (m_streamingTimeIndex + 1) % m_streamingTimes.size();

                // record total time time since sumbission
                timings.m_totalTime = m_cpuTimer.GetSecondsSince(updateList.m_totalTime);
                timings.m_mappingTime = updateList.m_mappingTime;
                timings.m_numTilesUnMapped = updateList.GetNumEvictions();
                timings.m_copyTime = 0;
                timings.m_numTilesCopied = (UINT)updateList.GetNumStandardUpdates();

                // notify evictions
                if (updateList.GetNumEvictions())
                {
                    updateList.m_pStreamingResource->NotifyEvicted(updateList.m_evictCoords);
                }

                // notify regular tiles
                if (updateList.GetNumStandardUpdates())
                {
                    timings.m_copyTime = updateList.m_copyTime;
                    // a gpu copy has completed, so we can update the corresponding timer
                    //timings.m_gpuTime = m_gpuTimer.MapReadBack(in_updateList.m_streamingTimeIndex);

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
            } // end if mapping complete
        }
        break; // end STATE_NOTIFY
        }
    }

    // signal filestreamer that it should submit work (if it hasn't already)
    if (signalUpload)
    {
        m_pFileStreamer->Signal();
    }
}

//-----------------------------------------------------------------------------
// STATE_MAP_TILES
//
// Just maps tiles. Runs concurrently with other states except {FREE, ALLOCATED, NOTIFY}
//-----------------------------------------------------------------------------
void Streaming::DataUploader::UpdateMapping()
{
    for (auto& updateList : m_updateLists)
    {
        // concurrent with all states except free and allocated
        if ((updateList.m_executionState > UpdateList::State::STATE_ALLOCATED) && (!updateList.m_mappingStarted))
        {
            auto mappingStartTime = m_cpuTimer.GetTime();

            // unmap tiles that are being evicted
            if (updateList.GetNumEvictions())
            {
                m_mappingUpdater.UnMap(GetMappingQueue(), updateList.m_pStreamingResource->GetTiledResource(), updateList.m_evictCoords);
            }

            // map standard tiles
            if (updateList.GetNumStandardUpdates())
            {
                m_mappingUpdater.Map(GetMappingQueue(),
                    updateList.m_pStreamingResource->GetTiledResource(),
                    updateList.m_pStreamingResource->GetHeap()->GetHeap(),
                    updateList.m_coords, updateList.m_heapIndices);
            }

            updateList.m_mappingTime = m_cpuTimer.GetSecondsSince(mappingStartTime);

            updateList.m_mappingFenceValue = m_mappingFenceValue;
            updateList.m_mappingStarted = true;

            m_mappingCommandQueue->Signal(m_mappingFence.Get(), m_mappingFenceValue);
            m_mappingFenceValue++;
        }
    }
    // note: coalescing fences was very bad on some vendor's hardware
    // likely because mapping is so slow
}
