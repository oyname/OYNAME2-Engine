#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>

class JobSystem
{
public:
    JobSystem();
    explicit JobSystem(uint32_t workerCount);
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    void Initialize(uint32_t workerCount = 0u);
    void Shutdown();

    uint32_t GetWorkerCount() const noexcept { return m_workerCount; }
    bool IsParallel() const noexcept { return m_workerCount > 0u; }

    void ParallelFor(size_t itemCount,
                     const std::function<void(size_t begin, size_t end)>& fn,
                     size_t minBatchSize = 64u);

private:
    void StartWorkers(uint32_t workerCount);
    void WorkerLoop();
    static uint32_t ChooseDefaultWorkerCount();

    uint32_t m_workerCount = 0u;
    std::vector<std::thread> m_workers;

    mutable std::mutex m_mutex;
    std::condition_variable m_workCv;
    std::condition_variable m_doneCv;

    const std::function<void(size_t, size_t)>* m_currentFn = nullptr;
    size_t m_itemCount = 0u;
    size_t m_chunkSize = 0u;
    std::atomic<size_t> m_nextChunk{ 0u };
    std::atomic<uint32_t> m_activeWorkers{ 0u };
    uint64_t m_workGeneration = 0ull;
    bool m_stop = false;
};
