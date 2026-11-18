#pragma once

// Reading and writing a connected TCP socket directly. JUCE's StreamingSocket switches the whole
// socket to non-blocking mode on every non-blocking read, so a write from another thread could
// fail with "would block" as soon as the send buffer filled (macOS buffers are small): Tutti
// mistook that for a dead link and hung up mid-transfer. These keep the socket blocking.
namespace tutti::io
{
    // Puts the socket back in blocking mode.
    void makeBlocking (int handle);

    // Waits up to timeoutMs for data (or the other side closing): 1 ready, 0 not yet, -1 error.
    int waitReadable (int handle, int timeoutMs);

    // Reads what has arrived: > 0 bytes, 0 when the other side closed, -1 on an error, -2 when
    // nothing was there after all (try again).
    int receive (int handle, char* buffer, int size);

    // Stops both directions without releasing the socket, so threads still using it wake up
    // (their calls fail) and the handle can't be reused by another socket until it's closed.
    void shutdownBoth (int handle);

    // Sends all of it (waiting while the send buffer is full). False if the link is gone.
    bool sendAll (int handle, const char* data, int size);
}
