#include "MultiLayerMenu.h"

namespace NL::Menus
{
    MultiLayerMenu::MultiLayerMenu(std::shared_ptr<spdlog::logger> a_logger)
    {
        ThrowIfNullptr(MultiLayerMenu, a_logger);
        m_logger = a_logger;

        // 1) Получаем ID3D11Device* из движка и поднимаем его в ComPtr с корректным AddRef.
        ID3D11Device* rawDevice = reinterpret_cast<ID3D11Device*>(RE::BSGraphics::Renderer::GetDevice());
        ThrowIfNullptr(MultiLayerMenu, rawDevice);

        Microsoft::WRL::ComPtr<ID3D11Device> device;
        auto hr = rawDevice->QueryInterface(IID_PPV_ARGS(&device));
        if (FAILED(hr))
        {
            std::string msg = fmt::format("failed {}: QueryInterface<ID3D11Device> hr={:X}", NameOf(MultiLayerMenu), hr);
            spdlog::error(msg);
            throw std::runtime_error(msg);
        }

        // Необязательно, но полезно иметь ID3D11Device1
        Microsoft::WRL::ComPtr<ID3D11Device1> device1;
        device.As(&device1);

        // 2) Берём immediate-контекст уровня 11.3 (как у вас было).
        Microsoft::WRL::ComPtr<ID3D11Device3> device3;
        hr = device->QueryInterface(IID_PPV_ARGS(&device3));

        if (FAILED(hr))
        {
            std::string msg = fmt::format("failed {}: QueryInterface<ID3D11Device3> hr={:X}", NameOf(MultiLayerMenu), hr);
            spdlog::error(msg);
            throw std::runtime_error(msg);
        }

        Microsoft::WRL::ComPtr<ID3D11DeviceContext3> imm3;
        device3->GetImmediateContext3(&imm3);
        ThrowIfNullptr(MultiLayerMenu, imm3.Get());

        // 3) Достаём размеры «менюшного» RT и SRV.
        const auto nativeMenuRenderData =
            RE::BSGraphics::Renderer::GetRendererData()->renderTargets[RE::RENDER_TARGETS::kMENUBG];

        D3D11_TEXTURE2D_DESC td{};
        if (nativeMenuRenderData.texture)
        {
            nativeMenuRenderData.texture->GetDesc(&td);
        }
        else
        {
            // Фолбэк на 1x1, если по какой-то причине текстуры нет (не должно случаться)
            td.Width = td.Height = 1;
            td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        }

        // 4) Заполняем RenderData современными держателями.
        m_renderData.device = device;   // ComPtr<ID3D11Device>
        m_renderData.device1 = device1; // ComPtr<ID3D11Device1> (может быть null на старых рантаймах)
        m_renderData.immCtx = imm3;     // ComPtr<ID3D11DeviceContext3>
        m_renderData.width = td.Width;
        m_renderData.height = td.Height;
        m_renderData.texture = nativeMenuRenderData.SRV; // SRV для SpriteBatch
        m_renderData.commonStates = std::make_shared<DirectX::CommonStates>(device.Get());

        // 5) Отдельный deferred-контекст ТОЛЬКО для SpriteBatch (меню).
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> drawCtx;
        hr = device->CreateDeferredContext(0, drawCtx.GetAddressOf());
        if (FAILED(hr))
        {
            std::string msg = fmt::format("failed {}: CreateDeferredContext hr={:X}", NameOf(MultiLayerMenu), hr);
            spdlog::error(msg);
            throw std::runtime_error(msg);
        }
        m_renderData.drawCtx = drawCtx; // ComPtr<ID3D11DeviceContext>

        // 6) SpriteBatch, привязанный к drawCtx.
        m_renderData.spriteBatchDeferred = std::make_unique<DirectX::SpriteBatch>(drawCtx.Get());

        // 7) Очередь для CL от CEF-потока (Single-Producer/Single-Consumer).
        m_renderData.pendingCopy = std::make_unique<NL::Render::CLRing>();

        // ===== Ниже — ваш прежний код свойств меню/подписок =====

        depthPriority = 12;
        menuFlags.set(RE::UI_MENU_FLAGS::kAlwaysOpen);
        menuFlags.set(RE::UI_MENU_FLAGS::kAllowSaving);
        menuFlags.set(RE::UI_MENU_FLAGS::kCustomRendering);
        menuFlags.set(RE::UI_MENU_FLAGS::kAssignCursorToRenderer);
        inputContext = Context::kNone;

        RE::UI::GetSingleton()->pad17D = false;
        RE::UI::GetSingleton()->AddEventSink(static_cast<RE::BSTEventSink<RE::MenuOpenCloseEvent>*>(this));

        const auto inputEventSource = RE::BSInputDeviceManager::GetSingleton();
        inputEventSource->lock.Lock();
        NL::Utils::PushFront<RE::BSTEventSink<RE::InputEvent*>>(RE::BSInputDeviceManager::GetSingleton()->sinks, this);
        inputEventSource->lock.Unlock();

        NL::Services::InputLangSwitchService::GetSingleton().SetActive(true);
    }

