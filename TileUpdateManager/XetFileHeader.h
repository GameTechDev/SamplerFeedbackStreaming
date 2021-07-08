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
- Array of Per-tile info: file offset, # bytes, index into metadata array
- Array of Per-metadata blob info: file offset, size of metadata blob
- Metadata blobs, each blob is aligned
- Texture Data, each tile is aligned
- packed mips, treat as not aligned

-----------------------------------------------------------------------------*/
struct XetFileHeader
{
    static UINT GetMagic() { return 0x20544558; }
    static UINT GetAlignment() { return 4096; }
    static UINT GetTileSize() { return 65536; }
    static UINT GetVersion() { return 2; }

    UINT m_magic{ GetMagic() };
    UINT m_version{ GetVersion() };
    DirectX::DDS_HEADER m_ddsHeader;
    DirectX::DDS_HEADER_DXT10 m_extensionHeader;

    UINT m_numMetadataBlobs;

    struct MipInfo
    {
        UINT m_numStandardMips;
        UINT m_numPackedMips;
        UINT m_numTilesForPackedMips;
        UINT m_numTilesForStandardMips; // the number of TileData[] entries after the header
    };
    MipInfo m_mipInfo;

    // use subresource tile dimensions to generate linear tile index
    struct StandardMipInfo
    {
        UINT m_widthTiles;
        UINT m_heightTiles;
        UINT m_depthTiles;

        // convenience value, can be computed from sum of previous subresource dimensions
        UINT m_subresourceTileIndex;
    };

    // if required, compute dimensions by shifting mip 0 dimensions by mip level
    struct PackedMipInfo
    {
        UINT m_rowPitch;
        UINT m_slicePitch;
        UINT m_fileOffset;
    };

    struct SubresourceInfo
    {
        union
        {
            StandardMipInfo m_standardMipInfo;
            PackedMipInfo m_packedMipInfo;
        };
    };

    // indices < m_numStandardMips are standard mips, >= are packed mips
    SubresourceInfo m_subresourceInfo[16];

    // array TileData[m_numTilesForStandardMips] for each tile
    struct TileData
    {
        UINT m_offset;          // file offset to tile data
        UINT m_numBytes;        // # bytes for the tile
        UINT m_metadataIndex;   // index of metadata to use
    };

    // array of MetaData[m_numMetadataBlobs] (may be size 0)
    struct MetaData
    {
        UINT m_offset;          // file offset to metadata blob
        UINT m_numBytes;        // size of the metadata blob
    };

    // metadata here (aligned)
    // tile data here (aligned)

    // packed mip data from m_packedMipInfo.m_fileOffset until EOF
};
