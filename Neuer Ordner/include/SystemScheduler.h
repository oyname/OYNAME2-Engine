#pragma once

#include "JobSystem.h"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

class SystemScheduler
{
public:
    using SystemFn = std::function<void()>;

    struct TaskDesc
    {
        std::string name;
        uint64_t readMask = 0ull;
        uint64_t writeMask = 0ull;
        SystemFn fn;
    };

    void Clear()
    {
        m_tasks.clear();
    }

    void AddTask(TaskDesc task)
    {
        m_tasks.push_back(std::move(task));
    }

    void Execute(JobSystem* jobSystem = nullptr)
    {
        std::vector<bool> completed(m_tasks.size(), false);
        size_t doneCount = 0u;

        while (doneCount < m_tasks.size())
        {
            std::vector<size_t> ready;
            uint64_t batchReadMask = 0ull;
            uint64_t batchWriteMask = 0ull;

            for (size_t i = 0; i < m_tasks.size(); ++i)
            {
                if (completed[i])
                    continue;

                bool depsResolved = true;
                for (size_t j = 0; j < m_tasks.size(); ++j)
                {
                    if (i == j || completed[j])
                        continue;

                    const bool jWritesWhatIRead = (m_tasks[j].writeMask & m_tasks[i].readMask) != 0ull;
                    const bool jWritesWhatIWrite = (m_tasks[j].writeMask & m_tasks[i].writeMask) != 0ull;
                    const bool jReadsWhatIWrite = (m_tasks[j].readMask & m_tasks[i].writeMask) != 0ull;
                    if (jWritesWhatIRead || jWritesWhatIWrite || jReadsWhatIWrite)
                    {
                        if (j < i)
                        {
                            depsResolved = false;
                            break;
                        }
                    }
                }

                if (!depsResolved)
                    continue;

                const bool readConflict = (batchWriteMask & m_tasks[i].readMask) != 0ull;
                const bool writeConflict = ((batchReadMask | batchWriteMask) & m_tasks[i].writeMask) != 0ull;
                if (readConflict || writeConflict)
                    continue;

                ready.push_back(i);
                batchReadMask |= m_tasks[i].readMask;
                batchWriteMask |= m_tasks[i].writeMask;
            }

            if (ready.empty())
            {
                for (size_t i = 0; i < m_tasks.size(); ++i)
                {
                    if (!completed[i])
                    {
                        m_tasks[i].fn();
                        completed[i] = true;
                        ++doneCount;
                    }
                }
                break;
            }

            auto runReadyRange = [&](size_t begin, size_t end)
            {
                for (size_t i = begin; i < end; ++i)
                    m_tasks[ready[i]].fn();
            };

            if (jobSystem && ready.size() > 1u)
                jobSystem->ParallelFor(ready.size(), runReadyRange, 1u);
            else
                runReadyRange(0u, ready.size());

            for (size_t idx : ready)
            {
                completed[idx] = true;
                ++doneCount;
            }
        }
    }

private:
    std::vector<TaskDesc> m_tasks;
};
