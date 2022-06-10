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

#include "CommandLineArgs.h"
#include "SharedConstants.h"
#include "SceneObject.h"
#include "FrameEventTracing.h"
#include "AssetUploader.h"

class Scene
{
public:
    Scene(const CommandLineArgs& in_args, HWND in_hwnd);
    ~Scene();

    // draw returns false on device removed/reset
    bool Draw();
    void SetFullScreen(bool in_fullScreen)
    {
        // if the full screen state desired != current, then the window is tranisitioning
        // ignore requests unless not transitioning
        if (m_desiredFullScreen == m_fullScreen)
        {
            m_desiredFullScreen = in_fullScreen;
        }
    }
    bool GetFullScreen() const { return m_fullScreen; }

    void MoveView(int in_x, int in_y, int in_z);
    void RotateViewKey(int in_x, int in_y, int in_z);
    void RotateViewPixels(int in_x, int in_y);

    void ToggleUI() { m_args.m_showUI = !m_args.m_showUI; }
    void ToggleUIModeMini() { m_args.m_uiModeMini = !m_args.m_uiModeMini; }
    void ToggleFeedback() { m_args.m_showFeedbackMaps = !m_args.m_showFeedbackMaps; }
    void ToggleMinMipView() { m_args.m_visualizeMinMip = !m_args.m_visualizeMinMip; }
    void ToggleRollerCoaster() { m_args.m_cameraRollerCoaster = !m_args.m_cameraRollerCoaster; }
    void SetVisualizationMode(CommandLineArgs::VisualizationMode in_mode) { m_args.m_dataVisualizationMode = in_mode; }

    void ToggleFrustum() { m_args.m_visualizeFrustum = !m_args.m_visualizeFrustum; }

    void ToggleUpLock() { m_args.m_cameraUpLock = !m_args.m_cameraUpLock; }
    void ToggleAnimation()
    {
        static float animationRate = 0;
        static float cameraAnimationRate = 0;
        if (animationRate && m_args.m_animationRate) animationRate = 0;
        if (cameraAnimationRate && m_args.m_cameraAnimationRate) cameraAnimationRate = 0;
        std::swap(animationRate, m_args.m_animationRate);
        std::swap(cameraAnimationRate, m_args.m_cameraAnimationRate);
    }
    RECT GetGuiRect();

