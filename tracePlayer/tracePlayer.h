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

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <wrl.h>
#include <string>
#include <dstorage.h>
#include <map>

class TracePlayer
{
public:
    struct Params
    {
        std::wstring m_filename;
        std::wstring m_mediaDir;
        UINT32 m_stagingBufferSizeMB{ 128 };

        std::wstring m_adapterDescription;  // e.g. "intel", will pick the GPU with this substring in the adapter description (not case sensitive)
        enum class PreferredArchitecture
        {
            NONE = 0,
            DISCRETE,
            INTEGRATED
        };
        PreferredArchitecture m_preferredArchitecture{ PreferredArchitecture::NONE };

        bool m_inspect{ false }; // inspect trace only, no playback
    };

    TracePlayer(const Params& in_params);
    ~TracePlayer();

    void PlaybackTrace(); // play trace (via DirectStorage)
    void Inspect();       // display information about the trace, e.g. # submits

    UINT64 GetNumRequests() const { return m_numRequestsTotal; }
    UINT64 GetNumFileBytesRead() const { return m_numFileBytesRead; } // bytes read during 1 playback
    UINT64 GetNumBytesWritten() const { return m_numBytesWritten; } // uncompressed bytes written to GPU
    const std::wstring& GetAdapterDescription() const { return m_params.m_adapterDescription; }
private:
    template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    Params m_params;

    ComPtr<IDXGIFactory5> m_factory;
    ComPtr<ID3D12Device> m_device;
    ComPtr<IDStorageFactory> m_dsFactory;
    ComPtr<IDStorageQueue> m_dsQueue;
    ComPtr<ID3D12CommandQueue> m_commandQueue;
    ComPtr<ID3D12Heap> m_heap;

    UINT64 m_fenceValue{ 0 };
    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent{ nullptr };

    struct Request
    {
        ID3D12Resource* m_pDstResource;
        D3D12_TILED_RESOURCE_COORDINATE m_dstCoord;
        IDStorageFile* m_srcFile;
        UINT32 m_srcOffset;
        UINT32 m_numBytes;
        UINT32 m_compressionFormat{ 0 };
    };
    typedef std::vector<Request> RequestArray;
    std::vector<RequestArray> m_submits;

    // release these when done
    std::vector<IDStorageFile*> m_fileHandles;
    std::vector<ID3D12Resource*> m_dstResources;

    UINT64 m_numRequestsTotal{ 0 };
    UINT64 m_numFileBytesRead{ 0 };
    UINT64 m_numBytesWritten{ 0 };

    void CreateDeviceWithName();
    void CreateFence();
    void InitDirectStorage();
    void LoadTraceFile();
    ID3D12Resource* CreateDestinationResource(UINT& out_numTiles, DXGI_FORMAT in_format, UINT in_width, UINT in_height, UINT in_subresourceCount);
    void UpdateTileMappings(ID3D12Resource* in_pResource, UINT in_tileOffset);
};
