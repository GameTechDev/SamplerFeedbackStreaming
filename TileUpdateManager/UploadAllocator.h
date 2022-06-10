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

#include <d3d12.h>
#include <vector>

#include "Streaming.h"

namespace Streaming
{
    class SimpleAllocator
    {
    public:
        SimpleAllocator(UINT in_maxNumElements);
        virtual ~SimpleAllocator();

        // output array will be sized to receive tile indices
        bool Allocate(std::vector<UINT>& out_indices, UINT in_numIndices);
        void Free(const std::vector<UINT>& in_indices);

        // assumes caller is doing due-diligence to allocate destination appropriately and check availability before calling
        void Allocate(UINT* out_pIndices, UINT in_numIndices);
        void Free(const UINT* in_pIndices, UINT in_numIndices);

        // for debug
        static const UINT InvalidIndex{ UINT(-1) };
        UINT GetAvailable() const { return m_index; }
        UINT GetCapacity() const { return (UINT)m_heap.size(); }
    private:
        std::vector<UINT> m_heap;
        UINT m_index;
    };

    //==================================================
    // UploadAllocator tracks tiles in an upload buffer
    // relies on objects to return their indices when they are done
    //==================================================
    class UploadAllocator : public SimpleAllocator
    {
    public:
        UploadAllocator(ID3D12Device* in_pDevice, UINT in_maxNumTiles);
        virtual ~UploadAllocator() {}

        Streaming::UploadBuffer& GetBuffer() { return m_buffer; }
    private:
        Streaming::UploadBuffer m_buffer;
    };

    //==================================================
    // BufferAllocator tracks tiles in a gpu-side staging buffer
    //==================================================
    class BufferAllocator : public SimpleAllocator
    {
    public:
        BufferAllocator(ID3D12Device* in_pDevice, UINT in_maxBlocks, UINT in_blockSize);
        virtual ~BufferAllocator() {}

        ID3D12Resource* GetResource() const { return m_buffer.Get(); }
    private:
        ComPtr<ID3D12Resource> m_buffer;
    };
}
