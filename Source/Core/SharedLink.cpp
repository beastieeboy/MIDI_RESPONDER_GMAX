#include "SharedLink.h"
#include <chrono>
#include <new>

#if defined(__APPLE__) || defined(__linux__)
 #include <fcntl.h>
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <unistd.h>
 #define SK_HAVE_SHM 1
#endif

namespace sk {

namespace {
    // Nazwa obiektu shm (na macOS maks. 31 znaków).
    constexpr const char* shmName = "/sekcja_krzemowa_v1";

    // Zapas: gdy shm nie jest dostępne, instancje w tym samym module dzielą blok statyczny.
    LinkBlock& localBlock()
    {
        static LinkBlock b;
        return b;
    }
}

SharedLink::SharedLink()
{
#if SK_HAVE_SHM
    const int fd = shm_open(shmName, O_CREAT | O_RDWR, 0600);
    if (fd >= 0)
    {
        struct stat st {};
        if (fstat(fd, &st) == 0 && st.st_size < (off_t) sizeof(LinkBlock))
            if (ftruncate(fd, (off_t) sizeof(LinkBlock)) != 0) {} // nowa pamięć jest wyzerowana

        void* p = mmap(nullptr, sizeof(LinkBlock), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (p != MAP_FAILED)
        {
            // Wyzerowana pamięć to poprawny stan atomików bez blokad na arm64/x86_64.
            block = static_cast<LinkBlock*>(p);
            uint32_t expected = 0;
            block->magic.compare_exchange_strong(expected, LinkBlock::magicValue);
            if (block->magic.load() == LinkBlock::magicValue)
                mapped = true;
            else
            {
                munmap(p, sizeof(LinkBlock));
                block = nullptr;
            }
        }
    }
#endif
    if (block == nullptr)
        block = &localBlock();
}

SharedLink::~SharedLink()
{
#if SK_HAVE_SHM
    // Obiektu nie usuwamy (shm_unlink): druga instancja może nadal z niego korzystać.
    if (mapped)
        munmap(block, sizeof(LinkBlock));
#endif
}

int64_t SharedLink::nowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static_assert(std::atomic<uint64_t>::is_always_lock_free, "atomiki 64-bit muszą być bez blokad");
static_assert(std::atomic<int64_t>::is_always_lock_free, "atomiki 64-bit muszą być bez blokad");

} // namespace sk
