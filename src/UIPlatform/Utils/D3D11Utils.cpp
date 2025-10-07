#include "D3D11Utils.h"
#include <detours\detours.h>

namespace NL::D3D11Utils
{
    bool IsDxgi11Device(ID3D11Device* dev)
    {
        Microsoft::WRL::ComPtr<IDXGIDevice> x;
        if (FAILED(dev->QueryInterface(IID_PPV_ARGS(&x))))
            return false;
        Microsoft::WRL::ComPtr<IDXGIAdapter> a;
        x->GetAdapter(&a);
        Microsoft::WRL::ComPtr<IDXGIFactory1> f1;
        return SUCCEEDED(a->GetParent(IID_PPV_ARGS(&f1))); // false => фабрика DXGI 1.0
    }

    void EnableD3D11InfoQueue(ID3D11Device* device)
    {
        Microsoft::WRL::ComPtr<ID3D11InfoQueue> iq;
        if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&iq))))
        {
            // Прерываться на серьёзных проблемах
            iq->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            iq->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE);

            // (необязательно) приглушить «шумной» WARNING по вашему вкусу
            D3D11_MESSAGE_ID hide[] = {
                D3D11_MESSAGE_ID_DEVICE_OMSETRENDERTARGETS_HAZARD};
            D3D11_INFO_QUEUE_FILTER f = {};
            f.DenyList.NumIDs = _countof(hide);
            f.DenyList.pIDList = hide;
            iq->AddStorageFilterEntries(&f);
        }
    }

    LUID GetAdapterLuidFromDevice(ID3D11Device* dev)
    {
        Microsoft::WRL::ComPtr<IDXGIDevice> xgi;
        dev->QueryInterface(IID_PPV_ARGS(&xgi));
        Microsoft::WRL::ComPtr<IDXGIAdapter> adp;
        xgi->GetAdapter(&adp);
        DXGI_ADAPTER_DESC desc{};
        adp->GetDesc(&desc);
        return desc.AdapterLuid;
    }

    HRESULT FindAdapter1ByLuid(const LUID& luid, IDXGIAdapter1** out)
    {
        *out = nullptr;
        Microsoft::WRL::ComPtr<IDXGIFactory1> f;
        HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&f)); // DXGI 1.1
        if (FAILED(hr))
            return hr;

        for (UINT i = 0;; ++i)
        {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> a1;

            if (f->EnumAdapters1(i, &a1) == DXGI_ERROR_NOT_FOUND)
                break;

            DXGI_ADAPTER_DESC1 d{};
            a1->GetDesc1(&d);
            if (d.AdapterLuid.HighPart == luid.HighPart && d.AdapterLuid.LowPart == luid.LowPart)
            {
                *out = a1.Detach();
                return S_OK;
            }
        }

        return DXGI_ERROR_NOT_FOUND;
    }

    HRESULT CreateCopyDeviceOnDxgi11Factory(
        ID3D11Device* renderDevice,
        Microsoft::WRL::ComPtr<ID3D11Device>& deviceCopy,
        Microsoft::WRL::ComPtr<ID3D11DeviceContext>& contextCopy)
    {
        const LUID luid = GetAdapterLuidFromDevice(renderDevice); // LUID исходного девайса
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter1;
        HRESULT hr = FindAdapter1ByLuid(luid, &adapter1); // Тот же GPU, но от DXGI 1.1+
        if (FAILED(hr))
        {
            return hr;
        }

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT; // для BGRA/интеропа
        flags |= D3D11_CREATE_DEVICE_DEBUG;

        static const D3D_FEATURE_LEVEL fls[] = {
            D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
        D3D_FEATURE_LEVEL flOut{};
        Microsoft::WRL::ComPtr<ID3D11Device> dev;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;

        hr = D3D11CreateDevice(
            adapter1.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, fls, _countof(fls), D3D11_SDK_VERSION, &dev, &flOut, &ctx);

        if (FAILED(hr))
        {
            return hr;
        }

        deviceCopy = dev;
        contextCopy = ctx;

        return S_OK;
    }
    /*
    Microsoft::WRL::ComPtr<IDXGIAdapter> GetAdapterFromDevice(ID3D11Device* d3dDevice)
    {
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
        HRESULT hr = d3dDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice));

        if (FAILED(hr))
        {
            return nullptr;
        }

        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        hr = dxgiDevice->GetAdapter(&adapter);
        if (FAILED(hr))
        {
            return nullptr;
        }

        return adapter;
    }
    */
    HRESULT CreateD3D11DeviceFromAdapter(
        const Microsoft::WRL::ComPtr<IDXGIAdapter>& adapter,
        Microsoft::WRL::ComPtr<ID3D11Device>& device,
        Microsoft::WRL::ComPtr<ID3D11DeviceContext>& context,
        D3D_FEATURE_LEVEL* outFeatureLevel,
        UINT extraFlags)
    {
        if (!adapter)
        {
            return E_INVALIDARG;
        }

        UINT flags = extraFlags;

#if defined(_DEBUG)
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        // Order from highest to lowest you want to support
        static const D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
            D3D_FEATURE_LEVEL_9_3,
            D3D_FEATURE_LEVEL_9_2,
            D3D_FEATURE_LEVEL_9_1};

        D3D_FEATURE_LEVEL chosenLevel = D3D_FEATURE_LEVEL_11_1;

        // When passing a non-null adapter, DriverType MUST be D3D_DRIVER_TYPE_UNKNOWN
        HRESULT hr = D3D11CreateDevice(
            adapter.Get(),                               // pAdapter
            D3D_DRIVER_TYPE_UNKNOWN,                     // DriverType must be UNKNOWN when adapter is specified
            nullptr,                                     // Software
            flags,                                       // Flags
            featureLevels,                               // pFeatureLevels
            static_cast<UINT>(std::size(featureLevels)), // FeatureLevels
            D3D11_SDK_VERSION,                           // SDKVersion
            device.ReleaseAndGetAddressOf(),             // ppDevice
            &chosenLevel,                                // pFeatureLevel
            context.ReleaseAndGetAddressOf()             // ppImmediateContext
        );

        if (SUCCEEDED(hr) && outFeatureLevel)
        {
            *outFeatureLevel = chosenLevel;
        }

        return hr;
    }
} // namespace NL::D3D11Utils

