#pragma once

#include "Common/SpinLock.h"
#include <directxtk/CommonStates.h>
#include <directxtk/SimpleMath.h>
#include <directxtk/SpriteBatch.h>

namespace NL::Render
{
    struct CLRing
    {
        static constexpr uint32_t kCap = 8;
        std::atomic<uint32_t> head{0}, tail{0};
        Microsoft::WRL::ComPtr<ID3D11CommandList> ring[kCap];

        bool push(Microsoft::WRL::ComPtr<ID3D11CommandList>&& cl)
        {
            uint32_t t = tail.load(std::memory_order_relaxed);
            uint32_t n = (t + 1) % kCap;
            if (n == head.load(std::memory_order_acquire))
                return false; // full → дроп/лог
            ring[t] = std::move(cl);
            tail.store(n, std::memory_order_release);
            return true;
        }
        bool pop(Microsoft::WRL::ComPtr<ID3D11CommandList>& out)
        {
            uint32_t h = head.load(std::memory_order_relaxed);
            if (h == tail.load(std::memory_order_acquire))
                return false; // empty
            out = std::move(ring[h]);
            ring[h].Reset();
            head.store((h + 1) % kCap, std::memory_order_release);
            return true;
        }
    };

    struct RenderData
    {/*
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext3* deviceContext = nullptr;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> deferredContext;
        std::unique_ptr<DirectX::SpriteBatch> spriteBatchDeferred;
        std::shared_ptr<DirectX::CommonStates> commonStates = nullptr;
        ID3D11ShaderResourceView* texture = nullptr; 
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        Common::SpinLock drawLock;
        */
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11Device1> device1;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext3> immCtx;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> drawCtx;
        std::unique_ptr<DirectX::SpriteBatch> spriteBatchDeferred;
        std::unique_ptr<CLRing> pendingCopy;
        std::shared_ptr<DirectX::CommonStates> commonStates = nullptr;
        ID3D11ShaderResourceView* texture = nullptr;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        Common::SpinLock drawLock;
    };
}
