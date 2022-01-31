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
#include <vector>
#include <synchapi.h>
#pragma comment(lib, "Synchronization.lib")

namespace Streaming
{
    template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    using BarrierList = std::vector<D3D12_RESOURCE_BARRIER>;

    //==================================================
    //==================================================
    struct UploadBuffer
    {
        ~UploadBuffer() { if (m_resource.Get()) m_resource->Unmap(0, nullptr); }
        ComPtr<ID3D12Resource> m_resource;
        void* m_pData{ nullptr };

        void Allocate(ID3D12Device* in_pDevice, UINT in_numBytes)
        {
            if (m_pData)
            {
                m_resource->Unmap(0, nullptr);
            }

            const auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            const auto resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(in_numBytes);
            in_pDevice->CreateCommittedResource(
                &uploadHeapProperties,
                D3D12_HEAP_FLAG_NONE, &resourceDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&m_resource));
            m_resource->Map(0, nullptr, reinterpret_cast<void**>(&m_pData));
        }
    };

    //==================================================
    //==================================================
    template<typename T> class AlignedAllocator : public std::allocator<T>
    {
    public:
        AlignedAllocator(UINT in_alignment = 256) : m_alignment(in_alignment) {} // default is both UINT32 SIMD8 and D3D12_TEXTURE_DATA_PITCH_ALIGNMENT
        UINT GetAlignment() { return m_alignment; }
        T* allocate(std::size_t n) { return (T*)_aligned_malloc(n * sizeof(T), m_alignment); }
        void deallocate(T* p, std::size_t n) { _aligned_free(p); }
    private:
        const UINT m_alignment;
    };

    //==================================================
    // auto t = AutoString::Concat("test: ", 3, "*", 2.75f, "\n");
    //==================================================
    class AutoString
    {
    public:
        template <typename...Ts> static std::wstring Concat(Ts...ts)
        {
            std::wstringstream w;
            Expander(w, ts...);
            return w.str();
        }
    private:
        static void Expander(std::wstringstream&) { }

        template <typename T, typename...Ts> static void Expander(std::wstringstream& in_w, const T& t, Ts...ts)
        {
            in_w << t;
            Expander(in_w, ts...);
        }
    };

    //==================================================
    // a single thread may wait on this flag, which may be set by any number of threads
    //==================================================
    class SynchronizationFlag
    {
    public:
        void Set()
        { 
            m_flag = true;
            WakeByAddressSingle(&m_flag);
        }

        void Wait()
        {
            // note: msdn recommends verifying that the value really changed, but we're not.
            bool undesiredValue = false;
            WaitOnAddress(&m_flag, &undesiredValue, sizeof(bool), INFINITE);
            m_flag = false;
        };
    private:
        bool m_flag{ false };
    };

    //==================================================
    // unused
    //==================================================
    class Lock
    {
    public:
        void Acquire()
        {
            const std::uint32_t desired = 1;
            bool waiting = true;

            while (waiting)
            {
                std::uint32_t expected = 0;
                waiting = !m_lock.compare_exchange_weak(expected, desired);
            }
        }

        void Release()
        {
            ASSERT(0 != m_lock);
            m_lock = 0;
        }

        bool TryAcquire()
        {
            std::uint32_t expected = 0;
            const std::uint32_t desired = 2; //  different value useful for debugging
            return m_lock.compare_exchange_weak(expected, desired);
        }
    private:
        std::atomic<uint32_t> m_lock{ 0 };
    };
}
