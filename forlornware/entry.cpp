#include <Windows.h>

#include <atomic>
#include <chrono>
#include <exception>
#include <string>
#include <string_view>
#include <thread>

#include "misc/communication/com.hpp"
#include "misc/teleport_handler/tp_handler.hpp"

namespace forlorn::runtime
{
    namespace
    {
        constexpr int kServerPort = 2304;
        constexpr auto kIdleDelay = std::chrono::milliseconds(10);

        std::atomic_bool g_running{true};

        using set_thread_description_t = HRESULT(WINAPI*)(HANDLE, PCWSTR);

        void log(std::string_view severity, std::string_view message)
        {
            std::string formatted = "[Forlornware][" + std::string(severity) + "] " + std::string(message);
            roblox::r_print(0, "%s", formatted.c_str());
            formatted.push_back('\n');
            OutputDebugStringA(formatted.c_str());
        }

        void log_info(std::string_view message)
        {
            log("info", message);
        }

        void log_error(std::string_view message)
        {
            log("error", message);
        }

        void set_thread_name(std::wstring_view name)
        {
            const HMODULE kernel32 = GetModuleHandleW(L"Kernel32.dll");
            if (!kernel32)
            {
                return;
            }

            const auto set_description = reinterpret_cast<set_thread_description_t>(
                GetProcAddress(kernel32, "SetThreadDescription"));
            if (!set_description)
            {
                return;
            }

            set_description(GetCurrentThread(), name.data());
        }

        void dispatch_script(const std::string& script)
        {
            try
            {
                task_scheduler::send_script(script);
                log_info("Dispatched script to scheduler.");
            }
            catch (const std::exception& ex)
            {
                log_error(std::string("Failed to dispatch script: ") + ex.what());
            }
            catch (...)
            {
                log_error("Failed to dispatch script: unknown error.");
            }
        }
    }

    void run()
    {
        set_thread_name(L"ForlornwareRuntime");
        log_info("Bootstrapping runtime threads.");

        teleport_handler::initialize();
        log_info("Teleport handler initialized.");

        script_server server;
        if (!server.initialize(kServerPort))
        {
            log_error(std::string("Unable to initialize script server: ") + server.last_error());
            return;
        }

        log_info("Script server listening on port 2304.");

        while (g_running.load(std::memory_order_acquire))
        {
            const auto script = server.receive_script();
            if (!script.has_value())
            {
                if (!g_running.load(std::memory_order_relaxed))
                {
                    break;
                }

                const auto error = server.last_error();
                if (!error.empty())
                {
                    log_error(std::string("Communication error: ") + error);
                }

                std::this_thread::sleep_for(kIdleDelay);
                continue;
            }

            if (script->empty())
            {
                log_info("Ignoring empty script payload.");
                continue;
            }

            dispatch_script(*script);
        }

        server.close();
        log_info("Runtime thread shutting down.");
    }

    void stop()
    {
        g_running.store(false, std::memory_order_release);
    }
}

#pragma region DLL_EXPORTS
extern "C" __declspec(dllexport) LRESULT NextHook(int code, WPARAM wParam, LPARAM lParam)
{
    return CallNextHookEx(nullptr, code, wParam, lParam);
}
#pragma endregion

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(mod);
        std::thread(forlorn::runtime::run).detach();
        break;
    }
    case DLL_PROCESS_DETACH:
        forlorn::runtime::stop();
        break;
    default:
        break;
    }

    return TRUE;
}
