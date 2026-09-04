#include "rcon_server.hpp"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <future>
#include <vector>

#include "rcon_protocol.hpp"

#pragma comment(lib, "Ws2_32.lib")

namespace openscumrcon
{
    namespace
    {
        constexpr int RESPONSE_WAIT_TIMEOUT_SECONDS = 25;

        bool recv_exact(SOCKET sock, char* buffer, int size)
        {
            int received = 0;
            while (received < size)
            {
                const int n = recv(sock, buffer + received, size - received, 0);
                if (n <= 0)
                {
                    return false;
                }
                received += n;
            }
            return true;
        }

        bool send_all(SOCKET sock, const char* data, int size)
        {
            int sent = 0;
            while (sent < size)
            {
                const int n = send(sock, data + sent, size - sent, 0);
                if (n <= 0)
                {
                    return false;
                }
                sent += n;
            }
            return true;
        }

        bool recv_packet(SOCKET sock, protocol::Packet& out)
        {
            char size_buf[4];
            if (!recv_exact(sock, size_buf, 4))
            {
                return false;
            }
            std::int32_t size = 0;
            std::memcpy(&size, size_buf, 4);
            if (size <= 0 || size > 8192)
            {
                // Refuse absurd sizes rather than trying to allocate/recv
                // something a malformed or hostile client claims - this is
                // listening on a LAN/VPN-only port with a password, but
                // cheap to be defensive here regardless.
                return false;
            }
            std::vector<char> body(static_cast<std::size_t>(size));
            if (!recv_exact(sock, body.data(), size))
            {
                return false;
            }
            return protocol::decode_packet_body(body, out);
        }

        void handle_connection(SOCKET client, const std::string& password, CommandQueue& queue)
        {
            protocol::Packet auth_packet;
            if (!recv_packet(client, auth_packet) || auth_packet.type != protocol::SERVERDATA_AUTH)
            {
                closesocket(client);
                return;
            }

            if (auth_packet.payload != password)
            {
                const auto fail = protocol::encode_packet(-1, protocol::SERVERDATA_AUTH_RESPONSE, "");
                send_all(client, fail.data(), static_cast<int>(fail.size()));
                closesocket(client);
                return;
            }

            const auto ok = protocol::encode_packet(auth_packet.request_id, protocol::SERVERDATA_AUTH_RESPONSE, "");
            if (!send_all(client, ok.data(), static_cast<int>(ok.size())))
            {
                closesocket(client);
                return;
            }

            while (true)
            {
                protocol::Packet command_packet;
                if (!recv_packet(client, command_packet))
                {
                    break;
                }
                if (command_packet.type != protocol::SERVERDATA_EXECCOMMAND)
                {
                    continue;
                }

                std::future<std::string> response_future = queue.enqueue(command_packet.payload);
                std::string response_text;
                if (response_future.wait_for(std::chrono::seconds(RESPONSE_WAIT_TIMEOUT_SECONDS))
                        == std::future_status::ready)
                {
                    response_text = response_future.get();
                }
                else
                {
                    response_text = "error: command timed out";
                }

                const auto response_packets = protocol::encode_response(command_packet.request_id, response_text);
                bool ok_send = true;
                for (const auto& packet_bytes : response_packets)
                {
                    if (!send_all(client, packet_bytes.data(), static_cast<int>(packet_bytes.size())))
                    {
                        ok_send = false;
                        break;
                    }
                }
                if (!ok_send)
                {
                    break;
                }
            }

            closesocket(client);
        }
    }

    RconServer::~RconServer()
    {
        stop();
    }

    bool RconServer::start(const std::string& bind_host, unsigned short port, std::string password, CommandQueue& queue)
    {
        if (m_running.exchange(true))
        {
            return false; // already running
        }
        m_queue = &queue;
        m_thread = std::thread(&RconServer::run, this, bind_host, port, std::move(password));
        return true;
    }

    void RconServer::stop()
    {
        if (!m_running.exchange(false))
        {
            return;
        }
        const SOCKET listen_socket = static_cast<SOCKET>(m_listen_socket.load());
        if (listen_socket != INVALID_SOCKET)
        {
            // Unblocks the accept() call in run() so the thread can exit.
            closesocket(listen_socket);
        }
        if (m_thread.joinable())
        {
            m_thread.join();
        }
    }

    void RconServer::run(std::string bind_host, unsigned short port, std::string password)
    {
        WSADATA wsa_data{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
        {
            m_running = false;
            return;
        }

        SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listen_socket == INVALID_SOCKET)
        {
            WSACleanup();
            m_running = false;
            return;
        }

        BOOL reuse = TRUE;
        setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        if (bind_host.empty() || bind_host == "0.0.0.0")
        {
            address.sin_addr.s_addr = INADDR_ANY;
        }
        else
        {
            inet_pton(AF_INET, bind_host.c_str(), &address.sin_addr);
        }

        if (bind(listen_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR
                || listen(listen_socket, SOMAXCONN) == SOCKET_ERROR)
        {
            closesocket(listen_socket);
            WSACleanup();
            m_running = false;
            return;
        }

        m_listen_socket = static_cast<long long>(listen_socket);

        while (m_running.load())
        {
            SOCKET client = accept(listen_socket, nullptr, nullptr);
            if (client == INVALID_SOCKET)
            {
                break; // listen_socket was closed by stop(), or a real error - either way, exit.
            }
            // v1: handle one connection at a time on this same thread. See
            // header comment for why, and for the planned extension.
            handle_connection(client, password, *m_queue);
        }

        closesocket(listen_socket);
        WSACleanup();
        m_running = false;
    }
}
