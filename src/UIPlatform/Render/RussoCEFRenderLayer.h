#pragma once

#include "PCH.h"
#include "IRenderLayer.h"

namespace NL::Render
{
    class RussoCEFRenderLayer : public IRenderLayer,
                               public CefRenderHandler
    {
        IMPLEMENT_REFCOUNTING(RussoCEFRenderLayer);

    public:
        static std::shared_ptr<RussoCEFRenderLayer> make_shared();
        static void release_shared(RussoCEFRenderLayer* a_render);

    protected:
        Microsoft::WRL::ComPtr<ID3D11Device1> m_deviceRender;

        Microsoft::WRL::ComPtr<ID3D11Device> m_deviceCopy;
        Microsoft::WRL::ComPtr<ID3D11Device1> m_deviceCopy1;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_immContextCopy;

#ifdef __ENABLE_DEBUG_INFO
        size_t instanceID = 0;
        std::chrono::steady_clock::time_point lastTime = std::chrono::high_resolution_clock::now();
        size_t frames = 0;

        long long totalDelayMS = 0;
        size_t delayCount = 0;

        size_t updateRateTextIndex = 0;
        size_t delayTextIndex = 0;
#endif

        struct Slot
        {
            // producer side
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texP = Microsoft::WRL::ComPtr<ID3D11Texture2D>();
            Microsoft::WRL::ComPtr<IDXGIKeyedMutex> mutexP = Microsoft::WRL::ComPtr<IDXGIKeyedMutex>();
            HANDLE sharedHandle = nullptr;

            // consumer side
            Microsoft::WRL::ComPtr<ID3D11Texture2D> texC = Microsoft::WRL::ComPtr<ID3D11Texture2D>();
            Microsoft::WRL::ComPtr<IDXGIKeyedMutex> mutexC = Microsoft::WRL::ComPtr<IDXGIKeyedMutex>();
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srvC = Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>();

#ifdef __ENABLE_DEBUG_INFO
            std::chrono::steady_clock::time_point updateTime = std::chrono::high_resolution_clock::now();
#endif
        };
        constexpr static inline uint32_t SLOT_COUNT = 4;
        std::array<Slot, SLOT_COUNT> m_slots;

        static constexpr UINT64 KM_PRODUCER = 0;
        static constexpr UINT64 KM_CONSUMER = 1;
        static constexpr uint32_t kInvalid = UINT32_MAX;
        Microsoft::WRL::ComPtr<ID3D11Query> m_copyDoneQuery;

        std::atomic<uint32_t> m_latestUpdated{kInvalid};
        std::atomic<uint32_t> m_latchedSlot{kInvalid};
        std::atomic<uint32_t> m_nextWrite{kInvalid};

        std::atomic<bool> initialized{false};

        std::optional<uint32_t> ReserveSlotForWrite();
        bool CopyAndPublish(uint32_t idx, ID3D11Texture2D* src);
        bool UpdateFrame(ID3D11Texture2D* src);
        RussoCEFRenderLayer::Slot* AcquireFrame();
        void CreateSlot(uint32_t i);

    public:
        ~RussoCEFRenderLayer() override = default;

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
