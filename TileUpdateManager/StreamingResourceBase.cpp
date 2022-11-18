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

#include "StreamingResourceBase.h"
#include "TileUpdateManagerSR.h"
#include "XeTexture.h"

#include "UpdateList.h"

#include "StreamingHeap.h"
#include "DataUploader.h"

/*-----------------------------------------------------------------------------
* Rules regarding order of operations:
*
* 1. A tile cannot be evicted (DecTileRef tries to set refcount = 0) if resident = 0 because a copy is pending
* 2. A tile cannot be loaded (AddTileRef tries to set refcount = 1) if resident = 1 because an eviction is pending
//---------------------------------------------------------------------------*/

//=============================================================================
// data structure to manage reserved resource
//=============================================================================
Streaming::StreamingResourceBase::StreamingResourceBase(
    // method that will fill a tile-worth of bits, for streaming
    const std::wstring& in_filename,
    Streaming::FileHandle* in_pFileHandle,
    // share upload buffers with other InternalResources
    Streaming::TileUpdateManagerSR* in_pTileUpdateManager,
    // share heap with other StreamingResources
    Streaming::Heap* in_pHeap) :
    m_readbackIndex(0)
    , m_pTileUpdateManager(in_pTileUpdateManager)
    , m_queuedFeedback(in_pTileUpdateManager->GetNumSwapBuffers())
    // delay eviction by enough to not affect a pending frame
    , m_pendingEvictions(in_pTileUpdateManager->GetNumSwapBuffers() + 1)
    , m_pHeap(in_pHeap)
    , m_pFileHandle(in_pFileHandle)
    , m_filename(in_filename)
{
    m_pTextureFileInfo = std::make_unique<Streaming::XeTexture>(in_filename);
    m_resources = std::make_unique<Streaming::InternalResources>(in_pTileUpdateManager->GetDevice(), m_pTextureFileInfo.get(), (UINT)m_queuedFeedback.size());
    m_tileMappingState.Init(m_resources->GetPackedMipInfo().NumStandardMips, m_resources->GetTiling());

    // no packed mips. odd, but possible. no need to check/update this variable again.
    if (0 == m_resources->GetPackedMipInfo().NumTilesForPackedMips)
    {
        m_packedMipStatus = PackedMipStatus::RESIDENT;
    }

    // initialize a structure that holds ref counts with dimensions equal to min-mip-map
    // set the bottom-most bits, representing the packed mips as being resident
    m_tileReferencesWidth = m_resources->GetNumTilesWidth();
    m_tileReferencesHeight = m_resources->GetNumTilesHeight();
    m_maxMip = UINT8(m_tileMappingState.GetNumSubresources());

    // there had better be standard mips, otherwise, why stream?
    ASSERT(m_maxMip);

    m_tileReferences.resize(m_tileReferencesWidth * m_tileReferencesHeight, m_maxMip);
    m_minMipMap.resize(m_tileReferences.size(), m_maxMip);

    // make sure my heap has an atlas corresponding to my format
    m_pHeap->AllocateAtlas(in_pTileUpdateManager->GetMappingQueue(), m_pTextureFileInfo->GetFormat());

    // Load packed mips. packed mips are not streamed or evicted.
    LoadPackedMips();
}

//-----------------------------------------------------------------------------
// destroy this object
// Free() all heap allocations, then release the heap
//-----------------------------------------------------------------------------
Streaming::StreamingResourceBase::~StreamingResourceBase()
{
    // do not delete StreamingResource between BeginFrame() and EndFrame(). It's complicated.
    ASSERT(!m_pTileUpdateManager->GetWithinFrame());

    // other threads are manipulating the eviction and load arrays. stop them.
    m_pTileUpdateManager->Finish();

    // remove this object's allocations from the heap, which might be shared
    m_tileMappingState.FreeHeapAllocations(m_pHeap);

    // debug message workaround if exit before packed mips load, or no mips
    if (m_packedMipHeapIndices.size())
    {
        m_pHeap->GetAllocator().Free(m_packedMipHeapIndices);
    }

    m_pendingEvictions.Clear();
    m_pendingTileLoads.clear();

    // tell TileUpdateManager to stop tracking
    m_pTileUpdateManager->Remove(this);
}

