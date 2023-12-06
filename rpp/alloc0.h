
#pragma once

#ifndef RPP_BASE
#error "Include base.h instead."
#endif

#ifdef COMPILER_MSVC
void* operator new(std::size_t, std::align_val_t, void* ptr) noexcept;
void* operator new[](std::size_t, std::align_val_t, void* ptr) noexcept;
void operator delete(void*, std::align_val_t, void*) noexcept;
void operator delete[](void*, std::align_val_t, void*) noexcept;
#endif

namespace rpp {

void* sys_alloc(u64 size);
void sys_free(void* mem);
i64 sys_net_allocs();

template<typename A>
concept Allocator = requires(u64 size, void* address) {
    Same<Literal, decltype(A::name)>;
    { A::alloc(size) } -> Same<void*>;
    { A::free(address) } -> Same<void>;
};

template<Literal N, bool Log = true>
struct Mallocator {
    static constexpr Literal name = N;
    static void* alloc(u64 size);
    static void free(void* mem);
};

struct Region_Allocator {
    template<u64 Brand>
    struct Scope {
        Scope() {
            begin(Brand);
        }
        ~Scope() {
            end(Brand);
        }
    };

    static void* alloc(u64 brand, u64 size);
    static void free(u64 brand, void* mem);

    static u64 depth();
    static u64 size();

private:
    static void begin(u64 brand);
    static void end(u64 brand);

    friend struct Stack_Scope;
};

template<u64 Brand>
struct Mregion {
    static constexpr Literal name = "Region";
    static void* alloc(u64 size) {
        return Region_Allocator::alloc(Brand, size);
    }
    static void free(void* mem) {
        Region_Allocator::free(Brand, mem);
    }
};

#define REGION_SCOPE2(counter) __region_scope_##counter
#define REGION_SCOPE1(R, brand, counter)                                                           \
    ::rpp::Region_Allocator::Scope<brand> REGION_SCOPE2(counter);                                  \
    static constexpr u64 R = brand;

#define Region_Scope(R) REGION_SCOPE1(R, LOCATION_HASH, __COUNTER__)

using Mdefault = Mallocator<"Default">;

using Mhidden = Mallocator<"Hidden", false>;

template<Allocator A, typename T, typename... Args>
    requires Constructable<T, Args...>
T* make(Args&&... args) {
    return new(A::alloc(sizeof(T))) T{std::forward<Args>(args)...};
}

template<Allocator A, typename T>
void destroy(T* value) {
    if constexpr(Must_Destruct<T>) {
        value->~T();
    }
    A::free(value);
}

template<typename T, Allocator Base>
struct Free_List {

    Free_List() = default;
    ~Free_List() {
        clear();
    }

    Free_List(const Free_List&) = delete;
    Free_List& operator=(const Free_List&) = delete;

    Free_List(Free_List&& src) : list_(src.list_) {
        src.list_ = null;
    }
    Free_List& operator=(Free_List&& src) {
        this->~Free_List();
        list_ = src.list_;
        src.list_ = null;
        return *this;
    }

    template<typename... Args>
        requires Constructable<T, Args...>
    T* make(Args&&... args) {
        return new(alloc()) T{std::forward<Args>(args)...};
    }

    void destroy(T* value) {
        if constexpr(Must_Destruct<T>) {
            value->~T();
        }
        free(value);
    }

    void clear() {
        while(list_) {
            Free_Node* next = list_->next;
            Base::free(list_);
            list_ = next;
        }
    }

private:
    T* alloc() {
        if(list_) {
            Free_Node* ret = list_;
            list_ = list_->next;
            Std::memset(ret, 0, sizeof(Free_Node));
            return reinterpret_cast<T*>(ret);
        }
        void* new_node = Base::alloc(sizeof(Free_Node));
        return reinterpret_cast<T*>(new_node);
    }

    void free(T* mem) {
        Free_Node* node = reinterpret_cast<Free_Node*>(mem);
        node->next = list_;
        list_ = node;
    }

    union Free_Node {
        T value;
        Free_Node* next = null;
    };

    Free_Node* list_ = null;
};

} // namespace rpp
