#pragma once

#include"cclockfreedef.h"

// Compiler-specific likely/unlikely hints
namespace cclockfree {
    //最大写入值 在 defaultfixsize - defaultfixsize * 2 之间
    template<class T, uint32_t defaultfixsize = 32, class ObjectBaseClass = CCLockfreeObject<CCLockfreeFunc>>
    class CCLockfreeFixQueue : public ObjectBaseClass {
    public:
        struct StoreLockfreeFixQueue {
            T                               m_data;
            std::atomic<uint8_t>            m_cWrite;
        };
    public:
        CCLockfreeFixQueue(){
            static_assert((defaultfixsize & (defaultfixsize - 1)) == 0,
                "defaultfixsize is not power(2) error!");
            m_nSpace = defaultfixsize;
            m_nCanRead = 0;
            m_nPreWrite = 0;
            m_nRead = 0;
            memset(m_pData, 0, sizeof(StoreLockfreeFixQueue) * defaultfixsize);
        }
        virtual ~CCLockfreeFixQueue() {
        }
        inline bool Push(const T& value) {
            int32_t nSpace = (int32_t)CCLockfreeInterlockedDecrement(&m_nSpace);
            if (CCLockfreequeueLikely(nSpace > 0)) {
                atomic_backoff bPause;
                StoreLockfreeFixQueue& node = m_pData[CCLockfreeInterlockedIncrement(&m_nPreWrite) % defaultfixsize];
                while (node.m_cWrite.load(std::memory_order_relaxed)) {
                    bPause.pause();
                }
                node.m_data = value;
                node.m_cWrite.store(true, std::memory_order_release);
                CCLockfreeInterlockedIncrement(&m_nCanRead);
                return true;
            }
            CCLockfreeInterlockedIncrement(&m_nSpace);
            return false;
        }
        inline bool Pop(T& value) {
            int32_t nCanRead = (int32_t)m_nCanRead;
            if (nCanRead <= 0)
                return false;
            nCanRead = CCLockfreeInterlockedDecrement(&m_nCanRead);
            if (CCLockfreequeueLikely(nCanRead > 0)) {
                atomic_backoff bPause;
                StoreLockfreeFixQueue& node = m_pData[CCLockfreeInterlockedIncrement(&m_nRead) % defaultfixsize];
                while (!node.m_cWrite.load(std::memory_order_acquire)) {
                    bPause.pause();
                }
                value = node.m_data;
                node.m_cWrite.store(false, std::memory_order_release);
                CCLockfreeInterlockedIncrement(&m_nSpace);
                return true;
            }
            CCLockfreeInterlockedIncrement(&m_nCanRead);
            return false;
        }
    protected:
        volatile uint32_t           m_nSpace;
        volatile uint32_t           m_nCanRead;
        volatile uint32_t           m_nPreWrite;
        volatile uint32_t           m_nRead;
        StoreLockfreeFixQueue       m_pData[defaultfixsize];
    };
}

