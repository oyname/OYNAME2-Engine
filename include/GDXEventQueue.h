#pragma once

#include <vector>
#include <mutex>
#include "Events.h"

// ---------------------------------------------------------------------------
// GDXEventQueue
//
// Threading model: Push() is safe to call from any thread (e.g. the window
// procedure running on the OS message thread).  SnapshotAndClear() must only
// be called from the engine thread.
//
// The key invariant: snapshot and clear happen inside a *single* lock scope
// so that a Push() arriving from WndProc between the two steps cannot be
// silently discarded.  The returned vector is iterated outside the lock,
// which keeps the critical section short and never blocks the OS thread.
// ---------------------------------------------------------------------------
class GDXEventQueue
{
public:
    // Thread-safe: may be called from any thread (e.g. WndProc).
    void Push(const Event& e)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_events.push_back(e);
    }

    // Engine-thread only.  Atomically moves all pending events out of the
    // queue and returns them as a snapshot.  Because the swap and the clear
    // happen under a single lock acquisition a Push() cannot slip in between
    // and lose an event (the previous Snapshot() + Clear() two-step had that
    // race).
    std::vector<Event> SnapshotAndClear()
    {
        std::vector<Event> out;
        std::lock_guard<std::mutex> lock(m_mutex);
        out.swap(m_events);   // O(1) — no copy, leaves m_events empty
        return out;
    }

private:
    mutable std::mutex  m_mutex;
    std::vector<Event>  m_events;
};
