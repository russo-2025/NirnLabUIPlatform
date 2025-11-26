#include "DebugRenderLayer.h"
#include "Utils/D3D11Utils.h"
#include <chrono>

#ifdef __ENABLE_DEBUG_INFO

#define FATAL_ERROR(...)                       \
{                                              \
    const auto msg = fmt::format(__VA_ARGS__); \
    spdlog::error(msg);                        \
    SKSE::stl::report_and_fail(msg.c_str());   \
}

namespace NL::Render
{
    void DebugRenderLayer::Init(RenderData* a_renderData)
    {
        if (!a_renderData || !a_renderData->device || !a_renderData->deviceContext)
        {
            FATAL_ERROR("{}::Init: invalid RenderData provided", NameOf(DebugRenderLayer));
        }

        m_device = a_renderData->device;
        m_context = a_renderData->deviceContext; 

        m_spriteBatch = std::make_unique<DirectX::SpriteBatch>(m_context.Get());

        static std::filesystem::path fontPath = L"Data\\NirnLabUIPlatform\\myfile.spritefont";

        m_font = std::make_unique<DirectX::SpriteFont>(m_device.Get(), fontPath.wstring().c_str());

        AddTextLine(L"Test v.12");

        spdlog::info("DebugRenderLayer initialized");
    }

    void DebugRenderLayer::Draw()
    {
        if (!m_spriteBatch || !m_font)
            return;

        // Рисуем текст сверху (например, в левом верхнем углу)
        m_spriteBatch->Begin();

        for (size_t i = 0; i < m_debugTextCount; ++i)
        {
            m_font->DrawString(m_spriteBatch.get(), m_debugText[i].c_str(), DirectX::XMFLOAT2(10.0f, 100.0f + i * 20.0f), DirectX::Colors::White);
        }

        m_spriteBatch->End();
    }
    
    size_t DebugRenderLayer::AddTextLine(std::wstring a_text)
    {
        size_t index = m_debugTextCount;
        m_debugText[index] = std::move(a_text);
        m_debugTextCount++;
        return index;
    }

    void DebugRenderLayer::UpdateTextLine(size_t a_index, std::wstring a_text)
    {
        if (a_index >= m_debugText.size())
        {
            return;
        }

        m_debugText[a_index] = std::move(a_text);
    }
}

#endif