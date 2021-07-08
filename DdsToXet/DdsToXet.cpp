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

// Convert a DDS into a custom layout
// internally aligned (according to file header) and 64KB tiled

#include <fstream>
#include <iostream>
#include <d3d12.h>
#include <assert.h>

#include "ArgParser.h"
#include "d3dx12.h"

#include "XetFileHeader.h"

using Microsoft::WRL::ComPtr;

#pragma comment(lib, "d3d12.lib")

//=============================================================================
// offsets & sizes for each mip of a texture
//=============================================================================
struct SourceSubResourceData
{
    UINT m_offset;
    UINT m_rowPitch;
    UINT m_slicePitch;
};
std::vector<SourceSubResourceData> m_subresourceData;

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Error(std::wstring in_s)
{
    std::wcout << "Error: " << in_s << std::endl;
    exit(-1);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
static DXGI_FORMAT GetFormatFromHeader(const DirectX::DDS_HEADER& in_ddsHeader)
{
    const auto& ddpf = in_ddsHeader.ddspf;
    if (ddpf.flags & DDS_FOURCC)
    {
        if (MAKEFOURCC('D', 'X', 'T', '1') == ddpf.fourCC)
        {
            return DXGI_FORMAT_BC1_UNORM;
        }
        // other formats?
    }

    Error(L"Texture Format Unknown");

    return DXGI_FORMAT_UNKNOWN;
}

//-----------------------------------------------------------------------------
// return aligned # bytes
// e.g. if alignment is 4096, and input is 42, returned value will be 4096.
//-----------------------------------------------------------------------------
UINT GetAlignedSize(UINT in_numBytes)
{
    UINT alignment = XetFileHeader::GetAlignment() - 1;
    UINT aligned = (in_numBytes + alignment) & (~alignment);
    return aligned;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void GetTiling(XetFileHeader& out_header)
{
    struct TiledResourceDesc
    {
        D3D12_PACKED_MIP_INFO m_mipInfo; // last n mips may be packed into a single tile
        std::vector<D3D12_SUBRESOURCE_TILING> m_tiling;
        D3D12_TILE_SHAPE m_tileShape;          // e.g. a 64K tile may contain 128x128 texels @ 4B/pixel
        UINT m_numTilesTotal;
    };
    TiledResourceDesc tiledDesc;

    UINT imageWidth = out_header.m_ddsHeader.width;
    UINT imageHeight = out_header.m_ddsHeader.height;
    UINT mipCount = out_header.m_ddsHeader.mipMapCount;

    ComPtr<ID3D12Device> device;
    D3D12CreateDevice(0, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));

    D3D12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Tex2D(out_header.m_extensionHeader.dxgiFormat, imageWidth, imageHeight, 1, mipCount);

    // Layout must be D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE when creating reserved resources
    rd.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;

    ComPtr<ID3D12Resource> resource;
    device->CreateReservedResource(&rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource));

    // query the reserved resource for its tile properties
    // allocate data structure according to tile properties
    {
        UINT subresourceCount = resource->GetDesc().MipLevels;
        tiledDesc.m_tiling.resize(subresourceCount);
        device->GetResourceTiling(resource.Get(),
            &tiledDesc.m_numTilesTotal,
            &tiledDesc.m_mipInfo,
            &tiledDesc.m_tileShape, &subresourceCount, 0,
            tiledDesc.m_tiling.data());
    }

    //--------------------------
    // pre-fill header information based on tiling
    //--------------------------
    UINT subresourceTileIndex = 0;
    for (UINT s = 0; s < tiledDesc.m_mipInfo.NumStandardMips; s++)
    {
        out_header.m_subresourceInfo[s].m_standardMipInfo = XetFileHeader::StandardMipInfo{
            tiledDesc.m_tiling[s].WidthInTiles,
            tiledDesc.m_tiling[s].HeightInTiles,
            0, // FIXME? texture array not supported
            subresourceTileIndex };

        subresourceTileIndex += tiledDesc.m_tiling[s].WidthInTiles * tiledDesc.m_tiling[s].HeightInTiles;
    }
    out_header.m_mipInfo = XetFileHeader::MipInfo{
        tiledDesc.m_mipInfo.NumStandardMips,
        tiledDesc.m_mipInfo.NumPackedMips,
        tiledDesc.m_mipInfo.NumTilesForPackedMips,
        tiledDesc.m_mipInfo.StartTileIndexInOverallResource
    };
}

