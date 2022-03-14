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

/*
Navigation Keys:

Q / W / E : strafe left / forward / strafe right
A / S / D : rotate left /  back   / rotate right
Z   /   C : rotate around look direction
X : toggle "UP" lock for rotation. ON is good for looking at terrain, OFF for general flight
shift: faster translation

SPACE: toggle animation
HOME: toggle all UI
END: toggle all UI
*/

#include "pch.h"

#include <filesystem>

#include "Scene.h"
#include "CommandLineArgs.h"
#include "ArgParser.h"
#include "ConfigurationParser.h"

Scene* g_pScene = nullptr;

bool g_hasFocus = false;

std::wstring g_configFileName = L"config.json";

struct KeyState
{
    union
    {
        UINT32 m_anyKeyDown;
        struct
        {
            int32_t forward : 2;
            int32_t back    : 2;
            int32_t left    : 2;
            int32_t right   : 2;
            int32_t up      : 2;
            int32_t down    : 2;
            int32_t rotxl   : 2;
            int32_t rotxr   : 2;
            int32_t rotyl   : 2;
            int32_t rotyr   : 2;
            int32_t rotzl   : 2;
            int32_t rotzr   : 2;
        } key;
    };
    KeyState() { m_anyKeyDown = 0; }
} g_keyState;

struct MouseState
{
    POINT pos{};
    POINT move{};
    bool m_dragging{false};
} g_mouseState;

