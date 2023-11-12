
#pragma once

#include "base.h"
#include "rc.h"

namespace rpp {
namespace Thread {

#ifdef OS_WINDOWS
using OS_Thread = void*;
using OS_Thread_Ret = u32;
constexpr OS_Thread OS_Thread_Null = null;
constexpr OS_Thread_Ret OS_Thread_Ret_Null = 0;
#else
using OS_Thread = pthread_t;
using OS_Thread_Ret = void*;
constexpr OS_Thread OS_Thread_Null = 0;
constexpr OS_Thread_Ret OS_Thread_Ret_Null = null;
#endif

Id sys_id(OS_Thread thread);
void sys_join(OS_Thread thread);
void sys_detach(OS_Thread thread);
OS_Thread sys_start(OS_Thread_Ret (*f)(void*), void* data);

template<typename T>
struct Promise {

    Promise() = default;
    ~Promise() = default;

    Promise(const Promise&) = delete;
    Promise(Promise&&) = delete;
    Promise& operator=(const Promise&) = delete;
    Promise& operator=(Promise&&) = delete;

    void fill(T&& val) {
        value = std::move(val);
        flag.signal();
    }

    T block() {
        flag.block();
        return std::move(value);
    }

    bool ready() {
        return flag.ready();
    }

private:
    Flag flag;
    T value;

    friend struct Reflect<Promise<T>>;
};

template<>
struct Promise<void> {

    Promise() = default;
    ~Promise() = default;

    Promise(const Promise&) = delete;
    Promise(Promise&&) = delete;
    Promise& operator=(const Promise&) = delete;
    Promise& operator=(Promise&&) = delete;

    void fill() {
        flag.signal();
    }

    void block() {
        flag.block();
    }

    bool ready() {
        return flag.ready();
    }

private:
    Flag flag;

    friend struct Reflect<Promise<void>>;
};

template<typename T, Allocator A = Alloc>
using Future = Arc<Promise<T>, A>;

template<Allocator A = Alloc>
struct Thread {

    Thread() = default;

    template<Invocable F>
    explicit Thread(F&& f) {
        F* data = reinterpret_cast<F*>(A::alloc(sizeof(F)));
        new(data) F{std::forward<F>(f)};
        thread = sys_start(&invoke<F>, data);
    }
    ~Thread() {
        if(thread) join();
    }

    Thread(const Thread&) = delete;
    Thread(Thread&& src) {
        thread = src.thread;
        src.thread = OS_Thread_Null;
    }

    Thread& operator=(const Thread&) = delete;
    Thread& operator=(Thread&& src) {
        this->~Thread();
        thread = src.thread;
        src.thread = OS_Thread_Null;
        return *this;
    }

    Id id() {
        assert(thread);
        return sys_id(thread);
    }
    void join() {
        assert(thread);
        sys_join(thread);
        thread = OS_Thread_Null;
    }
    void detach() {
        assert(thread);
        sys_detach(thread);
        thread = OS_Thread_Null;
    }

private:
    OS_Thread thread = OS_Thread_Null;

    template<Invocable F>
    static OS_Thread_Ret invoke(void* _f) {
        F* f = static_cast<F*>(_f);
        Mregion::create();
        Profile::start_thread();
        (*f)();
        f->~F();
        A::free(f);
        Profile::end_thread();
        Mregion::destroy();
        return OS_Thread_Ret_Null;
    }

    friend struct Reflect<Thread<A>>;
};

template<Allocator A = Alloc, typename F, typename... Args>
    requires Invocable<F, Args...>
auto spawn(F&& f, Args&&... args) -> Future<Invoke_Result<F, Args...>, A> {

    using Result = Invoke_Result<F, Args...>;
    auto future = Future<Result, A>::make();

    Thread thread{[future = future.dup(), f = std::forward<F>(f),
                   ... args = std::forward<Args>(args)]() mutable {
        if constexpr(Same<Result, void>) {
            f(std::forward<Args>(args)...);
            future->fill();
        } else {
            future->fill(f(std::forward<Args>(args)...));
        }
    }};
    thread.detach();

    return future;
}

} // namespace Thread

template<typename P>
struct rpp::detail::Reflect<Thread::Promise<P>> {
    using T = Thread::Promise<P>;
    static constexpr Literal name = "Promise";
    static constexpr Kind kind = Kind::record_;
    using members = List<FIELD(flag), FIELD(value)>;
};

template<>
struct rpp::detail::Reflect<Thread::Promise<void>> {
    using T = Thread::Promise<void>;
    static constexpr Literal name = "Promise";
    static constexpr Kind kind = Kind::record_;
    using members = List<FIELD(flag)>;
};

template<Allocator A>
struct rpp::detail::Reflect<Thread::Thread<A>> {
    using T = Thread::Thread<A>;
    static constexpr Literal name = "Thread";
    static constexpr Kind kind = Kind::record_;
    using members = List<FIELD(thread)>;
};

} // namespace rpp
