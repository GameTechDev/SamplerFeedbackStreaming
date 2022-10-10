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
#include <iomanip>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

#include "Gui.h"

#pragma comment(lib, "imgui.lib")

//-----------------------------------------------------------------------------
// NOTE: this doesn't allocate any resources. it relies on calling function to set any heaps
//-----------------------------------------------------------------------------
Gui::Gui(HWND in_hWnd, ID3D12Device* in_pDevice,
    ID3D12DescriptorHeap* in_pSrvHeap, const UINT in_descriptorHeapOffset,
    const UINT in_swapChainBufferCount, const DXGI_FORMAT in_swapChainFormat,
    const std::wstring& in_adapterDescription, CommandLineArgs& in_args) :
    m_initialArgs(in_args)
    , m_srvHeap(in_pSrvHeap)
    , m_width(300)
    , m_height(600)
    , m_bandwidthHistory(m_historySize)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableSetMousePos + ImGuiBackendFlags_HasSetMousePos;  // Enable Keyboard Controls

    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(in_hWnd);

    CD3DX12_CPU_DESCRIPTOR_HANDLE cpu(in_pSrvHeap->GetCPUDescriptorHandleForHeapStart(),
        in_descriptorHeapOffset, in_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpu(in_pSrvHeap->GetGPUDescriptorHandleForHeapStart(),
        in_descriptorHeapOffset, in_pDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

    ImGui_ImplDX12_Init(in_pDevice, in_swapChainBufferCount, in_swapChainFormat, in_pSrvHeap, cpu, gpu);
    ImGui_ImplDX12_CreateDeviceObjects();

    m_adapterDescription.resize(in_adapterDescription.size());
    ::WideCharToMultiByte(CP_UTF8, 0, in_adapterDescription.data(), -1, m_adapterDescription.data(), (int)m_adapterDescription.size(), NULL, NULL);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
Gui::~Gui()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(nullptr);
}

//-----------------------------------------------------------------------------
// draw the heap occupancy horizontal bar
//-----------------------------------------------------------------------------
void Gui::DrawHeapOccupancyBar(UINT in_numTilesCommitted, UINT in_totalHeapSize, float in_height)
{
    float percentOccupied = float(in_numTilesCommitted) / float(in_totalHeapSize);

    const float gap = 2.0f;

    auto pDrawList = ImGui::GetWindowDrawList();
    auto pos = ImGui::GetCursorScreenPos();
    auto width = ImGui::GetWindowWidth();

    pos.y += gap;

    ImVec2 topLeft{ pos.x, pos.y };
    ImVec2 bottomRight{ pos.x + width, pos.y + in_height };
    auto color = ImGui::ColorConvertFloat4ToU32(ImVec4{ 0.2f, 0.2f, 0.2f, 1.0f });
    pDrawList->AddRectFilled(topLeft, bottomRight, color);
    auto color2 = ImGui::ColorConvertFloat4ToU32(ImVec4{ 0.3f, 0.9f, 0.7f, 1.0f });

    width *= percentOccupied;
    bottomRight.x = pos.x + width;
    pDrawList->AddRectFilled(topLeft, bottomRight, color2);

    ImGui::SetCursorPosY(pos.y + gap + in_height + gap);
}

//-----------------------------------------------------------------------------
// compute MB/s in a consistent way across UI
//-----------------------------------------------------------------------------
float Gui::ComputeBandwidth(UINT in_numTiles, float in_numSeconds)
{
    float tilesPerSecond = float(in_numTiles) / in_numSeconds;
    float bytesPerTileDivMega = float(64 * 1024) / (1000.f * 1000.f);
    return (tilesPerSecond * bytesPerTileDivMega);
}

//-----------------------------------------------------------------------------
// draw a line graph of the provided ring buffer of values
//-----------------------------------------------------------------------------
void Gui::DrawLineGraph(const std::vector<float>& in_ringBuffer, UINT in_head, const ImVec2 in_windowDim)
{
    ImVec4 infoColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);

    std::vector<float> drawBuffer;
    drawBuffer.reserve(in_ringBuffer.size());
    drawBuffer.insert(drawBuffer.begin(), in_ringBuffer.begin() + in_head, in_ringBuffer.end());
    drawBuffer.insert(drawBuffer.begin() + (in_ringBuffer.size() - in_head), in_ringBuffer.begin(), in_ringBuffer.begin() + in_head);

    ImGui::PushStyleColor(ImGuiCol_Text, infoColor);
    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    // # tiles / cpu time 
    ASSERT(m_cpuTimes.GetNumEntries() == m_numUploads.GetNumEntries());

    auto numTiles = m_numUploads.GetRange();
    float seconds = m_cpuTimer.GetSecondsFromDelta(m_cpuTimes.GetRange());
    float mbps = ComputeBandwidth((UINT)numTiles, seconds);

    float graphMin = 0.0f;
    float graphMax = 0.0f;

    for (const auto& f : drawBuffer)
    {
        graphMax = std::max(graphMax, f);
    }

    float graphMaxScale = 12.5f;
    while (graphMaxScale < graphMax)
    {
        graphMaxScale *= 2;
    }

    std::stringstream overlay;
    overlay.setf(std::ios::fixed, std::ios::floatfield);
    overlay << "Bandwidth (MB/s) avg = " << std::setprecision(3) << std::setw(9) << mbps;
    ImGui::PlotLines("Label", drawBuffer.data(), (int)drawBuffer.size(), 0, overlay.str().c_str(), graphMin, graphMaxScale, in_windowDim);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bandwidth (MB/s) max: %.2f, scale: %.2f", graphMax, graphMaxScale);

    ImGui::PopStyleColor(2);
}