//-----------------------------------------------------------------------------
// when the StreamingResource gets an offset into the shared ResidencyMap,
// it can be initialized to the current minmipmap state
//-----------------------------------------------------------------------------
void Streaming::StreamingResourceBase::SetResidencyMapOffsetBase(UINT in_residencyMapOffsetBase)
{
    m_residencyMapOffsetBase = in_residencyMapOffsetBase;

    auto& outBuffer = m_pTileUpdateManager->GetResidencyMap();
    UINT8* pResidencyMap = m_residencyMapOffsetBase + (UINT8*)outBuffer.GetData();
    memcpy(pResidencyMap, m_minMipMap.data(), m_minMipMap.size());
}

//-----------------------------------------------------------------------------
// Upload or Evict tiles to match the incoming requested minimum mip
// if fails to adjust tile reference, then sets out_needRetry = true. Unchanged otherwise.
//-----------------------------------------------------------------------------
void Streaming::StreamingResourceBase::SetMinMip(UINT8 in_current, UINT in_x, UINT in_y, UINT in_s)
{
    // what mip level is currently referenced at this tile?
    UINT8 s = in_current;

    // addref mips we want
    // AddRef()s are ordered from bottom mip to top (all dependencies will be loaded first)
    while (s > in_s)
    {
        s -= 1; // already have "this" tile. e.g. have s == 1, desired in_s == 0, start with 0.
        AddTileRef(in_x >> s, in_y >> s, s);
    }

    // decref mips we don't need
    // DecRef()s are ordered from top mip to bottom (evict lower resolution tiles after all higher resolution ones)
    while (s < in_s)
    {
        DecTileRef(in_x >> s, in_y >> s, s);
        s++;
    }
}

//-----------------------------------------------------------------------------
// add to refcount for a tile
// if first time, add tile to list of pending loads
//-----------------------------------------------------------------------------
void Streaming::StreamingResourceBase::AddTileRef(UINT in_x, UINT in_y, UINT in_s)
{
    auto& refCount = m_tileMappingState.GetRefCount(in_x, in_y, in_s);

    // if refcount is 0xffff... then adding to it will wrap around. shouldn't happen.
    ASSERT(~refCount);

    // need to allocate?
    if (0 == refCount)
    {
        m_pendingTileLoads.push_back(D3D12_TILED_RESOURCE_COORDINATE{ in_x, in_y, 0, in_s });
    }
    refCount++;
}

//-----------------------------------------------------------------------------
// reduce ref count
// if 0, add tile to list of pending evictions
//-----------------------------------------------------------------------------
void Streaming::StreamingResourceBase::DecTileRef(UINT in_x, UINT in_y, UINT in_s)
{
    auto& refCount = m_tileMappingState.GetRefCount(in_x, in_y, in_s);

    ASSERT(0 != refCount);

    // last refrence? try to evict
    if (1 == refCount)
    {
        // queue up a decmapping request that will release the heap index after mapping and clear the resident flag
        m_pendingEvictions.Append(D3D12_TILED_RESOURCE_COORDINATE{ in_x, in_y, 0, in_s });
    }
    refCount--;
}

//-----------------------------------------------------------------------------
// initialize data structure afther creating the reserved resource and querying its tiling properties
//-----------------------------------------------------------------------------
void Streaming::StreamingResourceBase::TileMappingState::Init(UINT in_numMips, const D3D12_SUBRESOURCE_TILING* in_pTiling)
{
    ASSERT(in_numMips);
    m_refcounts.resize(in_numMips);
    m_heapIndices.resize(in_numMips);
    m_resident.resize(in_numMips);

    for (UINT mip = 0; mip < in_numMips; mip++)
    {
        UINT width = in_pTiling[mip].WidthInTiles;
        UINT height = in_pTiling[mip].HeightInTiles;
        m_refcounts[mip].resize(height);
        m_heapIndices[mip].resize(height);
        m_resident[mip].resize(height);

        for (auto& row : m_refcounts[mip])
        {
            row.assign(width, 0);
        }
        for (auto& row : m_heapIndices[mip])
        {
            row.assign(width, TileMappingState::InvalidIndex);
        }
        for (auto& row : m_resident[mip])
        {
            row.assign(width, 0);
        }
    }
}

//-----------------------------------------------------------------------------
// remove all allocations from the (shared) heap
//-----------------------------------------------------------------------------
void Streaming::StreamingResourceBase::TileMappingState::FreeHeapAllocations(Streaming::Heap* in_pHeap)
{
    for (auto& layer : m_heapIndices)
    {
        for (auto& row : layer)
        {
            for (auto& i : row)
            {
                if (TileMappingState::InvalidIndex != i)
                {
                    in_pHeap->GetAllocator().Free(i);
                    i = TileMappingState::InvalidIndex;
                }
            }
        }
    }
}