namespace NL::D3D11Hooks
{
    /*typedef HRESULT(WINAPI* D3D11CreateDevice_t)(
        IDXGIAdapter*,
        D3D_DRIVER_TYPE,
        HMODULE,
        UINT,
        const D3D_FEATURE_LEVEL*,
        UINT,
        UINT,
        ID3D11Device**,
        D3D_FEATURE_LEVEL*,
        ID3D11DeviceContext**);*/
    typedef HRESULT(WINAPI* D3D11CreateDeviceAndSwapChain_t)(
        IDXGIAdapter*,
        D3D_DRIVER_TYPE,
        HMODULE,
        UINT,
        const D3D_FEATURE_LEVEL*,
        UINT,
        UINT,
        const DXGI_SWAP_CHAIN_DESC*,
        IDXGISwapChain**,
        ID3D11Device**,
        D3D_FEATURE_LEVEL*,
        ID3D11DeviceContext**);

    // The original function pointer
    //D3D11CreateDevice_t Original_D3D11CreateDevice = nullptr;
    D3D11CreateDeviceAndSwapChain_t Original_D3D11CreateDeviceAndSwapChain = nullptr;
    /*
    HRESULT WINAPI Hooked_D3D11CreateDevice(
        IDXGIAdapter* pAdapter,
        D3D_DRIVER_TYPE DriverType,
        HMODULE Software,
        UINT Flags,
        const D3D_FEATURE_LEVEL* pFeatureLevels,
        UINT FeatureLevels,
        UINT SDKVersion,
        ID3D11Device** ppDevice,
        D3D_FEATURE_LEVEL* pFeatureLevel,
        ID3D11DeviceContext** ppImmediateContext)
    {
        // Log or modify parameters before calling the original function
        spdlog::info("D3D11CreateDevice called with IDXGIAdapter: 0x{:X}, SDKVersion: 0x{:X} DriverType: {}", (size_t)pAdapter, SDKVersion, (uint64_t)DriverType);

        // Call the original function
        HRESULT hr = Original_D3D11CreateDevice(
            pAdapter,
            DriverType,
            Software,
            Flags,
            pFeatureLevels,
            FeatureLevels,
            SDKVersion,
            ppDevice,
            pFeatureLevel,
            ppImmediateContext);

        // Post-processing after the original call
        if (SUCCEEDED(hr))
        {
            spdlog::info("D3D11Device created successfully!");
            // You can now modify or inspect the created device/context if needed
        }
        else
        {
            spdlog::error("D3D11Device creation failed with HRESULT: 0x{:X}", hr);
        }

        spdlog::info("Device Ptr: 0x{:X}", (size_t)(*ppDevice));
        spdlog::info("Device Feature Level: 0x{:X}", (size_t)(*ppDevice)->GetFeatureLevel());
        spdlog::info("Flags: 0x{:X}", (size_t)(*ppDevice)->GetCreationFlags());

        return hr;
    }
    */
    HRESULT WINAPI Hooked_D3D11CreateDeviceAndSwapChain(
        IDXGIAdapter* pAdapter,
        D3D_DRIVER_TYPE DriverType,
        HMODULE Software,
        UINT Flags,
        const D3D_FEATURE_LEVEL* pFeatureLevels,
        UINT FeatureLevels,
        UINT SDKVersion,
        const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
        IDXGISwapChain** ppSwapChain,
        ID3D11Device** ppDevice,
        D3D_FEATURE_LEVEL* pFeatureLevel,
        ID3D11DeviceContext** ppImmediateContext)
    {
        static bool firstTime = true;

        // Log or modify parameters before the original call
        spdlog::info("D3D11CreateDeviceAndSwapChain called! IDXGIAdapter: 0x{:X}, SDKVersion: 0x{:X}, DriverType: 0x{:X}", (size_t)pAdapter, SDKVersion, (size_t)DriverType);

        if (pSwapChainDesc != nullptr)
        {
            spdlog::info("Window Handle: 0x{:X}, Resolution: {}x{}", (size_t)pSwapChainDesc->OutputWindow, pSwapChainDesc->BufferDesc.Width, pSwapChainDesc->BufferDesc.Height);
        }

        if (firstTime)
        {
            // first call == skyrim d3d11 device
            firstTime = false;

            // Replace the adapter with a DXGI 1.1+ version if available
            DXGI_ADAPTER_DESC adapterDesc;
            pAdapter->GetDesc(&adapterDesc);

            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter1;
            HRESULT hr = D3D11Utils::FindAdapter1ByLuid(adapterDesc.AdapterLuid, &adapter1); // Тот же GPU, но от DXGI 1.1+
            if (FAILED(hr))
            {
                return hr;
            }

            hr = Original_D3D11CreateDeviceAndSwapChain(
                adapter1.Get(),
                DriverType,
                Software,
                Flags,
                pFeatureLevels,
                FeatureLevels,
                SDKVersion,
                pSwapChainDesc,
                ppSwapChain,
                ppDevice,
                pFeatureLevel,
                ppImmediateContext);

            if (SUCCEEDED(hr))
            {
                spdlog::info("D3D11CreateDeviceAndSwapChain succeeded!");

                // You can modify or store the created objects here
                // For example, you could hook the SwapChain's Present method next

                // Example: Access the device and log info
                if (ppDevice != nullptr && *ppDevice != nullptr)
                {
                    spdlog::info("Device Ptr: 0x{:X}", (size_t)(*ppDevice));
                    spdlog::info("Device Feature Level: 0x{:X}", (size_t)(*ppDevice)->GetFeatureLevel());
                    spdlog::info("Flags: 0x{:X}", (size_t)(*ppDevice)->GetCreationFlags());
                }
            }
            else
            {
                spdlog::error("D3D11CreateDeviceAndSwapChain failed!");
            }

            return hr;
        }
        else
        {
            return Original_D3D11CreateDeviceAndSwapChain(
                pAdapter,
                DriverType,
                Software,
                Flags,
                pFeatureLevels,
                FeatureLevels,
                SDKVersion,
                pSwapChainDesc,
                ppSwapChain,
                ppDevice,
                pFeatureLevel,
                ppImmediateContext);
        }
    }

