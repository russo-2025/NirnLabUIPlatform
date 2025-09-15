#include "NirnLabCefApp.h"

#include <dxgi1_6.h>
#pragma comment(lib, "dxgi.lib")
#include <optional>
#include <codecvt>
#include <string>

namespace
{
    using Microsoft::WRL::ComPtr;

    enum class GpuType
    {
        Unknown,
        Integrated,
        Discrete,
        External,
        Virtual,
        Software
    };

    struct DxgiAdapterInfo
    {
        std::wstring name;    // DXGI_ADAPTER_DESC3::Description
        LUID luid{};          // DXGI LUID
        UINT vendorId{};      // PCI vendor id
        UINT deviceId{};      // PCI device id
        SIZE_T dedicatedMB{}; // DedicatedVideoMemory / MB
        SIZE_T sharedMB{};    // SharedSystemMemory / MB
        GpuType type{GpuType::Unknown};
        UINT index{}; // Порядковый номер при EnumAdapters1

        std::string CefLuidArg() const
        {
            return std::to_string(luid.HighPart) + "," +
                   std::to_string(luid.LowPart);
        }
    };

    inline std::string VendorToStr(UINT vid)
    {
        switch (vid)
        {
        case 0x10DE:
            return "NVIDIA";
        case 0x8086:
            return "Intel";
        case 0x1002:
            return "AMD/ATI";
        default:
            return "Vendor 0x" + std::to_string(vid);
        }
    }

    std::string wstringToUtf8(const std::wstring& wstr)
    {
        if (wstr.empty())
        {
            return {};
        }

        // Get the required buffer size for the UTF-8 string
        int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
        if (sizeNeeded <= 0)
        {
            return {};
        }

        // Convert the wide string to a UTF-8 string
        std::string utf8Str(sizeNeeded, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), &utf8Str[0], sizeNeeded, nullptr, nullptr);

