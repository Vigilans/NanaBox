/*
 * PROJECT:    NanaBox
 * FILE:       NanaBox.cpp
 * PURPOSE:    Implementation for NanaBox
 *
 * LICENSE:    The MIT License
 *
 * MAINTAINER: MouriNaruto (Kenji.Mouri@outlook.com)
 */

#include <Windows.h>

#include <ShlObj.h>

#include "pch.h"

#include "App.h"
#include "MainWindow.h"
#include "QuickStartPage.h"
#include "SponsorPage.h"

#include <Mile.Project.Version.h>

#include "Utils.h"
#include "ConfigurationManager.h"
#include "NanaBoxResources.h"

#include <Mile.Helpers.h>
#include <Mile.Xaml.h>

namespace
{
    const std::wstring_view g_AppUserModelID =
        L"M2Team.NanaBox"
        L"_" MILE_PROJECT_DOT_VERSION_STRING
        L"_" MILE_PROJECT_BUILD_DATE_STRING;

    WTL::CAppModule g_Module;
}

void PrerequisiteCheck()
{
    try
    {
        NanaBox::HcsGetServiceProperties();
    }
    catch (winrt::hresult_error const& ex)
    {
        winrt::hstring InstructionText;
        winrt::hstring ContentText;

        if (ex.code() == HCS_E_ACCESS_DENIED)
        {
            InstructionText = Mile::WinRT::GetLocalizedString(
                L"Messages/AccessDeniedInstructionText");
            ContentText = Mile::WinRT::GetLocalizedString(
                L"Messages/AccessDeniedContentText");
        }
        else if (ex.code() == HCS_E_SERVICE_NOT_AVAILABLE)
        {
            InstructionText = Mile::WinRT::GetLocalizedString(
                L"Messages/HyperVNotAvailableInstructionText");
            ContentText = Mile::WinRT::GetLocalizedString(
                L"Messages/HyperVNotAvailableContentText");
        }
        else
        {
            InstructionText = ex.message();
        }

        ::ShowMessageDialog(
            nullptr,
            InstructionText,
            ContentText);

        ::ExitProcess(ex.code());
    }

    return;
}

