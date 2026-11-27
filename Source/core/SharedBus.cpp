#include "SharedBus.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <new>
#include <thread>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <cerrno>
 #include <fcntl.h>
 #include <signal.h>
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace cuemix
{
namespace
{
std::mutex& registryMutex()
{
    static std::mutex m;
    return m;
}

std::map<std::string, std::weak_ptr<SharedBus>>& registry()
{
    static std::map<std::string, std::weak_ptr<SharedBus>> r;
    return r;
}

void setError(std::string* error, const std::string& text)
{
    if (error != nullptr)
        *error = text;
}

// Waits for the creator to finish initialising the header.
bool waitUntilReady(BusShared* bus)
{
    for (int i = 0; i < 200; ++i)
    {
        if (bus->ready.load(std::memory_order_acquire) == 1)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

void initialise(BusShared* bus)
{
    // The memory is zero-filled by the OS, which is a valid state for every field; set the header
    // last so other processes only use it once it is complete.
    bus->magic.store(kBusMagic, std::memory_order_relaxed);
    bus->layoutVersion.store(kBusLayoutVersion, std::memory_order_relaxed);
    bus->ready.store(1, std::memory_order_release);
}
} // namespace

std::string SharedBus::defaultName()
{
    if (const char* env = std::getenv("CUEMIX_BUS"); env != nullptr && *env != 0)
        return env;
    return "/cuemix.v1";
}

int64_t SharedBus::nowNs() noexcept
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

#if defined(_WIN32)

int32_t SharedBus::currentPid() noexcept { return static_cast<int32_t>(GetCurrentProcessId()); }

bool SharedBus::processAlive(int32_t pid) noexcept
{
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
    if (h == nullptr)
        return GetLastError() == ERROR_ACCESS_DENIED;
    const bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
}

static std::wstring windowsName(const std::string& name)
{
    std::string n = name;
    while (!n.empty() && n.front() == '/')
        n.erase(n.begin());
    std::wstring w = L"Local\\";
    for (char c : n)
        w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    return w;
}

std::shared_ptr<SharedBus> SharedBus::open(const std::string& name, std::string* error)
{
    std::lock_guard<std::mutex> lock(registryMutex());
    if (auto existing = registry()[name].lock())
        return existing;

    const auto size = static_cast<unsigned long long>(sizeof(BusShared));
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                        static_cast<DWORD>(size >> 32), static_cast<DWORD>(size & 0xffffffffu),
                                        windowsName(name).c_str());
    if (mapping == nullptr)
    {
        setError(error, "CreateFileMapping failed");
        return nullptr;
    }
    const bool created = GetLastError() != ERROR_ALREADY_EXISTS;
    void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(BusShared));
    // The view keeps the mapping alive; the handle is not needed any more.
    CloseHandle(mapping);
    if (view == nullptr)
    {
        setError(error, "MapViewOfFile failed");
        return nullptr;
    }

    auto* bus = static_cast<BusShared*>(view);
    if (created)
        initialise(bus);
    else if (!waitUntilReady(bus))
    {
        UnmapViewOfFile(view);
        setError(error, "the shared bus never became ready");
        return nullptr;
    }

    if (bus->magic.load() != kBusMagic || bus->layoutVersion.load() != kBusLayoutVersion)
    {
        UnmapViewOfFile(view);
        setError(error, "another CueMix version is running; restart your DAW");
        return nullptr;
    }

    std::shared_ptr<SharedBus> result(new SharedBus());
    result->bus = bus;
    result->busName = name;
    registry()[name] = result;
    return result;
}

SharedBus::~SharedBus()
{
    if (bus != nullptr)
        UnmapViewOfFile(bus);
}

void SharedBus::unlink(const std::string&) {}

#else // POSIX (macOS, Linux)

int32_t SharedBus::currentPid() noexcept { return static_cast<int32_t>(::getpid()); }

bool SharedBus::processAlive(int32_t pid) noexcept
{
    if (pid <= 0)
        return false;
    if (::kill(static_cast<pid_t>(pid), 0) == 0)
        return true;
    return errno == EPERM; // exists, but owned by someone else
}

std::shared_ptr<SharedBus> SharedBus::open(const std::string& name, std::string* error)
{
    std::lock_guard<std::mutex> lock(registryMutex());
    if (auto existing = registry()[name].lock())
        return existing;

    const auto size = static_cast<off_t>(sizeof(BusShared));
    bool created = false;
    int fd = ::shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0666);
    if (fd >= 0)
    {
        created = true;
        // shm_open's mode is masked by the umask: make sure every user's DAW can open it.
        ::fchmod(fd, 0666);
        if (::ftruncate(fd, size) != 0)
        {
            ::close(fd);
            ::shm_unlink(name.c_str());
            setError(error, std::string("ftruncate failed: ") + std::strerror(errno));
            return nullptr;
        }
    }
    else if (errno == EEXIST)
    {
        fd = ::shm_open(name.c_str(), O_RDWR, 0666);
        if (fd < 0)
        {
            setError(error, std::string("shm_open failed: ") + std::strerror(errno));
            return nullptr;
        }
        // The creator may still be sizing it.
        struct stat st {};
        for (int i = 0; i < 200; ++i)
        {
            if (::fstat(fd, &st) == 0 && st.st_size >= size)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (st.st_size < size)
        {
            ::close(fd);
            setError(error, "the shared bus has the wrong size; restart your DAW");
            return nullptr;
        }
    }
    else
    {
        setError(error, std::string("shm_open failed: ") + std::strerror(errno));
        return nullptr;
    }

    void* mem = ::mmap(nullptr, static_cast<size_t>(size), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ::close(fd);
    if (mem == MAP_FAILED)
    {
        setError(error, std::string("mmap failed: ") + std::strerror(errno));
        return nullptr;
    }

    auto* bus = static_cast<BusShared*>(mem);
    if (created)
        initialise(bus);
    else if (!waitUntilReady(bus))
    {
        ::munmap(mem, static_cast<size_t>(size));
        setError(error, "the shared bus never became ready; restart your DAW");
        return nullptr;
    }

    if (bus->magic.load() != kBusMagic || bus->layoutVersion.load() != kBusLayoutVersion)
    {
        ::munmap(mem, static_cast<size_t>(size));
        setError(error, "another CueMix version is running; restart your DAW");
        return nullptr;
    }

    std::shared_ptr<SharedBus> result(new SharedBus());
    result->bus = bus;
    result->busName = name;
    registry()[name] = result;
    return result;
}

SharedBus::~SharedBus()
{
    if (bus != nullptr)
        ::munmap(bus, sizeof(BusShared));
}

void SharedBus::unlink(const std::string& name) { ::shm_unlink(name.c_str()); }

#endif

void writeSlotName(SlotShared& slot, const std::string& name) noexcept
{
    uint64_t words[kNameWords] = {};
    std::memcpy(words, name.data(), std::min(name.size(), sizeof(words) - 1));

    const uint32_t seq = slot.nameSeq.load(std::memory_order_relaxed);
    slot.nameSeq.store(seq + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    for (int i = 0; i < kNameWords; ++i)
        slot.name[i].store(words[i], std::memory_order_relaxed);
    slot.nameSeq.store(seq + 2, std::memory_order_release);
}

std::string readSlotName(const SlotShared& slot)
{
    uint64_t words[kNameWords + 1] = {};
    for (int attempt = 0; attempt < 64; ++attempt)
    {
        const uint32_t before = slot.nameSeq.load(std::memory_order_acquire);
        if ((before & 1u) != 0)
            continue;
        for (int i = 0; i < kNameWords; ++i)
            words[i] = slot.name[i].load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (slot.nameSeq.load(std::memory_order_relaxed) == before)
            break;
    }
    words[kNameWords - 1] &= ~(uint64_t(0xff) << 56); // keep it NUL-terminated whatever happened
    return std::string(reinterpret_cast<const char*>(words));
}

SlotInfo snapshotSlot(const SharedBus& bus, int index)
{
    auto& s = const_cast<SharedBus&>(bus).slot(index);
    SlotInfo info;
    info.index = index;
    info.claimed = s.state.load(std::memory_order_acquire) == 1;
    if (!info.claimed)
        return info;
    info.generation = s.generation.load(std::memory_order_acquire);
    info.channelId = s.channelId.load(std::memory_order_relaxed);
    info.sampleRate = s.sampleRate.load(std::memory_order_relaxed);
    info.colour = s.colour.load(std::memory_order_relaxed);
    info.name = readSlotName(s);
    const int64_t last = s.lastWriteNs.load(std::memory_order_relaxed);
    info.active = last != 0 && SharedBus::nowNs() - last < 1'000'000'000;
    return info;
}
} // namespace cuemix