        return utf8Str;
    }

    inline std::string TypeToStr(GpuType type)
    {
        switch (type)
        {
        case GpuType::Integrated:
            return "Integrated";
        case GpuType::Discrete:
            return "Discrete";
        case GpuType::External:
            return "External";
        case GpuType::Virtual:
            return "Virtual";
        case GpuType::Software:
            return "Software";
        default:
            return "Unknown";
        }
    }

    inline GpuType ClassifyByDxgiMemoryHeuristics(const DXGI_ADAPTER_DESC3& d3)
    {
        if (d3.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)
            return GpuType::Software;
        if (d3.Flags & DXGI_ADAPTER_FLAG3_REMOTE)
            return GpuType::Virtual;
        if (d3.DedicatedVideoMemory >= (256ull << 20))
            return GpuType::Discrete;
        if (d3.DedicatedVideoMemory == 0 && d3.SharedSystemMemory > 0)
            return GpuType::Integrated;
        return GpuType::Unknown;
    }

    // Возвращает high-performance адаптер (обычно дискретная), с фолбэком по максимальной DedicatedVideoMemory.
    inline std::optional<DxgiAdapterInfo> GetHighPerfAdapterInfoDXGI()
    {
        UINT flags = 0;

        ComPtr<IDXGIFactory1> f1;
        if (FAILED(CreateDXGIFactory2(flags, IID_PPV_ARGS(&f1))))
        {
            return std::nullopt;
        }

        ComPtr<IDXGIFactory6> f6;
        f1.As(&f6);

        auto fillInfo = [](IDXGIAdapter1* ad1, UINT index) -> DxgiAdapterInfo {
            DxgiAdapterInfo out{};
            DXGI_ADAPTER_DESC3 d3{};
            if (ComPtr<IDXGIAdapter4> ad4; SUCCEEDED(ad1->QueryInterface(IID_PPV_ARGS(&ad4))) && ad4)
            {
                ad4->GetDesc3(&d3);
            }
            else
            {
                DXGI_ADAPTER_DESC1 d1{};
                ad1->GetDesc1(&d1);
                d3.AdapterLuid = d1.AdapterLuid;
                lstrcpynW(d3.Description, d1.Description, ARRAYSIZE(d3.Description));
                d3.DedicatedVideoMemory = d1.DedicatedVideoMemory;
                d3.SharedSystemMemory = d1.SharedSystemMemory;
                d3.VendorId = d1.VendorId;
                d3.DeviceId = d1.DeviceId;
                d3.Flags = (DXGI_ADAPTER_FLAG3)(d1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE);
            }

            out.name = d3.Description;
            out.luid = d3.AdapterLuid;
            out.vendorId = d3.VendorId;
            out.deviceId = d3.DeviceId;
            out.dedicatedMB = static_cast<SIZE_T>(d3.DedicatedVideoMemory / (1024ull * 1024ull));
            out.sharedMB = static_cast<SIZE_T>(d3.SharedSystemMemory / (1024ull * 1024ull));
            out.type = ClassifyByDxgiMemoryHeuristics(d3);
            out.index = index;
            return out;
        };

        // 1) Лучший путь — спросить high-performance у фабрики v1.6+
        if (f6)
        {
            ComPtr<IDXGIAdapter1> highPerf;
            if (SUCCEEDED(f6->EnumAdapterByGpuPreference(
                    0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&highPerf))))
            {
                DXGI_ADAPTER_DESC1 d1{};
                highPerf->GetDesc1(&d1);
                if (!(d1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
                {
                    return fillInfo(highPerf.Get(), /*index=*/UINT_MAX);
                }
            }
        }

        // 2) Фолбэк — выбираем адаптер с максимальной DedicatedVideoMemory (не software)
        SIZE_T best = 0;
        ComPtr<IDXGIAdapter1> bestAd;
        UINT bestIndex = 0;
        for (UINT i = 0;; ++i)
        {
            ComPtr<IDXGIAdapter1> ad;
            if (f1->EnumAdapters1(i, &ad) == DXGI_ERROR_NOT_FOUND)
                break;
            DXGI_ADAPTER_DESC1 d1{};
            ad->GetDesc1(&d1);
            if (d1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                continue;
            if (d1.DedicatedVideoMemory > best)
            {
                best = d1.DedicatedVideoMemory;
                bestAd = ad;
                bestIndex = i;
            }
        }
        if (bestAd)
        {
            return fillInfo(bestAd.Get(), bestIndex);
        }

        return std::nullopt; // ничего подходящего (только Basic Render Driver и т.п.)
    }
}

namespace NL::CEF
{
    // command line switches https://peter.sh/experiments/chromium-command-line-switches/
    void NirnLabCefApp::OnBeforeCommandLineProcessing(CefString const& process_type, CefRefPtr<CefCommandLine> command_line)
    {
        // disable creation of a GPUCache/ folder on disk
        // command_line->AppendSwitch("disable-gpu-shader-disk-cache");

        // command_line->AppendSwitch("disable-accelerated-video-decode");

        // un-comment to show the built-in Chromium fps meter
        // command_line->AppendSwitch("show-fps-counter");

        // command_line->AppendSwitch("disable-gpu-vsync");

        // Most systems would not need to use this switch - but on older hardware,
        // Chromium may still choose to disable D3D11 for gpu workarounds.
        // Accelerated OSR will not at all with D3D11 disabled, so we force it on.
        command_line->AppendSwitchWithValue("use-angle", "d3d11");

        // tell Chromium to autoplay <video> elements without
        // requiring the muted attribute or user interaction
        command_line->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");

        // Allow local files
        command_line->AppendSwitch("allow-file-access-from-files");
        command_line->AppendSwitch("allow-insecure-localhost");

        // https://chromium.googlesource.com/chromium/src/+/main/docs/process_model_and_site_isolation.md
        command_line->AppendSwitch("disable-site-isolation-for-policy");
        command_line->AppendSwitch("disable-site-isolation-trials");
        command_line->AppendSwitchWithValue("process-per-site", "false");
        // command_line->AppendSwitch("single-process");

        // Allow remote debugging
        command_line->AppendSwitchWithValue("remote-allow-origins", "*");


        std::optional<DxgiAdapterInfo> info = GetHighPerfAdapterInfoDXGI();

        if (info.has_value())
        {
            spdlog::info("Selected video adapter:");
            spdlog::info("  Name      : {}", wstringToUtf8(info->name));
            spdlog::info("  Vendor    : {}", VendorToStr(info->vendorId));
            spdlog::info("  Device ID : {}", info->deviceId);
            spdlog::info("  Type      : {}", TypeToStr(info->type));
            spdlog::info("  Index     : {}", info->index == UINT_MAX ? "HighPerformance" : std::to_string(info->index));
            spdlog::info("  LUID      : {}", info->CefLuidArg());
            spdlog::info("  VRAM      : {} MB", info->dedicatedMB);
            spdlog::info("  SharedMem : {} MB", info->sharedMB);

            command_line->AppendSwitchWithValue("use-adapter-luid", info->CefLuidArg().c_str());
        }
        else
        {
            spdlog::warn("No suitable GPU adapter found. The 'use-adapter-luid' property will be ignored.");
        }
    }

    CefRefPtr<CefBrowserProcessHandler> CEF::NirnLabCefApp::GetBrowserProcessHandler()
    {
        return this;
    }

    void CEF::NirnLabCefApp::OnBeforeChildProcessLaunch(CefRefPtr<CefCommandLine> command_line)
    {
        command_line->AppendSwitchWithValue(IPC_CL_PROCESS_ID_NAME, std::to_string(::GetCurrentProcessId()).c_str());
    }
}
