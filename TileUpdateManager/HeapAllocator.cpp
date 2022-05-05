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

#include "HeapAllocator.h"

//=============================================================================
// Internal class that allocates tiles from a heap
// this heap could be shared by multiple resources
//=============================================================================
Streaming::HeapAllocator::HeapAllocator(UINT in_maxNumTilesHeap) :
    m_index(0)
    , m_heap(in_maxNumTilesHeap)
{
    // indices into heap for streaming texture
    for (auto& i : m_heap)
    {
        i = m_index;
        m_index++;
    }
}

Streaming::HeapAllocator::~HeapAllocator()
{
#ifdef _DEBUG
    ASSERT(m_index == (UINT)m_heap.size());
    // verify all indices accounted for and unique
    std::sort(m_heap.begin(), m_heap.end());
    for (UINT i = 0; i < (UINT)m_heap.size(); i++)
    {
        ASSERT(i == m_heap[i]);
    }
#endif
}

//-----------------------------------------------------------------------------
UINT Streaming::HeapAllocator::Allocate()
{
    UINT indexIntoHeap = InvalidIndex;
    if (m_index != 0)
    {
        m_index--;
        indexIntoHeap = m_heap[m_index];
    }

    return indexIntoHeap;
}

//-----------------------------------------------------------------------------
// returns the number of indices allocated. will be <= in_max.
//-----------------------------------------------------------------------------
UINT Streaming::HeapAllocator::Allocate(std::vector<UINT>& out_indices, UINT in_max)
{
    UINT numIndices = std::min(m_index, in_max);
    if (numIndices)
    {
        m_index -= numIndices;
        out_indices.assign(m_heap.begin() + m_index, m_heap.begin() + m_index + numIndices);
    }
    return numIndices;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Streaming::HeapAllocator::Free(UINT in_indexIntoHeap)
{
    ASSERT(m_index < m_heap.size());
    m_heap[m_index] = in_indexIntoHeap;
    m_index++;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Streaming::HeapAllocator::Free(const std::vector<UINT>& in_indices)
{
    UINT numIndices = (UINT)in_indices.size();
    ASSERT(numIndices);
    ASSERT((m_index + numIndices) <= (UINT)m_heap.size());
    memcpy(&m_heap[m_index], in_indices.data(), sizeof(UINT) * numIndices);
    m_index += numIndices;
}
