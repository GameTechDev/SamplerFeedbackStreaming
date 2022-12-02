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

#include "FileStreamerDS.h"
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
Streaming::FileStreamerDS::FileStreamerDS(ID3D12Device* in_pDevice, IDStorageFactory* in_pDSfactory) :
    m_pFactory(in_pDSfactory),
    Streaming::FileStreamer(in_pDevice)
{
    DSTORAGE_QUEUE_DESC queueDesc{};
    queueDesc.Capacity = DSTORAGE_MAX_QUEUE_CAPACITY;
    queueDesc.Priority = DSTORAGE_PRIORITY_NORMAL;
    queueDesc.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    queueDesc.Device = in_pDevice;

    ThrowIfFailed(in_pDSfactory->CreateQueue(&queueDesc, IID_PPV_ARGS(&m_fileQueue)));

    queueDesc.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
    ThrowIfFailed(in_pDSfactory->CreateQueue(&queueDesc, IID_PPV_ARGS(&m_memoryQueue)));
}

Streaming::FileStreamerDS::~FileStreamerDS()
{
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
    return new FileHandleDS(m_pFactory, in_path);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Streaming::FileStreamerDS::StreamTexture(Streaming::UpdateList& in_updateList)
{
    ASSERT(in_updateList.GetNumStandardUpdates());

    auto pTextureFileInfo = in_updateList.m_pStreamingResource->GetTextureFileInfo();
    DXGI_FORMAT textureFormat = pTextureFileInfo->GetFormat();
    auto pDstHeap = in_updateList.m_pStreamingResource->GetHeap();

    DSTORAGE_REQUEST request{};
    request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_TILES;
    request.Destination.Tiles.TileRegionSize = D3D12_TILE_REGION_SIZE{ 1, FALSE, 0, 0, 0 };

    if (VisualizationMode::DATA_VIZ_NONE == m_visualizationMode)
    {
        request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
        request.Source.File.Source = GetFileHandle(in_updateList.m_pStreamingResource->GetFileHandle());
        request.UncompressedSize = D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;

        UINT numCoords = (UINT)in_updateList.m_coords.size();
        for (UINT i = 0; i < numCoords; i++)
        {
            auto fileOffset = pTextureFileInfo->GetFileOffset(in_updateList.m_coords[i]);
            request.Source.File.Offset = fileOffset.offset;
            request.Source.File.Size = fileOffset.numBytes;

            D3D12_TILED_RESOURCE_COORDINATE coord{};
            ID3D12Resource* pAtlas = pDstHeap->ComputeCoordFromTileIndex(coord, in_updateList.m_heapIndices[i], textureFormat);

            request.Destination.Tiles.Resource = pAtlas;
            request.Destination.Tiles.TiledRegionStartCoordinate = coord;
            request.Options.CompressionFormat = (DSTORAGE_COMPRESSION_FORMAT)pTextureFileInfo->GetCompressionFormat();

            m_fileQueue->EnqueueRequest(&request);

            if (m_captureTrace)
            {
                std::wstring fileName = std::filesystem::path(in_updateList.m_pStreamingResource->GetFileName()).filename();
                TraceRequest(pAtlas, coord, fileName,
                    request.Source.File.Offset,
                    (UINT32)request.Source.File.Size,
                    (UINT32)request.Options.CompressionFormat);
            }
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

            D3D12_TILED_RESOURCE_COORDINATE coord{};
            ID3D12Resource* pAtlas = pDstHeap->ComputeCoordFromTileIndex(coord, in_updateList.m_heapIndices[i], textureFormat);

            request.Destination.Tiles.Resource = pAtlas;
            request.Destination.Tiles.TiledRegionStartCoordinate = coord;

            m_memoryQueue->EnqueueRequest(&request);
        }
    }

    in_updateList.m_copyFenceValue = m_copyFenceValue;
    in_updateList.m_copyFenceValid = true;
}

//-----------------------------------------------------------------------------
// signal to submit a set of batches
// must be executed in the same thread as the load methods above to avoid atomic m_copyFenceValue
//-----------------------------------------------------------------------------
void Streaming::FileStreamerDS::Signal()
{
    if (VisualizationMode::DATA_VIZ_NONE == m_visualizationMode)
    {
        m_fileQueue->EnqueueSignal(m_copyFence.Get(), m_copyFenceValue);
        m_fileQueue->Submit();

        if (m_captureTrace) { TraceSubmit(); }
    }
    else
    {
        m_memoryQueue->EnqueueSignal(m_copyFence.Get(), m_copyFenceValue);
        m_memoryQueue->Submit();
    }

    m_copyFenceValue++;
}
