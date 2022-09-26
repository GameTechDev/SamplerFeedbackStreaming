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

#include "TileUpdateManagerBase.h"
#include "DataUploader.h"
#include "StreamingResourceBase.h"
#include "XeTexture.h"
#include "StreamingHeap.h"

//=============================================================================
// constructor for streaming library base class
//=============================================================================
Streaming::TileUpdateManagerBase::TileUpdateManagerBase(
    // query resource for tiling properties. use its device to create internal resources
    ID3D12Device8* in_pDevice,

    // the Direct command queue the application is using to render, which TUM monitors to know when new feedback is ready
    ID3D12CommandQueue* in_pDirectCommandQueue,

    const TileUpdateManagerDesc& in_desc) :
m_numSwapBuffers(in_desc.m_swapChainBufferCount)
, m_gpuTimerResolve(in_pDevice, in_desc.m_swapChainBufferCount, D3D12GpuTimer::TimerType::Direct)
, m_renderFrameIndex(0)
, m_directCommandQueue(in_pDirectCommandQueue)
, m_withinFrame(false)
, m_device(in_pDevice)
, m_commandLists((UINT)CommandListName::Num)
, m_maxTileMappingUpdatesPerApiCall(in_desc.m_maxTileMappingUpdatesPerApiCall)
, m_addAliasingBarriers(in_desc.m_addAliasingBarriers)
, m_threadPriority(in_desc.m_threadPriority)
{
    ASSERT(D3D12_COMMAND_LIST_TYPE_DIRECT == in_pDirectCommandQueue->GetDesc().Type);

    ThrowIfFailed(in_pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_frameFence)));
    m_frameFence->SetName(L"Streaming::TileUpdateManagerBase::m_frameFence");

    m_pDataUploader = std::make_unique<Streaming::DataUploader>(
        in_pDevice,
        in_desc.m_maxNumCopyBatches,
        in_desc.m_stagingBufferSizeMB,
        in_desc.m_maxTileMappingUpdatesPerApiCall,
        m_threadPriority);

    // FIXME: what should the watermark be? 25% seems to be a good trade-off of latency vs. BW
    // this watermark applies to # of UpdateLists, each of which may contain multiple tiles to upload
    m_updateListWatermark = std::max((UINT)1, in_desc.m_maxNumCopyBatches / 8);

    const UINT numAllocators = m_numSwapBuffers;
    for (UINT c = 0; c < (UINT)CommandListName::Num; c++)
    {
        auto& cl = m_commandLists[c];
        cl.m_allocators.resize(numAllocators);
        for (UINT i = 0; i < numAllocators; i++)
        {
            ThrowIfFailed(in_pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cl.m_allocators[i])));

            std::wstringstream name;
            name << "Streaming::TileUpdateManagerBase::m_commandLists.m_allocators[" << c << "][" << i << "]";
            cl.m_allocators[i]->SetName(name.str().c_str());
        }
        ThrowIfFailed(in_pDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cl.m_allocators[m_renderFrameIndex].Get(), nullptr, IID_PPV_ARGS(&cl.m_commandList)));

        std::wstringstream name;
        name << "Streaming::TileUpdateManagerBase::m_commandLists.m_commandList[" << c << "]";
        cl.m_commandList->SetName(name.str().c_str());
        cl.m_commandList->Close();
    }

    // advance frame number to the first frame...
    m_frameFenceValue++;
}

Streaming::TileUpdateManagerBase::~TileUpdateManagerBase()
{
    // force DataUploader to flush now, rather than waiting for its destructor
    Finish();
}


//-----------------------------------------------------------------------------
// kick off threads that continuously streams tiles
// gives StreamingResources opportunities to update feedback
//-----------------------------------------------------------------------------
void Streaming::TileUpdateManagerBase::StartThreads()
{
    if (m_threadsRunning)
    {
        return;
    }

    m_threadsRunning = true;

    // process sampler feedback buffers, generate upload and eviction commands
    m_processFeedbackThread = std::thread([&]
        {
            ProcessFeedbackThread();
        });

    // modify residency maps as a result of gpu completion events
    m_updateResidencyThread = std::thread([&]
        {
            while (m_threadsRunning)
            {
                m_residencyChangedFlag.Wait();

                for (auto p : m_streamingResources)
                {
                    p->UpdateMinMipMap();
                }
            }
        });

    Streaming::SetThreadPriority(m_processFeedbackThread, m_threadPriority);
    Streaming::SetThreadPriority(m_updateResidencyThread, m_threadPriority);
}