    MultiLayerMenu::~MultiLayerMenu()
    {
        ClearAllSubMenu();
    }

    bool MultiLayerMenu::AddSubMenu(std::string_view a_menuName, std::shared_ptr<ISubMenu> a_subMenu)
    {
        std::lock_guard<std::mutex> lock(m_mapMenuMutex);
        const std::string menuName = a_menuName.data();
        if (m_menuMap.contains(menuName))
        {
            return false;
        }

        m_menuMap.insert({menuName, a_subMenu});
        a_subMenu->Init(&m_renderData);
        return true;
    }

    std::shared_ptr<ISubMenu> MultiLayerMenu::GetSubMenu(const std::string& a_menuName)
    {
        std::lock_guard<std::mutex> lock(m_mapMenuMutex);
        const auto menuIt = m_menuMap.find(a_menuName);
        if (menuIt != m_menuMap.end())
        {
            return menuIt->second;
        }

        return nullptr;
    }

    bool MultiLayerMenu::IsSubMenuExist(const std::string& a_menuName)
    {
        std::lock_guard<std::mutex> lock(m_mapMenuMutex);
        return m_menuMap.find(a_menuName) != m_menuMap.end();
    }

    bool MultiLayerMenu::RemoveSubMenu(const std::string& a_menuName)
    {
        std::lock_guard<std::mutex> lock(m_mapMenuMutex);
        const auto menuIt = m_menuMap.find(a_menuName);
        return m_menuMap.erase(a_menuName) > 0;
    }

    void MultiLayerMenu::ClearAllSubMenu()
    {
        std::lock_guard<std::mutex> lock(m_mapMenuMutex);
        m_menuMap.clear();
    }

#pragma region RE::IMenu

    void MultiLayerMenu::PostDisplay()
    {
        std::lock_guard<std::mutex> g(m_mapMenuMutex);
        if (m_menuMap.empty())
            return;

        auto* imm = m_renderData.immCtx.Get();

        // 1) Сохраняем текущие RT/DS и вьюпорты (динамически).
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> dsv;
        imm->OMGetRenderTargets(1, rtv.GetAddressOf(), dsv.GetAddressOf());

        UINT vpCount = 0;
        imm->RSGetViewports(&vpCount, nullptr);
        const UINT kMaxVP = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE; // 16
        vpCount = (vpCount > kMaxVP) ? kMaxVP : vpCount;

        std::vector<D3D11_VIEWPORT> vps(vpCount ? vpCount : 1);
        if (vpCount)
        {
            imm->RSGetViewports(&vpCount, vps.data());
        }
        else
        {
            vps[0] = {0, 0, float(m_renderData.width), float(m_renderData.height), 0.f, 1.f};
            vpCount = 1;
        }

        if (!rtv)
            return;

        // 2) Выполняем ВСЕ накопившиеся копии с восстановлением состояния
        {
            NL::Render::CopyPacket pkt;
            while (m_renderData.pendingCopy->pop(pkt))
            {
                // Дроп пакетов старой конфигурации (после ресайза/формата)
                if (pkt.generation != m_renderData.cefGeneration.load(std::memory_order_acquire))
                    continue;

                m_renderData.immCtx->ExecuteCommandList(pkt.cl.Get(), TRUE); // восстановить state
                m_renderData.cefReadIdx.store(pkt.writeIdx, std::memory_order_release);
            }
        }

        // 3) Собираем отрисовку меню на drawCtx
        auto* def = m_renderData.drawCtx.Get();

        ID3D11RenderTargetView* rtvPtr = rtv.Get();
        def->OMSetRenderTargets(1, &rtvPtr, dsv.Get());
        def->RSSetViewports(vpCount, vps.data());

        m_renderData.spriteBatchDeferred->Begin(
            DirectX::SpriteSortMode_Deferred,
            m_renderData.commonStates->AlphaBlend()); // premultiplied по умолчанию :contentReference[oaicite:6]{index=6}

        for (const auto& kv : m_menuMap)
            kv.second->Draw();

        m_renderData.spriteBatchDeferred->End();

        Microsoft::WRL::ComPtr<ID3D11CommandList> drawCL;
        if (SUCCEEDED(def->FinishCommandList(FALSE, drawCL.GetAddressOf())) && drawCL)
        {
            imm->ExecuteCommandList(drawCL.Get(), TRUE); // вернуть состояние двигателю игры :contentReference[oaicite:7]{index=7}
        }
    }

