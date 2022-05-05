//*********************************************************
//
// Copyright 2022 Intel Corporation 
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

#include "AssetUploader.h"
#include "DebugHelper.h"
#include "d3dx12.h"

using Microsoft::WRL::ComPtr;

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void AssetUploader::Init(ID3D12Device* in_pDevice)
{
	ComPtr<IDStorageFactory> factory;
	ThrowIfFailed(DStorageGetFactory(IID_PPV_ARGS(&factory)));

    DSTORAGE_QUEUE_DESC queueDesc{};
    queueDesc.Capacity = DSTORAGE_MAX_QUEUE_CAPACITY;
    queueDesc.Priority = DSTORAGE_PRIORITY_NORMAL;
    queueDesc.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
    queueDesc.Device = in_pDevice;
    ThrowIfFailed(factory->CreateQueue(&queueDesc, IID_PPV_ARGS(&m_memoryQueue)));

    in_pDevice->CreateFence(m_fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
    m_event = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

//-----------------------------------------------------------------------------
// FIXME? only buffers supported.
//-----------------------------------------------------------------------------
AssetUploader::Request::Request(ID3D12Resource* in_pDstResource,
    D3D12_RESOURCE_STATES in_before, D3D12_RESOURCE_STATES in_after) :
    m_before(in_before), m_after(in_after)
{
    m_pResource = in_pDstResource;

    auto desc = in_pDstResource->GetDesc();
    ASSERT(D3D12_RESOURCE_DIMENSION_BUFFER == desc.Dimension);

    m_data.resize(GetRequiredIntermediateSize(in_pDstResource, 0, desc.MipLevels));
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void AssetUploader::SubmitRequest(Request* in_pRequest)
{
    ASSERT(D3D12_RESOURCE_DIMENSION_BUFFER == in_pRequest->GetResource()->GetDesc().Dimension);
    m_requests.push_back(in_pRequest);

    DSTORAGE_REQUEST request = {};
    request.UncompressedSize = (UINT32)in_pRequest->GetBuffer().size();
    request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_MEMORY;
    request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_BUFFER;
    request.Source.Memory.Source = in_pRequest->GetBuffer().data();
    request.Source.Memory.Size = request.UncompressedSize; // uncompressed upload
    request.Destination.Buffer.Offset = 0;
    request.Destination.Buffer.Size = request.Source.Memory.Size;
    request.Destination.Buffer.Resource = in_pRequest->GetResource();

    m_memoryQueue->EnqueueRequest(&request);
}

//-----------------------------------------------------------------------------
// wait for any in-flight uploads to complete, then free scratch data
//-----------------------------------------------------------------------------
void AssetUploader::WaitForInFlight()
{
    if (m_inFlight.size())
    {
        // wait for previous command to complete before freeing that scratch memory
        if (m_fence->GetCompletedValue() < m_fenceValue)
        {
            ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValue, m_event));
            WaitForSingleObject(m_event, INFINITE);
        }

        // free scratch space
        for (auto p : m_inFlight)
        {
            delete p;
        }
        m_inFlight.clear();
    }
}

//-----------------------------------------------------------------------------
// waits for previous call to complete
// enqueues fence. submits DS work.
// adds Wait() on fence to queue. adds transition barriers to command list.
//-----------------------------------------------------------------------------
void AssetUploader::WaitForUploads(ID3D12CommandQueue* in_pDependentQueue, ID3D12GraphicsCommandList* in_pCommandList)
{
    WaitForInFlight();

    // submit current work and set up cross-queue fence/wait
    if (m_requests.size())
    {
        m_fenceValue++;
        m_memoryQueue->EnqueueSignal(m_fence.Get(), m_fenceValue);
        m_memoryQueue->Submit();

        in_pDependentQueue->Wait(m_fence.Get(), m_fenceValue);

        std::vector<D3D12_RESOURCE_BARRIER> barriers;

        // free scratch space
        for (auto p : m_requests)
        {
            barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(p->GetResource(), p->GetBefore(), p->GetAfter()));
        }
        in_pCommandList->ResourceBarrier((UINT)barriers.size(), barriers.data());
        
        // don't delete in-flight scratch data until commands complete
        m_inFlight.swap(m_requests);
    }
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
AssetUploader::~AssetUploader()
{
    WaitForInFlight();

    ASSERT(0 == m_requests.size());
    CloseHandle(m_event);
}
