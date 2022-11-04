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

// plays back trace files from expanse. trace files contain only DirectStorage Requests and Submits
// create trace files with the command line parameter "-captureTrace"
// for example, "expanse.exe -timingStart 100 -timingStop 150 -captureTrace"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <iostream>
#include <dstorage.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <sstream>
#include <filesystem>

#include "DebugHelper.h"
#include "ArgParser.h"
#include "ConfigurationParser.h"
#include "d3dx12.h"
#include "Timer.h"
#include "tracePlayer.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

#define ErrorMessage(...) { std::wcout << AutoString::Concat(__VA_ARGS__); exit(-1); }

//-----------------------------------------------------------------------------
// given a full path or a relative path (e.g. c:\foo or foo)
// return foo or exepath\foo or return an error
//-----------------------------------------------------------------------------
void FindPath(std::wstring& out_path)
{
    // if the desired media path doesn't exist, try looking relative to the executable
    if (!std::filesystem::exists(out_path))
    {
        WCHAR buffer[MAX_PATH];
        GetModuleFileName(nullptr, buffer, _countof(buffer));
        auto path = std::filesystem::path(buffer).remove_filename().append(out_path);
        if (std::filesystem::exists(path))
        {
            out_path = path;
        }
        else
        {
            ErrorMessage("Path not found: \"", out_path, "\" also tried: ", path);
        }
    }
}

//-----------------------------------------------------------------------------
// create device, optionally checking adapter description for e.g. "intel"
//-----------------------------------------------------------------------------
void TracePlayer::CreateDeviceWithName()
{
    UINT flags = 0;
#ifdef _DEBUG
    flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
    if (0 > CreateDXGIFactory2(flags, IID_PPV_ARGS(&m_factory)))
    {
        flags &= ~DXGI_CREATE_FACTORY_DEBUG;
        CreateDXGIFactory2(flags, IID_PPV_ARGS(&m_factory));
    }

    ComPtr<IDXGIAdapter1> adapter;
    std::wstring lowerCaseAdapterDesc = m_params.m_adapterDescription;

    if (lowerCaseAdapterDesc.size())
    {
        for (auto& c : lowerCaseAdapterDesc) { c = ::towlower(c); }
    }

    for (UINT i = 0; m_factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);

        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) || (std::wstring(L"Microsoft Basic Render Driver") == desc.Description))
        {
            continue;
        }

        if (lowerCaseAdapterDesc.size())
        {
            std::wstring description(desc.Description);
            for (auto& c : description) { c = ::towlower(c); }
            std::size_t found = description.find(lowerCaseAdapterDesc);
            if (found == std::string::npos)
            {
                continue;
            }
        }

        D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device));

        // if we care about adapter architecture, check that UMA corresponds to integrated vs. discrete
        if (Params::PreferredArchitecture::NONE != m_params.m_preferredArchitecture)
        {
            D3D12_FEATURE_DATA_ARCHITECTURE archFeatures{};
            m_device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE, &archFeatures, sizeof(archFeatures));

            if (((FALSE == archFeatures.UMA) && (Params::PreferredArchitecture::INTEGRATED == m_params.m_preferredArchitecture)) ||
                ((TRUE == archFeatures.UMA) && (Params::PreferredArchitecture::DISCRETE == m_params.m_preferredArchitecture)))
            {
                // adapter does not match requirements (name and/or architecture)
                m_device = nullptr;
                continue;
            }
        }

        // adapter matches requirements (name and/or architecture), exit loop
        break;
    }

    // get the description from whichever adapter was used to create the device
    if (nullptr != m_device.Get())
    {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        m_params.m_adapterDescription = desc.Description;
    }
    else
    {
        ErrorMessage("No adapter found with name \"", m_params.m_adapterDescription, "\" or architecture \"",
            (Params::PreferredArchitecture::NONE == m_params.m_preferredArchitecture ? "none" :
                (Params::PreferredArchitecture::DISCRETE == m_params.m_preferredArchitecture ? "discrete" : "integrated")),
            "\"\n");
    }
}


