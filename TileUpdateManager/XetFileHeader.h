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

#include "DDS.h"

/*-----------------------------------------------------------------------------
File Layout:

- Header
- Array of Per-tile info: file offset, # bytes.
    note all uncompressed tiles are 64KB. if the number of bytes = 64KB, then the tile is assumed uncompressed
- Texture Data. tiles are not aligned
- packed mips. the data is unaligned, but the contents have been pre-padded

-----------------------------------------------------------------------------*/
struct XetFileHeader
{
    static UINT GetMagic() { return 0x20544558; }
    static UINT GetTileSize() { return 65536; } // uncompressed size
    static UINT GetVersion() { return 3; }

    UINT m_magic{ GetMagic() };
    UINT m_version{ GetVersion() };
    DirectX::DDS_HEADER m_ddsHeader;
    DirectX::DDS_HEADER_DXT10 m_extensionHeader;

    UINT32 m_compressionFormat{ 0 }; // 0 is no compression

    struct MipInfo
    {
        UINT32 m_numStandardMips;
        UINT32 m_numTilesForStandardMips; // the TileData[] array has # entries = (m_numTilesForStandardMips + 1)
        UINT32 m_numPackedMips;
        UINT32 m_numTilesForPackedMips;   // only 1 entry for all packed mips at TileData[m_numTilesForStandardMips]
        UINT32 m_numUncompressedBytesForPackedMips; // if this equals the size at TileData[m_numTilesForStandardMips], then not compressed
    };
    MipInfo m_mipInfo;

    // use subresource tile dimensions to generate linear tile index
    struct StandardMipInfo
    {
        UINT32 m_widthTiles;
        UINT32 m_heightTiles;
        UINT32 m_depthTiles;

        // convenience value, can be computed from sum of previous subresource dimensions
        UINT32 m_subresourceTileIndex;
    };

    // properties of the uncompressed packed mips
    // all packed mips are padded and treated as a single entity
    struct PackedMipInfo
    {
        UINT32 m_rowPitch;   // before padding
        UINT32 m_slicePitch; // before padding

        UINT32 m_rowPitchPadded;   // after padding, from footprint
        UINT32 m_slicePitchPadded; // after padding, from footprint
    };

    // array SubresourceInfo[m_ddsHeader.mipMapCount]
    struct SubresourceInfo
    {
        union
        {
            StandardMipInfo m_standardMipInfo;
            PackedMipInfo m_packedMipInfo;
        };
    };

    // array TileData[m_numTilesForStandardMips + 1], 1 entry for each tile plus a final entry for packed mips
    struct TileData
    {
        UINT32 m_offset;          // file offset to tile data
        UINT32 m_numBytes;        // # bytes for the tile
    };

    // arrays for file lookup start after sizeof(XetFileHeader)
    // 1st: array SubresourceInfo[m_ddsHeader.mipMapCount]
    // 2nd: array TileData[m_numTilesForStandardMips + 1]
    // 3rd: packed mip data can be found at TileData[m_numTilesForStandardMips].m_offset TileData[m_numTilesForStandardMips].m_numBytes
};
