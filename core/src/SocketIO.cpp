// Plain socket calls, kept in their own file so the system networking headers never meet JUCE's.
#include "tutti/SocketIO.h"

#if defined (_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <winsock2.h>
#else
 #include <cerrno>
 #include <fcntl.h>
 #include <poll.h>
 #include <sys/socket.h>
 #include <sys/types.h>
#endif

namespace tutti::io
{
    void makeBlocking (int handle)
    {
       #if defined (_WIN32)
        u_long nonBlocking = 0;
        ioctlsocket ((SOCKET) handle, FIONBIO, &nonBlocking);
       #else
        const int flags = fcntl (handle, F_GETFL, 0);
        if (flags != -1 && (flags & O_NONBLOCK) != 0)
            fcntl (handle, F_SETFL, flags & ~O_NONBLOCK);
       #endif
    }

    int waitReadable (int handle, int timeoutMs)
    {
       #if defined (_WIN32)
        fd_set set;
        FD_ZERO (&set);
        FD_SET ((SOCKET) handle, &set);
        timeval tv { timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
        const int r = select (0, &set, nullptr, nullptr, &tv);
        if (r == SOCKET_ERROR)
            return WSAGetLastError() == WSAEINTR ? 0 : -1;
        return r > 0 ? 1 : 0;
       #else
        pollfd p { handle, POLLIN, 0 };
        const int r = poll (&p, 1, timeoutMs);
        if (r < 0)
            return errno == EINTR ? 0 : -1;
        return r > 0 ? 1 : 0;
       #endif
    }

    void shutdownBoth (int handle)
    {
       #if defined (_WIN32)
        ::shutdown ((SOCKET) handle, SD_BOTH);
       #else
        ::shutdown (handle, SHUT_RDWR);
       #endif
    }

    int receive (int handle, char* buffer, int size)
    {
        for (;;)
        {
           #if defined (_WIN32)
            const int n = ::recv ((SOCKET) handle, buffer, size, 0);
            if (n == SOCKET_ERROR)
            {
                const int error = WSAGetLastError();
                if (error == WSAEINTR)
                    continue;
                if (error == WSAEWOULDBLOCK)
                {
                    makeBlocking (handle);
                    return -2;
                }
                return -1;
            }
            return n;
           #else
            const auto n = ::recv (handle, buffer, static_cast<size_t> (size), 0);
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    makeBlocking (handle); // someone switched it to non-blocking: undo it
                    return -2;
                }
                return -1;
            }
            return static_cast<int> (n);
           #endif
        }
    }

    bool sendAll (int handle, const char* data, int size)
    {
        int done = 0;
        while (done < size)
        {
           #if defined (_WIN32)
            const int n = ::send ((SOCKET) handle, data + done, size - done, 0);
            if (n == SOCKET_ERROR)
            {
                const int error = WSAGetLastError();
                if (error == WSAEINTR)
                    continue;
                if (error == WSAEWOULDBLOCK)
                {
                    makeBlocking (handle);
                    continue;
                }
                return false;
            }
           #else
           #if defined (MSG_NOSIGNAL)
            const int flags = MSG_NOSIGNAL; // a closed link must not raise SIGPIPE (Linux)
           #else
            const int flags = 0;            // macOS: SO_NOSIGPIPE is set on the socket
           #endif
            const auto n = ::send (handle, data + done, static_cast<size_t> (size - done), flags);
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                {
                    makeBlocking (handle);
                    continue;
                }
                return false;
            }
           #endif
            if (n == 0)
                return false;
            done += static_cast<int> (n);
        }
        return true;
    }
}
