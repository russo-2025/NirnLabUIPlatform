#pragma once

#include "Common/SpinLock.h"
#include <directxtk/CommonStates.h>
#include <directxtk/SimpleMath.h>
#include <directxtk/SpriteBatch.h>

namespace NL::Render
{
    struct CopyPacket
    {
        Microsoft::WRL::ComPtr<ID3D11CommandList> cl;
        uint32_t writeIdx = 0;
        uint32_t generation = 0;
    };

    struct CLRing
    {
        static constexpr uint32_t kCap = 8;
        std::atomic<uint32_t> head{0}, tail{0};
        CopyPacket ring[kCap];

        bool push(CopyPacket&& p)
        {
            uint32_t t = tail.load(std::memory_order_relaxed);
            uint32_t n = (t + 1) % kCap;
            if (n == head.load(std::memory_order_acquire))
            { // full → перезапишем "последним"
                ring[t] = std::move(p);
                tail.store(n, std::memory_order_release);
                head.store((head.load() + 1) % kCap, std::memory_order_release);
                return true;
            }
            ring[t] = std::move(p);
            tail.store(n, std::memory_order_release);
            return true;
        }
        bool pop(CopyPacket& out)
        {
            uint32_t h = head.load(std::memory_order_relaxed);
            if (h == tail.load(std::memory_order_acquire))
                return false;
            out = std::move(ring[h]);
            ring[h] = {};
            head.store((h + 1) % kCap, std::memory_order_release);
            return true;
        }
    };

    struct RenderData
    {
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        Microsoft::WRL::ComPtr<ID3D11Device1> device1;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext3> immCtx;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> drawCtx;
        std::unique_ptr<DirectX::SpriteBatch> spriteBatchDeferred;
        std::unique_ptr<CLRing> pendingCopy;
        std::atomic<uint32_t> cefReadIdx{0};
        std::atomic<uint32_t> cefGeneration{0};
        std::shared_ptr<DirectX::CommonStates> commonStates = nullptr;
        ID3D11ShaderResourceView* texture = nullptr;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        Common::SpinLock drawLock;
    };
}
