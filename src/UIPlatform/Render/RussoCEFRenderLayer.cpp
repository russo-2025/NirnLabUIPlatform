#include "RussoCEFRenderLayer.h"
#include "Utils/D3D11Utils.h"

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
        hr = m_deviceCopy->CheckFormatSupport(DXGI_FORMAT_R8G8B8A8_UNORM, &formatSupport);
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

    std::optional<uint32_t> RussoCEFRenderLayer::ReserveSlotForWrite()
    {
        const uint32_t start = m_nextWrite.load(std::memory_order_relaxed);

        for (uint32_t a = 0; a < SLOT_COUNT; ++a)
        {
            const uint32_t idx = (start + a) % SLOT_COUNT;
            auto& s = m_slots[idx];
            if (s.mutexP->AcquireSync(0, 0) == S_OK)
            {
                return idx;
            }
        }

        const uint32_t latched = m_latchedSlot.load(std::memory_order_acquire);
        for (uint32_t a = 0; a < SLOT_COUNT; ++a)
        {
            const uint32_t idx = (start + a) % SLOT_COUNT;
            if (idx == latched)
            {
                continue;
            }

            auto& s = m_slots[idx];
            if (s.mutexP->AcquireSync(1, 0) == S_OK)
            {
                s.mutexP->ReleaseSync(0);
                if (s.mutexP->AcquireSync(0, 0) == S_OK)
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
            s.mutexP->ReleaseSync(0);
            return false;
        }

        m_immContextCopy->CopyResource(s.texP.Get(), src);

        m_immContextCopy->End(m_copyDoneQuery.Get());
        while (S_OK != m_immContextCopy->GetData(m_copyDoneQuery.Get(), nullptr, 0, 0))
        {
            std::this_thread::yield();
        }

        s.mutexP->ReleaseSync(1);

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

    ID3D11ShaderResourceView* RussoCEFRenderLayer::AcquireFrame()
    {
        const uint32_t latest = m_latestUpdated.load(std::memory_order_acquire);
        uint32_t latched = m_latchedSlot.load(std::memory_order_relaxed);

        if (latest == kInvalid)
        {
            return (latched == kInvalid) ? nullptr : m_slots[latched].srvC.Get();
        }

        if (latched == latest)
        {
            return m_slots[latched].srvC.Get();
        }

        if (m_slots[latest].mutexC->AcquireSync(1, 0) == S_OK)
        {
            if (latched != kInvalid)
                m_slots[latched].mutexC->ReleaseSync(0);
            m_latchedSlot.store(latest, std::memory_order_release);
            return m_slots[latest].srvC.Get();
        }

        return (latched == kInvalid) ? nullptr : m_slots[latched].srvC.Get();
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
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        td.CPUAccessFlags = 0;
        td.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

        // producer - create texture
        auto hr = m_deviceCopy->CreateTexture2D(&td, nullptr, m_slots[i].texP.ReleaseAndGetAddressOf());
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

        hr = m_slots[i].mutexC->AcquireSync(1, 0);
        if (hr == S_OK)
        {
            m_slots[i].mutexC->ReleaseSync(0);
        }
    }
}