//-----------------------------------------------------------------------------
// per frame, call StreamingResource::ProcessFeedback()
// expects the no change in # of streaming resources during thread lifetime
//-----------------------------------------------------------------------------
void Streaming::TileUpdateManagerBase::ProcessFeedbackThread()
{
    // array of indices to resources that need tiles loaded/evicted
    std::vector<UINT> staleResources;
    staleResources.reserve(m_streamingResources.size());

    // flags to prevent duplicates in the staleResources array
    std::vector<BYTE> pending(m_streamingResources.size(), 0);

    UINT64 previousFrameFenceValue = m_frameFenceValue;
    while (m_threadsRunning)
    {
        // prioritize loading packed mips, as objects shouldn't be displayed until packed mips load
        bool expected = true;
        if (m_havePackedMipsToLoad.compare_exchange_weak(expected, false))
        {
            for (auto p : m_streamingResources)
            {
                if (!p->InitPackedMips())
                {
                    m_havePackedMipsToLoad = true;
                }
            }
            if (m_havePackedMipsToLoad)
            {
                continue; // still working on loading packed mips. don't move on to other streaming tasks yet.
            }
        }

        // DEBUG: verify that no streaming resources have been added/removed during thread lifetime
        ASSERT(m_streamingResources.size() == pending.size());

        UINT64 frameFenceValue = m_frameFence->GetCompletedValue();

        // Only process feedback buffers once per frame
        if (previousFrameFenceValue != frameFenceValue)
        {
            previousFrameFenceValue = frameFenceValue;

            auto startTime = m_cpuTimer.GetTime();
            UINT j = 0;
            for (auto p : m_streamingResources)
            {
                // early exit, important for application exit or TUM::Finish() when adding/deleting objects
                if (!m_threadsRunning)
                {
                    break;
                }

                p->ProcessFeedback(frameFenceValue);
                if (p->IsStale() && !pending[j])
                {
                    staleResources.push_back(j);
                    pending[j] = 1;
                }
                j++;
            }
            // add the amount of time we just spent processing feedback for a single frame
            m_processFeedbackTime += UINT64(m_cpuTimer.GetTime() - startTime);
        }

        // push uploads and evictions for stale resources
        UINT uploadRequested = 0; // remember if any work was queued so we can signal afterwards
        UINT newStaleSize = 0; // track number of stale resources, then resize the array to the updated number

        // apply heuristic to prevent "storms" of small batches of submissions
        bool canUpload = (m_pDataUploader->GetNumUpdateListsAvailable() > m_updateListWatermark) && m_threadsRunning;

        for (UINT i = 0; i < staleResources.size(); i++)
        {
            UINT resourceIndex = staleResources[i];
            auto p = m_streamingResources[resourceIndex];

            if (canUpload && m_pDataUploader->GetNumUpdateListsAvailable())
            {
                uploadRequested += p->QueueTiles();
            }

            // if all loads/evictions handled, remove from staleResource list
            if (p->IsStale())
            {
                // compact, removing non-stale resource indices while retaining oldest-first ordering
                staleResources[newStaleSize] = resourceIndex;
                newStaleSize++;
            }
            else
            {
                pending[resourceIndex] = 0; // clear the flag that prevents duplicates
            }
        }

        staleResources.resize(newStaleSize);

        // if uploads were queued, tell the file streamer to signal the corresponding fence
        if (uploadRequested)
        {
            m_pDataUploader->SignalFileStreamer();
            // DebugPrint(uploadRequested, "\n"); // # tiles requested for a single fence
        }

        // nothing to do? wait for next frame
        if ((0 == staleResources.size()) && m_threadsRunning)
        {
            m_processFeedbackFlag.Wait();
        }
    }
}

