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

#include "UploadAllocator.h"

//-----------------------------------------------------------------------------
// allocates simply by increasing/decreasing an index into an array of available indices
//-----------------------------------------------------------------------------
Streaming::SimpleAllocator::SimpleAllocator(UINT in_maxNumTiles) :
    m_index(0), m_heap(in_maxNumTiles)
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
// input is array sized to receive tile indices
// returns false and does no allocations if there wasn't space
//-----------------------------------------------------------------------------
bool Streaming::SimpleAllocator::Allocate(std::vector<UINT>& out_indices, UINT in_numTiles)
{
    bool result = false;

    if (m_index >= in_numTiles)
    {
        out_indices.resize(in_numTiles);
        m_index -= in_numTiles;
        memcpy(out_indices.data(), &m_heap[m_index], in_numTiles * sizeof(UINT));
        result = true;
    }

    return result;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Streaming::SimpleAllocator::Free(const std::vector<UINT>& in_indices)
{
    UINT numIndices = (UINT)in_indices.size();
    ASSERT(numIndices);
    ASSERT((m_index + numIndices) <= (UINT)m_heap.size());
    memcpy(&m_heap[m_index], in_indices.data(), sizeof(UINT) * numIndices);
    m_index += numIndices;
}

//-----------------------------------------------------------------------------
// UploadAllocator tracks tiles in an upload buffer
// relies on objects to return their indices when they are done
//-----------------------------------------------------------------------------
Streaming::UploadAllocator::UploadAllocator(ID3D12Device* in_pDevice, UINT in_maxNumTiles) :
    SimpleAllocator(in_maxNumTiles)
{
    const UINT uploadBufferSize = in_maxNumTiles * D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
    m_buffer.Allocate(in_pDevice, uploadBufferSize);
    m_buffer.m_resource->SetName(L"UploadAllocator::m_buffer");
}

//-----------------------------------------------------------------------------
// BufferAllocator tracks blocks in an buffer
// relies on objects to return their indices when they are done
//-----------------------------------------------------------------------------
Streaming::BufferAllocator::BufferAllocator(ID3D12Device* in_pDevice, UINT in_maxNumTiles, UINT in_blockSize) :
    SimpleAllocator(in_maxNumTiles)
{
    UINT bufferSize = in_maxNumTiles * in_blockSize;

    const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    in_pDevice->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_buffer));

    m_buffer->SetName(L"BufferAllocator::m_buffer");
}