//-----------------------------------------------------------------------------
// apply limits arguments
// e.g. # spheres, path to terrain texture
//-----------------------------------------------------------------------------
void AdjustArguments(CommandLineArgs& out_args)
{
    out_args.m_numSpheres = std::min(out_args.m_numSpheres, (int)out_args.m_maxNumObjects);
    out_args.m_numSpheres = std::max(out_args.m_numSpheres, 1); // always show /something/
    out_args.m_sampleCount = std::min(out_args.m_sampleCount, (UINT)D3D12_MAX_MULTISAMPLE_SAMPLE_COUNT);
    out_args.m_anisotropy = std::min(out_args.m_anisotropy, (UINT)D3D12_REQ_MAXANISOTROPY);

    // override: use this texture for everything. no sky.
    if (out_args.m_textureFilename.size())
    {
        out_args.m_mediaDir.clear();
        out_args.m_skyTexture.clear();
        out_args.m_terrainTexture = out_args.m_textureFilename;
        out_args.m_textures.clear();
        out_args.m_textures.push_back(out_args.m_textureFilename);
    }

    // if there's a media directory, sky and earth are relative to media
    if (out_args.m_mediaDir.size())
    {
        // convenient for fixing other texture relative paths
        if (out_args.m_mediaDir.back() != L'\\')
        {
            out_args.m_mediaDir += L'\\';
        }

        if (std::filesystem::exists(out_args.m_mediaDir))
        {
            for (const auto& filename : std::filesystem::directory_iterator(out_args.m_mediaDir))
            {
                std::wstring f = std::filesystem::absolute(filename.path());
                out_args.m_textures.push_back(f);

                // matched the requested terrain texture name? substitute the full path
                if ((out_args.m_terrainTexture.size()) && (std::wstring::npos != f.find(out_args.m_terrainTexture)))
                {
                    out_args.m_terrainTexture = f;
                }
            }

            // media directory overrides textureFilename
            out_args.m_textureFilename = out_args.m_textures[0];

            // no terrain texture set or not found? set to something.
            if ((0 == out_args.m_terrainTexture.size()) || (!std::filesystem::exists(out_args.m_terrainTexture)))
            {
                out_args.m_terrainTexture = out_args.m_textureFilename;
            }
        }
        else
        {
            std::wstringstream caption;
            caption << "NOT FOUND: -mediaDir " << out_args.m_mediaDir;
            MessageBox(0, caption.str().c_str(), L"ERROR", MB_OK);
            exit(-1);
        }
    }
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void ParseCommandLine(CommandLineArgs& out_args)
{
    ArgParser argParser;

    // this one argument affects a global:
    argParser.AddArg(L"-config", L"Config File", g_configFileName);

    argParser.AddArg(L"-fullScreen", out_args.m_startFullScreen);
    argParser.AddArg(L"-WindowWidth", out_args.m_windowWidth);
    argParser.AddArg(L"-WindowHeight", out_args.m_windowHeight);
    argParser.AddArg(L"-SampleCount", out_args.m_sampleCount);
    argParser.AddArg(L"-TerrainSideSize", out_args.m_terrainSideSize);
    argParser.AddArg(L"-LodBias", out_args.m_lodBias);

    argParser.AddArg(L"-animationrate", out_args.m_animationRate);
    argParser.AddArg(L"-texture", out_args.m_textureFilename);
    argParser.AddArg(L"-vsync", out_args.m_vsyncEnabled);

    argParser.AddArg(L"-heapSizeTiles", out_args.m_streamingHeapSize);
    argParser.AddArg(L"-numHeaps", out_args.m_numHeaps);
    argParser.AddArg(L"-maxTileUpdatesPerApiCall", out_args.m_maxTileUpdatesPerApiCall);

    argParser.AddArg(L"-maxFeedbackTime", out_args.m_maxGpuFeedbackTimeMs);

    argParser.AddArg(L"-maxNumObjects", out_args.m_maxNumObjects);
    argParser.AddArg(L"-numSpheres", out_args.m_numSpheres);
    argParser.AddArg(L"-terrainTexture", out_args.m_terrainTexture);
    argParser.AddArg(L"-skyTexture", out_args.m_skyTexture);
    argParser.AddArg(L"-earthTexture", out_args.m_earthTexture);
    argParser.AddArg(L"-mediaDir", out_args.m_mediaDir);
    argParser.AddArg(L"-anisotropy", out_args.m_anisotropy);
    argParser.AddArg(L"-lightFromView", L"Light direction is look direction", out_args.m_lightFromView);

    argParser.AddArg(L"-cameraRate", out_args.m_cameraAnimationRate);
    argParser.AddArg(L"-rollerCoaster", out_args.m_cameraRollerCoaster);
    argParser.AddArg(L"-paintMixer", out_args.m_cameraPaintMixer);

    argParser.AddArg(L"-visualizeMinMip", [&](std::wstring) { out_args.m_visualizeMinMip = true; });
    argParser.AddArg(L"-hideFeedback", out_args.m_showFeedbackMaps);
    argParser.AddArg(L"-hideUI", [&](std::wstring) { out_args.m_showUI = false; });
    argParser.AddArg(L"-miniUI", [&](std::wstring) { out_args.m_uiModeMini = true; });
    argParser.AddArg(L"-updateAll", out_args.m_updateEveryObjectEveryFrame);
    argParser.AddArg(L"-addAliasingBarriers", L"Add per-draw aliasing barriers to assist PIX analysis", out_args.m_addAliasingBarriers);

    argParser.AddArg(L"-timingStart", out_args.m_timingStartFrame);
    argParser.AddArg(L"-timingStop", out_args.m_timingStopFrame);
    argParser.AddArg(L"-timingFileFrames", out_args.m_timingFrameFileName);
    argParser.AddArg(L"-exitImageFile", out_args.m_exitImageFileName);

    argParser.AddArg(L"-waitForAssetLoad", L"stall animation & statistics until assets have minimally loaded", out_args.m_waitForAssetLoad);
    argParser.AddArg(L"-adapter", L"find an adapter containing this string in the description, ignoring case", out_args.m_adapterDescription);

    argParser.AddArg(L"-directStorage", L"force enable DirectStorage", [&](std::wstring) { out_args.m_useDirectStorage = true; });
    argParser.AddArg(L"-directStorageOff", L"force disable DirectStorage", [&](std::wstring) { out_args.m_useDirectStorage = false; });

    argParser.Parse();
}

//-----------------------------------------------------------------------------
// Main message handler for the sample.
//-----------------------------------------------------------------------------
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    LRESULT guiResult = ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam);
    if (guiResult)
    {
        //return guiResult;
    }

    switch (message)
    {

    case WM_SIZE:
    {
        bool isFullScreen = (SIZE_MAXIMIZED == wParam);
        g_pScene->SetFullScreen(isFullScreen);
    }
    break;

    case WM_CREATE:
    {
        LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
        CommandLineArgs* pArgs = reinterpret_cast<CommandLineArgs*>(pCreateStruct->lpCreateParams);

        ASSERT(nullptr == g_pScene);

        ASSERT(nullptr != pArgs);
        g_pScene = new Scene(*pArgs, hWnd);
    }
    break;

    case WM_KILLFOCUS:
        g_keyState.m_anyKeyDown = 0;
        g_mouseState.m_dragging = false;
        g_hasFocus = false;
        break;

    case WM_SETFOCUS:
        g_hasFocus = true;
        break;

    case WM_KEYUP:
        switch (static_cast<UINT8>(wParam))
        {
        case 'W':
            g_keyState.key.forward = 0;
            break;
        case 'S':
            g_keyState.key.back = 0;
            break;
        case 'A':
            g_keyState.key.rotyl = 0;
            break;
        case 'D':
            g_keyState.key.rotyr = 0;
            break;
        case 'Q':
            g_keyState.key.left = 0;
            break;
        case 'E':
            g_keyState.key.right = 0;
            break;
        case 'F':
            g_keyState.key.up = 0;
            break;
        case 'V':
            g_keyState.key.down = 0;
            break;

        case 'Z':
            g_keyState.key.rotzl = 0;
            break;
        case 'C':
            g_keyState.key.rotzr = 0;
            break;

        case VK_UP:
            g_keyState.key.rotxl = 0;
            break;
        case VK_DOWN:
            g_keyState.key.rotxr = 0;
            break;
        case VK_LEFT:
            g_keyState.key.rotyl = 0;
            break;
        case VK_RIGHT:
            g_keyState.key.rotyr = 0;
            break;
        }
        break;

    case WM_KEYDOWN:
        switch (static_cast<UINT8>(wParam))
        {
        case 'W':
            g_keyState.key.forward = 1;
            break;
        case 'A':
            g_keyState.key.rotyl = 1;
            break;
        case 'S':
            g_keyState.key.back = 1;
            break;
        case 'D':
            g_keyState.key.rotyr = 1;
            break;
        case 'Q':
            g_keyState.key.left = 1;
            break;
        case 'E':
            g_keyState.key.right = 1;
            break;
        case 'F':
            g_keyState.key.up = 1;
            break;
        case 'V':
            g_keyState.key.down = 1;
            break;

        case 'Z':
            g_keyState.key.rotzl = 1;
            break;
        case 'C':
            g_keyState.key.rotzr = 1;
            break;

        case 'X':
            g_pScene->ToggleUpLock();
            break;

        case '1':
            g_pScene->SetVisualizationMode(CommandLineArgs::VisualizationMode::TEXTURE);
            break;
        case '2':
            g_pScene->SetVisualizationMode(CommandLineArgs::VisualizationMode::MIPLEVEL);
            break;
        case '3':
            g_pScene->SetVisualizationMode(CommandLineArgs::VisualizationMode::RANDOM);
            break;

        case VK_UP:
            g_keyState.key.rotxl = 1;
            break;
        case VK_DOWN:
            g_keyState.key.rotxr = 1;
            break;
        case VK_LEFT:
            g_keyState.key.rotyl = 1;
            break;
        case VK_RIGHT:
            g_keyState.key.rotyr = 1;
            break;

        case VK_HOME:
            if (0x8000 & GetKeyState(VK_SHIFT))
            {
                g_pScene->ToggleUIModeMini();
            }
            else
            {
                g_pScene->ToggleUI();
            }
            break;

        case VK_END:
            g_pScene->ToggleFeedback();
            break;

        case VK_PRIOR:
            g_pScene->ToggleMinMipView();
            break;
        case VK_NEXT:
            g_pScene->ToggleRollerCoaster();
            break;

        case VK_SPACE:
            g_pScene->ToggleAnimation();
            break;

        case VK_INSERT:
            g_pScene->ToggleFrustum();
            break;

        case VK_ESCAPE:
            if (g_pScene->GetFullScreen())
            {
                ShowWindow(hWnd, SW_RESTORE);
            }
            else
            {
                OutputDebugString(L"Normal Exit via ESC\n");
                PostQuitMessage(0);
            }
            break;
        }
        break;

    case WM_DESTROY:
        OutputDebugString(L"Exit via DESTROY\n");
        PostQuitMessage(0);
        break;
    default:
        // Handle any other messages
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0;
}

