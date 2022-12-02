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

#include "FileStreamerReference.h"
#include "UpdateList.h"
#include "XeTexture.h"
#include "StreamingResourceDU.h"
#include "StreamingHeap.h"

static const  D3D12_COMMAND_LIST_TYPE g_commandListType = D3D12_COMMAND_LIST_TYPE_COPY;

//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
Streaming::FileStreamerReference::FileStreamerReference(ID3D12Device* in_pDevice,
    UINT in_maxNumCopyBatches,                // maximum number of in-flight batches
    UINT in_maxTileCopiesInFlight):           // upload buffer size. 1024 would become a 64MB upload buffer
    Streaming::FileStreamer(in_pDevice),
    m_copyBatches(in_maxNumCopyBatches + 2)   // padded by a couple to try to help with observed issue perhaps due to OS thread sched.
    , m_uploadAllocator(in_maxTileCopiesInFlight)
    , m_requests(in_maxTileCopiesInFlight)    // pre-allocate an array of event handles corresponding to # of tiles that can fit in the upload heap
{
    m_uploadBuffer.Allocate(in_pDevice, in_maxTileCopiesInFlight * D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES);

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = g_commandListType;
    ThrowIfFailed(in_pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_copyCommandQueue)));
    m_copyCommandQueue->SetName(L"FileStreamerReference::m_copyCommandQueue");

    //---------------------------------------
    // allocate CopyBatches
    //---------------------------------------
    UINT copyBatchIndex = 0;
    for (auto& copyBatch : m_copyBatches)
    {
        copyBatch.Init(in_pDevice);

        std::wstringstream name;
        name << "CopyBatch[" << copyBatchIndex << "]::m_commandAllocator";
        copyBatch.GetCommandAllocator()->SetName(name.str().c_str());

        copyBatchIndex++;
    }

    ThrowIfFailed(in_pDevice->CreateCommandList(0, queueDesc.Type, m_copyBatches[0].GetCommandAllocator(), nullptr, IID_PPV_ARGS(&m_copyCommandList)));
    m_copyCommandList->SetName(L"FileStreamerReference::m_copyCommandList");
    m_copyCommandList->Close();

    // launch copy thread
    ASSERT(false == m_copyThreadRunning);
    m_copyThreadRunning = true;
    m_copyThread = std::thread([&]
        {
            DebugPrint(L"Created Copy Thread\n");
            while (m_copyThreadRunning)
            {
                CopyThread();
            }
            DebugPrint(L"Destroyed Copy Thread\n");
        });
}

Streaming::FileStreamerReference::~FileStreamerReference()
{
    m_copyThreadRunning = false;
    if (m_copyThread.joinable())
    {
        m_copyThread.join();
        DebugPrint(L"JOINED reference file streamer Thread\n");
    }
}

//=============================================================================
//=============================================================================
void Streaming::FileStreamerReference::CopyBatch::Init(ID3D12Device* in_pDevice)
{
    ThrowIfFailed(in_pDevice->CreateCommandAllocator(g_commandListType, IID_PPV_ARGS(&m_commandAllocator)));
}


//-----------------------------------------------------------------------------
// opening a file returns an opaque file handle
//-----------------------------------------------------------------------------
Streaming::FileHandle* Streaming::FileStreamerReference::OpenFile(const std::wstring& in_path)
{
    // open the file
    HANDLE fileHandle = CreateFile(in_path.c_str(), GENERIC_READ,
        FILE_SHARE_READ,
        nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_READONLY | FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING,
        nullptr);

    if (INVALID_HANDLE_VALUE == fileHandle)
    {
        std::wstringstream s;
        s << "File not found: " << in_path.c_str();
        MessageBox(0, s.str().c_str(), L"Error", MB_OK);
        exit(-1);
    }

    FileHandleReference* pFileHandle = new FileHandleReference(fileHandle);

    return pFileHandle;
}

//-----------------------------------------------------------------------------
// there are as many CopyBatches as there are UpdateLists
// the lifetime of a CopyBatch is completely within the lifetime of its associated UpdateList
// when called, there /must/ be an available CopyBatch. This can not fail.
//
// Nevertheless, sometimes there are no available copybatches - unless we wait.
// Best guess is OS pauses the thread delaying when the copybatch is released
// very rarely, the result is a (very) long delay waiting for an available batch
//-----------------------------------------------------------------------------
void Streaming::FileStreamerReference::StreamTexture(Streaming::UpdateList& in_updateList)
{
    ASSERT(in_updateList.GetNumStandardUpdates());

    const UINT numBatches = (UINT)m_copyBatches.size();

    while (1)
    {
        // by allocating the least-recently-used, measured ~100% success on first try
        // might have a race here with multiple threads, but it'll never read out-of-bounds
        auto& batch = m_copyBatches[m_batchAllocIndex];
        m_batchAllocIndex = (m_batchAllocIndex + 1) % numBatches;

        // multiple threads may be trying to allocate a CopyBatch
        CopyBatch::State expected = CopyBatch::State::FREE;
        if (batch.m_state.compare_exchange_weak(expected, CopyBatch::State::ALLOCATED))
        {
            // initialize while the CopyBatch is in the "allocated" state
            batch.m_pUpdateList = &in_updateList;
            batch.m_uploadIndices.resize(in_updateList.GetNumStandardUpdates());
            batch.m_copyStart = 0;
            batch.m_copyEnd = 0;
            batch.m_numEvents = 0;
            batch.m_lastSignaled = 0;

            // as soon as this state changes, the copy thread can start executing copies
            batch.m_state = CopyBatch::State::COPY_TILES;
            break;
        }
    }
}

