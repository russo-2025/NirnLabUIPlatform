#include "PCH.h"
#include "Hooks/ShutdownHook.hpp"
#include "Controllers/PublicAPIController.h"



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
    spdlog::set_pattern("[%T.%e] [%^%l%$] : %v"s);
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
    v.CompatibleVersions({SKSE::RUNTIME_1_6_640, SKSE::RUNTIME_1_6_1130});
    v.UsesAddressLibrary();
    v.UsesUpdatedStructs(); // v.UsesStructsPost629(true);
    return v;
}();
#else
    DLLEXPORT bool SKSEPlugin_Query(const SKSE::QueryInterface* skse, SKSE::PluginInfo* info)
    {
        info->infoVersion = SKSE::PluginInfo::kVersion;
        info->name = "NirnLabUIPlatform";
        info->version = 1;

        if (skse->IsEditor())
        {
            //_FATALERROR("loaded in editor, marking as incompatible");
            return false;
        }
        return true;
    }
#endif
extern "C" DLLEXPORT bool SKSEAPI Entry(const SKSE::LoadInterface* a_skse)
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
