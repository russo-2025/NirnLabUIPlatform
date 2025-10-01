#include "CEFCopyRenderLayer.h"

namespace NL::Render
{
    std::shared_ptr<CEFCopyRenderLayer> CEFCopyRenderLayer::make_shared()
    {
        const auto cefRender = new CEFCopyRenderLayer();
        cefRender->AddRef();
        return std::shared_ptr<CEFCopyRenderLayer>(cefRender, CEFCopyRenderLayer::release_shared);
    }

    void CEFCopyRenderLayer::release_shared(CEFCopyRenderLayer* a_render)
    {
        a_render->Release();
    }

    void CEFCopyRenderLayer::Init(RenderData* a_renderData)
    {
        IRenderLayer::Init(a_renderData);
        m_renderData = a_renderData;

        HRESULT hr = m_renderData->device->QueryInterface(IID_PPV_ARGS(&m_device1));
        if (FAILED(hr))
        {
            spdlog::error("{}: QI(ID3D11Device1) hr={:X}", NameOf(CEFCopyRenderLayer), hr);
        }
        m_targetsReady = false;
        m_cached = {};
    }

    const inline ::DirectX::SimpleMath::Vector2 _Cef_Menu_Draw_Vector = {0.f, 0.f};
    void CEFCopyRenderLayer::Draw()
    {
        if (!m_isVisible || !m_targetsReady || !m_renderData)
            return;

        const uint32_t readIdx = m_idx.load(std::memory_order_acquire);

        m_renderData->spriteBatchDeferred->Draw(
            m_cefSRV[readIdx].Get(),
            _Cef_Menu_Draw_Vector,
            nullptr,
            DirectX::Colors::White,
            0.0f);
    }

    void CEFCopyRenderLayer::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect)
    {
        rect = m_renderData ? CefRect(0, 0, m_renderData->width, m_renderData->height) : CefRect(0, 0, 800, 600);
    }

    void CEFCopyRenderLayer::OnPaint(
        CefRefPtr<CefBrowser> browser,
        PaintElementType type,
        const RectList& dirtyRects,
        const void* buffer,
        int width,
        int height)
    {
    }

    void CEFCopyRenderLayer::OnAcceleratedPaint(
        CefRefPtr<CefBrowser> browser,
        PaintElementType type,
        const RectList& dirtyRects,
        const CefAcceleratedPaintInfo& info)
    {
        if (type == PET_POPUP || !m_renderData || !m_device1)
            return;

        ID3D11Texture2D* srcTex = GetOrOpenSource(info.shared_texture_handle);
        if (!srcTex)
        {
            spdlog::error("{}: OpenSharedResource1 failed", NameOf(CEFCopyRenderLayer));
            return;
        }

        EnsureTargetsLike(srcTex);
        if (!m_targetsReady)
            return;

        const uint32_t writeIdx = 1u - m_idx.load(std::memory_order_relaxed);

        m_renderData->drawLock.Lock();

        Microsoft::WRL::ComPtr<IDXGIKeyedMutex> km;
        if (SUCCEEDED(srcTex->QueryInterface(IID_PPV_ARGS(&km))) && km)
        {
            km->AcquireSync(0, 5);
            m_renderData->deferredContext->CopyResource(m_cefTex[writeIdx].Get(), srcTex);
            km->ReleaseSync(0);
        }
        else
        {
            m_renderData->deferredContext->CopyResource(m_cefTex[writeIdx].Get(), srcTex);
        }

        m_idx.store(writeIdx, std::memory_order_release);

        m_renderData->drawLock.Unlock();
    }

    ID3D11Texture2D* CEFCopyRenderLayer::GetOrOpenSource(HANDLE h)
    {
        for (auto& e : m_srcCache)
            if (e.h == h)
                return e.tex.Get();

        for (auto& e : m_srcCache)
            if (!e.h)
            {
                Microsoft::WRL::ComPtr<ID3D11Texture2D> t;
                if (SUCCEEDED(m_device1->OpenSharedResource1(h, IID_PPV_ARGS(&t))) && t)
                {
                    e.h = h;
                    e.tex = t;
                    return e.tex.Get();
                }
            }

        Microsoft::WRL::ComPtr<ID3D11Texture2D> t;
        if (SUCCEEDED(m_device1->OpenSharedResource1(h, IID_PPV_ARGS(&t))) && t)
        {
            m_srcCache[0].h = h;
            m_srcCache[0].tex = t;
            return m_srcCache[0].tex.Get();
        }

        return nullptr;
    }

    void CEFCopyRenderLayer::EnsureTargetsLike(ID3D11Texture2D* src)
    {
        D3D11_TEXTURE2D_DESC sd{};
        src->GetDesc(&sd);

        if (m_targetsReady && sd.Width == m_cached.w && sd.Height == m_cached.h && sd.Format == m_cached.fmt)
            return;

        for (int i = 0; i < 2; i++)
        {
            m_cefTex[i].Reset();
            m_cefSRV[i].Reset();
        }

        D3D11_TEXTURE2D_DESC dst = sd;
        dst.Usage = D3D11_USAGE_DEFAULT;
        dst.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        dst.CPUAccessFlags = 0;
        dst.MiscFlags = 0;

        for (int i = 0; i < 2; i++)
        {
            HRESULT hr = m_renderData->device->CreateTexture2D(&dst, nullptr, m_cefTex[i].ReleaseAndGetAddressOf());
            if (FAILED(hr))
            {
                spdlog::error("{}: CreateTexture2D[{}] hr={:X}", NameOf(CEFCopyRenderLayer), i, hr);
                m_targetsReady = false;
                return;
            }
            hr = m_renderData->device->CreateShaderResourceView(m_cefTex[i].Get(), nullptr, m_cefSRV[i].ReleaseAndGetAddressOf());
            if (FAILED(hr))
            {
                spdlog::error("{}: CreateSRV[{}] hr={:X}", NameOf(CEFCopyRenderLayer), i, hr);
                m_targetsReady = false;
                return;
            }
        }

        m_cached = {sd.Width, sd.Height, sd.Format};
        m_targetsReady = true;
    }
}
