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

#include "SimpleAllocator.h"

//-----------------------------------------------------------------------------
// allocates simply by increasing/decreasing an index into an array of available indices
//-----------------------------------------------------------------------------
Streaming::SimpleAllocator::SimpleAllocator(UINT in_maxNumElements) :
    m_index(0), m_heap(in_maxNumElements)
{
    for (auto& i : m_heap)
    {
        i = m_index;
        m_index++;
    }
}

Streaming::SimpleAllocator::~SimpleAllocator()
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
// like above, but expects caller to have checked availability first and provided a safe destination
//-----------------------------------------------------------------------------
void Streaming::SimpleAllocator::Allocate(UINT* out_pIndices, UINT in_numIndices)
{
    ASSERT(m_index >= in_numIndices);
    m_index -= in_numIndices;
    memcpy(out_pIndices, &m_heap[m_index], in_numIndices * sizeof(UINT));
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Streaming::SimpleAllocator::Free(const UINT* in_pIndices, UINT in_numIndices)
{
    ASSERT(in_numIndices);
    ASSERT((m_index + in_numIndices) <= (UINT)m_heap.size());
    memcpy(&m_heap[m_index], in_pIndices, sizeof(UINT) * in_numIndices);
    m_index += in_numIndices;
}

//-----------------------------------------------------------------------------
// uses a lockless ringbuffer so allocate can be on a different thread than free
//-----------------------------------------------------------------------------
Streaming::AllocatorMT::AllocatorMT(UINT in_numElements) :
    m_ringBuffer(in_numElements), m_indices(in_numElements)
{
    for (UINT i = 0; i < in_numElements; i++)
    {
        m_indices[i] = i;
    }
}

Streaming::AllocatorMT::~AllocatorMT()
{
#ifdef _DEBUG
    ASSERT(0 == GetAllocated());
    // verify all indices accounted for and unique
    std::sort(m_indices.begin(), m_indices.end());
    for (UINT i = 0; i < (UINT)m_indices.size(); i++)
    {
        ASSERT(i == m_indices[i]);
    }
#endif
}

//-----------------------------------------------------------------------------
// multi-threaded allocator (single allocator, single releaser)
//-----------------------------------------------------------------------------
void Streaming::AllocatorMT::Allocate(UINT* out_pIndices, UINT in_numIndices)
{
    ASSERT(m_ringBuffer.GetAvailableToWrite() >= in_numIndices);
    UINT baseIndex = m_ringBuffer.GetWriteIndex();
    memcpy(out_pIndices, &m_indices[baseIndex], in_numIndices * sizeof(UINT));
    m_ringBuffer.Allocate(in_numIndices);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Streaming::AllocatorMT::Free(const UINT* in_pIndices, UINT in_numIndices)
{
    UINT baseIndex = m_ringBuffer.GetReadIndex();
    memcpy(&m_indices[baseIndex], in_pIndices, in_numIndices * sizeof(UINT));
    m_ringBuffer.Free(in_numIndices);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
UINT Streaming::AllocatorMT::Allocate()
{
    UINT baseIndex = m_ringBuffer.GetWriteIndex();
    UINT i = m_indices[baseIndex];
    m_ringBuffer.Allocate();
    return i;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Streaming::AllocatorMT::Free(UINT i)
{
    UINT baseIndex = m_ringBuffer.GetReadIndex();
    m_indices[baseIndex] = i;
    m_ringBuffer.Free();
}
