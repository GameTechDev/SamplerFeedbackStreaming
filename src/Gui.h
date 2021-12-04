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

#include "D3D12GpuTimer.h"
#include "CommandLineArgs.h"
#include "Timer.h" // for AverageOver

class Gui
{
public:
    Gui(HWND in_hWnd, ID3D12Device* in_pDevice,
        ID3D12DescriptorHeap* in_pSrvHeap, const UINT in_descriptorHeapOffset,
        const UINT in_swapChainBufferCount, const DXGI_FORMAT in_swapChainFormat,
        const std::wstring& in_adapterDescription, CommandLineArgs& in_args);
    ~Gui();

    struct DrawParams
    {
        float m_gpuDrawTime;       // time to draw the objects
        float m_gpuFeedbackTime;   // time to clear & resolve feedback and upload residency maps
        float m_cpuDrawTime;       // time cpu is submitting draw calls (render thread)
        float m_cpuFeedbackTime;   // time cpu is working on feedback & related datastructures (update thread)
        int m_scrollMipDim;
        UINT m_numTilesUploaded;
        UINT m_numTilesEvicted;
        UINT m_numTilesCommitted;
        UINT m_numTilesVirtual;
        UINT m_totalHeapSize;
        UINT m_windowHeight;
    };

    void Draw(ID3D12GraphicsCommandList* in_pCommandList, CommandLineArgs& in_args, const DrawParams& in_drawParams);

    void DrawMini(ID3D12GraphicsCommandList* in_pCommandList, const DrawParams& in_drawParams);

    float GetHeight() const { return m_height; }
    float GetWidth() const { return m_width; }
private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;

    float m_width;
    float m_height;

    // copy of arguments at creation for reset button
    const CommandLineArgs m_initialArgs;

    std::string m_adapterDescription;

    TotalSince m_cpuTimes;
    TotalSince m_numUploads;
    RawCpuTimer m_cpuTimer;

    static constexpr int m_historySize = 128;
    std::vector<float> m_bandwidthHistory;
    UINT m_bandwidthHistoryIndex{ 0 };
    void UpdateBandwidthHistory(UINT in_numTilesUploaded);

    bool m_benchmarkMode{ false };
    void ToggleBenchmarkMode(CommandLineArgs& in_args);

    bool m_demoMode{ false };
    void ToggleDemoMode(CommandLineArgs& in_args);

    float ComputeBandwidth(UINT in_numTiles, float in_numSeconds);

    void DrawLineGraph(const std::vector<float>& in_ringBuffer, UINT in_head, const ImVec2 in_windowDim);

    void DrawHeapOccupancyBar(UINT in_numTilesCommitted, UINT in_totalHeapSize, float in_height = 10.0f);
};
