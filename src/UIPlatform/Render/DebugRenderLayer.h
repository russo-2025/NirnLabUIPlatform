#pragma once

#include "PCH.h"
#include "RenderData.h"
#include <DirectXMath.h>
#include <directxtk/SpriteFont.h>
#include <directxtk/SpriteBatch.h>
#include <memory>
#include <string>

#ifdef __ENABLE_DEBUG_INFO

namespace NL::Render
{
    class DebugRenderLayer
    {
    protected:
        Microsoft::WRL::ComPtr<ID3D11Device> m_device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
        std::unique_ptr<DirectX::SpriteBatch> m_spriteBatch;
        std::unique_ptr<DirectX::SpriteFont> m_font;

    public:
        static DebugRenderLayer* GetSingleton()
        {
            static DebugRenderLayer* singleton = new DebugRenderLayer();
            return singleton;
        }

    private:
        DebugRenderLayer() {};

    public:
        void Init(RenderData* a_renderData);
        void Draw();

        size_t AddTextLine(std::wstring a_text);
        void UpdateTextLine(size_t a_index, std::wstring a_text);

    private:
        std::array<std::wstring, 30> m_debugText;
        size_t m_debugTextCount = 0;
    };
}

#endif