//-----------------------------------------------------------------------------
// flushes all internal queues
// submits all outstanding command lists
// stops all processing threads
//-----------------------------------------------------------------------------
void Streaming::TileUpdateManagerBase::Finish()
{
    ASSERT(!GetWithinFrame());
 
    if (m_threadsRunning)
    {
        // stop TileUpdateManager threads
        // do not want ProcessFeedback generating more work
        // don't want UpdateResidency to write to min maps when that might be replaced
        m_threadsRunning = false;

        // wake up threads so they can exit
        m_processFeedbackFlag.Set();
        m_residencyChangedFlag.Set();

        if (m_processFeedbackThread.joinable())
        {
            m_processFeedbackThread.join();
        }

        if (m_updateResidencyThread.joinable())
        {
            m_updateResidencyThread.join();
        }

        // now we are no longer producing work for the DataUploader, so its commands can be drained
        m_pDataUploader->FlushCommands();
    }
}

//-----------------------------------------------------------------------------
// allocate residency map buffer large enough for numswapbuffers * min mip map buffers for each StreamingResource
// StreamingResource::SetResidencyMapOffsetBase() will populate the residency map with latest
// descriptor handle required to update the assoiated shader resource view
//-----------------------------------------------------------------------------
void Streaming::TileUpdateManagerBase::AllocateResidencyMap(D3D12_CPU_DESCRIPTOR_HANDLE in_descriptorHandle)
{
    static const UINT alignment = 32; // these are bytes, so align by 32 corresponds to SIMD32
    static const UINT minBufferSize = 64 * 1024; // multiple of 64KB page

    UINT oldBufferSize = 0;
    if (nullptr != m_residencyMap.GetResource())
    {
        oldBufferSize = (UINT)m_residencyMap.GetResource()->GetDesc().Width;
    }

    // allocate residency map buffer large enough for numswapbuffers * min mip map buffers for each StreamingResource
    m_residencyMapOffsets.resize(m_streamingResources.size());
    UINT offset = 0;
    for (UINT i = 0; i < (UINT)m_residencyMapOffsets.size(); i++)
    {
        m_residencyMapOffsets[i] = offset;

        UINT minMipMapSize = m_streamingResources[i]->GetNumTilesWidth() * m_streamingResources[i]->GetNumTilesHeight();

        offset += minMipMapSize;

        offset = (offset + alignment - 1) & ~(alignment-1);
    }

    if (offset > oldBufferSize)
    {
        UINT bufferSize = std::max(offset, minBufferSize);
        m_residencyMap.Allocate(m_device.Get(), bufferSize);

        CreateMinMipMapView(in_descriptorHandle);

#if COPY_RESIDENCY_MAPS
        const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
        m_device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE, &resourceDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
            IID_PPV_ARGS(&m_residencyMapLocal));
        m_residencyMapLocal->SetName(L"m_residencyMapLocal");
#endif
    }

    // set offsets AFTER allocating resource. allows StreamingResource to initialize buffer state
    for (UINT i = 0; i < (UINT)m_streamingResources.size(); i++)
    {
        m_streamingResources[i]->SetResidencyMapOffsetBase(m_residencyMapOffsets[i]);
    }
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Streaming::TileUpdateManagerBase::CreateMinMipMapView(D3D12_CPU_DESCRIPTOR_HANDLE in_descriptorHandle)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.NumElements = (UINT)m_residencyMap.GetResource()->GetDesc().Width;
    srvDesc.Format = DXGI_FORMAT_R8_UINT;
    // there is only 1 channel
    srvDesc.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(0, 0, 0, 0);

#if COPY_RESIDENCY_MAPS
    m_device->CreateShaderResourceView(m_residencyMapLocal.Get(), &srvDesc, in_descriptorHandle);
#else
    m_device->CreateShaderResourceView(m_residencyMap.GetResource(), &srvDesc, in_descriptorHandle);
#endif
}

