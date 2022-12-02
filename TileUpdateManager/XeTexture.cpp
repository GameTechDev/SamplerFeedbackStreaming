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
    if (inFile.fail()) { Error(in_fileName + L" File doesn't exist (?)"); }

    inFile.read((char*)&m_fileHeader, sizeof(m_fileHeader));
    if (!inFile.good()) { Error(in_fileName + L" Unexpected Error reading header"); }

    if (m_fileHeader.m_magic != XetFileHeader::GetMagic()) { Error(in_fileName + L" Not a valid XET file"); }
    if (m_fileHeader.m_version != XetFileHeader::GetVersion()) { Error(in_fileName + L" Incorrect XET version"); }

    m_subresourceInfo.resize(m_fileHeader.m_ddsHeader.mipMapCount);
    inFile.read((char*)m_subresourceInfo.data(), m_subresourceInfo.size() * sizeof(m_subresourceInfo[0]));
    if (!inFile.good()) { Error(in_fileName + L" Unexpected Error reading subresource info"); }

    m_tileOffsets.resize(m_fileHeader.m_mipInfo.m_numTilesForStandardMips + 1); // plus 1 for the packed mips offset & size
    inFile.read((char*)m_tileOffsets.data(), m_tileOffsets.size() * sizeof(m_tileOffsets[0]));
    if (!inFile.good()) { Error(in_fileName + L" Unexpected Error reading packed mip info"); }
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
UINT Streaming::XeTexture::GetPackedMipFileOffset(UINT* out_pNumBytesTotal, UINT* out_pNumBytesUncompressed) const
{
    UINT packedOffset = m_tileOffsets[m_fileHeader.m_mipInfo.m_numTilesForStandardMips].m_offset;
    *out_pNumBytesTotal = m_tileOffsets[m_fileHeader.m_mipInfo.m_numTilesForStandardMips].m_numBytes;
    *out_pNumBytesUncompressed = m_fileHeader.m_mipInfo.m_numUncompressedBytesForPackedMips;
    return packedOffset;
}

//-----------------------------------------------------------------------------
// compute linear tile index from subresource info
//-----------------------------------------------------------------------------
UINT Streaming::XeTexture::GetLinearIndex(const D3D12_TILED_RESOURCE_COORDINATE& in_coord) const
{
    const auto& data = m_subresourceInfo[in_coord.Subresource].m_standardMipInfo;
    return data.m_subresourceTileIndex + (in_coord.Y * data.m_widthTiles) + in_coord.X;
}

//-----------------------------------------------------------------------------
// return value is byte offset into file
//-----------------------------------------------------------------------------
Streaming::XeTexture::FileOffset Streaming::XeTexture::GetFileOffset(const D3D12_TILED_RESOURCE_COORDINATE& in_coord) const
{
    // use index to look up file offset and number of bytes
    UINT index = GetLinearIndex(in_coord);
    FileOffset fileOffset;
    fileOffset.numBytes = m_tileOffsets[index].m_numBytes;
    fileOffset.offset = m_tileOffsets[index].m_offset;
    return fileOffset;
}
