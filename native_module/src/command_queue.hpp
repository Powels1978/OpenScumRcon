#pragma once

// Thread-safe hand-off between the RCON worker thread (accepts connections,
// speaks the wire protocol) and the game thread (the only thread allowed to
// touch UObjects / call ProcessEvent). Mirrors the architecture already
// decided in docs/ARCHITECTURE.md: worker thread enqueues, game thread
// drains via an EngineTick pre-hook (the same pattern Herbie's own mod uses
// per its own log output - "game-thread drain installed via
// EngineTick-pre-callback").

#include "command_authority.hpp"
#include <deque>
#include <future>
#include <mutex>
#include <string>

namespace openscumrcon
{
    struct PendingCommand
    {
        std::string command_text;
        std::promise<std::string> response;
        CommandAuthority authority = CommandAuthority::none;
    };

    class CommandQueue
    {
    public:
        // Called from the worker thread. Blocks the calling connection
        // handler until the game thread has produced a response (or the
        // returned future is abandoned, e.g. on shutdown).
        std::future<std::string> enqueue(std::string command_text, CommandAuthority authority = CommandAuthority::none)
        {
            std::promise<std::string> promise;
            std::future<std::string> future = promise.get_future();
            if (!is_rcon_authorized(authority))
            {
                promise.set_value("error: authenticated RCON connection required");
                return future;
            }
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_pending.push_back(PendingCommand{std::move(command_text), std::move(promise), authority});
            }
            return future;
        }

        // Called from the game thread (EngineTick pre-hook). Drains and
        // returns all commands queued since the last call - never blocks.
        std::deque<PendingCommand> drain()
        {
            std::deque<PendingCommand> out;
            std::lock_guard<std::mutex> lock(m_mutex);
            std::swap(out, m_pending);
            return out;
        }

    private:
        std::mutex m_mutex;
        std::deque<PendingCommand> m_pending;
    };
}
