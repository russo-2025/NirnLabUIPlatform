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

        if (!m_renderData || !m_renderData->device1)
            return;

        // Отдельный deferred-контекст только для копий
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> copyCtx;

        auto hr = m_renderData->device->CreateDeferredContext(0, copyCtx.GetAddressOf());

        if (FAILED(hr))
        {
            spdlog::error("{}: CreateDeferredContext failed hr={:08X}", NameOf(CEFCopyRenderLayer), hr);
            return;
        }

        m_copyCtx = copyCtx; // ComPtr<ID3D11DeviceContext>
        m_targetsReady = false;
        m_cached = {};
    }
    
    const inline ::DirectX::SimpleMath::Vector2 _Cef_Menu_Draw_Vector = {0.f, 0.f};
    void CEFCopyRenderLayer::Draw()
    {
        if (!m_isVisible || !m_targetsReady || !m_renderData)
            return;

        const uint32_t readIdx = (m_renderData->cefReadIdx.load(std::memory_order_acquire) & 1u);

        m_renderData->spriteBatchDeferred->Draw(
            m_cefSRV[readIdx].Get(), _Cef_Menu_Draw_Vector, nullptr, DirectX::Colors::White, 0.0f);
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
        CefRefPtr<CefBrowser> /*browser*/,
        PaintElementType type,
        const RectList& /*dirtyRects*/,
        const CefAcceleratedPaintInfo& info)
    {
        if (type == PET_POPUP || !m_renderData || !m_copyCtx)
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

        const uint32_t curIdx = m_idx.load(std::memory_order_relaxed) & 1u;
        const uint32_t writeIdx = 1u - curIdx;

        // Попытка захвата KeyedMutex (не блокируем надолго)
        Microsoft::WRL::ComPtr<IDXGIKeyedMutex> km;
        srcTex->QueryInterface(IID_PPV_ARGS(&km));

        bool canCopy = true;
        if (km)
        {
            HRESULT hr = km->AcquireSync(0 /*key*/, 0 /*ms*/); // 0 = не ждём
            if (hr == WAIT_TIMEOUT)
            {
                canCopy = false; // пропускаем этот апдейт, не блокируя рендер
            }
            else if (FAILED(hr))
            {
                spdlog::warn("{}: AcquireSync failed hr={:08X}", NameOf(CEFCopyRenderLayer), hr);
                canCopy = false;
            }
        }

        if (canCopy)
        {
            Microsoft::WRL::ComPtr<ID3D11CommandList> copyCL;
            m_copyCtx->CopyResource(m_cefTex[writeIdx].Get(), srcTex);
            if (km)
                km->ReleaseSync(0);

            if (SUCCEEDED(m_copyCtx->FinishCommandList(FALSE, copyCL.GetAddressOf())) && copyCL)
            {
                CopyPacket pkt;
                pkt.cl = std::move(copyCL);
                pkt.writeIdx = writeIdx & 1u;
                pkt.generation = m_renderData->cefGeneration.load(std::memory_order_relaxed);
                m_renderData->pendingCopy->push(std::move(pkt));
            }
        }
    }

    ID3D11Texture2D* CEFCopyRenderLayer::GetOrOpenSource(HANDLE h)
    {
        for (auto& e : m_srcCache)
            if (e.h == h)
                return e.tex.Get();

        // Найти свободную ячейку
        for (auto& e : m_srcCache)
            if (!e.h)
            {
                Microsoft::WRL::ComPtr<ID3D11Texture2D> t;
                if (SUCCEEDED(m_renderData->device1->OpenSharedResource1(h, IID_PPV_ARGS(&t))) && t)
                {
                    e.h = h;
                    e.tex = t;
                    return e.tex.Get();
                }
                return nullptr;
            }

        // LRU/перезапись 0-го слота с корректным release
        Microsoft::WRL::ComPtr<ID3D11Texture2D> t;
        if (SUCCEEDED(m_renderData->device1->OpenSharedResource1(h, IID_PPV_ARGS(&t))) && t)
        {
            m_srcCache[0].tex.Reset();
            m_srcCache[0].h = 0;
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
