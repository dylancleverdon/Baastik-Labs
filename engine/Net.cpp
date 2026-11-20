#include "Net.h"

#include "tutti/SocketIO.h"

namespace tutti::net
{
    namespace
    {
        class LambdaThread : public juce::Thread
        {
        public:
            LambdaThread (const juce::String& name, std::function<void()> body) : juce::Thread (name), fn (std::move (body)) {}
            void run() override { fn(); }

        private:
            std::function<void()> fn;
        };
    }

    Link::Link (std::unique_ptr<juce::StreamingSocket> s, uint64_t id, bool out)
        : socket (std::move (s)), linkId (id), isOutgoing (out)
    {
        if (socket != nullptr)
        {
            protocol::noSigPipe (*socket);
            io::makeBlocking (socket->getRawSocketHandle());
        }
    }

    Link::~Link()
    {
        close();
        if (reader != nullptr)
            reader->stopThread (2000);
        if (writer != nullptr)
            writer->stopThread (2000);
    }

    juce::String Link::remoteHost() const
    {
        return socket != nullptr ? socket->getHostName() : juce::String();
    }

    void Link::start (FrameFn onFrame, ClosedFn onClosed)
    {
        frameFn = std::move (onFrame);
        closedFn = std::move (onClosed);
        reader = std::make_unique<LambdaThread> ("Tutti link read", [this] { readLoop(); });
        writer = std::make_unique<LambdaThread> ("Tutti link write", [this] { writeLoop(); });
        reader->startThread();
        writer->startThread();
    }

    void Link::send (std::string bytes)
    {
        if (stopping.load())
            return;
        {
            std::lock_guard<std::mutex> g (lock);
            queued += bytes.size();
            outbox.push_back (std::move (bytes));
        }
        wake.signal();
    }

    void Link::closeBecause (const std::string& reason)
    {
        {
            std::lock_guard<std::mutex> g (lock);
            if (why.empty())
                why = reason;
        }
        close();
    }

    void Link::close()
    {
        {
            std::lock_guard<std::mutex> g (lock);
            if (why.empty())
                why = "closed by this Tutti";
        }
        if (stopping.exchange (true))
            return;
        wake.signal();
        // Only shut it down: the reader and writer may still be using the handle, and closing it
        // here would let another socket reuse it under them. The destructor releases it.
        if (socket != nullptr)
            io::shutdownBoth (socket->getRawSocketHandle());
        if (! closedReported.exchange (true) && closedFn)
            closedFn (linkId);
    }

    void Link::readLoop()
    {
        protocol::Decoder decoder;
        std::vector<char> buffer (1 << 16);
        const int handle = socket->getRawSocketHandle();
        while (! stopping.load())
        {
            const int ready = io::waitReadable (handle, 200);
            if (ready < 0)
            {
                closeBecause ("network error while waiting");
                break;
            }
            if (ready == 0)
                continue;
            const int n = io::receive (handle, buffer.data(), static_cast<int> (buffer.size()));
            if (n == -2)
                continue;
            if (n == 0)
            {
                closeBecause ("closed by the other computer");
                break;
            }
            if (n < 0)
            {
                closeBecause ("network error while reading");
                break;
            }
            decoder.feed (buffer.data(), static_cast<size_t> (n));
            while (auto frame = decoder.next())
                if (frameFn)
                    frameFn (linkId, std::move (*frame));
            if (decoder.failed())
            {
                closeBecause ("received data Tutti couldn't read");
                break;
            }
        }
        close();
    }

    void Link::writeLoop()
    {
        while (! stopping.load())
        {
            std::string next;
            {
                std::lock_guard<std::mutex> g (lock);
                if (! outbox.empty())
                {
                    next = std::move (outbox.front());
                    outbox.pop_front();
                }
            }
            if (next.empty())
            {
                wake.wait (200);
                continue;
            }
            // Waits while the send buffer is full; only a dead link fails.
            if (! io::sendAll (socket->getRawSocketHandle(), next.data(), static_cast<int> (next.size())))
            {
                closeBecause ("network error while sending");
                return;
            }
            queued -= next.size();
        }
    }

    Server::~Server() { stop(); }

    bool Server::start (int port, const juce::String& bindAddress, AcceptFn onAccept)
    {
        stop();
        listener = std::make_unique<juce::StreamingSocket>();
        if (! listener->createListener (port, bindAddress))
        {
            listener.reset();
            return false;
        }
        boundTo = bindAddress;
        auto* sock = listener.get();
        thread = std::make_unique<LambdaThread> ("Tutti listener", [sock, onAccept] {
            while (! juce::Thread::currentThreadShouldExit())
            {
                std::unique_ptr<juce::StreamingSocket> client (sock->waitForNextConnection());
                if (client == nullptr)
                {
                    if (! sock->isConnected())
                        return;
                    continue;
                }
                onAccept (std::move (client));
            }
        });
        thread->startThread();
        return true;
    }

    void Server::stop()
    {
        if (thread != nullptr)
        {
            thread->signalThreadShouldExit();
            listener->close();
            thread->stopThread (2000);
            thread.reset();
        }
        listener.reset();
        boundTo = {};
    }

    void connectAsync (const juce::String& host, int port, int timeoutMs,
                       std::function<void (std::unique_ptr<juce::StreamingSocket>)> done)
    {
        juce::Thread::launch ([host, port, timeoutMs, done = std::move (done)] {
            auto socket = std::make_unique<juce::StreamingSocket>();
            if (socket->connect (host, port, timeoutMs))
                done (std::move (socket));
            else
                done (nullptr);
        });
    }
}
