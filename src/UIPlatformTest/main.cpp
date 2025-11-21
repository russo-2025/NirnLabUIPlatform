#include "PCH.h"

NL::UI::Settings defaultSettings;
NL::UI::Version nirnLabUIPlatformVersion = {.libVersion = NL::UI::LibVersion::AS_INT, .apiVersion = NL::UI::APIVersion::AS_INT};
NL::UI::RequestPluginApiFn RequestPluginApi = nullptr;
NL::UI::IUIPlatformAPI* api = nullptr;

void InitLog()
{
#ifdef _DEBUG
    const auto level = spdlog::level::trace;
    auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
    const auto level = spdlog::level::info;
    auto path = logger::log_directory();
    if (!path)
    {
        SKSE::stl::report_and_fail("Failed to find standard logging directory"sv);
    }

    *path /= fmt::format("{}.log"sv, PLUGIN_NAME);
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

    auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
    log->set_level(level);
    log->flush_on(level);

    spdlog::set_default_logger(std::move(log));
    spdlog::set_pattern("[%T.%e] [%t] [%^%l%$] : %v"s);
}

#ifdef SKYRIM_IS_AE
extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() {
    SKSE::PluginVersionData v{};
    v.pluginVersion = 1;
    v.PluginName(PLUGIN_NAME);
    v.AuthorName("kkEngine"sv);
    // v.CompatibleVersions({SKSE::RUNTIME_SSE_1_6_640, REL::Version(1, 6, 1170, 0)});
    v.UsesAddressLibrary(true);
    // v.UsesStructsPost629(true);
    return v;
}();
#endif

extern "C" [[maybe_unused]] DLLEXPORT bool SKSEPlugin_Query(::SKSE::QueryInterface*, ::SKSE::PluginInfo* pluginInfo)
{
    pluginInfo->infoVersion = ::SKSE::PluginInfo::kVersion;
    pluginInfo->name = NL::UI::LibVersion::PROJECT_NAME;
    pluginInfo->version = NL::UI::LibVersion::AS_INT;
    return true;
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
    static bool s_canUseAPI = false;
    SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
        switch (a_msg->type)
        {
        case SKSE::MessagingInterface::kInputLoaded:
            RequestPluginApi = NL::UI::LoadNirnLabUIPlatform();
            if (!RequestPluginApi)
            {
                spdlog::error("RequestPluginApi is nullptr");
                SKSE::stl::report_and_fail("RequestPluginApi is nullptr");
            }

            api = RequestPluginApi(nirnLabUIPlatformVersion, &defaultSettings);

            if (!api)
            {
                spdlog::error("api is nullptr");
                SKSE::stl::report_and_fail("api is nullptr");
            }

            NL::UI::TestCase::StartTests(api);
            break;
        default:
            break;
        }
    });
}

void Init2ndMethodToGetAPI()
{
    SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
        switch (a_msg->type)
        {
        case SKSE::MessagingInterface::kInputLoaded:
            // All plugins are loaded
            try
            {
                NL::UI::IUIPlatformAPI* api = nullptr;
                NL::UI::Settings defaultSettings;

                if (NL::UI::DllLoader::CreateOrGetUIPlatformAPIWithVersionCheck(&api, &defaultSettings, NL::UI::APIVersion::AS_INT, PLUGIN_NAME))
                {
                    NL::UI::TestCase::StartTests(api);
                }
                else
                {
                    spdlog::error("Failed to load NirnLabUIPlatform API :(");
                }
            }
            catch (const std::exception& err)
            {
                spdlog::error("Failed to load NirnLabUIPlatform API, {}", err.what());
            }
            break;
        default:
            break;
        }
    });
}

void Init3rdMethodToGetAPI()
{
    SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
        NL::UI::SKSELoader::ProcessSKSEMessage(a_msg);
    });
    NL::UI::SKSELoader::GetUIPlatformAPIWithVersionCheck([](NL::UI::IUIPlatformAPI* a_api) {
        NL::UI::TestCase::StartTests(a_api);
    });
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
    if (a_skse->IsEditor())
    {
        return false;
    }

    // SKSE
    SKSE::Init(a_skse);
    SKSE::AllocTrampoline(1024);
    InitLog();
    // First method may not work correctly with some plugins
    // Init1stMethodToGetAPI();
    // Init2ndMethodToGetAPI();
    Init3rdMethodToGetAPI();

    const auto iniCollection = RE::INISettingCollection::GetSingleton();
    // [General]
    // Don't stop game when window is collapsed
    iniCollection->GetSetting("bAlwaysActive:General")->data.b = true;

    return true;
}