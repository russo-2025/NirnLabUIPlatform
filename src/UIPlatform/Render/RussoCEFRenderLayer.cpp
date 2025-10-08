#include "RussoCEFRenderLayer.h"
#include "Utils/D3D11Utils.h"

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
        
        spdlog::info("game d3d11 device 0x{:X} 0x{:X}", (size_t)a_renderData->device, (size_t)RE::BSGraphics::Renderer::GetSingleton()->GetRendererData()->forwarder);
        if (!D3D11Utils::IsDxgi11Device(m_renderData->device))
        {
            spdlog::error("{}: device is not a DXGI 1.1 device", NameOf(RussoCEFRenderLayer));
            throw std::runtime_error("RussoCEFRenderLayer::Init failed");
        }

        HRESULT hr = m_renderData->device->QueryInterface(IID_PPV_ARGS(&m_deviceRender));
        if (FAILED(hr))
        {
            spdlog::error("{}: failed QueryInterface(), code {:X}", NameOf(RussoCEFRenderLayer), hr);
            throw std::runtime_error("RussoCEFRenderLayer::Init failed");
        }

        /*
        // Create a separate D3D11 device for copying the texture
        Microsoft::WRL::ComPtr<IDXGIAdapter> pAdapter = GetAdapterFromDevice(m_renderData->device);

        if (!pAdapter)
        {
            spdlog::error("{}: failed GetAdapterFromDevice()", NameOf(RussoCEFRenderLayer));
            throw std::runtime_error("RussoCEFRenderLayer::Init failed");
        }

        Microsoft::WRL::ComPtr<ID3D11Device> deviceCopy;

        hr = CreateD3D11DeviceFromAdapter(
            pAdapter,
            deviceCopy,
            m_immContextCopy,
            nullptr,
            D3D11_CREATE_DEVICE_DEBUG | D3D11_CREATE_DEVICE_BGRA_SUPPORT);
        */
        Microsoft::WRL::ComPtr<ID3D11Device> deviceCopy;

        hr = D3D11Utils::CreateCopyDeviceOnDxgi11Factory(m_renderData->device, deviceCopy, m_immContextCopy);

        if (FAILED(hr) || !deviceCopy)
        {
            spdlog::error("{}: failed CreateD3D11DeviceFromAdapter(), code {:X}", NameOf(RussoCEFRenderLayer), hr);
            throw std::runtime_error("RussoCEFRenderLayer::Init failed");
        }

        Microsoft::WRL::ComPtr<ID3D11Device1> deviceCopy1;
        hr = deviceCopy->QueryInterface(IID_PPV_ARGS(&deviceCopy1));
        if (FAILED(hr))
        {
            spdlog::error("{}: failed QueryInterface() to ID3D11Device1, code {:X}", NameOf(RussoCEFRenderLayer), hr);
            throw std::runtime_error("RussoCEFRenderLayer::Init failed");
        }

        D3D11Utils::EnableD3D11InfoQueue(deviceCopy.Get());

        m_deviceCopy = deviceCopy;
        m_deviceCopy1 = deviceCopy1;

        if (!m_deviceCopy || !m_deviceCopy1 || !m_immContextCopy)
        {
            spdlog::error("{}: failed to create D3D11 device or context", NameOf(RussoCEFRenderLayer));
            throw std::runtime_error("RussoCEFRenderLayer::Init failed");
        }

        // Создание copy done query
        D3D11_QUERY_DESC queryDesc{};
        queryDesc.Query = D3D11_QUERY_EVENT;
        hr = m_deviceCopy->CreateQuery(&queryDesc, m_copyDoneQuery.GetAddressOf());
        if (FAILED(hr))
        {
            spdlog::error("{}: failed CreateQuery(), code {:X}", NameOf(RussoCEFRenderLayer), hr);
            throw std::runtime_error("RussoCEFRenderLayer::Init failed");
        }

        if (m_renderData->width == 0 || m_renderData->height == 0)
        {
            spdlog::error("{}: invalid texture dimensions (width: {}, height: {})", NameOf(RussoCEFRenderLayer), m_renderData->width, m_renderData->height);
            throw std::runtime_error("Invalid texture dimensions");
        }

        UINT formatSupport = 0;
        hr = m_deviceCopy->CheckFormatSupport(DXGI_FORMAT_R8G8B8A8_UNORM, &formatSupport);
        if (FAILED(hr) || !(formatSupport & D3D11_FORMAT_SUPPORT_TEXTURE2D) ||
            !(formatSupport & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE))
        {
            spdlog::error("{}: Format not fully supported", NameOf(RussoCEFRenderLayer));
            throw std::runtime_error("Unsupported texture format");
        }
