#pragma once
#include "VocalSample.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace sk {

// Niezmienna migawka zawartości pieca, czytana przez wątek audio.
struct PoolSnapshot
{
    static constexpr int maxSamples = 32;
    uint64_t generation = 0;
    int count = 0;
    const VocalSample* items[maxSamples] {};               // od najstarszego do najnowszego
    std::shared_ptr<VocalSample> owners[maxSamples];        // trzymają dane przy życiu
};

// "Piec": pula sampli wokalu. Zmiany (dodaj / usuń) tylko z wątku wiadomości;
// wątek audio bierze migawkę bez blokad i zgłasza najstarszą generację, której
// jeszcze używa (grające ziarna). Stare migawki – i sample – zwalnia wątek wiadomości.
class SamplePool
{
public:
    SamplePool();
    ~SamplePool();
    SamplePool(const SamplePool&) = delete;
    SamplePool& operator=(const SamplePool&) = delete;

    // --- wątek wiadomości ---
    // Dodaje sampel. Gdy piec jest pełny, najstarszy sampel "spala się" (wypada).
    void add(std::shared_ptr<VocalSample> s);
    void remove(int index);
    void clear();
    int size() const { return int(samples.size()); }
    const std::shared_ptr<VocalSample>& at(int i) const { return samples[size_t(i)]; }
    void collectGarbage();   // wywoływać regularnie (timer)
    int pendingGarbage() const { return int(retired.size()); }

    // --- wątek audio ---
    const PoolSnapshot* acquire() const { return current.load(std::memory_order_acquire); }
    void reportInUse(uint64_t oldestGeneration) { inUse.store(oldestGeneration, std::memory_order_release); }

private:
    void publish();

    std::vector<std::shared_ptr<VocalSample>> samples;
    std::atomic<const PoolSnapshot*> current { nullptr };
    std::atomic<uint64_t> inUse { 0 };
    std::vector<std::unique_ptr<PoolSnapshot>> retired;
    std::unique_ptr<PoolSnapshot> live;
    uint64_t nextGeneration = 1;
};

} // namespace sk
