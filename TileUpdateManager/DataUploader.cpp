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

#include "DataUploader.h"
#include "StreamingResourceDU.h"
#include "FileStreamerReference.h"
#include "FileStreamerDS.h"
#include "StreamingHeap.h"

//=============================================================================
// Internal class that uploads texture data into a reserved resource
//=============================================================================
Streaming::DataUploader::DataUploader(
    ID3D12Device* in_pDevice,
    UINT in_maxCopyBatches,                 // maximum number of batches
    UINT in_stagingBufferSizeMB,            // upload buffer size
    UINT in_maxTileMappingUpdatesPerApiCall // some HW/drivers seem to have a limit
) :
    m_updateLists(in_maxCopyBatches)
    , m_updateListAllocator(in_maxCopyBatches)
    , m_stagingBufferSizeMB(in_stagingBufferSizeMB)
    , m_gpuTimer(in_pDevice, in_maxCopyBatches, D3D12GpuTimer::TimerType::Copy)
    , m_mappingUpdater(in_maxTileMappingUpdatesPerApiCall)
{
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

    InitDirectStorage(in_pDevice);

    //NOTE: TileUpdateManager must call SetStreamer() to start streaming
    //SetStreamer(StreamerType::Reference);
}

Streaming::DataUploader::~DataUploader()
{
    // stop updating. all StreamingResources must have been destroyed already, presumably.

    FlushCommands();
    StopThreads();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Streaming::DataUploader::InitDirectStorage(ID3D12Device* in_pDevice)
{
    // initialize to default values
    DSTORAGE_CONFIGURATION dsConfig{};
    DStorageSetConfiguration(&dsConfig);

    ThrowIfFailed(DStorageGetFactory(IID_PPV_ARGS(&m_dsFactory)));

    DSTORAGE_DEBUG debugFlags = DSTORAGE_DEBUG_NONE;
#ifdef _DEBUG
    debugFlags = DSTORAGE_DEBUG_SHOW_ERRORS;
#endif
    m_dsFactory->SetDebugFlags(debugFlags);

    m_dsFactory->SetStagingBufferSize(m_stagingBufferSizeMB * 1024 * 1024);

    DSTORAGE_QUEUE_DESC queueDesc{};
    queueDesc.Capacity = DSTORAGE_MAX_QUEUE_CAPACITY;
    queueDesc.Priority = DSTORAGE_PRIORITY_NORMAL;
    queueDesc.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
    queueDesc.Device = in_pDevice;
    ThrowIfFailed(m_dsFactory->CreateQueue(&queueDesc, IID_PPV_ARGS(&m_memoryQueue)));

    ThrowIfFailed(in_pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_memoryFence)));
}

