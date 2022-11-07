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
// also converts v2 XeT to latest format

#include <fstream>
#include <iostream>
#include <d3d12.h>
#include <assert.h>
#include <filesystem>

#include <dstorage.h>

#include "ArgParser.h"
#include "d3dx12.h"

#include "XeTv2.h"
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
std::vector<SourceSubResourceData> m_subresourceData; // read from source DDS

// output subresource info
std::vector<XetFileHeader::SubresourceInfo> m_subresourceInfo;

// offsets table
std::vector<XetFileHeader::TileData> m_offsets;

// texture bytes
std::vector<BYTE> m_textureData;

// packed mip bytes
std::vector<BYTE> m_packedMipData;

D3D12_RESOURCE_DESC m_resourceDesc; // will be created and re-used

UINT32 m_compressionFormat{ 1 };

bool m_convertFromXet2{ false };

ComPtr<IDStorageCompressionCodec> m_compressor;
//DSTORAGE_COMPRESSION m_compressionLevel = DSTORAGE_COMPRESSION_FASTEST;
DSTORAGE_COMPRESSION m_compressionLevel = DSTORAGE_COMPRESSION_BEST_RATIO;
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
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

    if (in_ddsHeader.ddspf.flags & DDS_FOURCC)
    {
        UINT32 fourCC = in_ddsHeader.ddspf.fourCC;
        if (DirectX::DDSPF_DXT1.fourCC == fourCC) { format = DXGI_FORMAT_BC1_UNORM; }
        if (DirectX::DDSPF_DXT2.fourCC == fourCC) { format = DXGI_FORMAT_BC2_UNORM; }
        if (DirectX::DDSPF_DXT3.fourCC == fourCC) { format = DXGI_FORMAT_BC2_UNORM; }
        if (DirectX::DDSPF_DXT4.fourCC == fourCC) { format = DXGI_FORMAT_BC3_UNORM; }
        if (DirectX::DDSPF_DXT5.fourCC == fourCC) { format = DXGI_FORMAT_BC3_UNORM; }
        if (MAKEFOURCC('A', 'T', 'I', '1') == fourCC) { format = DXGI_FORMAT_BC4_UNORM; }
        if (MAKEFOURCC('A', 'T', 'I', '2') == fourCC) { format = DXGI_FORMAT_BC5_UNORM; }
        if (MAKEFOURCC('B', 'C', '4', 'U') == fourCC) { format = DXGI_FORMAT_BC4_UNORM; }
        if (MAKEFOURCC('B', 'C', '4', 'S') == fourCC) { format = DXGI_FORMAT_BC4_SNORM; }
        if (MAKEFOURCC('B', 'C', '5', 'U') == fourCC) { format = DXGI_FORMAT_BC5_UNORM; }
        if (MAKEFOURCC('B', 'C', '5', 'S') == fourCC) { format = DXGI_FORMAT_BC5_SNORM; }
    }
    if (DXGI_FORMAT_UNKNOWN == format) { Error(L"Texture Format Unknown"); }

    return format;
}

