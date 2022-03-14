#pragma once

#include "pch.h"

#include "FileStreamerDS.h"
#include "DXSampleHelper.h"
#include "StreamingResourceDU.h"

#include "XeTexture.h"
#include "UpdateList.h"
#include "StreamingHeap.h"

//=======================================================================================
//=======================================================================================
Streaming::FileStreamerDS::FileHandleDS::FileHandleDS(IDStorageFactory* in_pFactory, const std::wstring& in_path)
{
    ThrowIfFailed(in_pFactory->OpenFile(in_path.c_str(), IID_PPV_ARGS(&m_file)));
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
Streaming::FileStreamerDS::FileStreamerDS(ID3D12Device* in_pDevice) :
    Streaming::FileStreamer(in_pDevice)
{
    ThrowIfFailed(DStorageGetFactory(IID_PPV_ARGS(&m_factory)));

#ifdef _DEBUG
    m_factory->SetDebugFlags(DSTORAGE_DEBUG_SHOW_ERRORS);
#endif

    DSTORAGE_QUEUE_DESC queueDesc{};
    queueDesc.Capacity = DSTORAGE_MAX_QUEUE_CAPACITY;
    queueDesc.Priority = DSTORAGE_PRIORITY_NORMAL;
    queueDesc.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    queueDesc.Device = in_pDevice;

    ThrowIfFailed(m_factory->CreateQueue(&queueDesc, IID_PPV_ARGS(&m_fileQueue)));

    queueDesc.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
    ThrowIfFailed(m_factory->CreateQueue(&queueDesc, IID_PPV_ARGS(&m_memoryQueue)));
    m_memoryFenceEvent = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);

    ThrowIfFailed(in_pDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_memoryFence)));
}

Streaming::FileStreamerDS::~FileStreamerDS()
{
    ::CloseHandle(m_memoryFenceEvent);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
IDStorageFile* Streaming::FileStreamerDS::GetFileHandle(const Streaming::FileHandle* in_pHandle)
{
    return dynamic_cast<const FileHandleDS*>(in_pHandle)->GetHandle();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
Streaming::FileHandle* Streaming::FileStreamerDS::OpenFile(const std::wstring& in_path)
{
    return new FileHandleDS(m_factory.Get(), in_path);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Streaming::FileStreamerDS::StreamPackedMips(Streaming::UpdateList& in_updateList)
{
    ASSERT(in_updateList.GetNumPackedUpdates());
    ASSERT(0 == in_updateList.GetNumStandardUpdates());

    ID3D12Resource* pDstResource = in_updateList.m_pStreamingResource->GetTiledResource();

    UINT firstSubresource = in_updateList.m_pStreamingResource->GetPackedMipInfo().NumStandardMips;

    UINT numBytes = 0;
    void* pBytes = (void*)in_updateList.m_pStreamingResource->GetPaddedPackedMips(numBytes);

    DSTORAGE_REQUEST request = {};
    request.UncompressedSize = numBytes;
    request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
    request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_MULTIPLE_SUBRESOURCES;
    request.Source.Memory.Source = pBytes;
    request.Source.Memory.Size = numBytes;
    request.Destination.MultipleSubresources.Resource = pDstResource;
    request.Destination.MultipleSubresources.FirstSubresource = firstSubresource;

    m_memoryQueue->EnqueueRequest(&request);
    in_updateList.m_copyFenceValue = m_copyFenceValue;
    m_haveMemoryRequests = true;
    in_updateList.m_copyFenceValid = true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Streaming::FileStreamerDS::StreamTexture(Streaming::UpdateList& in_updateList)
{
    ASSERT(0 == in_updateList.GetNumPackedUpdates());
    ASSERT(in_updateList.GetNumStandardUpdates());

    auto pTextureStreamer = in_updateList.m_pStreamingResource->GetTextureStreamer();
    DXGI_FORMAT textureFormat = pTextureStreamer->GetFormat();

    DSTORAGE_REQUEST request = {};
    request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_TILES;
    request.Destination.Tiles.TileRegionSize = D3D12_TILE_REGION_SIZE{ 1, FALSE, 0, 0, 0 };

    if (VisualizationMode::DATA_VIZ_NONE == m_visualizationMode)
    {
        request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
        request.Source.File.Size = D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
        request.Source.File.Source = GetFileHandle(in_updateList.m_pStreamingResource->GetFileHandle());
        request.UncompressedSize = D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;

        UINT numCoords = (UINT)in_updateList.m_coords.size();
        for (UINT i = 0; i < numCoords; i++)
        {
            D3D12_TILED_RESOURCE_COORDINATE coord;
            ID3D12Resource* pAtlas = in_updateList.m_pStreamingResource->GetHeap()->ComputeCoordFromTileIndex(coord, in_updateList.m_heapIndices[i], textureFormat);

            request.Source.File.Offset = pTextureStreamer->GetFileOffset(in_updateList.m_coords[i]);
            request.Destination.Tiles.Resource = pAtlas;
            request.Destination.Tiles.TiledRegionStartCoordinate = coord;

            m_fileQueue->EnqueueRequest(&request);
        }
    }
    else // visualization color is loaded from memory
    {
        request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
        request.Source.Memory.Size = D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
        request.UncompressedSize = D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;

        UINT numCoords = (UINT)in_updateList.m_coords.size();
        for (UINT i = 0; i < numCoords; i++)
        {
            request.Source.Memory.Source = GetVisualizationData(in_updateList.m_coords[i], textureFormat);

            D3D12_TILED_RESOURCE_COORDINATE coord;
            ID3D12Resource* pAtlas = in_updateList.m_pStreamingResource->GetHeap()->ComputeCoordFromTileIndex(coord, in_updateList.m_heapIndices[i], textureFormat);

            request.Destination.Tiles.Resource = pAtlas;
            request.Destination.Tiles.TiledRegionStartCoordinate = coord;

            m_memoryQueue->EnqueueRequest(&request);
            m_haveMemoryRequests = true;
        }
    }

    in_updateList.m_copyFenceValue = m_copyFenceValue;
    in_updateList.m_copyFenceValid = true;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
inline bool Streaming::FileStreamerDS::GetCompleted(const Streaming::UpdateList& in_updateList) const
{
    bool completed = false;
    if ((VisualizationMode::DATA_VIZ_NONE == m_visualizationMode) && (0 == in_updateList.GetNumPackedUpdates()))
    {
        completed = in_updateList.m_copyFenceValue <= m_copyFence->GetCompletedValue();
    }
    else
    {
        completed = in_updateList.m_copyFenceValue <= m_memoryFence->GetCompletedValue();
    }
    return completed;
}

//-----------------------------------------------------------------------------
// signal to submit a set of batches
// must be executed in the same thread as the load methods above to avoid atomic m_copyFenceValue
//-----------------------------------------------------------------------------
void Streaming::FileStreamerDS::Signal()
{
    // might end up signaling two fences, but, we can live with that.
    if (m_haveMemoryRequests)
    {
        m_haveMemoryRequests = false;

        m_memoryQueue->EnqueueSignal(m_memoryFence.Get(), m_copyFenceValue);
        m_memoryQueue->Submit();
    }

    m_fileQueue->EnqueueSignal(m_copyFence.Get(), m_copyFenceValue);
    m_fileQueue->Submit();
    m_copyFenceValue++;
}