//-----------------------------------------------------------------------------
// search bottom layer. if refcount of any is positive, there is something resident.
//-----------------------------------------------------------------------------
bool Streaming::StreamingResourceBase::TileMappingState::GetAnyRefCount()
{
    auto& lastMip = m_refcounts.back();
    for (const auto& y : lastMip)
    {
        for (const auto& x : y)
        {
            if (x)
            {
                return true;
            }
        }
    }
    return false;
}

//-----------------------------------------------------------------------------
// optimization for UpdateMinMipMap:
// return true if all bottom layer standard tiles are resident
// FIXME? currently just checks the lowest tracked mip.
//-----------------------------------------------------------------------------
UINT8 Streaming::StreamingResourceBase::TileMappingState::GetMinResidentMip()
{
    UINT8 minResidentMip = (UINT8)m_resident.size();

    auto& lastMip = m_resident.back();
    for (const auto& y : lastMip)
    {
        for (const auto& x : y)
        {
            if (TileMappingState::Residency::Resident != x)
            {
                return minResidentMip;
            }
        }
    }
    return minResidentMip - 1;
}

//-----------------------------------------------------------------------------
// if the residency changes, must also notify TUM
//-----------------------------------------------------------------------------
void Streaming::StreamingResourceBase::SetResidencyChanged()
{
    m_tileResidencyChanged = true;
    m_pTileUpdateManager->SetResidencyChanged();
}

//-----------------------------------------------------------------------------
// called once per frame
// adds virtual memory updates to command queue
// queues memory content updates to copy thread
// Algorithm: evict then load tiles
//            loads lower mip dependencies first
// e.g. if we need tile 0,0,0 then 0,0,1 must have previously been loaded
//-----------------------------------------------------------------------------
void Streaming::StreamingResourceBase::ProcessFeedback(UINT64 in_frameFenceCompletedValue)
{
    // handle (some) pending evictions
    m_pendingEvictions.NextFrame();

    bool changed = false;
    const UINT width = GetNumTilesWidth();
    const UINT height = GetNumTilesHeight();

    if (m_setZeroRefCounts)
    {
        m_setZeroRefCounts = false;

        // has this resource already been zeroed? don't clear again, early exit
        // this is set to false if "changed" due to feedback below
        if (m_refCountsZero)
        {
            return;
        }
        m_refCountsZero = true;

        // all prior feedback is irrelevant
        for (auto& f : m_queuedFeedback)
        {
            f.m_feedbackQueued = false;
        }

        // since we're evicting everything, don't need to loop over the reference count structure
        // just set it all to max mip, then schedule eviction any tiles that have refcounts

        // set everything to max mip
        memset(m_tileReferences.data(), m_maxMip, m_tileReferences.size());

        // queue all resident tiles for eviction
        for (UINT flipS = 0; flipS < m_maxMip; flipS++)
        {
            UINT s = (m_maxMip - 1) - flipS; // traverse bottom up. ok because everything will be evicted
            bool noTiles = true; // if no tiles on this mip layer, won't be any tiles on higher-res mip layers

            for (UINT y = 0; y < m_tileMappingState.GetHeight(s); y++)
            {
                for (UINT x = 0; x < m_tileMappingState.GetWidth(s); x++)
                {
                    auto& refCount = m_tileMappingState.GetRefCount(x, y, s);
                    if (refCount)
                    {
                        noTiles = false;
                        changed = true;
                        refCount = 0;
                        m_pendingEvictions.Append(D3D12_TILED_RESOURCE_COORDINATE{ x, y, 0, s });
                    }
                }
            }
            if (noTiles)
            {
                break; // if refcount of all tiles on this layer = 0, early out
            }
        }

        // abandon all pending loads - all refcounts are 0
        m_pendingTileLoads.clear();
    }
    else
    {
        UINT feedbackIndex = 0;

        //------------------------------------------------------------------
        // determine if there is feedback to process
        // if there is more than one feedback ready to process (unlikely), only use the most recent one
        //------------------------------------------------------------------
        {
            bool feedbackFound = false;
            UINT64 latestFeedbackFenceValue = 0;
            for (UINT i = 0; i < (UINT)m_queuedFeedback.size(); i++)
            {
                if (m_queuedFeedback[i].m_feedbackQueued)
                {
                    UINT64 feedbackFenceValue = m_queuedFeedback[i].m_renderFenceForFeedback;
                    if ((in_frameFenceCompletedValue >= feedbackFenceValue) &&
                        ((!feedbackFound) || (latestFeedbackFenceValue <= feedbackFenceValue)))
                    {
                        feedbackFound = true;
                        feedbackIndex = i;
                        latestFeedbackFenceValue = feedbackFenceValue;

                        // this feedback will either be used or skipped. either way it is "consumed"
                        m_queuedFeedback[i].m_feedbackQueued = false;
                    }
                }
            }

            // no new feedback?
            if (!feedbackFound)
            {
                return;
            }
        }

        //------------------------------------------------------------------
        // update the refcount of each tile based on feedback
        //------------------------------------------------------------------
        {
            // mapped host feedback buffer
            UINT8* pResolvedData = nullptr;
            ID3D12Resource* pResolvedResource = m_resources->GetResolvedReadback(feedbackIndex);
            pResolvedResource->Map(0, nullptr, (void**)&pResolvedData);

            TileReference* pTileRow = m_tileReferences.data();
            for (UINT y = 0; y < height; y++)
            {
                for (UINT x = 0; x < width; x++)
                {
                    // clamp to the maximum we are tracking (not tracking packed mips)
                    UINT8 desired = std::min(pResolvedData[x], m_maxMip);
                    UINT8 initialValue = pTileRow[x];
                    if (desired != initialValue) { changed = true; }
                    SetMinMip(initialValue, x, y, desired);
                    pTileRow[x] = desired;
                } // end loop over x
                pTileRow += width;

#if RESOLVE_TO_TEXTURE
                pResolvedData += (width + 0x0ff) & ~0x0ff;
#else
                pResolvedData += width;
#endif

            } // end loop over y

            D3D12_RANGE emptyRange{ 0,0 };
            pResolvedResource->Unmap(0, &emptyRange);
        }

        // if there was a change, then it's no longer "zeroed"
        if (changed)
        {
            m_refCountsZero = false;
        }

        // abandon pending loads that are no longer relevant
        AbandonPendingLoads();

        // clear pending evictions that are no longer relevant
        m_pendingEvictions.Rescue(m_tileMappingState);
    }

    // update min mip map to adjust to new references
    // required so the eviction timeout is relative to the current expected mapping
    if (changed)
    {
        SetResidencyChanged();
    }
}

