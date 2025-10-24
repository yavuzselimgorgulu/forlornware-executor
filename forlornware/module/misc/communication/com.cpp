#include "com.hpp"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace
{
    std::string trim_message(std::string_view message)
    {
        std::string sanitized(message.begin(), message.end());
        while (!sanitized.empty() && (sanitized.back() == '\r' || sanitized.back() == '\n'))
        {
            sanitized.pop_back();
        }
        return sanitized;
    }

    std::string describe_error(int code)
    {
        LPSTR buffer = nullptr;
        const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
        const DWORD length = FormatMessageA(flags, nullptr, static_cast<DWORD>(code), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                            reinterpret_cast<LPSTR>(&buffer), 0, nullptr);

        std::string result;
        if (length != 0 && buffer != nullptr)
        {
            result.assign(buffer, buffer + length);
            LocalFree(buffer);
        }

        if (result.empty())
        {
            result = "Unknown error (" + std::to_string(code) + ")";
        }

        return trim_message(result);
    }
}

struct script_server::impl
{
    SOCKET server_socket = INVALID_SOCKET;
    SOCKET client_socket = INVALID_SOCKET;
    sockaddr_in server_addr{};
    WSADATA wsa_data{};
    bool wsa_active = false;
    std::string last_error;
};

script_server::script_server()
    : pimpl(new impl())
{
}

script_server::~script_server()
{
    close();
    delete pimpl;
}

bool script_server::initialize(int port)
{
    close();

    const WORD version = MAKEWORD(2, 2);
    const int startup_result = WSAStartup(version, &pimpl->wsa_data);
    if (startup_result != 0)
    {
        pimpl->last_error = "WSAStartup failed: " + describe_error(startup_result);
        return false;
    }

    pimpl->wsa_active = true;
    pimpl->server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (pimpl->server_socket == INVALID_SOCKET)
    {
        const int error = WSAGetLastError();
        const std::string message = "socket creation failed: " + describe_error(error);
        close();
        pimpl->last_error = message;
        return false;
    }

    BOOL reuse_addr = TRUE;
    setsockopt(pimpl->server_socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&reuse_addr), sizeof(reuse_addr));

    pimpl->server_addr = {};
    pimpl->server_addr.sin_family = AF_INET;
    pimpl->server_addr.sin_addr.s_addr = INADDR_ANY;
    pimpl->server_addr.sin_port = htons(static_cast<u_short>(port));

    if (bind(pimpl->server_socket, reinterpret_cast<sockaddr*>(&pimpl->server_addr), sizeof(pimpl->server_addr)) == SOCKET_ERROR)
    {
        const int error = WSAGetLastError();
        const std::string message = "bind failed: " + describe_error(error);
        close();
        pimpl->last_error = message;
        return false;
    }

    if (listen(pimpl->server_socket, SOMAXCONN) == SOCKET_ERROR)
    {
        const int error = WSAGetLastError();
        const std::string message = "listen failed: " + describe_error(error);
        close();
        pimpl->last_error = message;
        return false;
    }

    pimpl->last_error.clear();
    return true;
}

std::optional<std::string> script_server::receive_script()
{
    if (pimpl->server_socket == INVALID_SOCKET)
    {
        pimpl->last_error = "receive_script called on an uninitialized server";
        return std::nullopt;
    }

    sockaddr_in client_addr{};
    int client_size = sizeof(client_addr);
    pimpl->client_socket = accept(pimpl->server_socket, reinterpret_cast<sockaddr*>(&client_addr), &client_size);
    if (pimpl->client_socket == INVALID_SOCKET)
    {
        const int error = WSAGetLastError();
        pimpl->last_error = "accept failed: " + describe_error(error);
        return std::nullopt;
    }

    std::string script;
    std::array<char, 4096> buffer{};

    while (true)
    {
        const int bytes_received = recv(pimpl->client_socket, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (bytes_received > 0)
        {
            script.append(buffer.data(), bytes_received);
            continue;
        }

        if (bytes_received == 0)
        {
            break;
        }

        const int error = WSAGetLastError();
        pimpl->last_error = "recv failed: " + describe_error(error);
        closesocket(pimpl->client_socket);
        pimpl->client_socket = INVALID_SOCKET;
        return std::nullopt;
    }

    closesocket(pimpl->client_socket);
    pimpl->client_socket = INVALID_SOCKET;
    pimpl->last_error.clear();
    return script;
}

void script_server::close()
{
    if (pimpl->client_socket != INVALID_SOCKET)
    {
        closesocket(pimpl->client_socket);
        pimpl->client_socket = INVALID_SOCKET;
    }

    if (pimpl->server_socket != INVALID_SOCKET)
    {
        closesocket(pimpl->server_socket);
        pimpl->server_socket = INVALID_SOCKET;
    }

    if (pimpl->wsa_active)
    {
        WSACleanup();
        pimpl->wsa_active = false;
    }
}

std::string script_server::last_error() const
{
    return pimpl->last_error;
}
