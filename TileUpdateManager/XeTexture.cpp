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

#include "d3dx12.h"
#include "DDS.h"
#include "XeTexture.h"

static void Error(std::wstring in_s)
{
    MessageBox(0, in_s.c_str(), L"Error", MB_OK);
    exit(-1);
}

/*-----------------------------------------------------------------------------
DDS format:
UINT32 magic number
DDS_HEADER structure
DDS_HEADER_DXT10 structure
-----------------------------------------------------------------------------*/
Streaming::XeTexture::XeTexture(const std::wstring& in_fileName)
{
    std::ifstream inFile(in_fileName.c_str(), std::ios::binary);
    inFile.read((char*)&m_fileHeader, sizeof(m_fileHeader));
    if (!inFile.good()) { Error(in_fileName + L"Unexpected Error"); }

    if (m_fileHeader.m_magic != XetFileHeader::GetMagic()) { Error(in_fileName + L" Not a valid XET file"); }
    if (m_fileHeader.m_version != XetFileHeader::GetVersion()) { Error(in_fileName + L"Incorrect XET version"); }

    m_tileOffsets.resize(m_fileHeader.m_mipInfo.m_numTilesForStandardMips);
    inFile.read((char*)m_tileOffsets.data(), m_tileOffsets.size() * sizeof(m_tileOffsets[0]));
    if (!inFile.good()) { Error(in_fileName + L"Unexpected Error"); }

    m_metadataOffsets.resize(m_fileHeader.m_numMetadataBlobs);
    inFile.read((char*)m_metadataOffsets.data(), m_metadataOffsets.size() * sizeof(m_metadataOffsets[0]));
    if (!inFile.good()) { Error(in_fileName + L"Unexpected Error"); }

    inFile.seekg(0, std::ios::end);
    size_t fileSize = inFile.tellg();

    // file offset to first packed mip
    size_t packedOffset = m_fileHeader.m_subresourceInfo[m_fileHeader.m_mipInfo.m_numStandardMips].m_packedMipInfo.m_fileOffset;
    size_t packedNumBytes = fileSize - packedOffset;

    size_t alignment = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1;
    packedNumBytes = (packedNumBytes + alignment) & (~alignment);

    m_packedMips.resize(packedNumBytes);

    inFile.seekg(packedOffset);
    inFile.read((char*)m_packedMips.data(), packedNumBytes);
    inFile.close();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Streaming::XeTexture::WritePackedBits(void* out_pBits, UINT in_mip, UINT64 in_dstStrideBytes)
{
    UINT h = std::max<UINT>(1, GetImageHeight() >> in_mip);

    // XeT is strictly intended for BCn formats that are multiples of 4 rows
    UINT numRows = std::max<UINT>(1, (h + 3) / 4);

    BYTE* pDst = (BYTE*)out_pBits;

    UINT fileOffsetBase = m_fileHeader.m_subresourceInfo[m_fileHeader.m_mipInfo.m_numStandardMips].m_packedMipInfo.m_fileOffset;
    UINT byteOffset = m_fileHeader.m_subresourceInfo[in_mip].m_packedMipInfo.m_fileOffset - fileOffsetBase;
    UINT rowPitch = m_fileHeader.m_subresourceInfo[in_mip].m_packedMipInfo.m_rowPitch;

    for (UINT i = 0; i < numRows; i++)
    {
        memcpy(pDst, &m_packedMips[byteOffset], rowPitch);

        pDst += in_dstStrideBytes;
        byteOffset += rowPitch;
    }
}

//-----------------------------------------------------------------------------
// compute linear tile index from subresource info
//-----------------------------------------------------------------------------
UINT Streaming::XeTexture::GetLinearIndex(const D3D12_TILED_RESOURCE_COORDINATE& in_coord) const
{
    const auto& data = m_fileHeader.m_subresourceInfo[in_coord.Subresource].m_standardMipInfo;
    return data.m_subresourceTileIndex + (in_coord.Y * data.m_widthTiles) + in_coord.X;
}

//-----------------------------------------------------------------------------
// return value is byte offset into file
//-----------------------------------------------------------------------------
UINT Streaming::XeTexture::GetFileOffset(const D3D12_TILED_RESOURCE_COORDINATE& in_coord) const
{
    UINT index = GetLinearIndex(in_coord);

    // use index to look up file offset and number of bytes
    return m_tileOffsets[index].m_offset;
}