/*
        D3D11_FEATURE_DATA_D3D11_OPTIONS options;
        HRESULT hr = m_deviceCopy->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options));
        if (SUCCEEDED(hr))
        {
            if (options.)
            {
                // D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX is supported
            }
            else
            {
                // Not supported
            }
        }
*/

        for (uint32_t i = 0; i < SLOT_COUNT; i++)
        {
            CreateSlot(i);
        }

        m_latestUpdated.store(kInvalid, std::memory_order_relaxed);
        m_latchedSlot = kInvalid;
    }

    const inline ::DirectX::SimpleMath::Vector2 _Cef_Menu_Draw_Vector = {0.f, 0.f};
    void RussoCEFRenderLayer::Draw()
    {
        if (!m_isVisible)
        {
            return;
        }

        ID3D11ShaderResourceView* srv = AcquireFrame();

        if (!srv)
        {
            return;
        }

        m_renderData->spriteBatch->Begin(::DirectX::SpriteSortMode_Deferred, m_renderData->commonStates->NonPremultiplied());

        m_renderData->spriteBatch->Draw(
            srv,
            _Cef_Menu_Draw_Vector,
            nullptr,
            ::DirectX::Colors::White,
            0.f);

        m_renderData->spriteBatch->End();

        //ReleaseFrame();
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
        if (type == PET_POPUP ||
            m_renderData == nullptr ||
            m_deviceRender == nullptr)
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

    // Попытка мгновенно зарезервировать слот под запись: вернуть индекс с захваченным key==0
    std::optional<uint32_t> RussoCEFRenderLayer::ReserveSlotForWrite()
    {
        const uint32_t start = m_nextWrite.load(std::memory_order_relaxed);

        // 1) Нормальный путь: ищем слот уже в key==0
        for (uint32_t a = 0; a < SLOT_COUNT; ++a)
        {
            const uint32_t idx = (start + a) % SLOT_COUNT;
            auto& s = m_slots[idx];
            if (s.mutexP->AcquireSync(0, 0) == S_OK)
            {
                return idx;
            }
        }

        // 2) Recall: все висят на key==1 → аккуратно отберем любой не-latched
        const uint32_t latched = m_latchedSlot.load(std::memory_order_acquire);
        for (uint32_t a = 0; a < SLOT_COUNT; ++a)
        {
            const uint32_t idx = (start + a) % SLOT_COUNT;
            if (idx == latched)
                continue;

            auto& s = m_slots[idx];
            if (s.mutexP->AcquireSync(1, 0) == S_OK)
            {                                            // слот еще не забрал потребитель
                s.mutexP->ReleaseSync(0);                // перевели в режим записи
                if (s.mutexP->AcquireSync(0, 0) == S_OK) // сразу же захватили на запись
                    return idx;
                // если не получилось — идем дальше (крайне редкий случай гонки)
            }
        }

        return std::nullopt; // всё занято — дропаем кадр без блокировок
    }

    // Единая точка копирования + публикации (единственный CopyResource в коде)
    bool RussoCEFRenderLayer::CopyAndPublish(uint32_t idx, ID3D11Texture2D* src)
    {
        auto& s = m_slots[idx];

        D3D11_TEXTURE2D_DESC da{}, db{};
        s.texP->GetDesc(&da);
        src->GetDesc(&db);
        if (da.Width != db.Width || da.Height != db.Height || da.Format != db.Format)
        {
            s.mutexP->ReleaseSync(0); // вернуть в исходное состояние
            return false;
        }

        // ЕДИНСТВЕННЫЙ вызов CopyResource
        m_immContextCopy->CopyResource(s.texP.Get(), src);

        // Фэнс: дождаться завершения GPU-команд до публикации
        m_immContextCopy->End(m_copyDoneQuery.Get());
        while (S_OK != m_immContextCopy->GetData(m_copyDoneQuery.Get(), nullptr, 0, 0))
        {
            std::this_thread::yield();
        }

        // Публикация для потребителя
        s.mutexP->ReleaseSync(1);

        m_latestUpdated.store(idx, std::memory_order_release);
        uint64_t next_id = (idx + 1) % SLOT_COUNT;
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
            return false;

        return CopyAndPublish(*slot, src);
        /*
        uint32_t start = (m_latestUpdated.load(std::memory_order_relaxed) + 1u) % SLOT_COUNT;

        spdlog::info("{}: UpdateFrame started, start slot {}", NameOf(RussoCEFRenderLayer), start);
        std::optional<uint32_t> mb_chosen = std::nullopt;
        for (uint32_t p = 0; p < SLOT_COUNT; ++p)
        {
            uint32_t idx = (start + p) % SLOT_COUNT;
            auto& s = m_slots[idx];
            if (s.mutexP->AcquireSync(0, 0) == S_OK)
            {
                mb_chosen = idx;
                break;
            }
        }
        
        spdlog::info("{}: UpdateFrame finished search, chosen slot {}", NameOf(RussoCEFRenderLayer), mb_chosen.has_value() ? std::to_string(mb_chosen.value()) : "none");

        if (!mb_chosen.has_value())
        {
            return false;
        }

        uint32_t chosen = mb_chosen.value();
        auto& slot = m_slots[chosen];

        D3D11_TEXTURE2D_DESC a{}, b{};
        slot.texP->GetDesc(&a);
        src->GetDesc(&b);

        if (a.Width != b.Width || a.Height != b.Height || a.Format != b.Format)
        {
            spdlog::error("{}: texture size mismatch ({}x{} vs {}x{})", NameOf(RussoCEFRenderLayer), a.Width, a.Height, b.Width, b.Height);
            spdlog::error("{}: texture format mismatch ({} vs {})", NameOf(RussoCEFRenderLayer), (int)a.Format, (int)b.Format);
            spdlog::error("{}: texture description mismatch", NameOf(RussoCEFRenderLayer));
            slot.mutexP->ReleaseSync(1);
            return false;
        }

        m_immContextCopy->CopyResource(slot.texP.Get(), src);
        m_immContextCopy->End(m_copyDoneQuery.Get());
        m_immContextCopy->Flush();

        spdlog::info("{}: UpdateFrame submitted copy to slot {}", NameOf(RussoCEFRenderLayer), chosen);

        while (S_OK != m_immContextCopy->GetData(m_copyDoneQuery.Get(), nullptr, 0, 0))
        {
            std::this_thread::yield();
        }

        slot.mutexP->ReleaseSync(1);

        spdlog::info("{}: UpdateFrame chosen {}", NameOf(RussoCEFRenderLayer), chosen);
        m_latestUpdated.store((UINT)chosen, std::memory_order_release);

        return true;
        */
    }

    ID3D11ShaderResourceView* RussoCEFRenderLayer::AcquireFrame()
    {
        const uint32_t latest = m_latestUpdated.load(std::memory_order_acquire);
        uint32_t latched = m_latchedSlot.load(std::memory_order_relaxed);

        if (latest == kInvalid)
            return (latched == kInvalid) ? nullptr : m_slots[latched].srvC.Get();

        if (latched == latest)
            return m_slots[latched].srvC.Get();

        if (m_slots[latest].mutexC->AcquireSync(1, 0) == S_OK)
        {
            if (latched != kInvalid)
                m_slots[latched].mutexC->ReleaseSync(0);
            m_latchedSlot.store(latest, std::memory_order_release);
            return m_slots[latest].srvC.Get();
        }

        return (latched == kInvalid) ? nullptr : m_slots[latched].srvC.Get();

        /*
        UINT latest = m_latestUpdated.load(std::memory_order_acquire);

        if (latest == kInvalid)
        {
            return nullptr;
        }

        if (latest != m_latchedSlot)
        {
            HRESULT hr = m_slots[latest].mutexC->AcquireSync(1, 0);
            if (hr == S_OK)
            {
                if (m_latchedSlot != kInvalid)
                {
                    // spdlog::info("{}: AcquireFrame releasing old slot {}", NameOf(RussoCEFRenderLayer), m_latchedSlot);
                    m_slots[m_latchedSlot].mutexC->ReleaseSync(0);
                }

                m_latchedSlot = latest;
            }

        }

        if (m_latchedSlot == kInvalid)
        {
            return nullptr;
        }

        return m_slots[m_latchedSlot].srvC.Get();
        */
    }

    void RussoCEFRenderLayer::ReleaseFrame()
    {
        if (m_latchedSlot != kInvalid)
        {
            spdlog::info("{}: AcquireFrame releasing old slot {}", NameOf(RussoCEFRenderLayer), m_latchedSlot.load());
            m_slots[m_latchedSlot].mutexC->ReleaseSync(0);
            m_latchedSlot = kInvalid;
        }
    }

    void RussoCEFRenderLayer::CreateSlot(uint32_t i)
    {
        D3D11_TEXTURE2D_DESC td{};
        ZeroMemory(&td, sizeof(D3D11_TEXTURE2D_DESC));
        td.Width = m_renderData->width;
        td.Height = m_renderData->height;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.SampleDesc.Quality = 0;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        td.CPUAccessFlags = 0;
        td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

        // producer - create texture
        auto hr = m_deviceCopy->CreateTexture2D(&td, nullptr, m_slots[i].texP.ReleaseAndGetAddressOf());
        if (FAILED(hr))
        {
            spdlog::error("{}: failed CreateTexture2D() for producer, code {:X}", NameOf(RussoCEFRenderLayer), hr);
            throw std::runtime_error("RussoCEFRenderLayer::CreateSlot failed");
        }

        // producer - get mutex
        hr = m_slots[i].texP.As(&m_slots[i].mutexP);
        if (FAILED(hr))
        {
            spdlog::error("{}: failed QueryInterface() for producer mutex, code {:X}", NameOf(RussoCEFRenderLayer), hr);
            throw std::runtime_error("RussoCEFRenderLayer::CreateSlot failed");
        }

        // producer - create shared handle
        Microsoft::WRL::ComPtr<IDXGIResource1> dxgiRes;
        hr = m_slots[i].texP.As(&dxgiRes);
        if (FAILED(hr))
        {
            spdlog::error("{}: failed QueryInterface() for IDXGIResource, code {:X}", NameOf(RussoCEFRenderLayer), hr);
            throw std::runtime_error("RussoCEFRenderLayer::CreateSlot failed");
        }

        hr = dxgiRes->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &m_slots[i].sharedHandle);
        if (FAILED(hr) || !m_slots[i].sharedHandle)
        {
            spdlog::error("{}: failed GetSharedHandle(), code {:X}", NameOf(RussoCEFRenderLayer), hr);
            throw std::runtime_error("RussoCEFRenderLayer::CreateSlot failed");
        }

        // consumer - open shared texture by handle
        hr = m_deviceRender->OpenSharedResource1(m_slots[i].sharedHandle, __uuidof(ID3D11Texture2D), (void**)m_slots[i].texC.ReleaseAndGetAddressOf());
        if (FAILED(hr))
        {
            spdlog::error("{}: failed OpenSharedResource() for consumer, code {:X}", NameOf(RussoCEFRenderLayer), hr);
            throw std::runtime_error("RussoCEFRenderLayer::CreateSlot failed");
        }

        // consumer - get mutex
        hr = m_slots[i].texC.As(&m_slots[i].mutexC);
        if (FAILED(hr))
        {
            spdlog::error("{}: failed QueryInterface() for consumer mutex, code {:X}", NameOf(RussoCEFRenderLayer), hr);
            throw std::runtime_error("RussoCEFRenderLayer::CreateSlot failed");
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
            spdlog::error("{}: failed CreateShaderResourceView(), code {:X}", NameOf(RussoCEFRenderLayer), hr);
            throw std::runtime_error("RussoCEFRenderLayer::CreateSlot failed");
        }

        hr = m_slots[i].mutexC->AcquireSync(1, 0);
        if (hr == S_OK)
        {
            m_slots[i].mutexC->ReleaseSync(0);
        }
    }
}