//-----------------------------------------------------------------------------
// Configuration file load
//-----------------------------------------------------------------------------
std::wstring StrToWstr(const std::string& in)
{
    std::wstringstream w;
    w << in.c_str();
    return w.str();
}

void LoadConfigFile(CommandLineArgs& out_args)
{
    bool success = false;

    const UINT exeStrLen = 1024;
    WCHAR exeStr[exeStrLen];
    GetModuleFileName(NULL, exeStr, exeStrLen);
    std::wstring filePath;
    if (0 == GetLastError())
    {
        filePath = exeStr;
        auto lastSlash = filePath.find_last_of(L"/\\");
        filePath = filePath.substr(0, lastSlash + 1) + g_configFileName;
        if (std::filesystem::exists(filePath))
        {
            ConfigurationParser parser;
            success = parser.Read(filePath);

            if (success)
            {
                const auto& root = parser.GetRoot();

                if (root.isMember("fullScreen")) out_args.m_startFullScreen = root["fullScreen"].asBool();
                if (root.isMember("vsync")) out_args.m_vsyncEnabled = root["vsync"].asBool();
                if (root.isMember("windowWidth")) out_args.m_windowWidth = root["windowWidth"].asUInt();
                if (root.isMember("windowHeight")) out_args.m_windowHeight = root["windowHeight"].asUInt();
                if (root.isMember("sampleCount")) out_args.m_sampleCount = root["sampleCount"].asUInt();
                if (root.isMember("lodBias")) out_args.m_lodBias = root["lodBias"].asFloat();
                if (root.isMember("anisotropy")) out_args.m_anisotropy = root["anisotropy"].asUInt();

                if (root.isMember("directStorage")) out_args.m_useDirectStorage = root["directStorage"].asBool();

                if (root.isMember("animationrate")) out_args.m_animationRate = root["animationrate"].asFloat();
                if (root.isMember("cameraRate")) out_args.m_cameraAnimationRate = root["cameraRate"].asFloat();
                if (root.isMember("rollerCoaster")) out_args.m_cameraRollerCoaster = root["rollerCoaster"].asBool();
                if (root.isMember("paintMixer")) out_args.m_cameraRollerCoaster = root["paintMixer"].asBool();

                if (root.isMember("texture")) out_args.m_textureFilename = StrToWstr(root["texture"].asString());
                if (root.isMember("mediaDir")) out_args.m_mediaDir = StrToWstr(root["mediaDir"].asString());
                if (root.isMember("terrainTexture")) out_args.m_terrainTexture = StrToWstr(root["terrainTexture"].asString());
                if (root.isMember("skyTexture")) out_args.m_skyTexture = StrToWstr(root["skyTexture"].asString());
                if (root.isMember("earthTexture")) out_args.m_earthTexture = StrToWstr(root["earthTexture"].asString());
                if (root.isMember("maxNumObjects")) out_args.m_maxNumObjects = root["maxNumObjects"].asUInt();
                if (root.isMember("numSpheres")) out_args.m_numSpheres = root["numSpheres"].asUInt();
                if (root.isMember("lightFromView")) out_args.m_lightFromView = root["lightFromView"].asBool();

                if (root.isMember("sphereLong")) out_args.m_sphereLong = root["sphereLong"].asUInt();
                if (root.isMember("sphereLat")) out_args.m_sphereLat = root["sphereLat"].asUInt();

                if (root.isMember("terrainSideSize")) out_args.m_terrainSideSize = root["terrainSideSize"].asUInt();
                if (root.isMember("heightScale")) out_args.m_heightScale = root["heightScale"].asFloat();
                if (root.isMember("noiseScale")) out_args.m_noiseScale = root["noiseScale"].asFloat();
                if (root.isMember("octaves")) out_args.m_numOctaves = root["octaves"].asUInt();
                if (root.isMember("mountainSize")) out_args.m_mountainSize = root["mountainSize"].asFloat();

                if (root.isMember("heapSizeTiles")) out_args.m_streamingHeapSize = root["heapSizeTiles"].asUInt();
                if (root.isMember("numHeaps")) out_args.m_numHeaps = root["numHeaps"].asUInt();
                if (root.isMember("maxTileUpdatesPerApiCall")) out_args.m_maxTileUpdatesPerApiCall = root["maxTileUpdatesPerApiCall"].asUInt();

                if (root.isMember("numStreamingBatches")) out_args.m_numStreamingBatches = root["numStreamingBatches"].asUInt();
                if (root.isMember("streamingBatchSize")) out_args.m_streamingBatchSize = root["streamingBatchSize"].asUInt();
                if (root.isMember("maxTilesInFlight")) out_args.m_maxTilesInFlight = root["maxTilesInFlight"].asUInt();

                if (root.isMember("maxFeedbackTime")) out_args.m_maxGpuFeedbackTimeMs = root["maxFeedbackTime"].asFloat();

                if (root.isMember("visualizeMinMip")) out_args.m_visualizeMinMip = root["visualizeMinMip"].asBool();
                if (root.isMember("hideFeedback")) out_args.m_showFeedbackMaps = !root["hideFeedback"].asBool();

                if (root.isMember("hideUI")) out_args.m_showUI = !root["hideUI"].asBool();
                if (root.isMember("miniUI")) out_args.m_uiModeMini = root["miniUI"].asBool();

                if (root.isMember("addAliasingBarriers")) out_args.m_addAliasingBarriers = root["addAliasingBarriers"].asBool();
                if (root.isMember("updateAll")) out_args.m_updateEveryObjectEveryFrame = root["updateAll"].asBool();

                if (root.isMember("timingStart")) out_args.m_timingStartFrame = root["timingStart"].asUInt();
                if (root.isMember("timingStop")) out_args.m_timingStopFrame = root["timingStop"].asUInt();
                if (root.isMember("timingFileFrames")) out_args.m_timingFrameFileName = StrToWstr(root["timingFileFrames"].asString());
                if (root.isMember("exitImageFile")) out_args.m_exitImageFileName = StrToWstr(root["exitImage"].asString());

                if (root.isMember("waitForAssetLoad")) out_args.m_waitForAssetLoad = root["waitForAssetLoad"].asBool();
                if (root.isMember("adapter")) out_args.m_adapterDescription = StrToWstr(root["adapter"].asString());
            } // end if successful load
        } // end if file exists
    }

    if (!success)
    {
        std::wstring msg(L"Invalid Configuration file path: ");
        msg += filePath;
        MessageBox(0, msg.c_str(), L"ERROR", MB_OK);
        exit(-1);
    }
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int WINAPI WinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE,
    _In_ LPSTR,
    _In_ int)
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    // get the config file name
    // we want "?" returns all commands, but will have to re-parse so command lines override config file settings
    // have to use a temporary args, or booleans will be toggled twice
    CommandLineArgs tmpArgs;
    ParseCommandLine(tmpArgs);

    // load config file using config file acquired above
    CommandLineArgs args;
    LoadConfigFile(args);

    // re-parse because we want command lines to override config file
    ParseCommandLine(args);

    // apply limits and other constraints
    AdjustArguments(args);

    WNDCLASSEX wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.lpszClassName = TEXT("Sampler Feedback Streaming");

    RegisterClassEx(&wcex);

    RECT windowRect = { 0, 0, (LONG)args.m_windowWidth, (LONG)args.m_windowHeight };
    AdjustWindowRect(&windowRect, WS_VISIBLE | WS_OVERLAPPEDWINDOW, FALSE);

    HWND hWnd = CreateWindow(
        wcex.lpszClassName, L"Sampler Feedback Streaming",
        WS_VISIBLE | WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        0, 0, hInstance,
        &args);

    if (!hWnd)
    {
        UnregisterClass(wcex.lpszClassName, hInstance);
        return -1;
    }

    // full screen?
    if (args.m_startFullScreen)
    {
        PostMessage(hWnd, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
    }

    MSG msg{};
    while (WM_QUIT != msg.message)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            // handle mouse press ansynchronously
            UINT mouseButton = (GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON);
            if (g_hasFocus && (0x8000 & GetAsyncKeyState(mouseButton)))
            {
                POINT mousePos;
                if (GetCursorPos(&mousePos))
                {
                    if (g_mouseState.m_dragging)
                    {
                        g_mouseState.move.x = mousePos.x - g_mouseState.pos.x;
                        g_mouseState.move.y = mousePos.y - g_mouseState.pos.y;
                        g_mouseState.pos = mousePos;
                    }
                    else
                    {
                        RECT guiRect = g_pScene->GetGuiRect();
                        POINT screenPos = mousePos;
                        ScreenToClient(hWnd, &screenPos);
                        if (((guiRect.right) < screenPos.x) || (guiRect.bottom < screenPos.y))
                        {
                            g_mouseState.pos = mousePos;
                            g_mouseState.move.x = 0;
                            g_mouseState.move.y = 0;
                            g_mouseState.m_dragging = true;
                        } // end if not within the GUI region
                    } // end if dragging vs. starting
                } // end if got mouse pos
            }
            else
            {
                g_mouseState.m_dragging = false;
            }

            if (g_keyState.m_anyKeyDown)
            {
                g_pScene->MoveView(
                    g_keyState.key.left - g_keyState.key.right,
                    g_keyState.key.down - g_keyState.key.up,
                    g_keyState.key.forward - g_keyState.key.back);
                g_pScene->RotateViewKey(g_keyState.key.rotxl - g_keyState.key.rotxr, g_keyState.key.rotyl - g_keyState.key.rotyr, g_keyState.key.rotzl - g_keyState.key.rotzr);
            }
            if (g_mouseState.m_dragging)
            {
                g_pScene->RotateViewPixels(g_mouseState.move.y, g_mouseState.move.x);
                g_mouseState.move = POINT{ 0,0 };
            }

            bool drawSuccess = g_pScene->Draw();
            if (!drawSuccess)
            {
                delete g_pScene;
                g_pScene = nullptr;
                MessageBox(0, L"Device Lost", L"ERROR", MB_OK);
                exit(-1);
            }
        }
    }

    if (g_pScene)
    {
        if (args.m_exitImageFileName.size())
        {
            g_pScene->ScreenShot(args.m_exitImageFileName);
        }

        delete g_pScene;
    }

    UnregisterClass(wcex.lpszClassName, hInstance);

    return 0;
}
