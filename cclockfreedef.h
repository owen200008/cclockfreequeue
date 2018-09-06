#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <atomic>
#include <assert.h>

#ifdef _MSC_VER
#include <windows.h>
#define CCSwitchToThread() SwitchToThread();
#else
#include <emmintrin.h>
#include <thread>
#define CCSwitchToThread() std::this_thread::yield();
#endif

#ifdef __GNUC__
#if defined(__APPLE__)
#include <libkern/OsAtomic.h>
#define CCLockfreeInterlockedIncrement(value) (OSAtomicAdd32(1, (volatile int32_t *)value) - 1)
#define CCLockfreeInterlockedDecrementNoCheckReturn(value) OSAtomicAdd32(-1, (volatile int32_t *)value)
#define CCLockfreeInterlockedDecrement(value)  (OSAtomicAdd32(-1, (volatile int32_t *)value) + 1)
#define CCLockfreeInterlockedCompareExchange(value, comp, exchange) OSAtomicCompareAndSwap32(comp, exchange, (volatile int32_t *)value)
#else
#define CCLockfreeInterlockedIncrement(value) __sync_fetch_and_add(value, 1)
#define CCLockfreeInterlockedDecrementNoCheckReturn(value) __sync_fetch_and_sub(value, 1)
#define CCLockfreeInterlockedDecrement(value) CCLockfreeInterlockedDecrementNoCheckReturn(value)
#define CCLockfreeInterlockedCompareExchange(value, comp, exchange) __sync_bool_compare_and_swap(value, comp, exchange)
#endif

#define CCLockfreequeueLikely(x) __builtin_expect((x), true)
#define CCLockfreequeueUnLikely(x) __builtin_expect((x), false)
#elif defined(_MSC_VER)
#define CCLockfreeInterlockedIncrement(value) (::InterlockedIncrement(value) - 1)
#define CCLockfreeInterlockedDecrementNoCheckReturn(value) ::InterlockedDecrement(value)
#define CCLockfreeInterlockedDecrement(value) (::InterlockedDecrement(value) + 1)
#define CCLockfreeInterlockedCompareExchange(value, comp, exchange) (::InterlockedCompareExchange(value, exchange, comp) == comp)

#define CCLockfreequeueLikely(x) x
#define CCLockfreequeueUnLikely(x) x
#endif

// Compiler-specific likely/unlikely hints
namespace cclockfree {
    class atomic_backoff {
        //! Time delay, in units of "pause" instructions.
        /** Should be equal to approximately the number of "pause" instructions
        that take the same time as an context switch. Must be a power of two.*/
        static const int32_t LOOPS_BEFORE_YIELD = 16;
        int32_t count;
    public:
        // In many cases, an object of this type is initialized eagerly on hot path,
        // as in for(atomic_backoff b; ; b.pause()) { /*loop body*/ }
        // For this reason, the construction cost must be very small!
        atomic_backoff() : count(1) {}
        // This constructor pauses immediately; do not use on hot paths!
        atomic_backoff(bool) : count(1) { pause(); }

        static inline void pause(uintptr_t delay) {
            for (; delay>0; --delay)
                _mm_pause();
        }
        //! Pause for a while.
        void pause() {
            if (count <= LOOPS_BEFORE_YIELD) {
                pause(count);
                // Pause twice as long the next time.
                count *= 2;
            }
            else {
                // Pause is so long that we might as well yield CPU to scheduler.
                CCSwitchToThread();
            }
        }

        void SwapThread() {
            CCSwitchToThread();
        }

        //! Pause for a few times and return false if saturated.
        bool bounded_pause() {
            pause(count);
            if (count<LOOPS_BEFORE_YIELD) {
                // Pause twice as long the next time.
                count *= 2;
                return true;
            }
            else {
                return false;
            }
        }

        void reset() {
            count = 1;
        }
    };
    struct CCLockfreeFunc {
#if defined(malloc) || defined(free)
        static inline void* malloc(size_t size) { return ::malloc(size); }
        static inline void free(void* ptr) { return ::free(ptr); }
#else
        static inline void* malloc(size_t size) { return std::malloc(size); }
        static inline void free(void* ptr) { return std::free(ptr); }
#endif
        template<class... _Types>
        static inline void Trace(const char* pData, _Types&&... _Args) {
#ifdef _DEBUG
            printf(pData, std::forward<_Types>(_Args)...);
#endif
        }
    };

    template<class Traits = CCLockfreeFunc>
    class CCLockfreeObject {
    public:
        CCLockfreeObject() {
        }
        virtual ~CCLockfreeObject() {
        }

        // Diagnostic allocations
        void* operator new(size_t nSize) {
            return Traits::malloc(nSize);
        }
        void operator delete(void* p) {
            Traits::free(p);
        }
    };
}

