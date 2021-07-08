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

//==================================================
// HeapAllocator tracks which tiles in a heap are available
// relies on objects to return their indices when they are done
//==================================================
namespace Streaming
{
    class HeapAllocator
    {
    public:
        HeapAllocator(UINT in_maxNumTilesHeap);
        ~HeapAllocator();

        UINT GetNumFree() const { return m_index; }

        // returns InvalidIndex on failure
        UINT Allocate();
        void Free(UINT in_index);

        // returns the number of indices allocated. will be <= in_max.
        UINT Allocate(std::vector<UINT>& out_indices, UINT in_max);
        void Free(const std::vector<UINT>& in_indices);

        // for debug
        static const UINT InvalidIndex{ UINT(-1) };

        // for visualization only
        UINT GetNumAllocated() const { return (UINT)m_heap.size() - m_index; }
        UINT GetCapacity() const { return (UINT)m_heap.size(); }
    private:
        std::vector<UINT> m_heap;
        UINT m_index;
    };
}
