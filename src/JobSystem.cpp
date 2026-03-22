#include "Core/JobSystem.h"
#include "Core/Debug.h"

#include <algorithm>

JobSystem::JobSystem()
{
    Initialize();
}

JobSystem::JobSystem(uint32_t workerCount)
{
    Initialize(workerCount);
}

JobSystem::~JobSystem()
{
    Shutdown();
}

void JobSystem::Initialize(uint32_t workerCount)
{
    Shutdown();

    if (workerCount == 0u)
        workerCount = ChooseDefaultWorkerCount();

    StartWorkers(workerCount);
}

void JobSystem::Shutdown()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
        ++m_workGeneration;
    }
    m_workCv.notify_all();

    for (std::thread& worker : m_workers)
    {
        if (worker.joinable())
            worker.join();
    }
    m_workers.clear();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = false;
        m_currentFn = nullptr;
        m_itemCount = 0u;
        m_chunkSize = 0u;
        m_nextChunk.store(0u, std::memory_order_relaxed);
        m_activeWorkers.store(0u, std::memory_order_relaxed);
    }

    m_workerCount = 0u;
}

void JobSystem::ParallelFor(size_t itemCount,
    const std::function<void(size_t begin, size_t end)>& fn,
    size_t minBatchSize)
{
    if (itemCount == 0u)
        return;

#ifdef _DEBUG
    minBatchSize = std::max(minBatchSize, size_t(512u));
#endif

    if (!IsParallel() || itemCount <= minBatchSize)
    {
        fn(0u, itemCount);
        return;
    }

    const uint32_t totalWorkers = m_workerCount + 1u;
    const size_t targetChunks = std::max<size_t>(totalWorkers * 2u, 1u);
    const size_t chunkSize = std::max(minBatchSize, (itemCount + targetChunks - 1u) / targetChunks);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_currentFn = &fn;
        m_itemCount = itemCount;
        m_chunkSize = chunkSize;
        m_nextChunk.store(0u, std::memory_order_release);
        m_activeWorkers.store(m_workerCount, std::memory_order_release);
        ++m_workGeneration;
    }
    m_workCv.notify_all();

    while (true)
    {
        const size_t begin = m_nextChunk.fetch_add(chunkSize, std::memory_order_relaxed);
        if (begin >= itemCount)
            break;

        const size_t end = std::min(begin + chunkSize, itemCount);
        fn(begin, end);
    }

    std::unique_lock<std::mutex> lock(m_mutex);
    m_doneCv.wait(lock, [this]() { return m_activeWorkers.load(std::memory_order_acquire) == 0u; });
    m_currentFn = nullptr;
    m_itemCount = 0u;
    m_chunkSize = 0u;
}

void JobSystem::StartWorkers(uint32_t workerCount)
{
    m_workerCount = workerCount;

    m_workers.reserve(workerCount);
    for (uint32_t i = 0; i < workerCount; ++i)
        m_workers.emplace_back([this]() { WorkerLoop(); });
}

void JobSystem::WorkerLoop()
{
    uint64_t seenGeneration = 0ull;

    for (;;)
    {
        const std::function<void(size_t, size_t)>* fn = nullptr;
        size_t itemCount = 0u;
        size_t chunkSize = 0u;

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_workCv.wait(lock, [this, &seenGeneration]() { return m_stop || m_workGeneration != seenGeneration; });
            if (m_stop)
                return;

            seenGeneration = m_workGeneration;
            fn = m_currentFn;
            itemCount = m_itemCount;
            chunkSize = m_chunkSize;
        }

        if (fn == nullptr || chunkSize == 0u)
        {
            if (m_activeWorkers.fetch_sub(1u, std::memory_order_acq_rel) == 1u)
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_doneCv.notify_one();
            }
            continue;
        }

        while (true)
        {
            const size_t begin = m_nextChunk.fetch_add(chunkSize, std::memory_order_relaxed);
            if (begin >= itemCount)
                break;

            const size_t end = std::min(begin + chunkSize, itemCount);
            (*fn)(begin, end);
        }

        if (m_activeWorkers.fetch_sub(1u, std::memory_order_acq_rel) == 1u)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_doneCv.notify_one();
        }
    }
}

uint32_t JobSystem::ChooseDefaultWorkerCount()
{
    const unsigned hc = std::thread::hardware_concurrency();
    if (hc <= 1u)
        return 0u;

    return static_cast<uint32_t>(hc - 1u);
}