
#pragma once

#include "async.h"
#include "base.h"
#include "thread.h"

namespace rpp::Async {

template<Allocator A>
struct Pool;

template<Allocator A = Alloc>
struct Schedule {

    explicit Schedule(Pool<A>& pool) : pool{pool} {
    }
    void await_suspend(std::coroutine_handle<> task) {
        pool.enqueue(Handle{task});
    }
    void await_resume() {
    }
    bool await_ready() {
        return false;
    }

private:
    Pool<A>& pool;
};

template<Allocator A = Alloc>
struct Schedule_Event {

    explicit Schedule_Event(Event event, Pool<A>& pool) : event{move(event)}, pool{pool} {
    }
    void await_suspend(std::coroutine_handle<> task) {
        pool.enqueue_event(move(event), Handle{task});
    }
    void await_resume() {
    }
    bool await_ready() {
        return event.try_wait();
    }

private:
    Event event;
    Pool<A>& pool;
};

template<Allocator A = Alloc>
struct Pool {

    explicit Pool() : thread_states{Vec<Thread_State, A>::make(Thread::hardware_threads() - 1)} {

        u64 h_threads = Thread::hardware_threads();
        u64 n_threads = thread_states.length();
        assert(n_threads <= h_threads && n_threads <= 64);

        for(u64 i = 0; i < n_threads; i++) {
            threads.push(Thread::Thread([this, i, h_threads] {
                u64 j = i < h_threads / 2 ? i * 2 : (i - h_threads / 2) * 2 + 1;
                Thread::set_affinity(j);
                do_work(i);
            }));
        }
        pending_events.push(Event{});
        event_thread = Thread::Thread([this] { do_events(); });
    }
    ~Pool() {
        shutdown.exchange(true);

        for(auto& state : thread_states) {
            Thread::Lock lock(state.mut);
            state.cond.signal();
        }
        threads.clear();

        {
            Thread::Lock lock(events_mut);
            pending_events[0].signal();
        }
        event_thread.join();

        pending_events.clear();

        for(auto& state : thread_states) {
            for(auto& job : state.jobs) {
                // This still leaks pending continuations, as we can't control their destruction
                // order wrt their waiting tasks.
                job.handle.destroy();
            }
        }
    }

    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    Pool(Pool&&) = delete;
    Pool& operator=(Pool&&) = delete;

    Schedule<A> suspend() {
        return Schedule<A>{*this};
    }
    Schedule_Event<A> event(Event event) {
        return Schedule_Event<A>{move(event), *this};
    }

    u64 n_threads() const {
        return thread_states.length();
    }

private:
    void enqueue(Handle<> job) {
        for(u64 i = 0; i < thread_states.length(); i++) {
            Thread_State& state = thread_states[i];
            // Race on empty
            if(state.jobs.empty()) {
                Thread::Lock lock(state.mut);
                state.jobs.push(rpp::move(job));
                state.cond.signal();
                return;
            }
        }

        // All queues more or less busy, choose next from low discrepancy sequence
        u64 i = static_cast<u64>(sequence.incr() * Math::PHI32) % thread_states.length();
        Thread_State& state = thread_states[i];

        Thread::Lock lock(state.mut);
        state.jobs.push(move(job));
        state.cond.signal();
    }

    void enqueue_event(Event event, Handle<> job) {
        Thread::Lock lock(events_mut);
        events_to_enqueue.emplace(move(event), move(job));
        pending_events[0].signal();
    }

    void do_work(u64 thread_idx) {
        Thread_State& state = thread_states[thread_idx];
        for(;;) {
            Handle<> job;
            {
                Thread::Lock lock(state.mut);

                while(state.jobs.empty() && !shutdown.load()) {
                    state.cond.wait(state.mut);
                }
                if(shutdown.load()) return;

                job = move(state.jobs.front());
                state.jobs.pop();
            }
            job.handle.resume();
        }
    }

    void do_events() {
        for(;;) {
            u64 idx = Event::wait_any(Slice<Event>{pending_events});
            Thread::Lock lock(events_mut);
            if(idx == 0) {
                if(shutdown.load()) return;

                for(auto& [event, state] : events_to_enqueue) {
                    pending_events.push(move(event));
                    pending_event_jobs.push(move(state));
                }
                events_to_enqueue.clear();

                pending_events[0].reset();
            } else {
                auto job = move(pending_event_jobs[idx - 1]);

                if(pending_events.length() > 2) {
                    swap(pending_events[idx], pending_events.back());
                    swap(pending_event_jobs[idx - 1], pending_event_jobs.back());
                }
                pending_events.pop();
                pending_event_jobs.pop();

                enqueue(job);
            }
        }
    }

    Thread::Atomic shutdown, sequence;

    struct Thread_State {
        Thread::Mutex mut;
        Thread::Cond cond;
        Queue<Handle<>, A> jobs;
    };
    Vec<Thread_State, A> thread_states;
    Vec<Thread::Thread<A>, A> threads;

    Vec<Event, A> pending_events;
    Vec<Handle<>, A> pending_event_jobs;
    Vec<Pair<Event, Handle<>>, A> events_to_enqueue;

    Thread::Thread<A> event_thread;
    Thread::Mutex events_mut;

    template<Allocator>
    friend struct Schedule;
    template<Allocator>
    friend struct Schedule_Event;
};

} // namespace rpp::Async