//-----------------------------------------------------------------------------
// drop pending loads that are no longer relevant
//-----------------------------------------------------------------------------
void Streaming::StreamingResourceBase::AbandonPendingLoads()
{
    UINT numPending = (UINT)m_pendingTileLoads.size();
    for (UINT i = 0; i < numPending;)
    {
        auto& c = m_pendingTileLoads[i];
        if (m_tileMappingState.GetRefCount(c))
        {
            i++; // refcount still non-0, still want to load this tile
        }
        // on abandon, swap a later tile in and re-try the check
        // this re-orders the queue, but we can tolerate that
        // because the residency map is built bottom-up
        else
        {
            numPending--;
            c = m_pendingTileLoads[numPending];
        }
    }
    m_pendingTileLoads.resize(numPending);
}

//-----------------------------------------------------------------------------
// submit evictions and loads to be processed
//
// note: queues as many new tiles as possible
//-----------------------------------------------------------------------------
UINT Streaming::StreamingResourceBase::QueueTiles()
{
    UINT uploadsRequested = 0;

    // pushes as many tiles as it can into a single UpdateList
    if (m_pendingTileLoads.size() && m_pHeap->GetAllocator().GetAvailable())
    {
        UpdateList scratchUL;

        // queue as many new tiles as possible
        QueuePendingTileLoads(&scratchUL);
        uploadsRequested = (UINT)scratchUL.m_coords.size(); // number of uploads in UpdateList

        // only allocate an UpdateList if we have updates
        if (scratchUL.m_coords.size())
        {
            // calling function checked for availability, so UL allocation must succeed
            UpdateList* pUpdateList = m_pTileUpdateManager->AllocateUpdateList(this);
            ASSERT(pUpdateList);

            pUpdateList->m_coords.swap(scratchUL.m_coords);
            pUpdateList->m_heapIndices.swap(scratchUL.m_heapIndices);

            m_pTileUpdateManager->SubmitUpdateList(*pUpdateList);
        }
    }

    return uploadsRequested;
}

