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

        HRESULT hr = m_renderData->device->QueryInterface(IID_PPV_ARGS(&m_device1));
        if (FAILED(hr))
        {
            spdlog::error("{}: failed QueryInterface(ID3D11Device1), hr={:X}", NameOf(CEFCopyRenderLayer), hr);
        }

        hr = m_renderData->device->CreateDeferredContext(0, m_deferredContext.ReleaseAndGetAddressOf());
        if (FAILED(hr))
        {
            spdlog::error("{}: failed CreateDeferredContext, hr={:X}", NameOf(CEFCopyRenderLayer), hr);
        }

        // SpriteBatch на DEFERRED-контексте (окружение CommonStates использует device, это ОК)
        m_spriteBatchDeferred = std::make_unique<DirectX::SpriteBatch>(m_deferredContext.Get());

        // Текстуры создадим лениво под фактический shared src (format/size) в OnAcceleratedPaint
        m_targetsReady = false;
        ZeroMemory(&m_cachedDesc, sizeof(m_cachedDesc));
    }

    const inline ::DirectX::SimpleMath::Vector2 _Cef_Menu_Draw_Vector = {0.f, 0.f};
    void CEFCopyRenderLayer::Draw()
    {
        if (!m_isVisible || !m_targetsReady)
            return;

        // Берём последний готовый индекс
        const uint32_t readIdx = m_idx.load(std::memory_order_acquire);

        m_renderData->drawLock.Lock();

        // 1) Считаем у игры/ENB ТЕКУЩИЙ RTV/DSV и вьюпорты с immediate-контекста  // <<<
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
        m_renderData->deviceContext->OMGetRenderTargets(1, rtv.GetAddressOf(), dsv.GetAddressOf());

        UINT vpCount = 0;
        m_renderData->deviceContext->RSGetViewports(&vpCount, nullptr);
        D3D11_VIEWPORT vps[8] = {};
        if (vpCount > 0 && vpCount <= 8)
        {
            m_renderData->deviceContext->RSGetViewports(&vpCount, vps);
        }
        else
        {
            // Fallback: если по какой-то причине вьюпорта нет — подставим меню-размер
            vpCount = 1;
            vps[0].TopLeftX = 0.0f;
            vps[0].TopLeftY = 0.0f;
            vps[0].Width = static_cast<float>(m_renderData->width);
            vps[0].Height = static_cast<float>(m_renderData->height);
            vps[0].MinDepth = 0.0f;
            vps[0].MaxDepth = 1.0f;
        }

        // 2) Подготовим DEFERRED-контекст                                          // <<<
        //    (можно ClearState оставить, но обязательно после него заново ставим RTV/VP)
        m_deferredContext->ClearState();

        // Привязываем тот же RTV/DSV, что сейчас у игры/ENB на immediate
        if (rtv)
        {
            ID3D11RenderTargetView* rtvPtr = rtv.Get();
            m_deferredContext->OMSetRenderTargets(1, &rtvPtr, dsv.Get());
        }
        else
        {
            // Без RTV рисовать нельзя — выходим
            m_renderData->drawLock.Unlock();
            return;
        }

        // И тот же набор viewport’ов
        m_deferredContext->RSSetViewports(vpCount, vps);

        // 3) Уже на DEFERRED — рисуем наш спрайт
        m_spriteBatchDeferred->Begin(
            DirectX::SpriteSortMode_Deferred,
            m_renderData->commonStates->AlphaBlend() // CEF обычно premultiplied
        );

        m_spriteBatchDeferred->Draw(
            m_cefSRV[readIdx].Get(),
            _Cef_Menu_Draw_Vector, // тот же прямоугольник позиционирования
            nullptr,
            DirectX::Colors::White,
            0.0f);

        m_spriteBatchDeferred->End();

        // 4) Собираем и исполняем командлист с восстановлением состояния          // <<<
        Microsoft::WRL::ComPtr<ID3D11CommandList> cl;
        const HRESULT hr = m_deferredContext->FinishCommandList(FALSE, &cl);
        if (SUCCEEDED(hr) && cl)
        {
            m_renderData->deviceContext->ExecuteCommandList(cl.Get(), TRUE); // TRUE — вернуть ENB состояние
        }
        else
        {
            spdlog::error("{}: FinishCommandList failed, hr={:X}", NameOf(CEFCopyRenderLayer), hr);
        }

        m_renderData->drawLock.Unlock();
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

        Microsoft::WRL::ComPtr<ID3D11Texture2D> srcTex;
        const HRESULT hrOpen = m_device1->OpenSharedResource1(info.shared_texture_handle, IID_PPV_ARGS(&srcTex));
        if (FAILED(hrOpen) || !srcTex)
        {
            spdlog::error("{}: OpenSharedResource1 failed, hr={:X}", NameOf(CEFCopyRenderLayer), hrOpen);
            return;
        }

        // Подготовим цели под формат/размер источника
        EnsureTargetsLike(srcTex.Get());
        if (!m_targetsReady)
            return;

        const uint32_t writeIdx = 1u - m_idx.load(std::memory_order_relaxed);

        // Сериализуем запись команд в один deferred (совместно с Draw)
        m_renderData->drawLock.Lock();

        // Если ресурс под мьютексом — синхронизируемся
        Microsoft::WRL::ComPtr<IDXGIKeyedMutex> km;
        if (SUCCEEDED(srcTex->QueryInterface(IID_PPV_ARGS(&km))) && km)
        {
            // Ключи у CEF обычно 0/0; если у тебя есть конкретные ключи из info — подставь
            km->AcquireSync(0, 5);
            m_deferredContext->CopyResource(m_cefTex[writeIdx].Get(), srcTex.Get());
            km->ReleaseSync(0);
        }
        else
        {
            m_deferredContext->CopyResource(m_cefTex[writeIdx].Get(), srcTex.Get());
        }

        // Публикуем новый read-индекс (CPU-уровень; GPU исполнит копию до нашего draw в том же CL)
        m_idx.store(writeIdx, std::memory_order_release);

        m_renderData->drawLock.Unlock();
    }

    void CEFCopyRenderLayer::EnsureTargetsLike(ID3D11Texture2D* src)
    {
        D3D11_TEXTURE2D_DESC srcDesc{};
        src->GetDesc(&srcDesc);

        // Если ещё не создавали или формат/размер изменился — переинициализируем
        const bool needRecreate =
            !m_targetsReady ||
            srcDesc.Width != m_cachedDesc.Width ||
            srcDesc.Height != m_cachedDesc.Height ||
            srcDesc.Format != m_cachedDesc.Format;

        if (!needRecreate)
            return;

        for (int i = 0; i < 2; ++i)
        {
            m_cefTex[i].Reset();
            m_cefSRV[i].Reset();
        }

        D3D11_TEXTURE2D_DESC dst = srcDesc;
        dst.Usage = D3D11_USAGE_DEFAULT; // ВАЖНО: цель GPU-копии
        dst.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        dst.CPUAccessFlags = 0;
        dst.MiscFlags = 0; // нам не нужны shared-флаги

        for (int i = 0; i < 2; ++i)
        {
            HRESULT hr = m_renderData->device->CreateTexture2D(&dst, nullptr, m_cefTex[i].ReleaseAndGetAddressOf());
            if (FAILED(hr))
            {
                spdlog::error("{}: CreateTexture2D[{}] failed, hr={:X}", NameOf(CEFCopyRenderLayer), i, hr);
                m_targetsReady = false;
                return;
            }
            hr = m_renderData->device->CreateShaderResourceView(m_cefTex[i].Get(), nullptr, m_cefSRV[i].ReleaseAndGetAddressOf());
            if (FAILED(hr))
            {
                spdlog::error("{}: CreateShaderResourceView[{}] failed, hr={:X}", NameOf(CEFCopyRenderLayer), i, hr);
                m_targetsReady = false;
                return;
            }
        }

        m_cachedDesc = dst;
        m_targetsReady = true;
    }
}