//-----------------------------------------------------------------------------
// Create synchronization objects
//-----------------------------------------------------------------------------
void TracePlayer::CreateFence()
{
    ThrowIfFailed(m_device->CreateFence(
        m_fenceValue,
        D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&m_fence)));
    m_fenceValue++;

    // Create an event handle to use for frame synchronization.
    m_fenceEvent = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr)
    {
        ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void TracePlayer::InitDirectStorage()
{
    // initialize to default values
    DSTORAGE_CONFIGURATION dsConfig{};
    DStorageSetConfiguration(&dsConfig);

    ThrowIfFailed(DStorageGetFactory(IID_PPV_ARGS(&m_dsFactory)));

    DSTORAGE_DEBUG debugFlags = DSTORAGE_DEBUG_NONE;
#ifdef _DEBUG
    debugFlags = DSTORAGE_DEBUG_SHOW_ERRORS;
#endif
    m_dsFactory->SetDebugFlags(debugFlags);

    m_dsFactory->SetStagingBufferSize(m_params.m_stagingBufferSizeMB * 1024 * 1024);

    DSTORAGE_QUEUE_DESC queueDesc{};
    queueDesc.Capacity = DSTORAGE_MAX_QUEUE_CAPACITY;
    queueDesc.Priority = DSTORAGE_PRIORITY_NORMAL;
    queueDesc.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    queueDesc.Device = m_device.Get();
    ThrowIfFailed(m_dsFactory->CreateQueue(&queueDesc, IID_PPV_ARGS(&m_dsQueue)));

    ThrowIfFailed(m_device->CreateFence(m_fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
    m_fenceValue++;
}

//-----------------------------------------------------------------------------
// create internal atlas texture
//-----------------------------------------------------------------------------
ID3D12Resource* TracePlayer::CreateDestinationResource(UINT& out_numTiles, DXGI_FORMAT in_format, UINT in_width, UINT in_height, UINT in_subresourceCount)
{
    // create a maximum size reserved resource
    D3D12_RESOURCE_DESC rd = CD3DX12_RESOURCE_DESC::Tex2D(in_format, in_width, in_height);
    rd.MipLevels = (UINT16)in_subresourceCount;

    // Layout must be D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE when creating reserved resources
    rd.Layout = D3D12_TEXTURE_LAYOUT_64KB_UNDEFINED_SWIZZLE;

    // this will only ever be a copy dest
    ID3D12Resource* pResource{ nullptr };
    ThrowIfFailed(m_device->CreateReservedResource(&rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&pResource)));
    m_device->GetResourceTiling(pResource, &out_numTiles, nullptr, nullptr, nullptr, 0, nullptr);

    return pResource;
}

//-----------------------------------------------------------------------------
// map resource using linear assignment order defined by D3D12_REGION_SIZE UseBox = FALSE
// https://docs.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_tile_region_size
//-----------------------------------------------------------------------------
void TracePlayer::UpdateTileMappings(ID3D12Resource* in_pResource, UINT in_tileOffset)
{
    D3D12_SUBRESOURCE_TILING tiling{};
    UINT subresourceCount = in_pResource->GetDesc().MipLevels;
    UINT numTiles{ 0 };
    m_device->GetResourceTiling(in_pResource, &numTiles, nullptr, nullptr, &subresourceCount, 0, &tiling);

    // we are updating a single region: all the tiles
    UINT numResourceRegions = 1;
    D3D12_TILED_RESOURCE_COORDINATE resourceRegionStartCoordinates{ 0, 0, 0, 0 };
    D3D12_TILE_REGION_SIZE resourceRegionSizes{ numTiles, FALSE, 0, 0, 0 };

    // we can do this with a single range
    UINT numRanges = 1;
    std::vector<D3D12_TILE_RANGE_FLAGS>rangeFlags(numRanges, D3D12_TILE_RANGE_FLAG_NONE);
    std::vector<UINT> rangeTileCounts(numRanges, numTiles);

    // D3D12 defines that tiles are linear, not swizzled relative to each other
    // so all the tiles can be mapped in one call
    m_commandQueue->UpdateTileMappings(
        in_pResource,
        numResourceRegions,
        &resourceRegionStartCoordinates,
        &resourceRegionSizes,
        m_heap.Get(),
        (UINT)rangeFlags.size(),
        rangeFlags.data(),
        &in_tileOffset,
        rangeTileCounts.data(),
        D3D12_TILE_MAPPING_FLAG_NONE
    );
}

//-----------------------------------------------------------------------------
// parse trace file in to an efficient internal representation
// creates resources and opens files
//-----------------------------------------------------------------------------
void TracePlayer::LoadTraceFile()
{
    const ConfigurationParser traceFile(m_params.m_filename);

    std::map<UINT64, ID3D12Resource*> dstResources;
    std::map<std::string, IDStorageFile*> srcFiles;

    //---------------------------------
    // create destination resources
    //---------------------------------
    {
        const auto& resources = traceFile.GetRoot()["resources"];
        UINT64 numTilesTotal{ 0 };
        std::vector<UINT> tilesPerResource;
        for (const auto& r : resources)
        {
            UINT numTiles{ 0 };
            ID3D12Resource* pResource = CreateDestinationResource(
                numTiles, (DXGI_FORMAT)r["fmt"].asUInt(),
                r["dim"][0].asUInt(), r["dim"][1].asUInt(), r["dim"][2].asUInt());
            numTilesTotal += numTiles;

            m_dstResources.push_back(pResource);
            dstResources[r["rsrc"].asUInt64()] = pResource;

            tilesPerResource.push_back(numTiles);
        }
        UINT64 heapSize = numTilesTotal * D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
        CD3DX12_HEAP_DESC heapDesc(heapSize, D3D12_HEAP_TYPE_DEFAULT, 0, D3D12_HEAP_FLAG_DENY_BUFFERS | D3D12_HEAP_FLAG_DENY_RT_DS_TEXTURES);
        ThrowIfFailed(m_device->CreateHeap(&heapDesc, IID_PPV_ARGS(&m_heap)));
        UINT tileOffset = 0;
        for (UINT i = 0; i < m_dstResources.size(); i++)
        {
            UpdateTileMappings(m_dstResources[i], tileOffset);
            tileOffset += tilesPerResource[i];
        }
    }

    //---------------------------------
    // create submission array (and open files)
    //---------------------------------
    {
        const auto& submits = traceFile.GetRoot()["submits"];
        for (const auto& s : submits)
        {
            m_submits.resize(m_submits.size() + 1);
            auto& requestArray = m_submits.back();
            for (const auto& r : s)
            {
                Request request{};
                request.m_dstCoord.X = r["coord"][0].asUInt();
                request.m_dstCoord.Y = r["coord"][1].asUInt();
                request.m_dstCoord.Subresource = r["coord"][2].asUInt();
                request.m_pDstResource = dstResources[r["rsrc"].asUInt64()];
                request.m_srcOffset = r["off"].asUInt();
                request.m_numBytes = r["size"].asUInt();
                m_numFileBytesRead += request.m_numBytes;

                if (r.isMember("comp")) request.m_compressionFormat = r["comp"].asUInt();

                const std::string& filename = r["file"].asString();
                auto f = srcFiles.find(filename);
                if (srcFiles.end() == f)
                {
                    std::wstringstream wideFileName;
                    wideFileName << m_params.m_mediaDir << filename.c_str();
                    IDStorageFile* dsFile{ nullptr };
                    if (!std::filesystem::exists(wideFileName.str()))
                    {
                        ErrorMessage("file not found: ", wideFileName.str(), ". Did you set -mediadir?");
                    }
                    ThrowIfFailed(m_dsFactory->OpenFile(wideFileName.str().c_str(), IID_PPV_ARGS(&dsFile)));
                    m_fileHandles.push_back(dsFile);
                    srcFiles[filename] = dsFile;
                    request.m_srcFile = dsFile;
                }
                else
                {
                    request.m_srcFile = f->second;
                }
                m_numBytesWritten += D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;
                requestArray.push_back(request);
            }
            m_numRequestsTotal += s.size();
        }
    }
}

//-----------------------------------------------------------------------------
// execute all requests as quickly as possible
//-----------------------------------------------------------------------------
void TracePlayer::PlaybackTrace()
{
    DSTORAGE_REQUEST request{};
    request.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_TILES;
    request.Destination.Tiles.TileRegionSize = D3D12_TILE_REGION_SIZE{ 1, FALSE, 0, 0, 0 };
    request.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
    request.UncompressedSize = D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES;

    for (const auto& s : m_submits)
    {
        for (const auto& r : s)
        {
            request.Source.File.Source = r.m_srcFile;
            request.Source.File.Offset = r.m_srcOffset;
            request.Source.File.Size = r.m_numBytes;
            request.Destination.Tiles.Resource = r.m_pDstResource;
            request.Destination.Tiles.TiledRegionStartCoordinate = r.m_dstCoord;
            request.Options.CompressionFormat = (DSTORAGE_COMPRESSION_FORMAT)r.m_compressionFormat;

            m_dsQueue->EnqueueRequest(&request);
        }
        m_fenceValue++;
        m_dsQueue->EnqueueSignal(m_fence.Get(), m_fenceValue);
        m_dsQueue->Submit();
    }

    // Wait until the signal command has been processed.
    ThrowIfFailed(m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent));
    WaitForSingleObject(m_fenceEvent, INFINITE);
}

//-----------------------------------------------------------------------------
// TracePlayer constructor/destructor
//-----------------------------------------------------------------------------
TracePlayer::TracePlayer(const Params& in_params) : m_params(in_params)
{
    CreateDeviceWithName();
    CreateFence();
    InitDirectStorage();

    std::cout << "loading/parsing...\n";

    if (m_params.m_inspect)
    {
        Inspect();
    }
    else
    {
        LoadTraceFile();
    }
}

TracePlayer::~TracePlayer()
{
    for (auto p : m_fileHandles)
    {
        p->Close();
        p->Release();
    }
    for (auto p : m_dstResources)
    {
        p->Release();
    }
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
std::wstring AddCommaSeparators(UINT64 in_value)
{
    std::wstring value = std::to_wstring(in_value);
    for (int offset = int(value.size() - 3); offset > 0; offset -= 3)
    {
        value.insert(offset, L",");
    }
    return value;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void TracePlayer::Inspect()
{
    const ConfigurationParser traceFile(m_params.m_filename);

    const auto& submits = traceFile.GetRoot()["submits"];
    size_t numSubmits = submits.size();
    size_t maxSubmit{ 0 };
    size_t minSubmit{ size_t(-1) };

    std::cout << "# requests for each submit: ";
    bool first = true;
    for (const auto& s : submits)
    {
        if (first) { first = false; }
        else { std::cout << ","; }

        size_t numRequests = s.size();
        minSubmit = std::min(minSubmit, numRequests);
        maxSubmit = std::max(maxSubmit, numRequests);
        m_numRequestsTotal += numRequests;

        std::cout << numRequests;

        for (const auto& r : s)
        {
            m_numFileBytesRead += r["size"].asUInt64();
        }
    }

    UINT64 numTiles = m_numRequestsTotal; // FIXME? assumes all requests are single-tile read
    m_numBytesWritten += numTiles * D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES; // FIXME? assumes all requests are single-tile read

    std::cout << std::endl;

    std::cout << "# submits: " << numSubmits << std::endl;
    std::cout << "# requests: " << numTiles << std::endl;
    float avgRequestsPerSubmit = float(numTiles) / float(numSubmits);
    std::wcout << "average requests/submit: " << avgRequestsPerSubmit << " = " << AddCommaSeparators(UINT64(avgRequestsPerSubmit * 64 * 1024)) << " bytes/submit" << std::endl;
    std::cout << "min # requests/1 submit: " << minSubmit << std::endl;
    std::cout << "max # requests/1 submit: " << maxSubmit << std::endl;
    std::wcout << "# bytes ssd (read): " << AddCommaSeparators(m_numFileBytesRead) << std::endl;
    std::wcout << "# bytes gpu (written): " << AddCommaSeparators(m_numBytesWritten) << std::endl;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int main()
{
    TracePlayer::Params tracePlayerParams;

    UINT numItersPlayback{ 4 };
    bool inspect{ false };

    ArgParser argParser;
    tracePlayerParams.m_mediaDir = std::filesystem::current_path();

    //---------------------------
    // parse command line
    //---------------------------
    {
        ArgParser argParser;
        argParser.AddArg(L"-file", [&]
            {
                tracePlayerParams.m_filename = ArgParser::GetNextArg();
                FindPath(tracePlayerParams.m_filename);
            }, tracePlayerParams.m_filename, L"<Required> trace file of DS requests and submits");
        argParser.AddArg(L"-mediaDir", [&]
            {
                tracePlayerParams.m_mediaDir = ArgParser::GetNextArg();
                FindPath(tracePlayerParams.m_mediaDir);
            }, tracePlayerParams.m_mediaDir, L"directory containing texture files");

        argParser.AddArg(L"-staging", tracePlayerParams.m_stagingBufferSizeMB, L"DirectStorage staging buffer size in MB");
        argParser.AddArg(L"-adapter", tracePlayerParams.m_adapterDescription, L"find an adapter containing this string in the description, ignoring case");
        argParser.AddArg(L"-arch", (UINT&)tracePlayerParams.m_preferredArchitecture, L"GPU architecture: don't care (0), discrete (1), integrated (2)");

        argParser.AddArg(L"-iters", numItersPlayback, L"none (0), discrete (1), integrated (2)");
        argParser.AddArg(L"-inspect", tracePlayerParams.m_inspect, L"display information about archive, do not execute");
        argParser.Parse();

        if (0 == tracePlayerParams.m_filename.size())
        {
            ErrorMessage("trace file name not provided (-file filename.json)");
        }
        if (L'\\' != tracePlayerParams.m_mediaDir.back())
        {
            tracePlayerParams.m_mediaDir.append(L"\\");
        }
    }

    //---------------------------
    // play back trace
    //---------------------------
    TracePlayer tracePlayer(tracePlayerParams);

    if (tracePlayerParams.m_inspect)
    {
        return 0;
    }

    std::wcout << "adapter string: " << tracePlayer.GetAdapterDescription().c_str() << "\n";
    std::wcout << "file bytes to read (per iter): " << AddCommaSeparators(tracePlayer.GetNumFileBytesRead()).c_str() << "\n";
    std::cout << "number of requests: " << tracePlayer.GetNumRequests() << "\n";
    std::cout << "staging buffer size MB: " << tracePlayerParams.m_stagingBufferSizeMB << "\n";
    std::cout << "executing trace, # iterations = " << numItersPlayback << "\n";
    Timer timer;
    timer.Start();
    for (UINT i = 0; i < numItersPlayback; i++)
    {
        tracePlayer.PlaybackTrace();
    }
    double seconds = timer.Stop();

    double bytesToBandwidth = numItersPlayback / (1024. * 1024. * seconds);

    std::wcout << "bandwidth (MB/s from disk): " << tracePlayer.GetNumFileBytesRead() * bytesToBandwidth << "\n";
    std::cout << "bandwidth (MB/s uncompressed to GPU): " << tracePlayer.GetNumBytesWritten() * bytesToBandwidth << "\n";
}