//-----------------------------------------------------------------------------
// Generate ReadFile()s for each tile in the texture
//-----------------------------------------------------------------------------
void Streaming::FileStreamerReference::LoadTexture(Streaming::FileStreamerReference::CopyBatch& in_copyBatch, UINT in_numtilesToLoad)
{
    Streaming::UpdateList* pUpdateList = in_copyBatch.m_pUpdateList;

    BYTE* pStagingBaseAddress = (BYTE*)m_uploadBuffer.GetData();

    UINT startIndex = in_copyBatch.m_numEvents;
    UINT endIndex = startIndex + in_numtilesToLoad;

    if (VisualizationMode::DATA_VIZ_NONE == m_visualizationMode)
    {
        auto pTextureFileInfo = pUpdateList->m_pStreamingResource->GetTextureFileInfo();
        auto pFileHandle = FileStreamerReference::GetFileHandle(pUpdateList->m_pStreamingResource->GetFileHandle());
        for (UINT i = startIndex; i < endIndex; i++)
        {
            // get file offset to tile
            auto fileOffset = pTextureFileInfo->GetFileOffset(pUpdateList->m_coords[i]);

            // convert tile index into byte offset
            UINT byteOffset = D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES * in_copyBatch.m_uploadIndices[i];

            // add to base address of upload buffer
            BYTE* pDst = pStagingBaseAddress + byteOffset;

            auto& o = m_requests[in_copyBatch.m_uploadIndices[i]];
            in_copyBatch.m_numEvents++;

            o.Internal = 0;
            o.InternalHigh = 0;
            o.OffsetHigh = 0;
            o.Offset = fileOffset.offset;

            // align # bytes read
            UINT alignment = FileStreamerReference::MEDIA_SECTOR_SIZE - 1;
            UINT numBytes = (fileOffset.numBytes + alignment) & ~(alignment);
            o.Offset &= ~alignment; // rewind the offset to alignment

            ::ReadFile(pFileHandle, pDst, numBytes, nullptr, &o);
        }
        ASSERT(in_copyBatch.m_numEvents == endIndex);
    }
    else // visualization enabled
    {
        for (UINT i = startIndex; i < endIndex; i++)
        {
            // convert tile index into byte offset
            UINT byteOffset = D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES * in_copyBatch.m_uploadIndices[i];

            // add to base address of upload buffer
            BYTE* pDst = pStagingBaseAddress + byteOffset;
            void* pSrc = GetVisualizationData(pUpdateList->m_coords[i], in_copyBatch.m_pUpdateList->m_pStreamingResource->GetTextureFileInfo()->GetFormat());
            memcpy(pDst, pSrc, D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES);
        }
        // fast-forward last signaled to the end. there are no events to check because there are no file accesses
        in_copyBatch.m_lastSignaled = endIndex;
    }
}

//-----------------------------------------------------------------------------
//  CopyTiles() from linear buffer to destination texture
//-----------------------------------------------------------------------------
void Streaming::FileStreamerReference::CopyTiles(ID3D12GraphicsCommandList* out_pCopyCmdList,
    ID3D12Resource* in_pSrcResource, const UpdateList* in_pUpdateList, const std::vector<UINT>& in_indices)
{
    // generate copy command list
    D3D12_TILE_REGION_SIZE tileRegionSize{ 1, FALSE, 0, 0, 0 };
    DXGI_FORMAT textureFormat = in_pUpdateList->m_pStreamingResource->GetTextureFileInfo()->GetFormat();
    UINT numTiles = (UINT)in_indices.size();
    for (UINT i = 0; i < numTiles; i++)
    {
        D3D12_TILED_RESOURCE_COORDINATE coord;
        ID3D12Resource* pAtlas = in_pUpdateList->m_pStreamingResource->GetHeap()->ComputeCoordFromTileIndex(coord, in_pUpdateList->m_heapIndices[i], textureFormat);

        out_pCopyCmdList->CopyTiles(pAtlas, &coord,
            &tileRegionSize, in_pSrcResource,
            D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES * in_indices[i],
            D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE | D3D12_TILE_COPY_FLAG_NO_HAZARD);
    }
}

//-----------------------------------------------------------------------------
// close command list, execute on m_copyCommandQueue, signal fence, increment fence value
//-----------------------------------------------------------------------------
void Streaming::FileStreamerReference::ExecuteCopyCommandList(ID3D12GraphicsCommandList* in_pCmdList)
{
    in_pCmdList->Close();
    ID3D12CommandList* pCmdLists[] = { in_pCmdList };
    m_copyCommandQueue->ExecuteCommandLists(1, pCmdLists);
    m_copyCommandQueue->Signal(m_copyFence.Get(), m_copyFenceValue);
}

