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
#pragma once

// Basic asset loader. gather all the things to upload, then upload them all at once
// Buffers only. Textures TBD. 32MB limit (unenforced)

#include <dstorage.h>
#include <vector>
#include <wrl.h>
#include "d3dx12.h"

class AssetUploader
{
public:
    void Init(ID3D12Device* in_pDevice);
    ~AssetUploader();

    void SubmitRequest(
        ID3D12Resource* in_pResource,
        const void* in_pData, const size_t in_dataSize,
        D3D12_RESOURCE_STATES in_before, D3D12_RESOURCE_STATES in_after);

    // submits outstanding data. Will block if previous call hasn't completed yet.
    void WaitForUploads(ID3D12CommandQueue* in_pDependentQueue, ID3D12GraphicsCommandList* in_pCommandList);
private:
    class Request
    {
    public:
        Request(ID3D12Resource* in_pResource, D3D12_RESOURCE_STATES in_before, D3D12_RESOURCE_STATES in_after);
        std::vector<BYTE>& GetBuffer() { return m_data; }
        ID3D12Resource* GetResource() { return m_pResource; }
        D3D12_RESOURCE_STATES GetBefore() const { return m_before; }
        D3D12_RESOURCE_STATES GetAfter() const { return m_after; }
    private:
        std::vector<BYTE> m_data; // data to be uploaded
        ID3D12Resource* m_pResource; // destination for request

        // need state transition barriers after upload completion
        const D3D12_RESOURCE_STATES m_before;
        const D3D12_RESOURCE_STATES m_after;
    };

    std::vector<Request*> m_requests;
    Microsoft::WRL::ComPtr<IDStorageQueue> m_memoryQueue;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    HANDLE m_event{ nullptr };
    UINT64 m_fenceValue{ 0 };

    std::vector<Request*> m_inFlight;

    void WaitForInFlight();
};
