#pragma once

#include "cclockfreedef.h"

#define USE_QUICKPOP_CCLOCKFREEQUEUE

// Compiler-specific likely/unlikely hints
namespace cclockfree {
    struct CCLockfreeQueueFunc : CCLockfreeFunc {
        //! 避免冲突队列，不同队列减少冲突, 2的指数
        static const uint8_t ThreadWriteIndexModeIndex = 4;

        //! 分配开始时候的index
        static const uint32_t CCLockfreeQueueStartIndex = 0;

        static const uint8_t SpaceToAllocaBlockSize = 8;

        //! block size, 2的指数
        static const uint32_t BlockDefaultPerSize = 16;
        //! 分配环的指针数量
        static const uint8_t CirclePointNumber = 25; //默认取 log((0xFFFFFFFF + 1) / SpaceToAllocaBlockSize) - log(BlockDefaultPerSize);
    };

#define uint32_t_after(a, b)    ((int32_t)(b) - (int32_t)(a) < 0)           //is a after b, linux jiffies
    //采用空间换时间的方法
    template<class T, class Traits = CCLockfreeQueueFunc, class ObjectBaseClass = CCLockfreeObject<Traits>>
    class CCLockfreeQueue : public ObjectBaseClass {
    public:
        struct Circle {
        public:
            static Circle* CreateCircle(uint32_t nPerSize, uint32_t nBeginIndex) {
                Circle* pRet = (Circle*)Traits::malloc(sizeof(Circle));
                pRet->m_nResBeginIndex = nBeginIndex;
                pRet->m_nBeginIndex = nBeginIndex;
                pRet->m_nTotalSize = nPerSize * 2;
                pRet->m_nCheckTotalSizeValue = pRet->m_nTotalSize * Traits::ThreadWriteIndexModeIndex;
                pRet->m_bNoWrite = false;
                pRet->m_pPool = (StoreData*)Traits::malloc(pRet->m_nTotalSize * sizeof(StoreData));
                memset(pRet->m_pPool, 0, sizeof(StoreData) * pRet->m_nTotalSize);
                return pRet;
            }
            static Circle* CreateNextCircle(Circle* pCircle) {
                Circle* pRet = (Circle*)Traits::malloc(sizeof(Circle));
                pRet->m_nResBeginIndex = pCircle->m_nBeginIndex + pCircle->m_nCheckTotalSizeValue;
                pRet->m_nBeginIndex = pRet->m_nResBeginIndex;
                pRet->m_nTotalSize = pCircle->m_nTotalSize * 2;
                pRet->m_nCheckTotalSizeValue = pRet->m_nTotalSize * Traits::ThreadWriteIndexModeIndex;
                pRet->m_bNoWrite = false;
                pRet->m_pPool = (StoreData*)Traits::malloc(pRet->m_nTotalSize * sizeof(StoreData));
                memset(pRet->m_pPool, 0, sizeof(StoreData) * pRet->m_nTotalSize);
                return pRet;
            }
            static void ReleaseCircle(Circle* pCircle) {
                if (pCircle->m_pPool) {
                    Traits::free(pCircle->m_pPool);
                }
                Traits::free(pCircle);
            }
            inline int PushPosition(const T& value, uint32_t nPreWriteIndex) {
                uint32_t nGetBeginIndex = m_nBeginIndex;
                uint32_t nDis = nPreWriteIndex - nGetBeginIndex;
#ifdef _DEBUG
                assert((nPreWriteIndex - nGetBeginIndex) % Traits::ThreadWriteIndexModeIndex == 0);
#endif
                if (CCLockfreequeueUnLikely(nDis >= m_nCheckTotalSizeValue)) {
                    if (CCLockfreequeueUnLikely(nDis == m_nCheckTotalSizeValue)) {
                        uint32_t nSetIndex = (nPreWriteIndex - m_nResBeginIndex) / Traits::ThreadWriteIndexModeIndex;
                        uint8_t nCheckSign = ((nSetIndex * 2 / m_nTotalSize - 2) % 4 + 1) << 4;
                        //需要判断 前一个环是否已经读取完成
                        uint32_t nPerSize = m_nTotalSize / 2;
                        StoreData* pPoint = &m_pPool[nSetIndex % m_nTotalSize];
                        uint8_t bWriteValue = 0;
                        for (uint32_t i = 0; i < nPerSize; i++) {
                            bWriteValue = pPoint[i].m_nWrite.load(std::memory_order_relaxed);
                            if (bWriteValue != nCheckSign) {
                                m_bNoWrite = true;
                                atomic_backoff bPause;
                                while (bWriteValue == 0) {
                                    bPause.pause();
                                    bWriteValue = pPoint[i].m_nWrite.load(std::memory_order_relaxed);
                                }
                                for (uint32_t j = i + 1; j < nPerSize; j++) {
                                    while (0 == pPoint[j].m_nWrite.load(std::memory_order_relaxed))
                                        bPause.pause();
                                }
                                pPoint = pPoint == m_pPool ? &m_pPool[nPerSize] : m_pPool;
                                for (uint32_t j = 0; j < nPerSize; j++) {
                                    while (0 == pPoint[j].m_nWrite.load(std::memory_order_relaxed))
                                        bPause.pause();
                                }
                                return 1;
                            }
                        }
                        m_nBeginIndex = nGetBeginIndex + nPerSize * Traits::ThreadWriteIndexModeIndex;
                    }
                    else {
                        return 2;
                    }
                }
                uint32_t nSetIndex = (nPreWriteIndex - m_nResBeginIndex) / Traits::ThreadWriteIndexModeIndex;
                uint8_t nSign = (((nSetIndex * 2 / m_nTotalSize) % 4 + 1) << 4) + 0x01;
                StoreData& writeNode = m_pPool[nSetIndex % m_nTotalSize];
                writeNode.m_pData = value;
                writeNode.m_nWrite.store(nSign, std::memory_order_release);
                return 0;
            }
            inline int PopPosition(T& value, uint32_t nReadIndex) {
                uint32_t nGetBeginIndex = m_nBeginIndex;
                uint32_t nDis = nReadIndex - nGetBeginIndex;
#ifdef _DEBUG
                assert((nReadIndex - nGetBeginIndex) % Traits::ThreadWriteIndexModeIndex == 0);
#endif
                if (CCLockfreequeueUnLikely(nDis >= m_nCheckTotalSizeValue)) {
                    if (CCLockfreequeueUnLikely(nDis == m_nCheckTotalSizeValue)) {
                        if (m_bNoWrite) {
                            uint32_t nSetIndex = (nReadIndex - m_nResBeginIndex) / Traits::ThreadWriteIndexModeIndex;
                            uint8_t nCheckSign = ((nSetIndex * 2 / m_nTotalSize - 2) % 4 + 1) << 4;
                            uint32_t nPerSize = m_nTotalSize / 2;
                            StoreData* pPoint = &m_pPool[nSetIndex % m_nTotalSize];
                            atomic_backoff bPause;
                            for (uint32_t i = 0; i < nPerSize; i++) {
                                while (pPoint[i].m_nWrite.load(std::memory_order_relaxed) != nCheckSign)
                                    bPause.pause();
                            }
                            pPoint = pPoint == m_pPool ? &m_pPool[nPerSize] : m_pPool;
                            nCheckSign = ((nSetIndex * 2 / m_nTotalSize - 1) % 4 + 1) << 4;
                            for (uint32_t j = 0; j < nPerSize; j++) {
                                while (nCheckSign != pPoint[j].m_nWrite.load(std::memory_order_relaxed))
                                    bPause.pause();
                            }
                            return 1;
                        }
                        return 3;
                    }
                    return 2;
                }
                uint32_t nSetIndex = (nReadIndex - m_nResBeginIndex) / Traits::ThreadWriteIndexModeIndex;
                uint8_t nSign = (((nSetIndex * 2 / m_nTotalSize) % 4 + 1) << 4) + 0x01;
                StoreData& writeNode = m_pPool[nSetIndex % m_nTotalSize];
                atomic_backoff bPause;
                while (writeNode.m_nWrite.load(std::memory_order_acquire) != nSign) {
                    bPause.pause();
                }
                value = writeNode.m_pData;
                writeNode.m_nWrite.store(nSign & 0xF0, std::memory_order_release);
                return 0;
            }
            inline void ReleasePool() {
                //free data
                Traits::free(m_pPool);
                m_pPool = nullptr;
            }
        protected:
            struct StoreData {
                T                           m_pData;
                std::atomic<uint8_t>        m_nWrite;
            };
            uint32_t                        m_nResBeginIndex;
            volatile uint32_t               m_nBeginIndex;
            uint32_t                        m_nTotalSize;// m_nPerSize * 2
            uint32_t                        m_nCheckTotalSizeValue;
            StoreData*                      m_pPool;
            volatile bool                   m_bNoWrite;
        };
        struct MicroQueue {
            volatile uint8_t                                            m_nWriteCircle;
            volatile uint8_t                                            m_nReadCircle;

