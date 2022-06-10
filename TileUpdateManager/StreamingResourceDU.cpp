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

#include "StreamingResourceDU.h"
#include "StreamingHeap.h"
#include "TileUpdateManager.h"

//-----------------------------------------------------------------------------
// can map the packed mips as soon as we have heap indices
//-----------------------------------------------------------------------------
void Streaming::StreamingResourceDU::MapPackedMips(ID3D12CommandQueue* in_pCommandQueue)
{
    UINT firstSubresource = GetPackedMipInfo().NumStandardMips;

    // mapping packed mips is different from regular tiles: must be mapped before we can use copytextureregion() instead of copytiles()
    UINT numTiles = GetPackedMipInfo().NumTilesForPackedMips;

    std::vector<D3D12_TILE_RANGE_FLAGS> rangeFlags(numTiles, D3D12_TILE_RANGE_FLAG_NONE);

    // if the number of standard (not packed) mips is n, then start updating at subresource n
    D3D12_TILED_RESOURCE_COORDINATE resourceRegionStartCoordinates{ 0, 0, 0, firstSubresource };
    D3D12_TILE_REGION_SIZE resourceRegionSizes{ numTiles, FALSE, 0, 0, 0 };

    // perform packed mip tile mapping on the copy queue
    in_pCommandQueue->UpdateTileMappings(
        GetTiledResource(),
        1, // numRegions
        &resourceRegionStartCoordinates,
        &resourceRegionSizes,
        m_pHeap->GetHeap(),
        numTiles,
        rangeFlags.data(),
        m_packedMipHeapIndices.data(),
        nullptr,
        D3D12_TILE_MAPPING_FLAG_NONE
    );

    // DataUploader will synchronize around a mapping fence before uploading packed mips
}

//-----------------------------------------------------------------------------
// DataUploader has completed updating a reserved texture tile
//-----------------------------------------------------------------------------
void Streaming::StreamingResourceDU::NotifyCopyComplete(const std::vector<D3D12_TILED_RESOURCE_COORDINATE>& in_coords)
{
    for (const auto& t : in_coords)
    {
        ASSERT(TileMappingState::Residency::Loading == m_tileMappingState.GetResidency(t));
        m_tileMappingState.SetResident(t);
    }

    SetResidencyChanged();
}

//-----------------------------------------------------------------------------
// DataUploader has completed updating a reserved texture tile
//-----------------------------------------------------------------------------
void Streaming::StreamingResourceDU::NotifyEvicted(const std::vector<D3D12_TILED_RESOURCE_COORDINATE>& in_coords)
{
    ASSERT(in_coords.size());
    for (const auto& t : in_coords)
    {
        ASSERT(TileMappingState::Residency::Evicting == m_tileMappingState.GetResidency(t));
        m_tileMappingState.SetNotResident(t);
    }

    SetResidencyChanged();
}

//-----------------------------------------------------------------------------
// our packed mips have arrived!
//-----------------------------------------------------------------------------
void Streaming::StreamingResourceDU::NotifyPackedMips()
{
    m_packedMipStatus = PackedMipStatus::NEEDS_TRANSITION;
    m_pTileUpdateManager->NotifyPackedMips();

    // MinMipMap already set to packed mip values, don't need to go through UpdateMinMipMap
    //SetResidencyChanged();

    // don't need to hold on to packed mips any longer.
    std::vector<BYTE> empty;
    m_packedMips.swap(empty);
}