/*-----------------------------------------------------------------------------
This technique depends on a logic table that prevents race conditions

The table has two halves:
1) refcount > 0 is handled by loads. pending loads with refcount = 0 are dropped ("abandoned")
2) refcount == 0 is handled by evictions. pending evictions with refcount > 0 are dropped ("rescued")

ProcessFeedback() scans pending loads and pending evictions prune the drops above.

The logic table for evictions:

    ref count | heap index | resident | action
    ----------+------------+----------+--------
        0     |  invalid   |    0     | drop (tile already not resident)
        0     |  invalid   |    1     | drop (tile already has pending eviction)
        0     |   valid    |    0     | delay (tile has pending load, wait for it to complete)
        0     |   valid    |    1     | evict (tile is resident, so can be evicted)

The logic table for loads:

    ref count | heap index | resident | action
    ----------+------------+----------+--------
        n     |  invalid   |    0     | load (tile not resident, so can be loaded)
        n     |  invalid   |    1     | delay (tile has pending eviction, wait for it to complete)
        n     |   valid    |    0     | drop (tile already has pending load)
        n     |   valid    |    1     | drop (tile already resident)

Residency is set by the notification functions called by DataUploader (a separate thread)
Allocating and freeing heap indices is handled respectively by the load and eviction routines below
Note that the multi-frame delay for evictions prevents allocation of an index that is in-flight for a different tile

-----------------------------------------------------------------------------*/

//-----------------------------------------------------------------------------
// evict unused tiles
// additional logic to observe that everything can be evicted
//
// note there are only tiles to evict after processing feedback, but it's possible
// there was no UpdateList available at the time, so they haven't been evicted yet.
//-----------------------------------------------------------------------------
UINT Streaming::StreamingResourceBase::QueuePendingTileEvictions()
{
    if (0 == m_pendingEvictions.GetReadyToEvict().size()) { return 0; }

    auto& pendingEvictions = m_pendingEvictions.GetReadyToEvict();

    UINT numDelayed = 0;
    UINT numEvictions = 0;
    for (auto& coord : pendingEvictions)
    {
        // if the heap index is valid, but the tile is not resident, there's a /pending load/
        // a pending load might be streaming OR it might be in the pending list
        // if in the pending list, we will observe if the refcount is 0 and abandon the load

        // NOTE! assumes refcount is 0
        // ProcessFeedback() clears all pending evictions with refcount > 0
        // Hence, ProcessFeedback() must be called before this function
        ASSERT(0 == m_tileMappingState.GetRefCount(coord));

        auto residency = m_tileMappingState.GetResidency(coord);
        if (TileMappingState::Residency::Resident == residency)
        {
            // NOTE: effectively removed "Evicting." Now remove tiles from data structure, not from memory mapping.
            // result is improved perf from fewer UpdateTileMappings() calls.
            // existing artifacts (cracks when sampler crosses tile boundaries) are "no worse"
            // to put it back: set residency to evicting and add tiles to updatelist for eviction

            m_tileMappingState.SetResidency(coord, TileMappingState::Residency::NotResident);
            UINT& heapIndex = m_tileMappingState.GetHeapIndex(coord);
            m_pHeap->GetAllocator().Free(heapIndex);
            heapIndex = TileMappingState::InvalidIndex;

            numEvictions++;
        }
        // valid index but not resident means there is a pending load, do not evict
        // try again later
        else if (TileMappingState::Residency::Loading == residency)
        {
            pendingEvictions[numDelayed] = coord;
            numDelayed++;
        }
        // if evicting or not resident, drop

        // else: refcount positive or eviction already in progress? rescue this eviction (by not adding to pending evictions)
    }

    if (numEvictions)
    {
        SetResidencyChanged();
    }

    // replace the ready evictions with just the delayed evictions.
    pendingEvictions.resize(numDelayed);
    return numEvictions;
}

