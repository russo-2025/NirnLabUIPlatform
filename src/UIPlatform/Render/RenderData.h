#pragma once

#include "Common/SpinLock.h"
#include <directxtk/CommonStates.h>
#include <directxtk/SimpleMath.h>
#include <directxtk/SpriteBatch.h>

namespace NL::Render
{
    struct RenderData
    {
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext3* deviceContext = nullptr;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> deferredContext;
        std::unique_ptr<DirectX::SpriteBatch> spriteBatchDeferred;
        std::shared_ptr<DirectX::CommonStates> commonStates = nullptr;
        ID3D11ShaderResourceView* texture = nullptr; 
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        Common::SpinLock drawLock;
    };
}
