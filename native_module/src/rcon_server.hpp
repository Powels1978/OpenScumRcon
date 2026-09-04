#pragma once

// Worker-thread TCP listener speaking the Source RCON wire protocol
// (see rcon_protocol.hpp / docs/ARCHITECTURE.md). Runs entirely off the
// game thread; every incoming command is handed to CommandQueue and the
// connection handler blocks on the returned future until the game thread
// (via the EngineTick pre-hook in dllmain.cpp) has produced a response.
//
// v1 deliberately handles one client connection at a time - matches how
// this mod is actually used today (short-lived connections per command,
// see local_bridge's SourceRcon.run() which opens a fresh socket per call)
// and keeps the first working version simple. Concurrent connections are a
// straightforward later extension (accept loop -> one thread per
// connection) once the single-connection version is verified end-to-end.

#include <atomic>
#include <string>
#include <thread>

#include "command_queue.hpp"

namespace openscumrcon
{
    class RconServer
    {
    public:
        ~RconServer();

        // Starts the listener on a background thread. Safe to call once;
        // call stop() before the mod unloads.
        bool start(const std::string& bind_host, unsigned short port, std::string password, CommandQueue& queue);

        void stop();

    private:
        void run(std::string bind_host, unsigned short port, std::string password);

        CommandQueue* m_queue = nullptr;
        std::thread m_thread;
        std::atomic<bool> m_running{false};
        std::atomic<long long> m_listen_socket{-1}; // SOCKET, stored as integer to avoid pulling winsock2.h into this header
    };
}