//-----------------------------------------------------------------------------
// queue one UpdateList worth of uploads
// FIFO order: work from the front of the array
// NOTE: greedy, takes every available UpdateList if it can
//-----------------------------------------------------------------------------
void Streaming::StreamingResourceBase::QueuePendingTileLoads(Streaming::UpdateList* out_pUpdateList)
{
    ASSERT(out_pUpdateList);
    ASSERT(m_pHeap->GetAllocator().GetAvailable());

    // clamp to heap availability
    UINT maxCopies = std::min((UINT)m_pendingTileLoads.size(), m_pHeap->GetAllocator().GetAvailable());

    UINT skippedIndex = 0;
    UINT numConsumed = 0;
    for (auto& coord : m_pendingTileLoads)
    {
        numConsumed++;

        // if the heap index is not valid, but the tile is resident, there's a /pending eviction/
        // a pending eviction might be streaming

        // NOTE! assumes refcount is non-zero
        // ProcessFeedback() clears all pending loads with refcount == 0
        // Hence, ProcessFeedback() must be called before this function
        ASSERT(m_tileMappingState.GetRefCount(coord));

        auto residency = m_tileMappingState.GetResidency(coord);
        // only load if definitely not resident
        if (TileMappingState::Residency::NotResident == residency)
        {
            UINT heapIndex = m_pHeap->GetAllocator().Allocate();

            m_tileMappingState.SetResidency(coord, TileMappingState::Residency::Loading);
            m_tileMappingState.GetHeapIndex(coord) = heapIndex;

            out_pUpdateList->m_coords.push_back(coord);
            out_pUpdateList->m_heapIndices.push_back(heapIndex);

            // limit # of copies in a single updatelist
            maxCopies--;
            if (0 == maxCopies)
            {
                break;
            }
        }
        // if there is a pending eviction, do not load. Try again later.
        else if (TileMappingState::Residency::Evicting == residency)
        {
            // accumulate skipped tiles at front of the pending list
            m_pendingTileLoads[skippedIndex] = coord;
            skippedIndex++;
        }
        // if loading or resident, drop

        // else: refcount 0 or tile was rescued by QueuePendingTileEvictions()? abandon this load. also drops duplicate adds.
    }

    // delete consumed tiles, which are in-between the skipped tiles and the still-pending tiles
    if (numConsumed)
    {
        m_pendingTileLoads.erase(m_pendingTileLoads.begin() + skippedIndex, m_pendingTileLoads.begin() + numConsumed);
    }
}

//-----------------------------------------------------------------------------
// TileUpdateManager calls this for every object sharing its resources
// if something has changed: traverses residency status, generates min mip map, writes to upload buffer
// returns true if update performed, indicating TUM should upload the results
//-----------------------------------------------------------------------------
void Streaming::StreamingResourceBase::UpdateMinMipMap()
{
    // m_tileResidencyChanged is an atomic that forms a happens-before relationship between this thread and DataUploader Notify* routines
    // m_tileResidencyChanged is also set when ClearAll() evicts everything

    if (!m_tileResidencyChanged) return;

    m_tileResidencyChanged = false;

    // FIXME? sometimes the notifications come out-of-order
    //ASSERT(m_packedMipsResident);

    auto& outBuffer = m_pTileUpdateManager->GetResidencyMap();
    UINT8* pResidencyMap = m_residencyMapOffsetBase + (UINT8*)outBuffer.GetData();

    if (m_tileMappingState.GetAnyRefCount())
    {
        const UINT width = GetNumTilesWidth();
        const UINT height = GetNumTilesHeight();

#if 0
        // FIXME? if the optimization below introduces artifacts, this might work:
        const UINT8 minResidentMip = (UINT8)m_tileMappingState.GetNumSubresources();
#else
        // a simple optimization that's especially effective for large textures
        // and harmless for smaller ones:
        // find the minimum fully-resident mip
        const UINT8 minResidentMip = m_tileMappingState.GetMinResidentMip();
#endif
        // Search bottom up for best mip
        // tiles that have refcounts may still have pending copies, so we have to check residency (can't just memcpy m_tileReferences)
        // note that tiles can load out of order, but the min mip map cannot have holes, so exit if any lower-res tile is absent
        // for 16kx16k textures, that's 7-1 iterations maximum (maximum for bc7: 64*64*(7-1)=24576, bc1: 32*64*(6-1)=10240)
        // in practice don't expect to hit the maximum, as the entire texture would have to be loaded
        // FIXME? could probably optimize e.g. by vectorizing
        UINT tileIndex = 0;
        for (UINT y = 0; y < height; y++)
        {
            for (UINT x = 0; x < width; x++)
            {
                // mips >= maxmip are pre-loaded packed mips and not tracked
                // leverage results from previous frame. in the static case, this should # iterations down to exactly # regions
                UINT8 s = std::max(minResidentMip, m_minMipMap[tileIndex]);
                UINT8 minMip = s;

                // note: it's ok for a region of the min mip map to include a higher-resolution region than feedback required
                // the min mip map will be updated on evictions, which will affect "this" region and the referencing region for the tile
                while (s > 0)
                {
                    s--;
                    if ((TileMappingState::Residency::Resident == m_tileMappingState.GetResidency(x >> s, y >> s, s)) &&
                        // do not include a tile that may be evicted (resident with 0 refcount is candidate for eviction)
                        (0 != m_tileMappingState.GetRefCount(x >> s, y >> s, s)))
                    {
                        minMip = s;
                    }
                    else
                    {
                        break;
                    }
                }
                m_minMipMap[tileIndex] = minMip;
                tileIndex++;
            } // end y
        } // end x
    }
    // if we know that only packed mips are resident, then write a basic residency map
    // if refcount is 0, then tile state is either not resident or eviction pending
    else
    {
        memset(m_minMipMap.data(), m_maxMip, m_minMipMap.size());
    }
    memcpy(pResidencyMap, m_minMipMap.data(), m_minMipMap.size());
}