            Circle* volatile                                            m_pWrite;
            Circle* volatile                                            m_pRead;
            Circle* volatile                                            m_pCircle[Traits::CirclePointNumber];
            ~MicroQueue() {
                for (int i = 0; i <= m_nWriteCircle; i++) {
                    Circle::ReleaseCircle(m_pCircle[i]);
                }
            }
            void InitMicroQueue(uint32_t nIndex) {
                m_nWriteCircle = 0;
                m_nReadCircle = 0;
                m_pCircle[0] = Circle::CreateCircle(Traits::BlockDefaultPerSize, nIndex);
                atomic_thread_fence(std::memory_order_release);
                m_pWrite = m_pCircle[0];
                m_pRead = m_pWrite;
            }
            inline void PushMicroQueue(const T& value, uint32_t nPreWriteIndex) {
                atomic_backoff pause;
                Circle* pCircle = m_pWrite;
                atomic_thread_fence(std::memory_order_acquire);
                //判断当前写入环是否可以用
                while (true) {
                    switch (m_pWrite->PushPosition(value, nPreWriteIndex)) {
                    case 0: {
                        return;
                    }
                    case 1: {
                        uint8_t nextCircle = m_nWriteCircle + 1;
                        pCircle = Circle::CreateNextCircle(pCircle);
                        m_pCircle[nextCircle] = pCircle;
                        atomic_thread_fence(std::memory_order_release);
                        m_pWrite = pCircle;
                        m_nWriteCircle = nextCircle;
                        pCircle->PushPosition(value, nPreWriteIndex);
                        return;
                    }
                    }
                    //没有命中，缓存可能更新可能不更新
                    pause.pause();
                    //重读需要判断，是否需要
                    Circle* pNewCircle = m_pWrite;
                    if (pNewCircle != pCircle)
                        atomic_thread_fence(std::memory_order_acquire);
                    pCircle = pNewCircle;
                }
            }
            inline void PopMicroQueue(T& value, uint32_t nNowReadIndex) {
                atomic_backoff pause;
                Circle* pReadCircle = m_pRead;
                atomic_thread_fence(std::memory_order_acquire);
                while (true) {
                    switch (pReadCircle->PopPosition(value, nNowReadIndex)) {
                    case 0: {
                        return;
                    }
                    case 1: {
                        //need read next circle
                        while (m_nReadCircle == m_nWriteCircle) {
                            pause.pause();
                        }
                        m_pRead = m_pCircle[++m_nReadCircle];
                        pReadCircle->ReleasePool();
                        pReadCircle = m_pRead;
                        break;
                    }
                    default: {
                        pause.pause();

                        //重读需要判断，是否需要
                        Circle * pNewCircle = m_pRead;
                        if (pNewCircle != pReadCircle)
                            atomic_thread_fence(std::memory_order_acquire);
                        pReadCircle = pNewCircle;
                        break;
                    }
                    }
                }
            }
        };
    public:
        CCLockfreeQueue() {
            static_assert((Traits::BlockDefaultPerSize & (Traits::BlockDefaultPerSize - 1)) == 0,
                "Traits::BlockDefaultPerSize is not power(2) error!");
            static_assert((Traits::ThreadWriteIndexModeIndex & (Traits::ThreadWriteIndexModeIndex - 1)) == 0,
                "Traits::ThreadWriteIndexModeIndex is not power(2) error!");

            uint32_t nSetBeginIndex = Traits::CCLockfreeQueueStartIndex;
            m_nReadIndex = nSetBeginIndex;
            m_nPreWriteIndex = nSetBeginIndex;
#ifdef USE_QUICKPOP_CCLOCKFREEQUEUE
            m_nPreReadIndex = m_nReadIndex;
#endif
            for (uint8_t i = 0; i < Traits::ThreadWriteIndexModeIndex; i++) {
                m_queue[i].InitMicroQueue(nSetBeginIndex + i);
            }
        }
        virtual ~CCLockfreeQueue() {

        }
        uint32_t GetSize() {
            uint32_t nRead = m_nReadIndex;
            uint32_t nWrite = m_nPreWriteIndex;
            return nWrite - nRead;
        }
        void Push(const T& value) {
            uint32_t nPreWriteIndex = CCLockfreeInterlockedIncrement(&m_nPreWriteIndex);
            m_queue[nPreWriteIndex % Traits::ThreadWriteIndexModeIndex].PushMicroQueue(value, nPreWriteIndex);
        }

#ifdef USE_QUICKPOP_CCLOCKFREEQUEUE
        bool Pop(T& value) {
            //read after
            uint32_t nPreReadIndex = CCLockfreeInterlockedIncrement(&m_nPreReadIndex);
            uint32_t nPreWriteIndex = m_nPreWriteIndex;
            if (uint32_t_after(nPreWriteIndex, nPreReadIndex)) {
                uint32_t nRead = CCLockfreeInterlockedIncrement(&m_nReadIndex);
                m_queue[nRead % Traits::ThreadWriteIndexModeIndex].PopMicroQueue(value, nRead);
                return true;
            };
            //Queue is empty
            CCLockfreeInterlockedDecrementNoCheckReturn(&m_nPreReadIndex);
            return false;
        }
#else
        bool PopIndex(T& value, uint32_t& nReadindex) {
            uint32_t nWriteIndex, nNowReadIndex;
            do {
                nNowReadIndex = m_nReadIndex;
                nWriteIndex = m_nPreWriteIndex;
                if (nNowReadIndex == nWriteIndex) {
                    return false;
                }
            } while (CCLockfreeInterlockedCompareExchange(&m_nReadIndex, nNowReadIndex, nNowReadIndex + 1) != nNowReadIndex);

            m_queue[nNowReadIndex % Traits::ThreadWriteIndexModeIndex].PopMicroQueue(value, nNowReadIndex);
            nReadindex = nNowReadIndex;
            return true;
        }
        bool Pop(T& value) {
            uint32_t nWriteIndex, nNowReadIndex;
            do {
                nNowReadIndex = m_nReadIndex;
                nWriteIndex = m_nPreWriteIndex;
                if (nNowReadIndex == nWriteIndex) {
                    return false;
                }
            } while (!CCLockfreeInterlockedCompareExchange(&m_nReadIndex, nNowReadIndex, nNowReadIndex + 1));

            m_queue[nNowReadIndex % Traits::ThreadWriteIndexModeIndex].PopMicroQueue(value, nNowReadIndex);
            return true;
        }
#endif
    protected:
        volatile uint32_t                                           m_nPreWriteIndex;
#ifdef USE_QUICKPOP_CCLOCKFREEQUEUE
        volatile uint32_t                                           m_nPreReadIndex;
#endif
        volatile uint32_t                                           m_nReadIndex;

        MicroQueue                                                  m_queue[Traits::ThreadWriteIndexModeIndex];
    };
}