//-----------------------------------------------------------------------------
// find the base address of each mip level
//-----------------------------------------------------------------------------
void FillSubresourceData(std::vector<SourceSubResourceData>& out_subresourceData,
    const XetFileHeader& in_header)
{
    UINT offset = 0;

    UINT w = in_header.m_ddsHeader.width;
    UINT h = in_header.m_ddsHeader.height;

    UINT bytesPerElement = 0;

    switch (in_header.m_extensionHeader.dxgiFormat)
    {
    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
        bytesPerElement = 8;
        break;
    default: // BC7
        bytesPerElement = 16;
    }
    UINT numMips = in_header.m_mipInfo.m_numStandardMips + in_header.m_mipInfo.m_numPackedMips;
    out_subresourceData.resize(numMips);
    for (size_t i = 0; i < numMips; i++)
    {
        UINT numBlocksWide = std::max<UINT>(1, (w + 3) / 4);
        UINT numBlocksHigh = std::max<UINT>(1, (h + 3) / 4);

        UINT rowBytes = numBlocksWide * bytesPerElement;
        UINT subresourceBytes = rowBytes * numBlocksHigh;

        out_subresourceData[i] = SourceSubResourceData{ offset, rowBytes, subresourceBytes };

        offset += subresourceBytes;

        if (w > 1) { w >>= 1; }
        if (h > 1) { h >>= 1; }
    }
}

//-----------------------------------------------------------------------------
// convert standard dds into tiled layout
//-----------------------------------------------------------------------------
UINT WriteBits(BYTE* out_pDst,
    const D3D12_TILED_RESOURCE_COORDINATE& in_coord,
    const SourceSubResourceData& in_subresourceData,
    const BYTE* in_pSrc)
{
    // this is a BC7 decoder
    // we know that tiles will be 64KB
    // 1 tile of size 256x256 will have a row size of 1024 bytes, and 64 rows (4 texels per row)
    // we will always be copying 64 rows, since every row is 4 texels
    const UINT tileRowBytes = 1024;
    const UINT numRowsPerTile = 64;

    UINT srcOffset = in_subresourceData.m_offset;

    // offset into this tile
    UINT startRow = in_coord.Y * numRowsPerTile;
    srcOffset += (in_subresourceData.m_rowPitch * startRow);
    srcOffset += in_coord.X * tileRowBytes;

    // copy the rows of this tile
    for (UINT row = 0; row < numRowsPerTile; row++)
    {
        memcpy(out_pDst, in_pSrc + srcOffset, tileRowBytes);

        out_pDst += tileRowBytes;
        srcOffset += in_subresourceData.m_rowPitch;
    }

    return XetFileHeader::GetTileSize();
}

