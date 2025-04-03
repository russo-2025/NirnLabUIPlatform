#pragma once

#include "PCH.h"
#include "IRenderLayer.h"
#include "Common/SpinLock.h"

namespace NL::Render
{
    /// <summary>
    /// OBSOLETE
    /// Using this class may cause CTD
    /// </summary>
    class [[deprecated("MAY CAUSE CTD, DON'T USE")]] CEFRenderLayer : public IRenderLayer,
                                                           public CefRenderHandler
    {
        IMPLEMENT_REFCOUNTING(CEFRenderLayer);

    public:
        // deprecated error
        // static std::shared_ptr<CEFRenderLayer> make_shared();
        // static void release_shared(CEFRenderLayer* a_render);

    protected:
        HANDLE m_sharedTextureHandle = nullptr;
        ID3D11Texture2D* m_cefTexture = nullptr;
        ID3D11ShaderResourceView* m_cefSRV = nullptr;
        Microsoft::WRL::ComPtr<ID3D11Device1> m_device1 = nullptr;

    public:
        ~CEFRenderLayer() override;

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
