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

// reference: https://docs.microsoft.com/en-us/windows/win32/gdi/capturing-an-image

#include <string>
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <vector>
#include "d3dx12.h"

#pragma warning(push, 0)
#include <gdiplus.h>
#pragma warning(pop)
#pragma comment( lib, "gdiplus.lib" ) 

namespace WindowCapture
{
    template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    //-----------------------------------------------------------------------------
    // from MSDN:
    // https://docs.microsoft.com/en-us/windows/desktop/gdiplus/-gdiplus-retrieving-the-class-identifier-for-an-encoder-use
    //-----------------------------------------------------------------------------
    int GetEncoderClsid(const WCHAR* format, CLSID* pClsid)
    {
        UINT  num = 0;          // number of image encoders
        UINT  size = 0;         // size of the image encoder array in bytes

        Gdiplus::ImageCodecInfo* pImageCodecInfo = NULL;

        Gdiplus::GetImageEncodersSize(&num, &size);
        if (size == 0)
            return -1;  // Failure

        pImageCodecInfo = (Gdiplus::ImageCodecInfo*)(malloc(size));
        if (pImageCodecInfo == NULL)
            return -1;  // Failure

        Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);

        for (UINT j = 0; j < num; ++j)
        {
            if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0)
            {
                *pClsid = pImageCodecInfo[j].Clsid;
                free(pImageCodecInfo);
                return j;  // Success
            }
        }

        free(pImageCodecInfo);
        return -1;  // Failure
    }

    void CaptureRenderTarget(ID3D12Resource* in_pRsrc, ID3D12CommandQueue* in_pQ, const std::wstring& in_filename)
    {
        ComPtr<ID3D12Device> device;
        in_pRsrc->GetDevice(IID_PPV_ARGS(&device));

        ComPtr<ID3D12CommandAllocator> commandAllocator;
        ComPtr<ID3D12GraphicsCommandList> commandList;
        ComPtr<ID3D12Fence> renderFence;
        ComPtr<ID3D12Resource> stagingResource;

        ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator)));
        device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList));
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&renderFence)));

        auto srcDesc = in_pRsrc->GetDesc();

        UINT64 bufferSize = 0;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
        device->GetCopyableFootprints(&srcDesc, 0, 1, 0, &layout, nullptr, nullptr, &bufferSize);

        D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize);
        const auto heapDesc = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
        ThrowIfFailed(device->CreateCommittedResource(
            &heapDesc, D3D12_HEAP_FLAG_NONE,
            &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&stagingResource)));

        D3D12_RESOURCE_BARRIER b = CD3DX12_RESOURCE_BARRIER::Transition(in_pRsrc, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
        commandList->ResourceBarrier(1, &b);

        const auto dstLocation = CD3DX12_TEXTURE_COPY_LOCATION(stagingResource.Get(), layout);
        const auto srcLocation = CD3DX12_TEXTURE_COPY_LOCATION(in_pRsrc, 0);
        commandList->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);

        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        commandList->ResourceBarrier(1, &b);

        // submit all our initialization commands
        commandList->Close();
        ID3D12CommandList* ppCommandLists[] = { commandList.Get() };
        in_pQ->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

        HANDLE renderFenceEvent = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (renderFenceEvent == nullptr)
        {
            ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
        }

        ThrowIfFailed(in_pQ->Signal(renderFence.Get(), 1));
        ThrowIfFailed(renderFence->SetEventOnCompletion(1, renderFenceEvent));
        WaitForSingleObject(renderFenceEvent, INFINITE);
        ::CloseHandle(renderFenceEvent);

        BYTE* pData = nullptr;
        stagingResource->Map(0, nullptr, (void**)&pData);

        std::vector<BYTE> bytes(layout.Footprint.RowPitch * layout.Footprint.Height, 0);
        for (UINT y = 0; y < layout.Footprint.Height; y++)
        {
            UINT i = y * layout.Footprint.RowPitch;
            for (UINT x = 0; x < layout.Footprint.Width; x++)
            {

                bytes[i + 0] = pData[i + 2];
                bytes[i + 1] = pData[i + 1];
                bytes[i + 2] = pData[i + 0];
                i += 4;
            }

        }

        // Start Gdiplus 
        Gdiplus::GdiplusStartupInput gdi;
        ULONG_PTR token;
        Gdiplus::GdiplusStartup(&token, &gdi, nullptr);

        Gdiplus::Bitmap bitmap(layout.Footprint.Width, layout.Footprint.Height, layout.Footprint.RowPitch, PixelFormat32bppRGB, bytes.data());

        // Get the class identifier for the PNG encoder.
        CLSID pngClsid{};
        GetEncoderClsid(L"image/png", &pngClsid);

        // Save image1 as a stream in the compound file.
        Gdiplus::Status status = bitmap.Save(in_filename.c_str(), &pngClsid, nullptr);
        if (Gdiplus::Ok != status)
        {
            MessageBox(0, L"Image save failed", L"Failed", MB_OK);
            exit(-1);
        }

        //Gdiplus::GdiplusShutdown(token);

        stagingResource->Unmap(0, nullptr);
    }
};