//=============================================================================
// class used to delay decmaps by a number of frames = # swap buffers
// easy way to prevent decmapping an in-flight tile
//=============================================================================
Streaming::StreamingResourceBase::EvictionDelay::EvictionDelay(UINT in_numSwapBuffers)
{
    m_mappings.resize(in_numSwapBuffers);
}

//-----------------------------------------------------------------------------
// step pending evictions once per frame
//-----------------------------------------------------------------------------
void Streaming::StreamingResourceBase::EvictionDelay::NextFrame()
{
    // start with A, B, C
    // after swaps, have C, A, B
    // then insert C, A, BC
    // then clear 0, A, BC
    UINT lastMapping = (UINT)m_mappings.size() - 1;
    for (UINT i = lastMapping; i > 0; i--)
    {
        m_mappings[i].swap(m_mappings[i - 1]);
    }
    m_mappings.back().insert(m_mappings.back().end(), m_mappings[0].begin(), m_mappings[0].end());
    m_mappings[0].clear();
}

//-----------------------------------------------------------------------------
// dump all pending evictions. return heap indices to heap
//-----------------------------------------------------------------------------
void Streaming::StreamingResourceBase::EvictionDelay::Clear()
{
    for (auto& i : m_mappings)
    {
        i.clear();
    }
}

