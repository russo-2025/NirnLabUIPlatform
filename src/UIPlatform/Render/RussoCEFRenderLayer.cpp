#include "RussoCEFRenderLayer.h"
#include "Utils/D3D11Utils.h"
#include "Render/DebugRenderLayer.h"

#define FATAL_ERROR(...)                           \
    {                                              \
        const auto msg = fmt::format(__VA_ARGS__); \
        spdlog::error(msg);                        \
        SKSE::stl::report_and_fail(msg.c_str());   \
    }

namespace NL::Render
{
    std::shared_ptr<RussoCEFRenderLayer> RussoCEFRenderLayer::make_shared()
    {
        const auto cefRender = new RussoCEFRenderLayer();
        cefRender->AddRef();
        return std::shared_ptr<RussoCEFRenderLayer>(cefRender, RussoCEFRenderLayer::release_shared);
    }

    void RussoCEFRenderLayer::release_shared(RussoCEFRenderLayer* a_render)
    {
        a_render->Release();
    }

    void RussoCEFRenderLayer::Init(RenderData* a_renderData)
    {
        IRenderLayer::Init(a_renderData);

        if (D3D11Hooks::GetHookedSkyrimD3D11Device() != m_renderData->device || D3D11Hooks::GetHookedSkyrimD3D11Device() != (ID3D11Device*)RE::BSGraphics::Renderer::GetRendererData()->forwarder)
        {
            FATAL_ERROR("{}::Init: skyrim d3d11 device does not match hooked device", NameOf(RussoCEFRenderLayer));
        }

        if (!D3D11Utils::IsDxgi11Device(m_renderData->device))
        {
            FATAL_ERROR("{}::Init: skyrim d3d11 device is not a DXGI 1.1 device", NameOf(RussoCEFRenderLayer));
        }

        HRESULT hr = m_renderData->device->QueryInterface(IID_PPV_ARGS(&m_deviceRender));
        if (FAILED(hr))
        {
            FATAL_ERROR("{}::Init: failed QueryInterface() to skyrim ID3D11Device1, code {:X}", NameOf(RussoCEFRenderLayer), hr);
        }

        Microsoft::WRL::ComPtr<ID3D11Device> deviceCopy;

        hr = D3D11Utils::CreateCopyDeviceOnDxgi11Factory(m_renderData->device, deviceCopy, m_immContextCopy);

        if (FAILED(hr) || !deviceCopy)
        {
            FATAL_ERROR("{}::Init: failed CreateCopyDeviceOnDxgi11Factory(), code {:X}", NameOf(RussoCEFRenderLayer), hr);
        }

        Microsoft::WRL::ComPtr<ID3D11Device1> deviceCopy1;
        hr = deviceCopy->QueryInterface(IID_PPV_ARGS(&deviceCopy1));
        if (FAILED(hr))
        {
            FATAL_ERROR("{}::Init: failed QueryInterface() to deviceCopy1 ID3D11Device1, code {:X}", NameOf(RussoCEFRenderLayer), hr);
        }

        // debug
        //D3D11Utils::EnableD3D11InfoQueue(deviceCopy.Get());

        m_deviceCopy = deviceCopy;
        m_deviceCopy1 = deviceCopy1;

        if (!m_deviceCopy || !m_deviceCopy1 || !m_immContextCopy)
        {
            FATAL_ERROR("{}::Init: failed to create D3D11 device or context", NameOf(RussoCEFRenderLayer));
        }
        D3D11_QUERY_DESC queryDesc{};
        queryDesc.Query = D3D11_QUERY_EVENT;
        hr = m_deviceCopy->CreateQuery(&queryDesc, m_copyDoneQuery.GetAddressOf());
        if (FAILED(hr))
        {
            FATAL_ERROR("{}::Init: failed CreateQuery(), code {:X}", NameOf(RussoCEFRenderLayer), hr);
        }

        if (m_renderData->width == 0 || m_renderData->height == 0)
        {
            FATAL_ERROR("{}::Init: invalid texture dimensions (width: {}, height: {})", NameOf(RussoCEFRenderLayer), m_renderData->width, m_renderData->height);
        }

        UINT formatSupport = 0;
        hr = m_deviceCopy->CheckFormatSupport(DXGI_FORMAT_B8G8R8A8_UNORM, &formatSupport);
        if (FAILED(hr) || !(formatSupport & D3D11_FORMAT_SUPPORT_TEXTURE2D) ||
            !(formatSupport & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE))
        {
            FATAL_ERROR("{}::Init: Format not fully supported", NameOf(RussoCEFRenderLayer));
        }

        for (uint32_t i = 0; i < SLOT_COUNT; i++)
        {
            CreateSlot(i);
        }

        m_latestUpdated.store(kInvalid, std::memory_order_relaxed);
        m_latchedSlot = kInvalid;

        spdlog::info("{}::Init: initialized successfully", NameOf(RussoCEFRenderLayer));

        initialized.store(true, std::memory_order_release);

#ifdef __ENABLE_DEBUG_INFO
        static size_t instanceCount = 0;
        instanceID = instanceCount;
        instanceCount++;

        std::wstring updateRateText = L"UpdateRate[" + std::to_wstring(instanceID) + L"]: " + std::to_wstring(0);
        updateRateTextIndex = Render::DebugRenderLayer::GetSingleton()->AddTextLine(std::move(updateRateText));

        std::wstring delayText = L"Delay[" + std::to_wstring(instanceID) + L"]: " + std::to_wstring(0);
        delayTextIndex = Render::DebugRenderLayer::GetSingleton()->AddTextLine(std::move(delayText));
#endif
    }