//-----------------------------------------------------------------------------
// return aligned # bytes based on conservative 4KB alignment
//-----------------------------------------------------------------------------
UINT GetAlignedSize(UINT in_numBytes)
{
    UINT alignment = 4096 - 1;
    UINT aligned = (in_numBytes + alignment) & (~alignment);
    return aligned;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void GetTiling(XetFileHeader& out_header)
{
    std::vector<D3D12_SUBRESOURCE_TILING> subresourceTiling;
    D3D12_TILE_SHAPE tileShape{}; // e.g. a 64K tile may contain 128x128 texels @ 4B/pixel
    UINT numTilesTotal = 0;

    D3D12_PACKED_MIP_INFO packedMipInfo; // last n mips may be packed into a single tile

    UINT imageWidth = out_header.m_ddsHeader.width;
    UINT imageHeight = out_header.m_ddsHeader.height;

    if (0 == out_header.m_ddsHeader.mipMapCount)
    {
        out_header.m_ddsHeader.mipMapCount = 1;
    }
    UINT mipCount = out_header.m_ddsHeader.mipMapCount;

    ComPtr<ID3D12Device> device;
    D3D12CreateDevice(0, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));
    m_resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(out_header.m_extensionHeader.dxgiFormat, imageWidth, imageHeight, 1, mipCount);

    // Layout must be D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE when creating reserved resources
    m_resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;

    ComPtr<ID3D12Resource> resource;
    device->CreateReservedResource(&m_resourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource));

    // query the reserved resource for its tile properties
    // allocate data structure according to tile properties
    UINT subresourceCount = resource->GetDesc().MipLevels;
    subresourceTiling.resize(subresourceCount);
    device->GetResourceTiling(resource.Get(),
        &numTilesTotal,
        &packedMipInfo,
        &tileShape, &subresourceCount, 0,
        subresourceTiling.data());

    //--------------------------
    // pre-fill header information based on tiling
    //--------------------------
    m_subresourceInfo.resize(subresourceCount);
    UINT subresourceTileIndex = 0;
    for (UINT s = 0; s < packedMipInfo.NumStandardMips; s++)
    {
        m_subresourceInfo[s].m_standardMipInfo = XetFileHeader::StandardMipInfo{
            subresourceTiling[s].WidthInTiles,
            subresourceTiling[s].HeightInTiles,
            0, // FIXME? texture array not supported
            subresourceTileIndex };

        subresourceTileIndex += subresourceTiling[s].WidthInTiles * subresourceTiling[s].HeightInTiles;
    }

    out_header.m_mipInfo.m_numStandardMips = packedMipInfo.NumStandardMips;
    out_header.m_mipInfo.m_numTilesForStandardMips = numTilesTotal - packedMipInfo.NumTilesForPackedMips;
    out_header.m_mipInfo.m_numPackedMips = packedMipInfo.NumPackedMips;
    out_header.m_mipInfo.m_numTilesForPackedMips = packedMipInfo.NumTilesForPackedMips;
    out_header.m_mipInfo.m_numUncompressedBytesForPackedMips = 0; // will be filled in later
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
UINT WriteTile(BYTE* out_pDst,
    const D3D12_TILED_RESOURCE_COORDINATE& in_coord,
    const SourceSubResourceData& in_subresourceData,
    const BYTE* in_pSrc)
{
    // this is a BC7 or BC1 decoder
    // we know that tiles will be 64KB
    // 1 tile of BC7 size 256x256 will have a row size of 1024 bytes, and 64 rows (4 texels per row)
    // 1 tile of BC1 size 512x256 will also have row size 1024 bytes and 64 rows
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
//-----------------------------------------------------------------------------
void CompressTile(std::vector<BYTE>& inout_tile)
{
    auto bound = m_compressor->CompressBufferBound(D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES);
    std::vector<BYTE> scratch(bound);

    size_t compressedDataSize = 0;
    HRESULT hr = m_compressor->CompressBuffer(
        inout_tile.data(), D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES,
        m_compressionLevel,
        scratch.data(), bound, &compressedDataSize);

    assert(compressedDataSize <= D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES);

    scratch.resize(compressedDataSize);

    inout_tile.swap(scratch);
}

//-----------------------------------------------------------------------------
// builds offset table and fills tiled texture data
//-----------------------------------------------------------------------------
void WriteTiles(const XetFileHeader& in_header, const BYTE* in_pSrc)
{
    UINT imageWidth = in_header.m_ddsHeader.width;
    UINT imageHeight = in_header.m_ddsHeader.height;
    UINT mipCount = in_header.m_ddsHeader.mipMapCount;

    // texture data starts after the header, and after the table of offsets
    UINT offset = 0;

    std::vector<BYTE> tile; // scratch space for writing tiled texture data

    // find the base address of each /tiled/ mip level
    for (UINT s = 0; s < in_header.m_mipInfo.m_numStandardMips; s++)
    {
        for (UINT y = 0; y < m_subresourceInfo[s].m_standardMipInfo.m_heightTiles; y++)
        {
            for (UINT x = 0; x < m_subresourceInfo[s].m_standardMipInfo.m_widthTiles; x++)
            {
                tile.resize(D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES); // reset to standard tile size

                if (m_convertFromXet2)
                {
                    memcpy(tile.data(), in_pSrc, D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES);
                    in_pSrc += D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
                }
                else
                {
                    WriteTile(tile.data(), D3D12_TILED_RESOURCE_COORDINATE{ x, y, 0, s }, m_subresourceData[s], in_pSrc);
                }

                if (m_compressionFormat)
                {
                    CompressTile(tile);
                }

                m_textureData.resize(m_textureData.size() + tile.size()); // grow the texture space to hold the new tile
                memcpy(&m_textureData[offset], tile.data(), tile.size()); // copy bytes

                // add tileData to array
                XetFileHeader::TileData outData{ 0 };
                outData.m_offset = offset;
                outData.m_numBytes = (UINT)tile.size();
                m_offsets.push_back(outData);

                offset = (UINT)m_textureData.size();
            }
        }
    }
}

//-----------------------------------------------------------------------------
// pad packed mips according to copyable footprint requirements
//-----------------------------------------------------------------------------
void PadPackedMips(const XetFileHeader& in_header, const BYTE* in_psrc, std::vector<BYTE>& out_paddedPackedMips)
{
    UINT firstSubresource = in_header.m_mipInfo.m_numStandardMips;
    UINT numSubresources = in_header.m_mipInfo.m_numPackedMips;
    UINT64 totalBytes = 0;
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> srcLayout(numSubresources);
    std::vector<UINT> numRows(numSubresources);
    std::vector<UINT64> rowSizeBytes(numSubresources);

    ComPtr<ID3D12Device> device;
    D3D12CreateDevice(0, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device));

    device->GetCopyableFootprints(&m_resourceDesc, firstSubresource, numSubresources,
        0, srcLayout.data(), numRows.data(), rowSizeBytes.data(), &totalBytes);

    out_paddedPackedMips.resize(totalBytes);

    BYTE* pDst = out_paddedPackedMips.data();

    for (UINT i = 0; i < numSubresources; i++)
    {
        for (UINT r = 0; r < numRows[i]; r++)
        {
            memcpy(pDst, in_psrc, rowSizeBytes[i]);
            pDst += srcLayout[i].Footprint.RowPitch;
            in_psrc += rowSizeBytes[i];
        }
    }
}

