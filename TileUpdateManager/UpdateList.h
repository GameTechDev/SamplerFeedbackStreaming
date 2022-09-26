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

#include "Streaming.h"

namespace Streaming
{
    class StreamingResourceDU;
}

//==================================================
// UploadBuffer keeps an upload buffer per swapchain backbuffer
// and tracks occupancy of current buffer
// intended to allow multiple objects to share the same upload buffers
// NOTE: requires a "reset" per frame
//==================================================
namespace Streaming
{
    struct UpdateList
    {
        enum class State : std::uint32_t
        {
            // states used by the ProcessFeedback thread
            STATE_FREE,                  // available / unused
            STATE_ALLOCATED,             // allocated by StreamingResource for ProcessFeedback

            // statistics are gathered on a common thread
            STATE_SUBMITTED,             // start file i/o (DS) if necessary. if mapping only, go directly to notify

            STATE_UPLOADING,             // make sure the copy fence is valid, since copying and mapping can be concurrent
            STATE_MAP_PENDING,           // check for mapping complete

            STATE_PACKED_MAPPING,        // wait for packed mips to be mapped before uploading
            STATE_PACKED_COPY_PENDING    // wait for upload of packed mips to complete

        };

        // initialize to ready
        std::atomic<State> m_executionState{ State::STATE_FREE };
        std::atomic<bool> m_copyFenceValid{ false };

        // for the tiled resource, streaming info, and to notify complete
        Streaming::StreamingResourceDU* m_pStreamingResource{ nullptr };

        UINT64 m_copyFenceValue{ 0 };     // gpu copy fence
        UINT64 m_mappingFenceValue{ 0 };  // gpu mapping fence

        // tile loads:
        std::vector<D3D12_TILED_RESOURCE_COORDINATE> m_coords; // tile coordinates
        std::vector<UINT> m_heapIndices;                       // indices into shared heap (for mapping)

        // tile evictions:
        std::vector<D3D12_TILED_RESOURCE_COORDINATE> m_evictCoords;

        UINT GetNumStandardUpdates() const { return (UINT)m_coords.size(); }
        UINT GetNumEvictions() const { return (UINT)m_evictCoords.size(); }

        void Reset(Streaming::StreamingResourceDU* in_pStreamingResource);

        INT64 m_copyLatencyTimer{ 0 }; // used only to get an approximate latency for tile copies
    };
}
