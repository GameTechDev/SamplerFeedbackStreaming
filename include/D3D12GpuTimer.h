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
#include <wrl.h>

#include <cstdint>
#include <vector>
#include <string>
#include <utility>
#include <map>

#include "d3dx12.h"
#include "DebugHelper.h"

class D3D12GpuTimer
{
public:
    enum class TimerType
    {
        Direct,
        Copy
    };

    D3D12GpuTimer(
        ID3D12Device* in_pDevice, // required to create internal resources
        std::uint32_t in_numTimers,
        TimerType in_timeType);   // is this timer going to be used on a copy command list?

    void SetTimerName(std::uint32_t in_index, const std::string& in_name);

    void BeginTimer(ID3D12GraphicsCommandList* in_pCommandList, std::uint32_t in_index);
    void EndTimer(ID3D12GraphicsCommandList* in_pCommandList, std::uint32_t in_index);

    // resolve every timer
    void ResolveAllTimers(ID3D12GraphicsCommandList* in_pCommandList);

    // resolve a specific timer
    // optionally, Map() and read back old value
    void ResolveTimer(ID3D12GraphicsCommandList* in_pCommandList, UINT in_index, bool in_mapReadback = true);

    // map and read a timer. useful if we know a valid time is ready.
    float MapReadBack(UINT in_index);

    typedef std::vector<std::pair<float, std::string>> TimeArray;
    const TimeArray& GetTimes() const { return m_times; }

    // for GpuScopeTimer
    std::uint32_t GetDynamicIndex(const std::string& in_name);
private:
    template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    std::uint32_t m_numTimers;   // how many we expose. we need double to record begin + end
    std::uint32_t m_totalTimers;
    TimeArray m_times;
    std::uint64_t m_gpuFrequency;

    Microsoft::WRL::ComPtr<ID3D12QueryHeap> m_heap;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_buffer;

    // for GpuScopeTimer
    std::uint32_t m_dynamicIndex;
    std::map<std::string, std::uint32_t> m_timeMap;
};

