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
#include "ConfigurationParser.h"

namespace Streaming
{
    struct UpdateList;

    // file handle internals different between reference and DS FileStreamers
    class FileHandle
    {
    public:
        virtual ~FileHandle() {}
    };

    class FileStreamer
    {
    public:
        FileStreamer(ID3D12Device* in_pDevice);
        virtual ~FileStreamer();

        virtual FileHandle* OpenFile(const std::wstring& in_path) = 0;

        virtual void StreamTexture(Streaming::UpdateList& in_updateList) = 0;

        virtual void Signal() = 0;

        enum class VisualizationMode
        {
            DATA_VIZ_NONE,
            DATA_VIZ_MIP,
            DATA_VIZ_TILE
        };
        void SetVisualizationMode(UINT in_mode) { m_visualizationMode = (VisualizationMode)in_mode; }

        bool GetCompleted(const UpdateList& in_updateList) const;

        void CaptureTraceFile(bool in_captureTrace) { m_captureTrace = in_captureTrace; } // enable/disable writing requests/submits to a trace file
    protected:
        // copy queue fence
        ComPtr<ID3D12Fence> m_copyFence;
        UINT64 m_copyFenceValue{ 0 };

        // Visualization
        VisualizationMode m_visualizationMode;

        // get visualization colors
        void* GetVisualizationData(const D3D12_TILED_RESOURCE_COORDINATE& in_coord, DXGI_FORMAT in_format);

        static const UINT m_lutSize{ 16 };
        static float m_lut[m_lutSize][3];

        static BYTE m_BC7[m_lutSize][D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES];
        void InitializeBC7();

        static BYTE m_BC1[m_lutSize][D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES];
        void InitializeBC1();

        // trace file
        bool m_captureTrace{ false };

        void TraceRequest(const D3D12_TILED_RESOURCE_COORDINATE& in_coord,
            UINT64 in_offset, UINT64 in_fileHandle, UINT32 in_numBytes);
        void TraceSubmit();
    private:
        bool m_firstSubmit{ true };
        ConfigurationParser m_trace; // array of submits, each submit is an array of requests
        UINT m_traceSubmitIndex{ 0 };
        UINT m_traceRequestIndex{ 0 };
    };
}
