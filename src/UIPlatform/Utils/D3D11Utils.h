#pragma once

namespace NL::D3D11Utils
{
    bool IsDxgi11Device(ID3D11Device* dev);

    // debug
    void EnableD3D11InfoQueue(ID3D11Device* device);

    LUID GetAdapterLuidFromDevice(ID3D11Device* dev);

    HRESULT FindAdapter1ByLuid(const LUID& luid, IDXGIAdapter1** out);

    HRESULT CreateCopyDeviceOnDxgi11Factory(
        ID3D11Device* renderDevice,
        Microsoft::WRL::ComPtr<ID3D11Device>& deviceCopy,
        Microsoft::WRL::ComPtr<ID3D11DeviceContext>& contextCopy);

    //Microsoft::WRL::ComPtr<IDXGIAdapter> GetAdapterFromDevice(ID3D11Device* d3dDevice);

    HRESULT CreateD3D11DeviceFromAdapter(
        const Microsoft::WRL::ComPtr<IDXGIAdapter>& adapter,
        Microsoft::WRL::ComPtr<ID3D11Device>& device,
        Microsoft::WRL::ComPtr<ID3D11DeviceContext>& context,
        D3D_FEATURE_LEVEL* outFeatureLevel = nullptr,
        UINT extraFlags = 0 // e.g., D3D11_CREATE_DEVICE_BGRA_SUPPORT for D2D interop
    );
} // namespace NL::D3D11Utils

namespace NL::D3D11Hooks
{
    bool Init();
    bool Shutdown();
    ID3D11Device* GetHookedSkyrimD3D11Device();
} // namespace NL::D3D11Hooks