    bool Init()
    {
        // Get a handle to the D3D11 DLL
        HMODULE d3d11Module = GetModuleHandleA("d3d11.dll");
        if (!d3d11Module)
        {
            d3d11Module = LoadLibraryA("d3d11.dll");
            if (!d3d11Module)
            {
                spdlog::error("Failed to load d3d11.dll");
                return false;
            }
        }
        /*
        // Get the address of the original D3D11CreateDevice function
        Original_D3D11CreateDevice = reinterpret_cast<D3D11CreateDevice_t>(
            GetProcAddress(d3d11Module, "D3D11CreateDevice"));

        if (!Original_D3D11CreateDevice)
        {
            spdlog::error("Failed to get address of D3D11CreateDevice");
            return false;
        }
        */
        // Get the address of the original D3D11CreateDeviceAndSwapChain function
        Original_D3D11CreateDeviceAndSwapChain = reinterpret_cast<D3D11CreateDeviceAndSwapChain_t>(
            GetProcAddress(d3d11Module, "D3D11CreateDeviceAndSwapChain"));

        if (!Original_D3D11CreateDeviceAndSwapChain)
        {
            spdlog::error("Failed to get address of D3D11CreateDeviceAndSwapChain");
            return false;
        }

        // Start the transaction for installing the hook
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        // Attach the detour
        //DetourAttach(&(PVOID&)Original_D3D11CreateDevice, Hooked_D3D11CreateDevice);
        DetourAttach(&(PVOID&)Original_D3D11CreateDeviceAndSwapChain, Hooked_D3D11CreateDeviceAndSwapChain);

        // Commit the transaction
        LONG error = DetourTransactionCommit();
        if (error != NO_ERROR)
        {
            spdlog::error("Failed to install D3D11CreateDevice and D3D11CreateDeviceAndSwapChain hook, error: {}", error);
            return false;
        }

        spdlog::info("D3D11CreateDevice and D3D11CreateDeviceAndSwapChain hook installed successfully");
        return true;
    }

    bool Shutdown()
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        // Detach the detour
        //DetourDetach(&(PVOID&)Original_D3D11CreateDevice, Hooked_D3D11CreateDevice);
        DetourDetach(&(PVOID&)Original_D3D11CreateDeviceAndSwapChain, Hooked_D3D11CreateDeviceAndSwapChain);

        LONG error = DetourTransactionCommit();
        if (error != NO_ERROR)
        {
            spdlog::error("Failed to remove D3D11CreateDevice and D3D11CreateDeviceAndSwapChain hook, error: {}", error);
            return false;
        }

        spdlog::info("D3D11CreateDevice and D3D11CreateDeviceAndSwapChain hook removed successfully");
        return true;
    }
} // namespace NL::D3D11Hooks