//-----------------------------------------------------------------------------
// move through CopyBatch state machine
//-----------------------------------------------------------------------------
void Streaming::FileStreamerReference::CopyThread()
{
    bool submitCopyCommands = false;

    for (auto& c : m_copyBatches)
    {
        switch (c.m_state)
        {
        case CopyBatch::State::COPY_TILES:
            // have any copies completed?
            // do this first to free some heap space for loading
            if ((c.m_copyStart != c.m_copyEnd) && (c.m_copyFenceValue <= m_copyFence->GetCompletedValue()))
            {
                m_uploadAllocator.Free(&c.m_uploadIndices[c.m_copyStart], c.m_copyEnd - c.m_copyStart);
                c.m_copyStart = c.m_copyEnd;
            }

            // can we start new loads?
            if (c.m_numEvents < c.m_pUpdateList->GetNumStandardUpdates())
            {
                UINT numtilesToLoad = c.m_pUpdateList->GetNumStandardUpdates() - c.m_numEvents;
                numtilesToLoad = std::min(numtilesToLoad, m_uploadAllocator.GetAvailable());
                if (numtilesToLoad)
                {
                    m_uploadAllocator.Allocate(&c.m_uploadIndices[c.m_numEvents], numtilesToLoad);
                    LoadTexture(c, numtilesToLoad);
                    ASSERT(c.m_numEvents <= c.m_pUpdateList->GetNumStandardUpdates());
                }
            }

            // have any loads completed?
            for (; c.m_lastSignaled < c.m_numEvents; c.m_lastSignaled++)
            {
                UINT requestIndex = c.m_uploadIndices[c.m_lastSignaled];
                if (0 != WaitForSingleObject(m_requests[requestIndex].hEvent, 0))
                {
                    break;
                }
            }

            // start copies for any completed events ONLY IF there are no in-flight copies
            if ((c.m_copyEnd < c.m_lastSignaled) && (c.m_copyStart == c.m_copyEnd))
            {
                c.m_copyFenceValue = m_copyFenceValue;
                if (!submitCopyCommands)
                {
                    submitCopyCommands = true;
                    m_copyCommandList->Reset(c.GetCommandAllocator(), nullptr);
                }

                // generate copy commands
                // copy from we left of last time (copyEnd) until the last load that completed (lastSignaled)
                D3D12_TILE_REGION_SIZE tileRegionSize{ 1, FALSE, 0, 0, 0 };
                DXGI_FORMAT textureFormat = c.m_pUpdateList->m_pStreamingResource->GetTextureFileInfo()->GetFormat();
                for (UINT i = c.m_copyEnd; i < c.m_lastSignaled; i++)
                {
                    D3D12_TILED_RESOURCE_COORDINATE coord;
                    ID3D12Resource* pAtlas = c.m_pUpdateList->m_pStreamingResource->GetHeap()->ComputeCoordFromTileIndex(coord, c.m_pUpdateList->m_heapIndices[i], textureFormat);

                    m_copyCommandList->CopyTiles(pAtlas, &coord,
                        &tileRegionSize, m_uploadBuffer.GetResource(),
                        D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES * c.m_uploadIndices[i],
                        D3D12_TILE_COPY_FLAG_LINEAR_BUFFER_TO_SWIZZLED_TILED_RESOURCE | D3D12_TILE_COPY_FLAG_NO_HAZARD);
                }
                c.m_copyEnd = c.m_lastSignaled;
                ASSERT(c.m_copyEnd <= c.m_pUpdateList->GetNumStandardUpdates());
            }

            // if the outstanding copy is for the rest of the tiles, we can hand this batch off...
            if (c.m_pUpdateList->GetNumStandardUpdates() == c.m_copyEnd)
            {
                c.m_pUpdateList->m_copyFenceValue = c.m_copyFenceValue;
                c.m_pUpdateList->m_copyFenceValid = true;
                c.m_pUpdateList = nullptr; // clear for debugging purposes. the updatelist can be re-cycled before the copyBatch
                c.m_state = CopyBatch::State::WAIT_COMPLETE;
            }
            break;

        case CopyBatch::State::WAIT_COMPLETE:
            // can't recycle this command allocator until the corresponding fence has completed
            // note that the updatelist pointer is invalid
            if (c.m_copyFenceValue <= m_copyFence->GetCompletedValue())
            {
                ASSERT(nullptr == c.m_pUpdateList);
                m_uploadAllocator.Free(&c.m_uploadIndices[c.m_copyStart], c.m_copyEnd - c.m_copyStart);
                c.m_state = CopyBatch::State::FREE;
            }
            break;

        default:
            break;
        } // end switch

    } // end loop over CopyBatches

    if (submitCopyCommands)
    {
        ExecuteCopyCommandList(m_copyCommandList.Get());
        m_copyFenceValue++;
    }
}