    void ScreenShot(std::wstring& in_fileName) const;

private:
    Scene(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene& operator=(Scene&&) = delete;
    template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    void Animate(); // camera and objects

    std::atomic<bool> m_fullScreen{ false };
    bool m_desiredFullScreen{ false };
    UINT m_windowWidth{ 0 };
    UINT m_windowHeight{ 0 };
    // remember placement for restore from full screen
    WINDOWPLACEMENT m_windowPlacement{};
    void Resize();

    void RotateView(float in_x, float in_y, float in_z);

    CommandLineArgs m_args;

    const HWND m_hwnd;
    WINDOWINFO m_windowInfo;
    bool m_windowedSupportsTearing;
    bool m_deviceRemoved; // when true, resize and draw immediately exit, returning false if appicable

    ComPtr<IDXGIFactory5> m_factory;
    ComPtr<IDXGIAdapter1> m_adapter;
    ComPtr<ID3D12Device8> m_device;
    ComPtr<ID3D12CommandQueue> m_commandQueue;

    static const UINT m_swapBufferCount = SharedConstants::SWAP_CHAIN_BUFFER_COUNT;
    static const FLOAT m_clearColor[4];
    ComPtr<IDXGISwapChain3> m_swapChain;
    UINT m_frameIndex; // comes from swap chain
    UINT64 m_renderFenceValue; // also serves as a frame count
    UINT64 m_frameFenceValues[m_swapBufferCount];
    ComPtr<ID3D12Fence> m_renderFence;
    HANDLE m_renderFenceEvent;

    // used for animation or statistics gathering
    // e.g. rotate by radians*m_framenumber
    // frameNumber can optionally be stalled while waiting for initial resource load
    UINT m_frameNumber{ 0 };
    bool m_allLoaded{ true }; // set to false when new objects added. set to true when packed mips have loaded.

    ComPtr<ID3D12CommandAllocator> m_commandAllocators[m_swapBufferCount];
    ComPtr<ID3D12GraphicsCommandList1> m_commandList;

    ComPtr<ID3D12Resource> m_renderTargets[m_swapBufferCount];
    ComPtr<ID3D12Resource> m_colorBuffer; //  MSAA render target
    ComPtr<ID3D12Resource> m_depthBuffer;

    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    ComPtr<ID3D12DescriptorHeap> m_dsvHeap;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;

    UINT m_rtvDescriptorSize;
    UINT m_srvUavCbvDescriptorSize;
    UINT m_dsvDescriptorSize;

    DirectX::XMMATRIX m_projection;
    DirectX::XMMATRIX m_viewMatrix;
    DirectX::XMMATRIX m_viewMatrixInverse;
    float m_aspectRatio;
    const float m_fieldOfView{ DirectX::XM_PI / 6.0f };

    CD3DX12_VIEWPORT m_viewport;
    CD3DX12_RECT m_scissorRect;

    struct FrameConstantData
    {
        DirectX::XMMATRIX g_view;
        DirectX::XMFLOAT4 g_lightDir;
        DirectX::XMFLOAT4 g_lightColor;     // RGB + specular intensity
        DirectX::XMFLOAT4 g_specColor;

        int g_visualizeFeedback;
    };

    FrameConstantData* m_pFrameConstantData; // left in the mapped state
    ComPtr<ID3D12Resource> m_frameConstantBuffer;

    bool m_showFrustum{ false };
    bool m_useDirectStorage{ false };

    class Gui* m_pGui;
    class TextureViewer* m_pTextureViewer;
    class BufferViewer* m_pMinMipMapViewer;
    class BufferViewer* m_pFeedbackViewer;
    class FrustumViewer* m_pFrustumViewer;

    void MsaaResolve();

    void WaitForGpu();

    float GetFrameTime();

    bool IsDeviceOk(HRESULT in_hr);
    void CreateRenderTargets();
    void MoveToNextFrame();

    //-------------------------------
    // startup sequence (constructor)
    //-------------------------------
    void InitDebugLayer();
    void CreateDescriptorHeaps();
    void CreateCommandQueue();
    void CreateSwapChain();
    void CreateFence();
    void CreateConstantBuffers();
    void CreateSampler();
    //-------------------------------

    // just for the sampler bias, which can be adjusted by the UI
    ComPtr<ID3D12DescriptorHeap> m_samplerHeap;
    void SetSampler();

    std::unique_ptr<class TileUpdateManager> m_pTileUpdateManager;

    void DrainTiles();
    UINT DetermineMaxNumFeedbackResolves();
    void DrawObjects();   // draw all the objects

    void CreateTerrainViewers();
    void DeleteTerrainViewers();

    //-----------------------------------
    // scene graph
    //-----------------------------------
    std::vector<class SceneObjects::BaseObject*> m_objects;
    UINT m_numSpheresLoaded{ 0 };

    UINT m_terrainObjectIndex{ 0 };
    SceneObjects::BaseObject* m_pTerrainSceneObject{ nullptr }; // convenience pointer, do not delete or draw, this is also in m_objects.
    SceneObjects::BaseObject* m_pFirstSphere{ nullptr };   // all regular planets
    SceneObjects::BaseObject* m_pEarth{ nullptr };         // special geometry (no mirror in U)
    SceneObjects::BaseObject* m_pSky{ nullptr }; // lifetime owned by m_objects

    DirectX::XMMATRIX SetSphereMatrix();
    void LoadSpheres(); // progressively over multiple frames

    // each frame, update objects until timeout reached
    UINT m_queueFeedbackIndex; // index based on number of gpu feedback resolves per frame
    std::vector<UINT> m_prevNumFeedbackObjects; // to correlate # objects with feedback time

    //-----------------------------------
    // statistics gathering
    //-----------------------------------
    FrameEventTracing::RenderEventList m_renderThreadTimes;
    FrameEventTracing::UpdateEventList m_updateFeedbackTimes;
    class D3D12GpuTimer* m_pGpuTimer;
    std::unique_ptr<FrameEventTracing> m_csvFile{ nullptr };
    float m_gpuProcessFeedbackTime{ 0 };

    // render thread can request other threads to stop processing e.g. on time budget exceeded
    std::atomic<bool> m_interrupt{ false };

    // statistics: compute per-frame # evictions & uploads from delta from previous total
    UINT m_numTotalEvictions{ 0 };
    UINT m_numTotalUploads{ 0 };

    UINT m_numEvictionsPreviousFrame{ 0 };
    UINT m_numUploadsPreviousFrame{ 0 };

    void StartStreamingLibrary();
    std::vector<StreamingHeap*> m_sharedHeaps;

    void GatherStatistics(float in_cpuProcessFeedbackTime, float in_gpuProcessFeedbackTime);
    UINT m_startUploadCount{ 0 };
    float m_totalTileLatency{ 0 }; // per-tile upload latency. NOT the same as per-UpdateList
    Timer m_cpuTimer;

    void StartScene();
    void DrawUI(float in_cpuProcessFeedbackTime);

    AssetUploader m_assetUploader;
};
