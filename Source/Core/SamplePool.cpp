#include "SamplePool.h"
#include <algorithm>

namespace sk {

SamplePool::SamplePool()
{
    publish();
}

SamplePool::~SamplePool()
{
    current.store(nullptr);
}

void SamplePool::add(std::shared_ptr<VocalSample> s)
{
    if (! s)
        return;
    if ((int) samples.size() >= PoolSnapshot::maxSamples)
        samples.erase(samples.begin());
    samples.push_back(std::move(s));
    publish();
}

void SamplePool::remove(int index)
{
    if (index < 0 || index >= (int) samples.size())
        return;
    samples.erase(samples.begin() + index);
    publish();
}

void SamplePool::clear()
{
    samples.clear();
    publish();
}

void SamplePool::publish()
{
    auto snap = std::make_unique<PoolSnapshot>();
    snap->generation = nextGeneration++;
    snap->count = std::min<int>(int(samples.size()), PoolSnapshot::maxSamples);
    for (int i = 0; i < snap->count; ++i)
    {
        snap->owners[i] = samples[size_t(i)];
        snap->items[i] = samples[size_t(i)].get();
    }
    current.store(snap.get(), std::memory_order_release);
    if (live)
        retired.push_back(std::move(live));
    live = std::move(snap);
    collectGarbage();
}

void SamplePool::collectGarbage()
{
    // Wątek audio zgłasza najstarszą generację w użyciu; zawsze <= tej, którą właśnie czyta,
    // a generacje rosną, więc migawki starsze od zgłoszonej nie są już nigdzie potrzebne.
    const uint64_t oldest = inUse.load(std::memory_order_acquire);
    retired.erase(std::remove_if(retired.begin(), retired.end(),
                                 [oldest](const std::unique_ptr<PoolSnapshot>& s) { return s->generation < oldest; }),
                  retired.end());
}

} // namespace sk