    const inline ::DirectX::SimpleMath::Vector2 _Cef_Menu_Draw_Vector = {0.f, 0.f};
    void RussoCEFRenderLayer::Draw()
    {
        if (!m_isVisible)
        {
            return;
        }

        RussoCEFRenderLayer::Slot* slot = AcquireFrame();
        if (!slot)
        {
            return;
        }

        ID3D11ShaderResourceView* srv = slot->srvC.Get();

        if (!srv)
        {
            return;
        }


#ifdef __ENABLE_DEBUG_INFO
        frames++;
        auto currentTime = std::chrono::high_resolution_clock::now();

        delayCount++;
        totalDelayMS += std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - slot->updateTime).count();

        double elapsed = std::chrono::duration<double>(currentTime - lastTime).count();
        if (elapsed >= 1.0)
        {
            std::wstring updateRateText = L"UpdateRate[" + std::to_wstring(instanceID) + L"]: " + std::to_wstring(frames);
            Render::DebugRenderLayer::GetSingleton()->UpdateTextLine(updateRateTextIndex, std::move(updateRateText));

            std::wstring delayText = L"Delay[" + std::to_wstring(instanceID) + L"]: " + std::to_wstring(totalDelayMS / delayCount);
            Render::DebugRenderLayer::GetSingleton()->UpdateTextLine(delayTextIndex, std::move(delayText));

            delayCount = 0;
            totalDelayMS = 0;
            frames = 0;
            lastTime = currentTime;
        }
#endif

        m_renderData->spriteBatch->Begin(::DirectX::SpriteSortMode_Deferred, m_renderData->commonStates->NonPremultiplied());

        m_renderData->spriteBatch->Draw(
            srv,
            _Cef_Menu_Draw_Vector,
            nullptr,
            ::DirectX::Colors::White,
            0.f);