class GpuScopeTimer
{
public:
    GpuScopeTimer(D3D12GpuTimer* in_pGpuTimer,
        ID3D12GraphicsCommandList* in_pCommandList, const std::string& in_name) :
        m_pGpuTimer(in_pGpuTimer), m_index(0)
    {
        if (in_pGpuTimer)
        {
            m_commandList = in_pCommandList;
            m_index = m_pGpuTimer->GetDynamicIndex(in_name);
            m_pGpuTimer->SetTimerName(m_index, in_name);
            m_pGpuTimer->BeginTimer(in_pCommandList, m_index);
        }
    }
    ~GpuScopeTimer()
    {
        if (m_pGpuTimer)
        {
            m_pGpuTimer->EndTimer(m_commandList.Get(), m_index);
        }
    }
private:
    D3D12GpuTimer* m_pGpuTimer;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
    std::uint32_t m_index;
};

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
inline D3D12GpuTimer::D3D12GpuTimer(
    ID3D12Device* in_pDevice,
    std::uint32_t in_numTimers,
    TimerType in_timeType)
    : m_numTimers(in_numTimers)
    , m_totalTimers(in_numTimers * 2) // begin + end, so we can take a difference
    , m_gpuFrequency(0)
    , m_dynamicIndex(0)
    , m_times(m_numTimers)
{
    for (auto& t : m_times)
    {
        t.first = -1; // invalid time
    }

    const UINT64 bufferSize = m_totalTimers * sizeof(UINT64);

    const auto heapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    const auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
    ThrowIfFailed(in_pDevice->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_buffer)));
    m_buffer->SetName(L"GPUTimeStamp Buffer");

    D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
    queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    if (TimerType::Copy == in_timeType)
    {
        queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_COPY_QUEUE_TIMESTAMP;
    }
    queryHeapDesc.Count = m_totalTimers;

    ThrowIfFailed(in_pDevice->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&m_heap)));
    m_heap->SetName(L"GpuTimeStamp QueryHeap");

    // create a queue just to get the gpu frequency
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> cmdQueue = nullptr;
    ThrowIfFailed(in_pDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&cmdQueue)));

    ThrowIfFailed(cmdQueue->GetTimestampFrequency(&m_gpuFrequency));
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
inline void D3D12GpuTimer::SetTimerName(std::uint32_t in_index, const std::string& in_name)
{
    if (in_index < m_times.size())
    {
        m_times[in_index].second = in_name;
    }
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
inline void D3D12GpuTimer::BeginTimer(ID3D12GraphicsCommandList* in_pCommandList, std::uint32_t in_index)
{
    const UINT index = in_index * 2;
    in_pCommandList->EndQuery(m_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
inline void D3D12GpuTimer::EndTimer(ID3D12GraphicsCommandList* in_pCommandList, std::uint32_t in_index)
{
    const UINT index = (in_index * 2) + 1;
    in_pCommandList->EndQuery(m_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
inline float D3D12GpuTimer::MapReadBack(UINT in_index)
{
    UINT index = in_index * 2;

    INT64* pTimestamps = nullptr;
    const auto range = CD3DX12_RANGE(index, index + 2);
    ThrowIfFailed(m_buffer->Map(0, &range, (void**)&pTimestamps));
    INT64 deltaTime = pTimestamps[index + 1] - pTimestamps[index];
    if (deltaTime < 0)
    {
        deltaTime = -deltaTime;
    }

    const float delta = float(deltaTime) / float(m_gpuFrequency);
    m_times[in_index].first = delta;

    // Unmap with an empty range (written range).
    D3D12_RANGE emptyRange{ 0,0 };
    m_buffer->Unmap(0, &emptyRange);

    return delta;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
inline void D3D12GpuTimer::ResolveTimer(ID3D12GraphicsCommandList* in_pCommandList, UINT in_index, bool in_mapReadback)
{
    UINT index = in_index * 2;
    in_pCommandList->ResolveQueryData(m_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index, 2, m_buffer.Get(), index*8);

    if (in_mapReadback)
    {
        UINT64* pTimestamps = nullptr;
        const auto range = CD3DX12_RANGE(index, index + 1);
        ThrowIfFailed(m_buffer->Map(0, &range, (void**)&pTimestamps));
        UINT64 deltaTime = pTimestamps[index + 1] - pTimestamps[index];
        if (pTimestamps[index] > pTimestamps[index + 1])
        {
            deltaTime = pTimestamps[index] - pTimestamps[index + 1];
        }

        const float delta = float(deltaTime) / float(m_gpuFrequency);
        m_times[in_index].first = delta;

        // Unmap with an empty range (written range).
        D3D12_RANGE emptyRange{ 0,0 };
        m_buffer->Unmap(0, &emptyRange);
    }
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
inline void D3D12GpuTimer::ResolveAllTimers(ID3D12GraphicsCommandList* in_pCommandList)
{
    if (m_timeMap.size()) { m_totalTimers = UINT(m_timeMap.size() * 2); }

    in_pCommandList->ResolveQueryData(m_heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, m_totalTimers, m_buffer.Get(), 0);

    void* pData = nullptr;
    const auto range = CD3DX12_RANGE(0, m_totalTimers);
    ThrowIfFailed(m_buffer->Map(0, &range, &pData));

    const UINT64* pTimestamps = reinterpret_cast<UINT64*>(pData);
    for (std::uint32_t i = 0; i < m_numTimers; i++)
    {
        UINT64 deltaTime = pTimestamps[1] - pTimestamps[0];
        if (pTimestamps[1] < pTimestamps[0])
        {
            deltaTime = pTimestamps[0] - pTimestamps[1];
        }

        const float delta = float(deltaTime) / float(m_gpuFrequency);
        m_times[i].first = delta;

        pTimestamps += 2;
    }

    // Unmap with an empty range (written range).
    D3D12_RANGE emptyRange{0,0};
    m_buffer->Unmap(0, &emptyRange);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
inline std::uint32_t D3D12GpuTimer::GetDynamicIndex(const std::string& in_name)
{
    bool exists = m_timeMap.count(in_name);

    std::uint32_t index = m_dynamicIndex;
    if (exists)
    {
        index = m_timeMap[in_name];
    }
    else
    {
        m_timeMap[in_name] = m_dynamicIndex;
        m_dynamicIndex++;
    }

    return index;
}
