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

#include "UpdateList.h"

//-----------------------------------------------------------------------------
// params: device to create command allocator, index into allocation heap, max # updates per UpdateList (global value)
//-----------------------------------------------------------------------------
void Streaming::UpdateList::Init(UINT in_maxNumUpdates)
{
    ASSERT(0 != in_maxNumUpdates);
    m_heapIndices.reserve(in_maxNumUpdates);
};

//-----------------------------------------------------------------------------
// called when the state changes FREE->ALLOCATED
//-----------------------------------------------------------------------------
void Streaming::UpdateList::Reset(Streaming::StreamingResourceDU* in_pStreamingResource)
{
    m_pStreamingResource = in_pStreamingResource;

    m_copyFenceValid = false;
    m_coords.clear();         // indicates standard tile map & upload
    m_heapIndices.clear();    // because AddUpdate() does a push_back()
    m_evictCoords.clear();    // indicates tiles to un-map
    m_numPackedMips = 0;      // indicates to map & load packed mips
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Streaming::UpdateList::AddPackedMipRequest(UINT in_numMips)
{
    ASSERT(UpdateList::State::STATE_ALLOCATED == m_executionState);
    ASSERT(0 == GetNumStandardUpdates());

    m_numPackedMips = in_numMips;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Streaming::UpdateList::AddUpdate(
    const D3D12_TILED_RESOURCE_COORDINATE& in_coord,
    UINT in_heapIndex)
{
    ASSERT(State::STATE_ALLOCATED == m_executionState);

    m_coords.push_back(in_coord);
    m_heapIndices.push_back(in_heapIndex);
}