//-----------------------------------------------------------------------------
// get time since last frame
// use # uploads to compute average bandwidth
// while here, update the average cpu time
//-----------------------------------------------------------------------------
void Gui::UpdateBandwidthHistory(UINT in_numTilesUploaded)
{
    float seconds = m_cpuTimer.GetSecondsFromDelta(m_cpuTimes.GetMostRecentDelta());
    m_bandwidthHistory[m_bandwidthHistoryIndex] = ComputeBandwidth(in_numTilesUploaded, seconds);
    m_bandwidthHistoryIndex = (m_bandwidthHistoryIndex + 1) % m_bandwidthHistory.size();
}

//-----------------------------------------------------------------------------
// mini UI mode is just bandwidth and heap occupancy
//-----------------------------------------------------------------------------
void Gui::DrawMini(ID3D12GraphicsCommandList* in_pCommandList, const DrawParams& in_drawParams)
{
    m_cpuTimes.Update(m_cpuTimer.GetTime());
    m_numUploads.AddDelta(in_drawParams.m_numTilesUploaded);
    UpdateBandwidthHistory(in_drawParams.m_numTilesUploaded);

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    float scale = 4.0f;

    float top = std::max(float(in_drawParams.m_windowHeight - m_height), 0.0f);

    ImVec2 v(0, top);
    ImGui::SetWindowPos(v);

    // ignore height
    ImVec2 windowSize(scale * m_width, 600);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);

    ImGui::Begin("SamplerFeedbackStreaming", 0, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
    
    ImGui::SetWindowFontScale(scale);

    ImGui::SetWindowPos(v);
    ImGui::SetWindowSize(windowSize);

    DrawLineGraph(m_bandwidthHistory, m_bandwidthHistoryIndex, ImVec2(windowSize.x, 100.0f));

    float percentOccupied = float(in_drawParams.m_numTilesCommitted) / float(in_drawParams.m_totalHeapSize);
    float heapSize = (in_drawParams.m_totalHeapSize * 64) / 1024.f;
    float heapOccupied = heapSize * percentOccupied;

    ImGui::Text("Heap MB: %7.2f of %7.2f (%.2f%%)",
        heapOccupied,
        heapSize,
        100.f * percentOccupied);

    DrawHeapOccupancyBar(in_drawParams.m_numTilesCommitted, in_drawParams.m_totalHeapSize, scale * 10.0f);

    m_height = ImGui::GetCursorPosY() - top;

    windowSize.y = m_height;
    ImGui::SetWindowSize(windowSize);

    ImGui::End();
    ImGui::PopStyleVar();

    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), in_pCommandList);
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Gui::ToggleDemoMode(CommandLineArgs& in_args)
{
    m_demoMode = !m_demoMode;

    static float bias = 0;
    static float cameraRate = 0.4f;
    static float animationRate = 0.4f;

    std::swap(bias, in_args.m_lodBias);
    std::swap(cameraRate, in_args.m_cameraAnimationRate);
    std::swap(animationRate, in_args.m_animationRate);
    in_args.m_showFeedbackMaps = false;
    in_args.m_numSpheres = (int)m_initialArgs.m_maxNumObjects;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Gui::ToggleBenchmarkMode(CommandLineArgs& in_args)
{
    m_benchmarkMode = !m_benchmarkMode;

    static bool paintmixer = true;
    static float bias = -2;
    static float cameraRate = 2;
    static float animationRate = 2;

    std::swap(paintmixer, in_args.m_cameraPaintMixer);
    std::swap(bias, in_args.m_lodBias);
    std::swap(cameraRate, in_args.m_cameraAnimationRate);
    std::swap(animationRate, in_args.m_animationRate);
    in_args.m_showFeedbackMaps = false;
    in_args.m_numSpheres = (int)m_initialArgs.m_maxNumObjects;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void Gui::Draw(ID3D12GraphicsCommandList* in_pCommandList,
    CommandLineArgs& in_args, const DrawParams& in_drawParams, ButtonChanges& out_buttonChanges)
{
    m_cpuTimes.Update(m_cpuTimer.GetTime());
    m_numUploads.AddDelta(in_drawParams.m_numTilesUploaded);
    UpdateBandwidthHistory(in_drawParams.m_numTilesUploaded);

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // use maximum height until we calculate height
    ImVec2 windowSize(m_width, (float)in_args.m_windowHeight);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::Begin("SamplerFeedbackStreaming", 0, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
    ImGui::SetWindowFontScale(1.f);
    {
        ImVec2 v(0, 0);
        ImGui::SetWindowPos(v);
    }
    ImGui::SetWindowSize(windowSize);
    ImGui::Text(m_adapterDescription.data());

    const UINT indent = 20;

    //---------------------------------------------------------------------
    // animation properties. affects bandwidth
    //---------------------------------------------------------------------
    {
        ImGui::SliderFloat("Spin", &in_args.m_animationRate, 0, 2.0f);
        ImGui::SliderFloat("Camera", &in_args.m_cameraAnimationRate, 0, 2.0f);
        ImGui::Indent(indent);
        ImGui::Checkbox("Roller Coaster", &in_args.m_cameraRollerCoaster);
        ImGui::Unindent(indent);
    }

    //---------------------------------------------------------------------
    // live statistics
    //---------------------------------------------------------------------
    {
        DrawLineGraph(m_bandwidthHistory, m_bandwidthHistoryIndex, ImVec2(m_width, 50.0f));
        // GPU timers
        ImGui::Text("GPU ms: Feedback |   Draw");
        ImGui::Text("         %7.2f | %6.3f",
            in_drawParams.m_gpuFeedbackTime * 1000.f,
            in_drawParams.m_gpuDrawTime * 1000.f);
        // CPU timers
        ImGui::Separator();
        ImGui::Text("CPU ms: Feedback |  Draw  |  Frame");
        ImGui::Text("         %7.2f | %6.2f | %6.2f",
            in_drawParams.m_cpuFeedbackTime * 1000.f, in_drawParams.m_cpuDrawTime * 1000.f,
            (1000.f * m_cpuTimer.GetSecondsFromDelta(m_cpuTimes.GetRange())) / (float)m_cpuTimes.GetNumEntries());
    }

    //---------------------------------------------------------------------
    // heap statistics
    //---------------------------------------------------------------------
    ImGui::Separator();
    ImGui::Text("Reserved KB: %d", (in_drawParams.m_numTilesVirtual * 64));
    ImGui::Text("Committed KB: %d (%.2f %%)", (in_drawParams.m_numTilesCommitted * 64), 100.f * float(in_drawParams.m_numTilesCommitted) / float(in_drawParams.m_numTilesVirtual));

    ImGui::Text("Heap Occupancy KB: %.2f%% of %d",
        100.f * float(in_drawParams.m_numTilesCommitted) / float(in_drawParams.m_totalHeapSize), (in_drawParams.m_totalHeapSize * 64));
    DrawHeapOccupancyBar(in_drawParams.m_numTilesCommitted, in_drawParams.m_totalHeapSize, 10.0f);

    //---------------------------------------------------------------------
    // number of objects. affects heap occupancy
    //---------------------------------------------------------------------
    ImGui::SliderInt("Num Objects", &in_args.m_numSpheres, 0, (int)in_args.m_maxNumObjects);

    //---------------------------------------------------------------------
    // terrain feedback viewer
    //---------------------------------------------------------------------
    in_args.m_showFeedbackMaps = ImGui::CollapsingHeader("Terrain Object Feedback Viewer");
    if (in_args.m_showFeedbackMaps)
    {
        ImGui::Indent(indent);
        ImGui::Checkbox("Mip Window Orientation", &in_args.m_showFeedbackMapVertical);
        ImGui::Checkbox("Raw Feedback", &in_args.m_showFeedbackViewer);
        ImGui::SliderInt("Scroll", &in_args.m_visualizationBaseMip, 0, in_drawParams.m_scrollMipDim);
        ImGui::Unindent(indent);
    }

    //---------------------------------------------------------------------
    // misc. options
    //---------------------------------------------------------------------
    if (ImGui::CollapsingHeader("Misc. Options"))
    {
        ImGui::Indent(indent);
        out_buttonChanges.m_directStorageToggle = ImGui::Checkbox("DirectStorage", &in_args.m_useDirectStorage);
        ImGui::Checkbox("VSync", &in_args.m_vsyncEnabled);

        out_buttonChanges.m_frustumToggle = ImGui::Checkbox("Lock Frustum", &in_args.m_visualizeFrustum);
        ImGui::Checkbox("Uploads Enabled", &in_args.m_enableTileUpdates);
        ImGui::Checkbox("Update Every Object Every Frame", &in_args.m_updateEveryObjectEveryFrame);
        ImGui::Checkbox("Lock \"Up\" Dir", &in_args.m_cameraUpLock);

        //---------------------------------------------------------------------
        // performance/quality controls
        //---------------------------------------------------------------------
        ImGui::PushItemWidth(100);
        ImGui::SliderFloat("Sampler Bias", &in_args.m_lodBias, -2.0f, 4.0f);
        ImGui::SliderFloat("Feedback Timeout", &in_args.m_maxGpuFeedbackTimeMs, 0, 30);
        ImGui::PopItemWidth();

        ImGui::Unindent(indent);
    }

    const char* visualizationModes[] = { "Texture", "Color = Mip Level", "Random Tile Color" };
    out_buttonChanges.m_visualizationChange = ImGui::Combo("Visualize", &in_args.m_dataVisualizationMode, visualizationModes, _countof(visualizationModes));

    //---------------------------------------------------------------------
    // emphasize this important visualization
    //---------------------------------------------------------------------
    ImVec4 colorMinMip{ 0.4f, 0.2f, 0.8f, 1.0f };
    ImGui::PushStyleColor(ImGuiCol_Button, colorMinMip);
    if (ImGui::Button("Tile Min Mip Overlay", ImVec2(-1, 0))) { in_args.m_visualizeMinMip = !in_args.m_visualizeMinMip; }
    ImGui::PopStyleColor(1);

    //---------------------------------------------------------------------
    // demo mode
    //---------------------------------------------------------------------
    if (ImGui::Button("DEMO MODE", ImVec2(-1, 0)))
    {
        if (m_benchmarkMode) { ToggleBenchmarkMode(in_args); }
        ToggleDemoMode(in_args);
    }

    //---------------------------------------------------------------------
    // benchmark mode
    //---------------------------------------------------------------------
    if (ImGui::Button("BENCHMARK MODE", ImVec2(-1, 0)))
    {
        if (m_demoMode) { ToggleDemoMode(in_args); }
        ToggleBenchmarkMode(in_args);
    }

    // resize the UI to fit the dynamically-sized components
    // NOTE: may be incorrect first frame
    m_height = ImGui::GetCursorPosY();

    windowSize.y = m_height;
    ImGui::SetWindowSize(windowSize);

    ImGui::End();
    ImGui::PopStyleVar();

    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), in_pCommandList);
}
