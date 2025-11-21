#include "PCH.h"
#include "Hooks/ShutdownHook.hpp"
#include "Controllers/PublicAPIController.h"
#include "Utils/D3D11Utils.h"

inline void ShowMessageBox(const char* a_msg)
{
    MessageBoxA(0, a_msg, "ERROR", MB_ICONERROR);
}

void InitDefaultLog()
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

    *path /= fmt::format("{}.log"sv, NL::UI::LibVersion::PROJECT_NAME);
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

    auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
    log->set_level(level);
    log->flush_on(level);

    spdlog::set_default_logger(std::move(log));
    spdlog::set_pattern("[%T.%e] [%t] [%^%l%$] : %v"s);
}

void InitCefSubprocessLog()
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

    *path /= fmt::format("{}.log"sv, NL_UI_SUBPROC_NAME);
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

    auto log = std::make_shared<spdlog::logger>(NL_UI_SUBPROC_NAME, std::move(sink));
    log->set_level(level);
    log->flush_on(level);
    log->set_pattern("[%T.%e] [%^%l%$] : %v"s);

    spdlog::register_logger(std::move(log));
}

#ifdef SKYRIM_IS_AE
extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() {
    SKSE::PluginVersionData v{};
    v.pluginVersion = NL::UI::LibVersion::AS_INT;
    v.PluginName(NL::UI::LibVersion::PROJECT_NAME);
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

extern "C" DLLEXPORT bool SKSEAPI Entry(const SKSE::LoadInterface* a_skse)
{
    DLLEXPORT constinit auto SKSEPlugin_Version = []() {
        SKSE::PluginVersionData v{};
        v.pluginVersion = NL::UI::LibVersion::AS_INT;
        v.PluginName(NL::UI::LibVersion::PROJECT_NAME);
        v.AuthorName("kkEngine"sv);
        v.CompatibleVersions({SKSE::RUNTIME_SSE_1_6_640, REL::Version(1, 6, 1170, 0)});
        v.UsesAddressLibrary(true);
        v.UsesStructsPost629(true);
        return v;
    }();

    DLLEXPORT bool SKSEAPI Entry(const SKSE::LoadInterface* a_skse)
    {
        if (a_skse->IsEditor())
        {
            return false;
        }

        try
        {
            // SKSE
            SKSE::Init(a_skse);
            SKSE::AllocTrampoline(1024);
            InitDefaultLog();
            InitCefSubprocessLog();

            // Hooks
            NL::Hooks::WinProcHook::Install();
            NL::Hooks::ShutdownHook::Install();

            // API controller
            NL::Controllers::PublicAPIController::GetSingleton().Init();
        }
        catch (const std::exception& e)
        {
            ShowMessageBox(e.what());
            return false;
        }

        return true;
    }

    DLLEXPORT NL::UI::ResponseVersionMessage GetUIPlatformAPIVersion()
    {
        try
        {
            // SKSE
            SKSE::Init(a_skse);
            SKSE::AllocTrampoline(1024);
            InitDefaultLog();
            InitCefSubprocessLog();

            spdlog::info("winproc install");
            // Hooks
            NL::Hooks::WinProcHook::Install();
            NL::D3D11Hooks::Init();

            spdlog::info("shutdown install");
            NL::Hooks::ShutdownHook::Install();

            // API controller
            NL::Controllers::PublicAPIController::GetSingleton().Init();
        }
        catch (const std::exception& e)
        {
            ShowMessageBox(e.what());
            return false;
        }
    }

    DLLEXPORT bool CreateOrGetUIPlatformAPI(NL::UI::IUIPlatformAPI** a_outApi, NL::UI::Settings* a_settings)
    {
        auto& controller = NL::Controllers::PublicAPIController::GetSingleton();
        if (!controller.InitIfNotPlatformService(a_settings))
        {
            return false;
        }

        if (a_outApi == nullptr)
        {
            return false;
        }

        *a_outApi = controller.GetAPIMessage()->API;
        return true;
    }

    DLLEXPORT bool CreateOrGetUIPlatformAPIWithVersionCheck(NL::UI::IUIPlatformAPI** a_outApi,
                                                            NL::UI::Settings* a_settings,
                                                            std::uint32_t a_requestLibVersion,
                                                            const char* a_requestLibName)
    {
        const auto thisLibVer = GetUIPlatformAPIVersion();
        spdlog::info("NirnLabUIPlatform version: {}.{}", NL::UI::LibVersion::GetMajorVersion(thisLibVer.libVersion), NL::UI::LibVersion::GetMinorVersion(thisLibVer.libVersion));

        const auto requestMajorVer = NL::UI::APIVersion::GetMajorVersion(a_requestLibVersion);
        if (requestMajorVer != NL::UI::APIVersion::MAJOR)
        {
            spdlog::error("Can't return API for \"{}\", this ver is {}.{} and their ver is {}.{}",
                          a_requestLibName == nullptr ? "null" : a_requestLibName,
                          NL::UI::APIVersion::MAJOR,
                          NL::UI::APIVersion::MINOR,
                          NL::UI::APIVersion::GetMajorVersion(a_requestLibVersion),
                          NL::UI::APIVersion::GetMinorVersion(a_requestLibVersion));
            return false;
        }

        spdlog::info("API requested by \"{}\", this ver is {}.{} and thier ver is {}.{}",
                     a_requestLibName == nullptr ? "null" : a_requestLibName,
                     NL::UI::APIVersion::MAJOR,
                     NL::UI::APIVersion::MINOR,
                     NL::UI::APIVersion::GetMajorVersion(a_requestLibVersion),
                     NL::UI::APIVersion::GetMinorVersion(a_requestLibVersion));
        return ::CreateOrGetUIPlatformAPI(a_outApi, a_settings);
    }
}

extern "C" NL_DLL_API NL::UI::IUIPlatformAPI* SKSEAPI RequestPluginAPI(const NL::UI::Version a_interfaceVersion, NL::UI::Settings* settings)
{
    logger::info("RequestPluginAPI called");

    auto& controller = NL::Controllers::PublicAPIController::GetSingleton();

    auto ver = controller.GetVersionMessage();

    if (ver->libVersion != a_interfaceVersion.libVersion || ver->apiVersion != a_interfaceVersion.apiVersion)
    {
        logger::error("invalid version; pls update lib/headers;");
        logger::error("version {}.{} is expected, not {}.{}", ver->libVersion, ver->apiVersion, a_interfaceVersion.libVersion, a_interfaceVersion.apiVersion);
        return nullptr;
    }

    logger::info("RequestPluginAPI end");

    controller.SetSettingsProvider(settings);

    auto& platformService = NL::Services::UIPlatformService::GetSingleton();
    if (!platformService.IsInited() && !platformService.InitAndShowMenuWithSettings(controller.GetSettingsProvider()))
    {
        spdlog::error("{}: Can't response API because ui platform failed to init", NameOf(PublicAPIController));
        return nullptr;
    }

    return controller.GetAPIMessage();
}