//-----------------------------------------------------------------------------
// handle request to load a texture from cpu memory
// used for packed mips, which don't participate in fine-grained streaming
//-----------------------------------------------------------------------------
UINT64 Streaming::DataUploader::LoadTexture(ID3D12Resource* in_pResource, UINT in_firstSubresource,
    const std::vector<BYTE>& in_paddedData, UINT in_uncompressedSize, UINT32 in_compressionFormat)
{
    DSTORAGE_REQUEST request = {};
    request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
    request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_MULTIPLE_SUBRESOURCES;
    request.Source.Memory.Source = in_paddedData.data();
    request.Source.Memory.Size = (UINT32)in_paddedData.size();
    request.UncompressedSize = in_uncompressedSize;
    request.Destination.MultipleSubresources.Resource = in_pResource;
    request.Destination.MultipleSubresources.FirstSubresource = in_firstSubresource;
    request.Options.CompressionFormat = (DSTORAGE_COMPRESSION_FORMAT)in_compressionFormat;

    m_memoryQueue->EnqueueRequest(&request);
    return m_memoryFenceValue;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Streaming::DataUploader::SubmitTextureLoads()
{
    m_memoryQueue->EnqueueSignal(m_memoryFence.Get(), m_memoryFenceValue);
    m_memoryQueue->Submit();
    m_memoryFenceValue++;
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
        // buffer size in megabytes * 1024 * 1024 bytes / (tile size = 64 * 1024 bytes)
        UINT maxTileCopiesInFlight = m_stagingBufferSizeMB * (1024 / 64);

        m_pFileStreamer = std::make_unique<Streaming::FileStreamerReference>(device.Get(),
            (UINT)m_updateLists.size(), maxTileCopiesInFlight);
    }
    else
    {
        m_pFileStreamer = std::make_unique<Streaming::FileStreamerDS>(device.Get(), m_dsFactory.Get());
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
            // initialize timer on the thread that will use it
            RawCpuTimer fenceMonitorThread;
            m_pFenceThreadTimer = &fenceMonitorThread;

            DebugPrint(L"Created Fence Monitor Thread\n");
            while (m_threadsRunning)
            {
                FenceMonitorThread();

                // if no outstanding work, sleep
                if (0 == m_updateListAllocator.GetAllocated())
                {
                    m_fenceMonitorFlag.Wait();
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
        m_fenceMonitorFlag.Set();

        // stop submitting new work
        if (m_submitThread.joinable())
        {
            m_submitThread.join();
            DebugPrint(L"JOINED Submit Thread\n");
        }

        // finish up any remaining work
        if (m_fenceMonitorThread.joinable())
        {
            m_fenceMonitorThread.join();
            DebugPrint(L"JOINED Fence Monitor Thread\n");
        }
    }
}

//-----------------------------------------------------------------------------
// wait for all pending commands to complete, at which point all queues will be drained
//-----------------------------------------------------------------------------
void Streaming::DataUploader::FlushCommands()
{
    DebugPrint("DataUploader waiting on ", m_updateListAllocator.GetAllocated(), " tasks to complete\n");
    while (m_updateListAllocator.GetAllocated()) // wait so long as there is outstanding work
    {
        m_fenceMonitorFlag.Set(); // (paranoia)
        _mm_pause();
    }
    // if this loop doesn't exit, then a race condition occurred while allocating/freeing updatelists

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
Streaming::UpdateList* Streaming::DataUploader::AllocateUpdateList(Streaming::StreamingResourceDU* in_pStreamingResource)
{
    UpdateList* pUpdateList = nullptr;

    // Heuristic
    // if all the updatelists are in-flight, do not allocate another updatelist until the free pool hits a watermark
    if (m_updateListsEmpty)
    {
        // FIXME: what should the watermark be? 25% seems to be a good trade-off of latency vs. BW
        UINT w = (UINT)m_updateLists.size() / 4;

        if (w > m_updateListAllocator.GetAvailable())
        {
            return nullptr;
        }
        else
        {
            m_updateListsEmpty = false;
        }
    }

    if (m_updateListAllocator.GetAvailable())
    {
        UINT index = m_updateListAllocator.Allocate();
        pUpdateList = &m_updateLists[index];
        ASSERT(UpdateList::State::STATE_FREE == pUpdateList->m_executionState);

        pUpdateList->Reset(in_pStreamingResource);
        pUpdateList->m_executionState = UpdateList::State::STATE_ALLOCATED;

        // start fence polling thread now
        m_fenceMonitorFlag.Set();
    }
    else
    {
        m_updateListsEmpty = true;
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

    // return the index to this updatelist to the pool
    UINT i = UINT(&in_updateList - m_updateLists.data());
    m_updateListAllocator.Free(i);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Streaming::DataUploader::SubmitUpdateList(Streaming::UpdateList& in_updateList)
{
    ASSERT(UpdateList::State::STATE_ALLOCATED == in_updateList.m_executionState);

    // set to submitted, allowing mapping within submitThread
    // fenceMonitorThread will wait for the copy fence to become valid before progressing state
    in_updateList.m_executionState = UpdateList::State::STATE_SUBMITTED;

    if (in_updateList.GetNumStandardUpdates())
    {
        m_pFileStreamer->StreamTexture(in_updateList);
    }

    m_submitFlag.Set();
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
    bool loadTextures = false;
    for (auto& updateList : m_updateLists)
    {
        // assign a start time to every in-flight update list. this will give us an upper bound on latency.
        // latency is only measured for tile uploads
        if ((UpdateList::State::STATE_FREE != updateList.m_executionState) && (0 == updateList.m_copyLatencyTimer))
        {
            updateList.m_copyLatencyTimer = m_pFenceThreadTimer->GetTime();
        }

        switch (updateList.m_executionState)
        {

        case UpdateList::State::STATE_PACKED_MAPPING:
            ASSERT(0 == updateList.GetNumStandardUpdates());
            ASSERT(0 == updateList.GetNumEvictions());

            // wait for mapping complete before streaming packed tiles
            if (updateList.m_mappingFenceValue <= m_mappingFence->GetCompletedValue())
            {
                UINT uncompressedSize = 0;
                auto& data = updateList.m_pStreamingResource->GetPaddedPackedMips(uncompressedSize);
                updateList.m_copyFenceValue = LoadTexture(
                    updateList.m_pStreamingResource->GetTiledResource(),
                    updateList.m_pStreamingResource->GetPackedMipInfo().NumStandardMips,
                    data, uncompressedSize,
                    updateList.m_pStreamingResource->GetTextureFileInfo()->GetCompressionFormat());
                updateList.m_executionState = UpdateList::State::STATE_PACKED_COPY_PENDING;

                loadTextures = true;
            }
            break;

        case UpdateList::State::STATE_PACKED_COPY_PENDING:
            ASSERT(0 == updateList.GetNumStandardUpdates());
            ASSERT(0 == updateList.GetNumEvictions());

            if (m_memoryFence->GetCompletedValue() >= updateList.m_copyFenceValue)
            {
                updateList.m_pStreamingResource->NotifyPackedMips();
                FreeUpdateList(updateList);
            }
            break;

        case UpdateList::State::STATE_UPLOADING:
            // there can be a race where mapping completes before the CPU has written the copy fence
            // if the copy fence has been set, there may be a pending copy, so signal the FileStreamer.
            if (updateList.m_copyFenceValid)
            {
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
            if (updateList.m_mappingFenceValue > m_mappingFence->GetCompletedValue())
            {
                break;
            }

            // notify evictions
            if (updateList.GetNumEvictions())
            {
                updateList.m_pStreamingResource->NotifyEvicted(updateList.m_evictCoords);

                m_numTotalEvictions.fetch_add(updateList.GetNumEvictions(), std::memory_order_relaxed);
            }

            // notify regular tiles
            if (updateList.GetNumStandardUpdates())
            {
                updateList.m_pStreamingResource->NotifyCopyComplete(updateList.m_coords);

                auto updateLatency = m_pFenceThreadTimer->GetTime() - updateList.m_copyLatencyTimer;
                m_totalTileCopyLatency.fetch_add(updateLatency * updateList.GetNumStandardUpdates(), std::memory_order_relaxed);

                m_numTotalUploads.fetch_add(updateList.GetNumStandardUpdates(), std::memory_order_relaxed);

            }

            // UpdateList complete
            FreeUpdateList(updateList);
        }
        break;

        default:
            break;
        }
    } // end loop over updatelists

    if (loadTextures)
    {
        SubmitTextureLoads();
    }
}

//-----------------------------------------------------------------------------
// Submit Thread
// On submission, all updatelists need mapping
// set next state depending on the task
// Note: QueryPerformanceCounter() needs to be called from the same CPU for values to be compared,
//       but this thread starts work while a different thread handles completion
//-----------------------------------------------------------------------------
void Streaming::DataUploader::SubmitThread()
{
    bool signalMap = false;

    for (auto& updateList : m_updateLists)
    {
        switch (updateList.m_executionState)
        {
        case UpdateList::State::STATE_SUBMITTED:
        {
            // all UpdateLists require mapping
            signalMap = true;
            updateList.m_mappingFenceValue = m_mappingFenceValue;

            // WARNING: UpdateTileMappings performance is an issue on some hardware
            // throughput will degrade if UpdateTileMappings isn't ~free

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
            else if (updateList.GetNumEvictions())
            {
                // if no uploads, skip the uploading state
                updateList.m_executionState = UpdateList::State::STATE_COPY_PENDING;
            }
            else // must be mapping packed mips
            {
                updateList.m_pStreamingResource->MapPackedMips(GetMappingQueue());
                // special state for packed mips: mapping must happen before copying
                updateList.m_executionState = UpdateList::State::STATE_PACKED_MAPPING;
            }
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