//-----------------------------------------------------------------------------
// stores padded packed mips. returns uncompressed size.
//-----------------------------------------------------------------------------
UINT WritePackedMips(const XetFileHeader& in_header, BYTE* in_pBytes, size_t in_numBytes)
{
    UINT numPackedMipBytes = 0;
    for (UINT i = 0; i < in_header.m_mipInfo.m_numPackedMips; i++)
    {
        UINT s = in_header.m_mipInfo.m_numStandardMips + i;
        m_subresourceInfo[s].m_packedMipInfo = XetFileHeader::PackedMipInfo{
            m_subresourceData[s].m_rowPitch,
            m_subresourceData[s].m_slicePitch,
            0xbaadbaad, 0xbaadbaad }; // FIXME: include padded row pitch and slice pitch
        numPackedMipBytes += m_subresourceData[s].m_slicePitch;
    }

    // packed mip data is at the end of the DDS file
    UINT srcOffset = UINT(in_numBytes - numPackedMipBytes);

    BYTE* pSrc = &in_pBytes[srcOffset];
    PadPackedMips(in_header, pSrc, m_packedMipData);
    UINT numBytesPadded = (UINT)m_packedMipData.size(); // uncompressed and padded

    size_t numBytesCompressed = numBytesPadded; // unless we compress...
    if (m_compressionFormat)
    {
        // input to CompressBuffer() is a UINT32
        auto bound = m_compressor->CompressBufferBound(numBytesPadded);
        std::vector<BYTE> scratch(bound);

        HRESULT hr = m_compressor->CompressBuffer(
            m_packedMipData.data(), numBytesPadded,
            m_compressionLevel,
            scratch.data(), bound, &numBytesCompressed);
        scratch.resize(numBytesCompressed);
        m_packedMipData.swap(scratch);
    }

    // last offset structure points at the packed mips
    XetFileHeader::TileData outData{ 0 };
    outData.m_offset = m_offsets.back().m_offset + m_offsets.back().m_numBytes;
    outData.m_numBytes = (UINT32)numBytesCompressed;
    m_offsets.push_back(outData);

    return numBytesPadded;
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
    argParser.AddArg(L"-compress", m_compressionFormat, L"compression format");
    argParser.Parse();

    //--------------------------
    // read dds file
    //--------------------------
    HANDLE inFileHandle = CreateFile(inFileName.data(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (NULL == inFileHandle)
    {
        Error(L"Failed to open file");
    }

    HANDLE inFileMapping = CreateFileMapping(inFileHandle, NULL, PAGE_READONLY, 0, 0, NULL);
    if (NULL == inFileMapping)
    {
        Error(L"Failed to create mapping");
    }
    BYTE* pInFileBytes = (BYTE*)MapViewOfFile(inFileMapping, FILE_MAP_READ, 0, 0, 0);
    if (NULL == pInFileBytes)
    {
        Error(L"Failed to map file");
    }

    XetFileHeader header;
    header.m_compressionFormat = m_compressionFormat;

    //--------------------------
    // interpret contents based on dds header
    //--------------------------
    BYTE* pBits = pInFileBytes;
    if (DirectX::DDS_MAGIC == *(UINT32*)pBits)
    {
        pBits += sizeof(UINT32);
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
    }
    else
    {
        XetFileHeaderV2 srcHeader = *(XetFileHeaderV2*)pBits;
        if (XetFileHeaderV2::GetMagic() != srcHeader.m_magic)
        {
            Error(L"Not a valid DDS or XET file");
        }

        if (XetFileHeaderV2::GetVersion() != srcHeader.m_version)
        {
            Error(L"Not a valid XET version");
        }

        m_convertFromXet2 = true; // changes behavior within WriteTiles()

        header.m_ddsHeader = srcHeader.m_ddsHeader;
        header.m_extensionHeader = srcHeader.m_extensionHeader;

        XetFileHeaderV2::TileData* pTileData = (XetFileHeaderV2::TileData*)pBits;
        size_t numBytes = sizeof(XetFileHeaderV2);
        numBytes += sizeof(XetFileHeaderV2::TileData) * srcHeader.m_mipInfo.m_numTilesForStandardMips;
        size_t alignment = XetFileHeaderV2::GetAlignment() - 1;
        auto aligned = (numBytes + alignment) & (~alignment);
        pBits += aligned;
    }

    // NOTE: pBits now points at beginning of DDS data

    GetTiling(header);
    // find offsets and rowpitch in source data
    FillSubresourceData(m_subresourceData, header);

    //--------------------------
    // reserve output space
    //--------------------------
    std::filesystem::path inFilePath(inFileName);
    auto fileSize = std::filesystem::file_size(inFilePath);

    m_textureData.reserve(fileSize); // reserve enough space to hold the whole uncompressed source
    m_offsets.reserve(header.m_mipInfo.m_numTilesForStandardMips + 1);

    //--------------------------
    // write tiles
    //--------------------------
    if (m_compressionFormat)
    {
        HRESULT hr = DStorageCreateCompressionCodec((DSTORAGE_COMPRESSION_FORMAT)m_compressionFormat, 2, IID_PPV_ARGS(&m_compressor));
    }
    WriteTiles(header, pBits);
    header.m_mipInfo.m_numUncompressedBytesForPackedMips = WritePackedMips(header, pInFileBytes, fileSize);

    //------------------------------------------
    // correct offsets to account for alignment after header
    //------------------------------------------
    UINT64 textureDataOffset = sizeof(header) +
        (m_subresourceInfo.size() * sizeof(m_subresourceInfo[0])) +
        (m_offsets.size() * sizeof(m_offsets[0]));

    // align only for legacy support for uncompressed file formats
    std::vector<BYTE> alignedTextureDataGap;
    if (!m_compressionFormat)
    {
        UINT alignedTextureDataOffset = GetAlignedSize((UINT)textureDataOffset);
        alignedTextureDataGap.resize(alignedTextureDataOffset - textureDataOffset, 0);
        textureDataOffset += alignedTextureDataGap.size();
    }

    // correct the tile offsets to account for the preceding data
    for (auto& o : m_offsets)
    {
        o.m_offset += (UINT)textureDataOffset;
    }

    std::ofstream outFile(outFileName, std::ios::out | std::ios::binary);

    outFile.write((char*)&header, sizeof(header));
    outFile.write((char*)m_subresourceInfo.data(), m_subresourceInfo.size() * sizeof(m_subresourceInfo[0]));
    outFile.write((char*)m_offsets.data(), m_offsets.size() * sizeof(m_offsets[0]));

    // alignment is here only for legacy support for uncompressed file formats
    if (alignedTextureDataGap.size())
    {
        outFile.write((char*)alignedTextureDataGap.data(), alignedTextureDataGap.size());
    }

    outFile.write((char*)m_textureData.data(), (UINT)m_textureData.size());
    outFile.write((char*)m_packedMipData.data(), (UINT)m_packedMipData.size());

    UnmapViewOfFile(pInFileBytes);
    CloseHandle(inFileMapping);
    CloseHandle(inFileHandle);

    return 0;
}
