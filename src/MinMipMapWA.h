#pragma once
#include <d3d12.h>

#define MIN_MIP_MAP_WA_REGION_SIZE 128

class MinMipMapWA
{
public:
    MinMipMapWA(ID3D12Device* in_pDevice, UINT64 in_imageWidth, UINT in_imageHeight);

    ComPtr<ID3D12Resource>& GetResource() { return m_manualMinMipResolve; }

    void WA(ID3D12Resource* out_pMinMipMap, ID3D12GraphicsCommandList* in_pCmdList);

private:
    static const UINT m_hardwareRegionDim{ MIN_MIP_MAP_WA_REGION_SIZE };

    ComPtr<ID3D12RootSignature> m_rootSignature;
    ComPtr<ID3D12PipelineState> m_pipelineState;

    ComPtr<ID3D12PipelineState> m_convertPSO;

    ComPtr<ID3D12Resource> m_manualMinMipResolve;
    ComPtr<ID3D12Resource> m_mipResolve32b;

    ComPtr<ID3D12DescriptorHeap> m_srvHeap;
    ComPtr<ID3D12DescriptorHeap> m_clearUavHeap;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;

    static ComPtr<ID3DBlob> g_vertexShader;
    static ComPtr<ID3DBlob> g_pixelShader;

    bool m_firstFrame{ true };
};