int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nShowCmd)
{
    UNREFERENCED_PARAMETER(hPrevInstance);

    winrt::init_apartment(winrt::apartment_type::single_threaded);

    winrt::check_hresult(::SetCurrentProcessExplicitAppUserModelID(
        g_AppUserModelID.data()));

    ::SetProcessShutdownParameters(0x3FF, 0);

    std::map<std::wstring, std::wstring> Options;
    std::wstring Subcommand;
    std::wstring Argument;

    {
        int argc = 0;
        LPWSTR* argv = ::CommandLineToArgvW(
            ::GetCommandLineW(), &argc);
        if (argv)
        {
            for (int i = 1; i < argc; ++i)
            {
                std::wstring Token = argv[i];
                std::wstring OptionName;
                if (Token.size() > 2 &&
                    Token[0] == L'-' && Token[1] == L'-')
                {
                    OptionName = Token.substr(2);
                }
                else if (Token.size() > 1 &&
                    (Token[0] == L'-' || Token[0] == L'/'))
                {
                    OptionName = Token.substr(1);
                }

                if (!OptionName.empty())
                {
                    std::wstring OptionValue;
                    auto Sep = OptionName.find_first_of(L"=:");
                    if (Sep != std::wstring::npos)
                    {
                        OptionValue = OptionName.substr(Sep + 1);
                        OptionName.resize(Sep);
                    }
                    Options[OptionName] = OptionValue;
                }
                else if (Subcommand.empty() &&
                    (0 == ::_wcsicmp(Token.c_str(), L"start") ||
                     0 == ::_wcsicmp(Token.c_str(), L"stop")))
                {
                    Subcommand = Token;
                }
                else
                {
                    Argument = Token;
                }
            }
            ::LocalFree(argv);
        }
    }

    if (0 == ::_wcsicmp(Subcommand.c_str(), L"stop"))
    {
        if (!::MileIsCurrentProcessElevated())
        {
            std::wstring ModulePath = ::GetCurrentProcessModulePath();
            SHELLEXECUTEINFOW Information = {};
            Information.cbSize = sizeof(SHELLEXECUTEINFOW);
            Information.fMask = SEE_MASK_NOCLOSEPROCESS;
            Information.lpVerb = L"runas";
            Information.nShow = SW_HIDE;
            Information.lpFile = ModulePath.c_str();
            Information.lpParameters = lpCmdLine;
            if (!::ShellExecuteExW(&Information))
            {
                return ::HRESULT_FROM_WIN32(::GetLastError());
            }
            ::WaitForSingleObjectEx(Information.hProcess, INFINITE, FALSE);
            DWORD ExitCode = 0;
            ::GetExitCodeProcess(Information.hProcess, &ExitCode);
            ::CloseHandle(Information.hProcess);
            return static_cast<int>(ExitCode);
        }

        if (Argument.empty())
        {
            return ERROR_INVALID_PARAMETER;
        }

        std::wstring VmName = Argument;

        // If argument is a .7b config file, read the VM name from it
        std::wstring Extension = VmName.substr(
            VmName.size() > 3 ? VmName.size() - 3 : 0);
        if (0 == ::_wcsicmp(Extension.c_str(), L".7b"))
        {
            try
            {
                std::wstring AbsolutePath = ::GetAbsolutePath(VmName);
                std::string Content =
                    ::ReadAllTextFromUtf8TextFile(AbsolutePath);
                auto Config = NanaBox::DeserializeConfiguration(Content);
                VmName = winrt::to_hstring(Config.Name).c_str();
            }
            catch (...)
            {
                return ERROR_INVALID_PARAMETER;
            }
        }

        try
        {
            auto ComputeSystem =
                winrt::make_self<NanaBox::ComputeSystem>(
                    winrt::hstring(VmName));

            ComputeSystem->Pause();
            ComputeSystem->Terminate();
        }
        catch (winrt::hresult_error const& ex)
        {
            return ex.code();
        }

        return 0;
    }

    winrt::com_ptr<winrt::NanaBox::implementation::App> App =
        winrt::make_self<winrt::NanaBox::implementation::App>();
    auto ExitHandler = Mile::ScopeExitTaskHandler([&]()
    {
        App->Close();
    });

    if (Options.count(L"AcquireSponsorEdition"))
    {
        HWND WindowHandle = ::CreateWindowExW(
            WS_EX_STATICEDGE | WS_EX_DLGMODALFRAME,
            L"Mile.Xaml.ContentWindow",
            nullptr,
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            CW_USEDEFAULT,
            0,
            CW_USEDEFAULT,
            0,
            nullptr,
            nullptr,
            nullptr,
            nullptr);
        if (!WindowHandle)
        {
            return -1;
        }

        if (FAILED(::MileAllowNonClientDefaultDrawingForWindow(
            WindowHandle,
            FALSE)))
        {
            return -1;
        }

        winrt::NanaBox::SponsorPage Window =
            winrt::make<winrt::NanaBox::implementation::SponsorPage>(
                WindowHandle);
        ::ShowXamlWindow(
            WindowHandle,
            460,
            320,
            winrt::get_abi(Window),
            nullptr);

        ::ExitProcess(0);
    }

    bool PackagedMode = Mile::WinRT::IsPackagedMode();
    std::wstring TargetBinaryPath;

    if (PackagedMode)
    {
        try
        {
            std::wstring AppBinaryPath;
            {
                std::wstring RawPath = ::GetCurrentProcessModulePath();
                std::wcsrchr(&RawPath[0], L'\\')[0] = L'\0';
                RawPath.resize(std::wcslen(RawPath.c_str()));
                AppBinaryPath = RawPath;
            }

            std::wstring TempBinaryPath;
            {
                std::wstring RawPath = ::GetLocalStateFolderPath();
                GUID TempFolderGuid;
                winrt::check_hresult(::CoCreateGuid(&TempFolderGuid));
                RawPath.append(L"\\");
                RawPath.append(::FromGuid(TempFolderGuid));
                TempBinaryPath = RawPath;
            }

            winrt::check_bool(::CreateDirectoryW(
                TempBinaryPath.c_str(),
                nullptr));

            winrt::check_bool(::CopyFileW(
                (AppBinaryPath + L"\\NanaBox.exe").c_str(),
                (TempBinaryPath + L"\\NanaBox.exe").c_str(),
                FALSE));

            winrt::check_bool(::CopyFileW(
                (AppBinaryPath + L"\\resources.pri").c_str(),
                (TempBinaryPath + L"\\resources.pri").c_str(),
                FALSE));

            winrt::check_bool(::CopyFileW(
                (AppBinaryPath + L"\\Mile.Xaml.Styles.SunValley.xbf").c_str(),
                (TempBinaryPath + L"\\Mile.Xaml.Styles.SunValley.xbf").c_str(),
                FALSE));

            TargetBinaryPath = TempBinaryPath;
        }
        catch (...)
        {
            winrt::hresult_error Exception = Mile::WinRT::ToHResultError();
            ::ShowErrorMessageDialog(Exception);
            ::ExitProcess(Exception.code());
        }
    }

    if (!::MileIsCurrentProcessElevated() || PackagedMode)
    {
        try
        {
            std::wstring ApplicationName;
            if (PackagedMode && !TargetBinaryPath.empty())
            {
                ApplicationName = TargetBinaryPath + L"\\NanaBox.exe";
            }
            else
            {
                ApplicationName = ::GetCurrentProcessModulePath();
            }

            SHELLEXECUTEINFOW Information = {};
            Information.cbSize = sizeof(SHELLEXECUTEINFOW);
            Information.fMask = SEE_MASK_NOCLOSEPROCESS;
            Information.lpVerb = L"runas";
            Information.nShow = nShowCmd;
            Information.lpFile = ApplicationName.c_str();
            Information.lpParameters = lpCmdLine;
            winrt::check_bool(::ShellExecuteExW(&Information));
            ::WaitForSingleObjectEx(Information.hProcess, INFINITE, FALSE);
            ::CloseHandle(Information.hProcess);

            if (PackagedMode && !TargetBinaryPath.empty())
            {
                ::SimpleRemoveDirectory(TargetBinaryPath.c_str());
            }
        }
        catch (...)
        {
            winrt::hresult_error Exception = Mile::WinRT::ToHResultError();

            if (Exception.code() != ::HRESULT_FROM_WIN32(ERROR_CANCELLED))
            {
                ::ShowErrorMessageDialog(Exception);
            }

            if (PackagedMode && !TargetBinaryPath.empty())
            {
                ::SimpleRemoveDirectory(TargetBinaryPath.c_str());
            }

            ::ExitProcess(Exception.code());
        }

        ::ExitProcess(0);
    }

    ::PrerequisiteCheck();

    std::wstring ConfigurationFilePath;

    if (!Argument.empty())
    {
        ConfigurationFilePath = ::GetAbsolutePath(Argument);
    }

    if (ConfigurationFilePath.empty())
    {
        HWND WindowHandle = ::CreateWindowExW(
            WS_EX_STATICEDGE | WS_EX_DLGMODALFRAME,
            L"Mile.Xaml.ContentWindow",
            nullptr,
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
            CW_USEDEFAULT,
            0,
            CW_USEDEFAULT,
            0,
            nullptr,
            nullptr,
            nullptr,
            nullptr);
        if (!WindowHandle)
        {
            return -1;
        }

        if (FAILED(::MileAllowNonClientDefaultDrawingForWindow(
            WindowHandle,
            FALSE)))
        {
            return -1;
        }

        winrt::NanaBox::QuickStartPage Window =
            winrt::make<winrt::NanaBox::implementation::QuickStartPage>(
                WindowHandle,
                &ConfigurationFilePath);
        ::ShowXamlWindow(
            WindowHandle,
            460,
            460,
            winrt::get_abi(Window),
            nullptr);
        if (ConfigurationFilePath.empty())
        {
            return 0;
        }
    }

    {
        std::wstring CurrentPath = ConfigurationFilePath;
        std::wcsrchr(&CurrentPath[0], L'\\')[0] = L'\0';
        CurrentPath.resize(std::wcslen(CurrentPath.c_str()));
        ::SetCurrentDirectoryW(CurrentPath.c_str());
    }

    WTL::CMessageLoop MessageLoop;

    ::SystemAtlAxWinInit();

    g_Module.Init(nullptr, hInstance);
    g_Module.AddMessageLoop(&MessageLoop);

    NanaBox::MainWindow MainWindow(ConfigurationFilePath);
    if (!MainWindow.Create(
        nullptr,
        MainWindow.rcDefault,
        L"NanaBox",
        WS_OVERLAPPEDWINDOW))
    {
        return -1;
    }

    MainWindow.ShowWindow(nShowCmd);
    MainWindow.UpdateWindow();

    int Result = MessageLoop.Run();

    g_Module.RemoveMessageLoop();
    g_Module.Term();

    return Result;
}