        m_renderData->spriteBatch->End();
    }

    void RussoCEFRenderLayer::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect)
    {
        rect = m_renderData ? CefRect(0, 0, m_renderData->width, m_renderData->height) : CefRect(0, 0, 800, 600);
    }

    void RussoCEFRenderLayer::OnPaint(
        CefRefPtr<CefBrowser> browser,
        PaintElementType type,
        const RectList& dirtyRects,
        const void* buffer,
        int width,
        int height)
    {
    }

    void RussoCEFRenderLayer::OnAcceleratedPaint(
        CefRefPtr<CefBrowser> browser,
        PaintElementType type,
        const RectList& dirtyRects,
        const CefAcceleratedPaintInfo& info)
    {
        if (type == PET_POPUP || !initialized.load(std::memory_order_acquire))
        {
            return;
        }

        ID3D11Texture2D* tex = nullptr;
        auto hr = m_deviceCopy1->OpenSharedResource1(info.shared_texture_handle, IID_PPV_ARGS(&tex));
        if (FAILED(hr))
        {
            spdlog::error("{}: failed OpenSharedResource(), code {:X}", NameOf(RussoCEFRenderLayer), hr);
            return;
        }

        UpdateFrame(tex);

        tex->Release();
    }

    std::optional<uint32_t> RussoCEFRenderLayer::ReserveSlotForWrite()
    {
        const uint32_t start = m_nextWrite.load(std::memory_order_relaxed);

        // first try to find a completely free slot
        for (uint32_t a = 0; a < SLOT_COUNT; ++a)
        {
            const uint32_t idx = (start + a) % SLOT_COUNT;
            auto& s = m_slots[idx];
            if (s.mutexP->AcquireSync(KM_PRODUCER, 0) == S_OK)
            {
                return idx;
            }
        }

        // then try to find a slot that is not latched by consumer
        const uint32_t latched = m_latchedSlot.load(std::memory_order_acquire);
        for (uint32_t a = 0; a < SLOT_COUNT; ++a)
        {
            const uint32_t idx = (start + a) % SLOT_COUNT;
            if (idx == latched)
            {
                continue;
            }

            // try to acquire with key 1 (in use by consumer)
            auto& s = m_slots[idx];
            if (s.mutexP->AcquireSync(KM_CONSUMER, 0) == S_OK)
            {
                // release key 0 (free) if held
                s.mutexP->ReleaseSync(KM_PRODUCER);
                // now try to acquire with key 0 (free)
                if (s.mutexP->AcquireSync(KM_PRODUCER, 0) == S_OK)
                {
                    return idx;
                }
            }
        }

        return std::nullopt;
    }

    bool RussoCEFRenderLayer::CopyAndPublish(uint32_t idx, ID3D11Texture2D* src)
    {
        auto& s = m_slots[idx];

        D3D11_TEXTURE2D_DESC da{}, db{};
        s.texP->GetDesc(&da);
        src->GetDesc(&db);
        if (da.Width != db.Width || da.Height != db.Height || da.Format != db.Format)
        {
            spdlog::error("{}: texture description mismatch during CopyAndPublish", NameOf(RussoCEFRenderLayer));
            s.mutexP->ReleaseSync(KM_PRODUCER);
            return false;
        }

        m_immContextCopy->CopyResource(s.texP.Get(), src);

        m_immContextCopy->End(m_copyDoneQuery.Get());
        while (S_OK != m_immContextCopy->GetData(m_copyDoneQuery.Get(), nullptr, 0, 0))
        {
            std::this_thread::yield();
        }

#ifdef __ENABLE_DEBUG_INFO
        s.updateTime = std::chrono::high_resolution_clock::now();
#endif
        s.mutexP->ReleaseSync(KM_CONSUMER);

        m_latestUpdated.store(idx, std::memory_order_release);
        uint32_t next_id = (idx + 1) % SLOT_COUNT;
        m_nextWrite.store(next_id, std::memory_order_relaxed);

        return true;
    }

    bool RussoCEFRenderLayer::UpdateFrame(ID3D11Texture2D* src)
    {
        if (!src)
        {
            return false;
        }

        auto slot = ReserveSlotForWrite();
        if (!slot.has_value())
        {
            return false;
        }

        return CopyAndPublish(slot.value(), src);
    }

    RussoCEFRenderLayer::Slot* RussoCEFRenderLayer::AcquireFrame()
    {
        const uint32_t latest = m_latestUpdated.load(std::memory_order_acquire);
        uint32_t latched = m_latchedSlot.load(std::memory_order_relaxed);

        if (latest == kInvalid)
        {
            return (latched == kInvalid) ? nullptr : &m_slots[latched];
        }

        if (latched == latest)
        {
            return &m_slots[latched];
        }

        if (m_slots[latest].mutexC->AcquireSync(KM_CONSUMER, 0) == S_OK)
        {
            if (latched != kInvalid)
            {
                m_slots[latched].mutexC->ReleaseSync(KM_PRODUCER);
            }

            m_latchedSlot.store(latest, std::memory_order_release);
            return &m_slots[latest];
        }

        return (latched == kInvalid) ? nullptr : &m_slots[latched];
    }

    void RussoCEFRenderLayer::CreateSlot(uint32_t i)
    {
        D3D11_TEXTURE2D_DESC td{};
        ZeroMemory(&td, sizeof(D3D11_TEXTURE2D_DESC));
        td.Width = m_renderData->width;
        td.Height = m_renderData->height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        // td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; //old format
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.SampleDesc.Quality = 0;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.CPUAccessFlags = 0;
        td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

        const UINT dataSize = m_renderData->width * m_renderData->height * 4;
        std::vector<uint8_t> initialData(dataSize, 0);

        D3D11_SUBRESOURCE_DATA subresourceData{};
        subresourceData.pSysMem = initialData.data();
        subresourceData.SysMemPitch = m_renderData->width * 4;
        subresourceData.SysMemSlicePitch = dataSize;

        m_slots[i] = Slot{};

        // producer - create texture
        auto hr = m_deviceCopy->CreateTexture2D(&td, &subresourceData, m_slots[i].texP.ReleaseAndGetAddressOf());
        if (FAILED(hr))
        {
            FATAL_ERROR("{}::CreateSlot: failed CreateTexture2D() for producer, code {:X}", NameOf(RussoCEFRenderLayer), hr);
        }

        // producer - get mutex
        hr = m_slots[i].texP.As(&m_slots[i].mutexP);
        if (FAILED(hr))
        {
            FATAL_ERROR("{}::CreateSlot: failed QueryInterface() for producer mutex, code {:X}", NameOf(RussoCEFRenderLayer), hr);
        }

        // producer - create shared handle
        Microsoft::WRL::ComPtr<IDXGIResource1> dxgiRes;
        hr = m_slots[i].texP.As(&dxgiRes);
        if (FAILED(hr))
        {
            FATAL_ERROR("{}::CreateSlot: failed QueryInterface() for IDXGIResource, code {:X}", NameOf(RussoCEFRenderLayer), hr);
        }

        hr = dxgiRes->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &m_slots[i].sharedHandle);
        if (FAILED(hr) || !m_slots[i].sharedHandle)
        {
            FATAL_ERROR("{}::CreateSlot: failed GetSharedHandle(), code {:X}", NameOf(RussoCEFRenderLayer), hr);
        }

        // consumer - open shared texture by handle
        hr = m_deviceRender->OpenSharedResource1(m_slots[i].sharedHandle, __uuidof(ID3D11Texture2D), (void**)m_slots[i].texC.ReleaseAndGetAddressOf());
        if (FAILED(hr))
        {
            FATAL_ERROR("{}::CreateSlot: failed OpenSharedResource() for consumer, code {:X}", NameOf(RussoCEFRenderLayer), hr);
        }

        // consumer - get mutex
        hr = m_slots[i].texC.As(&m_slots[i].mutexC);
        if (FAILED(hr))
        {
            FATAL_ERROR("{}::CreateSlot: failed QueryInterface() for consumer mutex, code {:X}", NameOf(RussoCEFRenderLayer), hr);
        }

        // consumer - create SRV
        D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
        sd.Format = td.Format;
        sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        sd.Texture2D.MostDetailedMip = 0;
        sd.Texture2D.MipLevels = 1;

        hr = m_deviceRender->CreateShaderResourceView(m_slots[i].texC.Get(), &sd, m_slots[i].srvC.ReleaseAndGetAddressOf());
        if (FAILED(hr))
        {
            FATAL_ERROR("{}::CreateSlot: failed CreateShaderResourceView(), code {:X}", NameOf(RussoCEFRenderLayer), hr);
        }

        hr = m_slots[i].mutexC->AcquireSync(KM_CONSUMER, 0);
        if (hr == S_OK)
        {
            m_slots[i].mutexC->ReleaseSync(KM_PRODUCER);
        }
    }
}