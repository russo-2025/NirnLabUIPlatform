#pragma once

#include "Version.h"
#include "JSTypes.h"
#include "IBrowser.h"
#include "Settings.h"

#ifdef NL_DLL_IMPL
    #define NL_DLL_API __declspec(dllexport)
#else
    #define NL_DLL_API __declspec(dllimport)
#endif

namespace NL::UI
{
    class IUIPlatformAPI
    {
    public:
        using BrowserRefHandle = std::uint32_t;
        static constexpr BrowserRefHandle InvalidBrowserRefHandle = 0;
        using OnShutdownFunc_t = void (*)();

    public:
        virtual ~IUIPlatformAPI() = default;

        /// <summary>
        /// Add or get browser
        /// </summary>
        /// <param name="a_browserName">Unique name, returns the same browser for the same name</param>
        /// <param name="a_funcInfoArr">Function info array for binding to the browser's js. Pass nullptr for no bindings</param>
        /// <param name="a_funcInfoArrSize"></param>
        /// <param name="a_startUrl">Navigate to this url when the browser is created</param>
        /// <param name="a_outBrowser">Browser interface</param>
        /// <returns>A handle to release when you no longer need the browser</returns>
        virtual BrowserRefHandle __cdecl AddOrGetBrowser(const char* a_browserName,
                                                         NL::JS::JSFuncInfo* const* a_funcInfoArr,
                                                         const std::uint32_t a_funcInfoArrSize,
                                                         const char* a_startUrl,
                                                         NL::CEF::IBrowser*& a_outBrowser) = 0;

        /// <summary>
        /// Releases browser if no one has the handle
        /// </summary>
        /// <param name="a_handle"></param>
        /// <returns></returns>
        virtual void __cdecl ReleaseBrowserHandle(BrowserRefHandle a_handle) = 0;

        /// <summary>
        /// Add or get browser with custom settings
        /// </summary>
        /// <param name="a_browserName">Unique name, returns the same browser for the same name</param>
        /// <param name="a_funcInfoArr">Function info array for binding to the browser's js. Pass nullptr for no bindings</param>
        /// <param name="a_funcInfoArrSize"></param>
        /// <param name="a_startUrl">Navigate to this url when the browser is created</param>
        /// <param name="a_settings">Additional settings for new browser only</param>
        /// <param name="a_outBrowser">Browser interface</param>
        /// <returns></returns>
        virtual BrowserRefHandle __cdecl AddOrGetBrowser(const char* a_browserName,
                                                         NL::JS::JSFuncInfo* const* a_funcInfoArr,
                                                         const std::uint32_t a_funcInfoArrSize,
                                                         const char* a_startUrl,
                                                         NL::UI::BrowserSettings* a_settings,
                                                         NL::CEF::IBrowser*& a_outBrowser) = 0;

        /// <summary>
        /// Registers a shutdown callback function.
        /// After this callback, you should stop using any browser
        /// </summary>
        /// <param name="a_callback"></param>
        virtual void RegisterOnShutdown(OnShutdownFunc_t a_callback) = 0;
    };

    struct Version
    {
        /// <summary>
        /// NirnLabUIPlatform version
        /// </summary>
        std::uint32_t libVersion = NL::UI::LibVersion::AS_INT;

        /// <summary>
        /// NirnLabUIPlatform API version
        /// </summary>
        std::uint32_t apiVersion = NL::UI::APIVersion::AS_INT;
    };

    typedef IUIPlatformAPI* (*RequestPluginApiFn)(const Version a_interfaceVersion, Settings* settings);

    inline void AppendPathToEnv(std::filesystem::path p)
    {
        if (!p.is_absolute())
            std::runtime_error("An absolute path expected: " + p.string());

        if (!std::filesystem::is_directory(p))
            std::runtime_error("Expected path to be a directory: " + p.string());

        std::vector<wchar_t> path;
        path.resize(GetEnvironmentVariableW(L"PATH", nullptr, 0));
        GetEnvironmentVariableW(L"PATH", &path[0], path.size());

        std::wstring newPath = path.data();
        newPath += L';';
        newPath += p.wstring();

        if (!SetEnvironmentVariableW(L"PATH", newPath.data()))
        {
            std::runtime_error("Failed to modify PATH env: Error " +
                               std::to_string(GetLastError()));
        }

        spdlog::info("env path added");
    }

    inline RequestPluginApiFn LoadNirnLabUIPlatform()
    {

        std::string dllName = NL::UI::LibVersion::PROJECT_NAME;
        dllName += ".dll";

        std::filesystem::path nirnLabDir = std::filesystem::current_path() / L"Data" / L"NirnLabUIPlatform";
        std::filesystem::path nirnLabDll = nirnLabDir / dllName;

        AppendPathToEnv(nirnLabDir);

        if (!std::filesystem::exists(nirnLabDll))
        {
            spdlog::error("invalid dll in dir: `{}`", nirnLabDll.string());
            SKSE::stl::report_and_fail("invalid dll in dir");
        }

        auto nirnLabUILib = LoadLibraryA(dllName.c_str());

        if (!nirnLabUILib)
        {
            spdlog::error("could not load `{}`", dllName);
            SKSE::stl::report_and_fail("could not load " + dllName);
        }

        RequestPluginApiFn fn = (RequestPluginApiFn)GetProcAddress(nirnLabUILib, "RequestPluginAPI");
        if (!fn)
        {
            spdlog::error("could not locate the function RequestPluginAPI");
            SKSE::stl::report_and_fail("could not locate the function RequestPluginAPI");
        }

        spdlog::info("function successfully loaded from {}", dllName);

        return fn;
    };
}
