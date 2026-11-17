#pragma once

#include "tutti/Protocol.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>

namespace tutti::net
{
    // One TCP connection carrying protocol frames. A reader thread decodes incoming frames and a
    // writer thread drains the send queue, so nothing here ever blocks the caller.
    class Link
    {
    public:
        using FrameFn = std::function<void (uint64_t linkId, protocol::Frame)>;
        using ClosedFn = std::function<void (uint64_t linkId)>;

        Link (std::unique_ptr<juce::StreamingSocket> socket, uint64_t id, bool outgoing);
        ~Link();

        void start (FrameFn onFrame, ClosedFn onClosed);
        void send (std::string bytes);
        void close();

        uint64_t id() const { return linkId; }
        bool outgoing() const { return isOutgoing; }
        size_t queuedBytes() const { return queued.load(); }
        juce::String remoteHost() const;

    private:
        void readLoop();
        void writeLoop();

        std::unique_ptr<juce::StreamingSocket> socket;
        const uint64_t linkId;
        const bool isOutgoing;
        FrameFn frameFn;
        ClosedFn closedFn;

        std::mutex lock;
        std::deque<std::string> outbox;
        juce::WaitableEvent wake;
        std::atomic<size_t> queued { 0 };
        std::atomic<bool> stopping { false };
        std::atomic<bool> closedReported { false };
        std::unique_ptr<juce::Thread> reader, writer;
    };

    // Accepts connections on a port.
    class Server
    {
    public:
        using AcceptFn = std::function<void (std::unique_ptr<juce::StreamingSocket>)>;

        ~Server();
        bool start (int port, const juce::String& bindAddress, AcceptFn onAccept);
        void stop();
        bool running() const { return thread != nullptr; }
        const juce::String& address() const { return boundTo; }

    private:
        std::unique_ptr<juce::StreamingSocket> listener;
        std::unique_ptr<juce::Thread> thread;
        juce::String boundTo;
    };

    // Connects on a background thread; `done` gets the socket (connected) or nullptr.
    void connectAsync (const juce::String& host, int port, int timeoutMs,
                       std::function<void (std::unique_ptr<juce::StreamingSocket>)> done);
}
