#pragma once

#include "PCH.h"
#include "IRenderLayer.h"
#include "Common/SpinLock.h"

namespace NL::Render
{
    class CEFCopyRenderLayer : public IRenderLayer,
                               public CefRenderHandler
    {
        IMPLEMENT_REFCOUNTING(CEFCopyRenderLayer);

    public:
        static std::shared_ptr<CEFCopyRenderLayer> make_shared();
        static void release_shared(CEFCopyRenderLayer* a_render);

    private:
        // shared-источники CEF (крошечный кэш handle→texture)
        struct SrcCacheEntry
        {
            HANDLE h = nullptr;
            Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
        };
        std::array<SrcCacheEntry, 3> m_srcCache{};

        // локальные цели (двойной буфер)
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_cefTex[2];
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_cefSRV[2];
        std::atomic<uint32_t> m_idx{0}; // текущий read

        // формат/размер
        struct
        {
            UINT w = 0, h = 0;
            DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
        } m_cached{};
        bool m_targetsReady = false;

        // ссылки
        RenderData* m_renderData = nullptr;
        Microsoft::WRL::ComPtr<ID3D11Device1> m_device1;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_copyCtx;

    private:
        ID3D11Texture2D* GetOrOpenSource(HANDLE h);
        void EnsureTargetsLike(ID3D11Texture2D* src);

    public:
        ~CEFCopyRenderLayer() override = default;

        // IRenderLayer
        void Init(RenderData* a_renderData) override;
        void Draw() override;

        // CefRenderHandler
        void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
        void OnPaint(
            CefRefPtr<CefBrowser> browser,
            PaintElementType type,
            const RectList& dirtyRects,
            const void* buffer,
            int width,
            int height) override;
        void OnAcceleratedPaint(
            CefRefPtr<CefBrowser> browser,
            PaintElementType type,
            const RectList& dirtyRects,
            const CefAcceleratedPaintInfo& info) override;
    };
}