//-----------------------------------------------------------------------------
// drop pending evictions for tiles that now have non-zero refcount
//-----------------------------------------------------------------------------
void Streaming::StreamingResourceBase::EvictionDelay::Rescue(const Streaming::StreamingResourceBase::TileMappingState& in_tileMappingState)
{
    // note: it is possible even for the most recent evictions to have refcount > 0
    // because a tile can be evicted then loaded again within a single ProcessFeedback() call
    for (auto& evictions : m_mappings)
    {
        UINT numPending = (UINT)evictions.size();
        for (UINT i = 0; i < numPending;)
        {
            auto& c = evictions[i];
            // on rescue, swap a later tile in and re-try the check
            // this re-orders the queue, but we can tolerate that
            // because the residency map is built bottom-up
            if (in_tileMappingState.GetRefCount(c))
            {
                numPending--;
                c = evictions[numPending];
            }
            else // refcount still 0, this tile may still be evicted
            {
                i++;
            }
        }
        evictions.resize(numPending);
    }
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Streaming::StreamingResourceBase::LoadPackedMips()
{
    UINT numBytes = 0;
    UINT offset = m_pTextureFileInfo->GetPackedMipFileOffset(&numBytes, &m_packedMipsUncompressedSize);
    m_packedMips.resize(numBytes);
    std::ifstream inFile(m_filename.c_str(), std::ios::binary);
    inFile.seekg(offset);
    inFile.read((char*)m_packedMips.data(), numBytes);
    inFile.close();
}

//-----------------------------------------------------------------------------
// called when creating/changing FileStreamer
//-----------------------------------------------------------------------------
void Streaming::StreamingResourceBase::SetFileHandle(const DataUploader* in_pDataUploader)
{
    m_pFileHandle.reset(in_pDataUploader->OpenFile(m_filename));
}

//-----------------------------------------------------------------------------
// set mapping and initialize bits for the packed tile(s)
//-----------------------------------------------------------------------------
bool Streaming::StreamingResourceBase::InitPackedMips()
{
    // nothing to do if the copy has been requested
    // return true if ready to sample
    if ((UINT)m_packedMipStatus >= (UINT)PackedMipStatus::REQUESTED)
    {
        return true;
    }

    // allocate heap space
    // only allocate if all required tiles can be allocated at once
    if ((PackedMipStatus::HEAP_RESERVED > m_packedMipStatus) &&
        (m_pHeap->GetAllocator().GetAvailable() >= m_resources->GetPackedMipInfo().NumTilesForPackedMips))
    {
        m_pHeap->GetAllocator().Allocate(m_packedMipHeapIndices, m_resources->GetPackedMipInfo().NumTilesForPackedMips);
        m_packedMipStatus = PackedMipStatus::HEAP_RESERVED;
    }

    ASSERT(m_packedMipHeapIndices.size() == m_resources->GetPackedMipInfo().NumTilesForPackedMips);

    // attempt to upload by acquiring an update list. may take many tries.
    Streaming::UpdateList* pUpdateList = m_pTileUpdateManager->AllocateUpdateList(this);

    if (pUpdateList)
    {
        pUpdateList->m_heapIndices = m_packedMipHeapIndices;
        m_pTileUpdateManager->SubmitUpdateList(*pUpdateList);

        m_packedMipStatus = PackedMipStatus::REQUESTED;
        return true;
    }

    return false;
}

//-----------------------------------------------------------------------------
// FIXME? could handle packed mips completely separate
// NOTE: this query will only return true one time
//-----------------------------------------------------------------------------
bool Streaming::StreamingResourceBase::GetPackedMipsNeedTransition()
{
    if (PackedMipStatus::NEEDS_TRANSITION == m_packedMipStatus)
    {
        m_packedMipStatus = PackedMipStatus::RESIDENT;
        return true;
    }

    return false;
}

void Streaming::StreamingResourceBase::ClearFeedback(ID3D12GraphicsCommandList* in_pCmdList, const D3D12_GPU_DESCRIPTOR_HANDLE in_gpuDescriptor)
{
    m_resources->ClearFeedback(in_pCmdList, in_gpuDescriptor);
}

//-----------------------------------------------------------------------------
// command to resolve feedback to the appropriate non-opaque buffer
//-----------------------------------------------------------------------------
void Streaming::StreamingResourceBase::ResolveFeedback(ID3D12GraphicsCommandList1* out_pCmdList)
{
    // move to next readback index
    m_readbackIndex = (m_readbackIndex + 1) % m_queuedFeedback.size();

    // remember that feedback was queued, and which frame it was queued in.
    auto& f = m_queuedFeedback[m_readbackIndex];
    f.m_renderFenceForFeedback = m_pTileUpdateManager->GetFrameFenceValue();
    f.m_feedbackQueued = true;

    m_resources->ResolveFeedback(out_pCmdList, m_readbackIndex);
}

#if RESOLVE_TO_TEXTURE
//-----------------------------------------------------------------------------
// call after resolving to read back to CPU
//-----------------------------------------------------------------------------
void Streaming::StreamingResourceBase::ReadbackFeedback(ID3D12GraphicsCommandList* out_pCmdList)
{
    // write readback command to command list if resolving to texture
    m_resources->ReadbackFeedback(out_pCmdList, m_readbackIndex);
}
#endif

//-----------------------------------------------------------------------------
// eject all tiles and remove mappings into heap
//-----------------------------------------------------------------------------
void Streaming::StreamingResourceBase::ClearAllocations()
{
    ASSERT(!m_pTileUpdateManager->GetWithinFrame());

    m_pTileUpdateManager->Finish();

    m_tileMappingState.FreeHeapAllocations(m_pHeap);
    m_tileMappingState.Init(m_resources->GetPackedMipInfo().NumStandardMips, m_resources->GetTiling());
    m_tileReferences.assign(m_tileReferences.size(), m_maxMip);
    m_minMipMap.assign(m_minMipMap.size(), m_maxMip);

    m_pendingEvictions.Clear();
    m_pendingTileLoads.clear();

    // want to upload a residency of all maxMip
    // NOTE: UpdateMinMipMap() will see there are no tiles resident,
    //       clear all tile mappings, and upload a cleared min mip map
    SetResidencyChanged();
}
