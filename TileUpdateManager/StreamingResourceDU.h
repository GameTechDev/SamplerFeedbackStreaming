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

#include "StreamingResource.h"
#include "XeTexture.h"

//-----------------------------------------------------------------
// custom StreamingResource interface for DataUploader
//-----------------------------------------------------------------
namespace Streaming
{
    class StreamingResourceDU : private StreamingResourceBase
    {
    public:
        XeTexture* GetTextureFileInfo() const { return m_pTextureFileInfo.get(); }
        Streaming::Heap* GetHeap() const { return m_pHeap; }

        // just for packed mips
        const D3D12_PACKED_MIP_INFO& GetPackedMipInfo() const { return m_resources->GetPackedMipInfo(); }

        void NotifyCopyComplete(const std::vector<D3D12_TILED_RESOURCE_COORDINATE>& in_coords);
        void NotifyPackedMips();
        void NotifyEvicted(const std::vector<D3D12_TILED_RESOURCE_COORDINATE>& in_coords);

        ID3D12Resource* GetTiledResource() const { return m_resources->GetTiledResource(); }

        const FileHandle* GetFileHandle() const { return m_pFileHandle.get(); }

        std::vector<BYTE>& GetPaddedPackedMips(UINT& out_uncompressedSize) { out_uncompressedSize = m_packedMipsUncompressedSize; return m_packedMips; }

        // packed mips are treated differently from regular tiles: they aren't tracked by the data structure, and share heap indices
        void MapPackedMips(ID3D12CommandQueue* in_pCommandQueue);
    };
}
