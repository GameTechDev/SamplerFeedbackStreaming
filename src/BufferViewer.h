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

#include "TextureViewer.h"

// create a window to show a buffer of UINT

class BufferViewer : public TextureViewer
{
public:
    BufferViewer(ID3D12Resource* in_pBuffer,
        UINT in_width, UINT in_height, UINT in_rowPitch, UINT in_offset,
        const DXGI_FORMAT in_swapChainFormat,
        // optionally provide a descriptor heap and an offset into that heap
        // if not provided, will create a descriptor heap just for that texture
        ID3D12DescriptorHeap* in_pDescriptorHeap = nullptr,
        INT in_descriptorOffset = 0);

    virtual ~BufferViewer() {}

    void Draw(ID3D12GraphicsCommandList* in_pCL,
        DirectX::XMFLOAT2 in_position,
        DirectX::XMFLOAT2 in_windowDim,
        D3D12_VIEWPORT in_viewPort); 
};