    RE::UI_MESSAGE_RESULTS MultiLayerMenu::ProcessMessage(RE::UIMessage& a_message)
    {
        return RE::UI_MESSAGE_RESULTS::kPassOn;
    }

#pragma endregion

#pragma region RE::MenuEventHandler

    bool MultiLayerMenu::CanProcess(RE::InputEvent* a_event)
    {
        return !m_menuMap.empty();
    }

    bool MultiLayerMenu::ProcessMouseMove(RE::MouseMoveEvent* a_event)
    {
        std::lock_guard<std::mutex> lock(m_mapMenuMutex);
        for (const auto& subMenu : m_menuMap)
        {
            if (subMenu.second->ProcessMouseMove(a_event))
            {
                return true;
            }
        }

        return false;
    }

    bool MultiLayerMenu::ProcessButton(RE::ButtonEvent* a_event)
    {
        std::lock_guard<std::mutex> lock(m_mapMenuMutex);
        for (const auto& subMenu : m_menuMap)
        {
            if (subMenu.second->ProcessButton(a_event))
            {
                return true;
            }
        }

        return false;
    }

#pragma endregion

#pragma region RE::BSTEventSink<RE::MenuOpenCloseEvent>

    RE::BSEventNotifyControl MultiLayerMenu::ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>* a_eventSource)
    {
        if (a_event->menuName == RE::CursorMenu::MENU_NAME)
        {
            RE::UI::GetSingleton()->pad17D = a_event->opening;
        }
        else if (m_isKeepOpen && !a_event->opening && a_event->menuName == MultiLayerMenu::MENU_NAME)
        {
            auto msgQ = RE::UIMessageQueue::GetSingleton();
            if (msgQ)
            {
                msgQ->AddMessage(NL::Menus::MultiLayerMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kShow, NULL);
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }

#pragma endregion

#pragma region RE::BSTEventSink<RE::InputEvent*>

    RE::BSEventNotifyControl MultiLayerMenu::ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>* a_eventSource)
    {
        if (a_event == nullptr || *a_event == nullptr)
        {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto inputEvent = *a_event;
        RE::InputEvent* nextEvent = nullptr;
        auto result = RE::BSEventNotifyControl::kContinue;
        if (!CanProcess(inputEvent)) [[unlikely]]
        {
            return RE::BSEventNotifyControl::kContinue;
        }

        std::lock_guard<std::mutex> lock(m_mapMenuMutex);
        while (inputEvent != nullptr)
        {
            nextEvent = inputEvent->next;
            inputEvent->next = nullptr;

            for (const auto& subMenu : m_menuMap)
            {
                switch (inputEvent->GetEventType())
                {
                case RE::INPUT_EVENT_TYPE::kMouseMove:
                    if (subMenu.second->ProcessMouseMove(inputEvent->AsMouseMoveEvent()))
                    {
                        result = RE::BSEventNotifyControl::kStop;
                        continue;
                    }
                    break;
                case RE::INPUT_EVENT_TYPE::kButton:
                    if (subMenu.second->ProcessButton(inputEvent->AsButtonEvent()))
                    {
                        result = RE::BSEventNotifyControl::kStop;
                        continue;
                    }
                    break;
                default:
                    break;
                }
            }

            inputEvent->next = nextEvent;
            inputEvent = inputEvent->next;
        }

        return result;
    }

#pragma endregion

}