//-----------------------------------------------------------------------------
// builds offset table and fills tiled texture data
// returns # bytes written to texture data
//-----------------------------------------------------------------------------
void WriteTiles(
    std::vector<BYTE>& out_textureData, std::vector<XetFileHeader::TileData>& out_offsets,
    const XetFileHeader& in_header, const BYTE* in_pSrc)
{
    UINT imageWidth = in_header.m_ddsHeader.width;
    UINT imageHeight = in_header.m_ddsHeader.height;
    UINT mipCount = in_header.m_ddsHeader.mipMapCount;

    // texture data starts after the header, and after the table of offsets
    UINT offset = 0;

    // note (uncompressed) tiles are naturally aligned at greater than file alignment granularity.
    const UINT tileSizeBytes = GetAlignedSize(XetFileHeader::GetTileSize());

    // find the base address of each /tiled/ mip level
    {
        for (UINT s = 0; s < in_header.m_mipInfo.m_numStandardMips; s++)
        {
            for (UINT y = 0; y < in_header.m_subresourceInfo[s].m_standardMipInfo.m_heightTiles; y++)
            {
                for (UINT x = 0; x < in_header.m_subresourceInfo[s].m_standardMipInfo.m_widthTiles; x++)
                {
                    // write tile
                    out_textureData.resize(out_textureData.size() + tileSizeBytes);
                    WriteBits(&out_textureData[offset], D3D12_TILED_RESOURCE_COORDINATE{ x, y, 0, s }, m_subresourceData[s], in_pSrc);

                    // add tileData to array
                    XetFileHeader::TileData outData{0};
                    outData.m_offset = offset;
                    outData.m_numBytes = tileSizeBytes;
                    out_offsets.push_back(outData);

                    offset = (UINT)out_textureData.size();
                }
            }
        }
    }
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int main()
{
    std::wstring inFileName;
    std::wstring outFileName;

    ArgParser argParser;
    argParser.AddArg(L"-in", inFileName);
    argParser.AddArg(L"-out", outFileName);
    argParser.Parse();

    //--------------------------
    // read dds file
    //--------------------------
    std::ifstream inFile(inFileName, std::ios::in | std::ios::binary);
    if (!inFile.is_open()) { Error(L"File not found"); }

    // pre-size an array to hold the bits, then load them all
    std::vector<BYTE> bytes;
    inFile.seekg(0, std::ios::end);
    bytes.reserve(inFile.tellg());
    inFile.seekg(0, std::ios::beg);
    bytes.insert(bytes.begin(), std::istreambuf_iterator<char>(inFile), std::istreambuf_iterator<char>());
    inFile.close();

    //--------------------------
    // interpret contents based on dds header
    //--------------------------
    BYTE* pBits = bytes.data();

    UINT32 magic = *(UINT32*)pBits;
    pBits += sizeof(magic);
    if (DirectX::DDS_MAGIC != magic) { Error(L"Not a valid DDS file"); }

    XetFileHeader header;
    header.m_ddsHeader = *(DirectX::DDS_HEADER*)pBits;
    pBits += header.m_ddsHeader.size;

    if ((header.m_ddsHeader.ddspf.flags & DDS_FOURCC) && (MAKEFOURCC('D', 'X', '1', '0') == header.m_ddsHeader.ddspf.fourCC))
    {
        header.m_extensionHeader = *(DirectX::DDS_HEADER_DXT10*)pBits;
        pBits += sizeof(DirectX::DDS_HEADER_DXT10);
    }
    else
    {
        DirectX::DDS_HEADER_DXT10 extensionHeader{};
        header.m_extensionHeader = extensionHeader;
        header.m_extensionHeader.dxgiFormat = GetFormatFromHeader(header.m_ddsHeader);
    }
    // NOTE: pBits now points at beginning of DDS data

    GetTiling(header);
    // find offsets and rowpitch in source data
    FillSubresourceData(m_subresourceData, header);

    //--------------------------
    // reserve output space
    //--------------------------
    std::vector<BYTE> textureData;
    textureData.reserve(bytes.size());
    // offsets table
    std::vector<XetFileHeader::TileData> offsets;
    offsets.reserve(header.m_mipInfo.m_numTilesForStandardMips);

    // offsets into metadata
    // m : n relationship where n tiles may use m metadata blocks, where m <= n
    std::vector<XetFileHeader::MetaData> metadataOffsets;
    metadataOffsets.reserve(offsets.size());
    // metadata itself should take less space than tile data
    std::vector<BYTE> metadata;
    metadata.reserve(bytes.size());

    //--------------------------
    // write tiles
    //--------------------------
    header.m_numMetadataBlobs = 0;
    WriteTiles(textureData, offsets, header, pBits);
    assert(textureData.size() == header.m_mipInfo.m_numTilesForStandardMips * D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES);

    //------------------------------------------
    // compute gap between header and aligned texture data
    //------------------------------------------
    UINT offsetsSize = UINT(sizeof(offsets[0]) * offsets.size());
    UINT metadataOffsetsSize = UINT(sizeof(metadataOffsets[0]) * metadataOffsets.size());
    // no gap between header and offets table
    UINT numHeaderBytes = UINT(sizeof(XetFileHeader) + offsetsSize + metadataOffsetsSize);

    // alignment gap after header
    UINT alignedHeaderSize = GetAlignedSize(numHeaderBytes);
    UINT headerGapNumBytes = alignedHeaderSize - numHeaderBytes;
    std::vector<BYTE> headerGap(headerGapNumBytes, 0);

    // note the metadata blobs are already in an aligned-size block
    UINT alignedMetadataSize = UINT(sizeof(metadata[0]) * metadata.size());

    UINT metadataBaseOffset = alignedHeaderSize;
    UINT textureBaseOffset = metadataBaseOffset + alignedMetadataSize;

    //------------------------------------------
    // correct offsets to account for alignment after header or metadata
    //------------------------------------------
    UINT packedMipBytes = 0;
    UINT packedMipOffset = textureBaseOffset + (UINT)textureData.size();
    for (UINT i = 0; i < header.m_mipInfo.m_numPackedMips; i++)
    {
        UINT s = header.m_mipInfo.m_numStandardMips + i;
        header.m_subresourceInfo[s].m_packedMipInfo = XetFileHeader::PackedMipInfo{
            m_subresourceData[s].m_rowPitch,
            m_subresourceData[s].m_slicePitch,
            packedMipBytes + packedMipOffset };
        packedMipBytes += m_subresourceData[s].m_slicePitch;
    }

    for (auto& m : metadataOffsets)
    {
        m.m_offset += metadataBaseOffset;
    }

    for (auto& o : offsets)
    {
        o.m_offset += textureBaseOffset;
    }

    std::ofstream outFile(outFileName, std::ios::out | std::ios::binary);

    outFile.write((char*)&header, sizeof(header));
    outFile.write((char*)offsets.data(), offsetsSize);
    outFile.write((char*)metadataOffsets.data(), metadataOffsetsSize);
    outFile.write((char*)headerGap.data(), headerGapNumBytes);

    outFile.write((char*)metadata.data(), alignedMetadataSize);

    outFile.write((char*)textureData.data(), (UINT)textureData.size());

    //------------------------------------------
    // copy packed mip bits directly from source dds
    // packed mip data will be aligned
    //------------------------------------------
    UINT srcOffset = (UINT)bytes.size() - packedMipBytes;
    outFile.write((char*)&bytes[srcOffset], packedMipBytes);

    outFile.close();

    return 0;
}
