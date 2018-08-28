#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <atomic>
#include <assert.h>
#include <vector>
#include <map>

#ifdef _WIN32
#include <windows.h>
#define CCSwitchToThread() SwitchToThread();
#else
#include <emmintrin.h>
#include <thread>
#define CCSwitchToThread() std::this_thread::yield();
#endif

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


struct CCLockfreeQueueFunc {
    //! 避免冲突队列，不同队列减少冲突, 2的指数
    static const uint8_t ThreadWriteIndexModeIndex = 4;

    //! 分配开始时候的index
    static const uint32_t CCLockfreeQueueStartIndex = 0; 

    static const uint8_t SpaceToAllocaBlockSize = 8;

    //! block size, 2的指数
    static const uint32_t BlockDefaultPerSize = 16;
    //! 分配环的指针数量
    static const uint8_t CirclePointNumber = 25; //默认取 log((0xFFFFFFFF + 1) / SpaceToAllocaBlockSize) - log(BlockDefaultPerSize);

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

template<class Traits = CCLockfreeQueueFunc>
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
#define USE_NEW 
#ifdef USE_NEW

#ifdef __GNUC__
#if defined(__APPLE__)
#include <libkern/OsAtomic.h>
#define CCLockfreeInterlockedIncrement(value) (OSAtomicAdd32(1, (volatile int32_t *)value) - 1);
#define CCLockfreeInterlockedCompareExchange(value, comp, exchange) OSAtomicCompareAndSwap32(comp, exchange, (volatile int32_t *)value)
#else
#define CCLockfreeInterlockedIncrement(value) __sync_fetch_and_add(value, 1)
#define CCLockfreeInterlockedCompareExchange(value, comp, exchange) __sync_bool_compare_and_swap(value, comp, exchange)
#endif

#elif defined(_MSC_VER)
#define CCLockfreeInterlockedIncrement(value) (::InterlockedIncrement(value) - 1)
#define CCLockfreeInterlockedDecrementNoCheckReturn(value) ::InterlockedDecrement(value)
#define CCLockfreeInterlockedCompareExchange(value, comp, exchange) (::InterlockedCompareExchange(value, exchange, comp) == comp)
#endif

#define USE_QUICKPOP
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
            if (nDis > m_nCheckTotalSizeValue) {
                return 2;
            }
            else if(nDis == m_nCheckTotalSizeValue){
                //需要判断 前一个环是否已经读取完成
                uint32_t nPerSize = m_nTotalSize / 2;
                StoreData* pPoint = &m_pPool[((nPreWriteIndex - m_nResBeginIndex) / Traits::ThreadWriteIndexModeIndex) % m_nTotalSize];
                uint8_t bWriteValue = 0;
                for (uint32_t i = 0; i < nPerSize; i++) {
                    bWriteValue = pPoint[i].m_nWrite.load(std::memory_order_relaxed);
                    if (bWriteValue != 0x10) {
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
                //需要把数据更新为0
                memset(pPoint, 0, sizeof(StoreData) * nPerSize);
                std::atomic_thread_fence(std::memory_order_release);
                m_nBeginIndex = nGetBeginIndex + nPerSize * Traits::ThreadWriteIndexModeIndex;
            }
            StoreData& writeNode = m_pPool[((nPreWriteIndex - m_nResBeginIndex) / Traits::ThreadWriteIndexModeIndex) % m_nTotalSize];
            writeNode.m_pData = value;
            writeNode.m_nWrite.store(0x11, std::memory_order_release);
            return 0;
        }
        inline int PopPosition(T& value, uint32_t nReadIndex) {
            uint32_t nGetBeginIndex = m_nBeginIndex;
            uint32_t nDis = nReadIndex - nGetBeginIndex;
#ifdef _DEBUG
            assert((nReadIndex - nGetBeginIndex) % Traits::ThreadWriteIndexModeIndex == 0);
#endif
            if (nDis > m_nCheckTotalSizeValue) {
                return 2;
            }
            else if(nDis == m_nCheckTotalSizeValue){
                if (m_bNoWrite) {
                    atomic_backoff bPause;
                    for (uint32_t i = 0; i < m_nTotalSize; i++) {
                        while (m_pPool[i].m_nWrite.load(std::memory_order_relaxed) != 0x10)
                            bPause.pause();
                    }
                    return 1;
                }
                return 3;
            }
            StoreData& writeNode = m_pPool[((nReadIndex - m_nResBeginIndex) / Traits::ThreadWriteIndexModeIndex) % m_nTotalSize];
            atomic_backoff bPause;
            while (writeNode.m_nWrite.load(std::memory_order_acquire) != 0x11) {
                bPause.pause();
            }
            value = writeNode.m_pData;
            writeNode.m_nWrite.store(0x10, std::memory_order_release);
            return 0;
        }
        inline void ReleasePool(){
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
            m_pWrite = m_pCircle[0];
            m_pRead = m_pWrite;
        }
        inline void PushMicroQueue(const T& value, uint32_t nPreWriteIndex) {
            atomic_backoff pause;
            Circle* pCircle;
            //判断当前写入环是否可以用
            while(true) {
                pCircle = m_pWrite;
                atomic_thread_fence(std::memory_order_acquire);
                switch (m_pWrite->PushPosition(value, nPreWriteIndex)) {
                case 0: {
                        return;
                    }  
                case 1:{
                        uint8_t nextCircle = m_nWriteCircle + 1;
                        pCircle = Circle::CreateNextCircle(pCircle);
                        m_pCircle[nextCircle] = pCircle;
                        atomic_thread_fence(std::memory_order_release);
                        m_pWrite = m_pCircle[nextCircle];
                        m_nWriteCircle = nextCircle;
                        pCircle->PushPosition(value, nPreWriteIndex);
                        return;
                    }
                }
                //没有命中，缓存可能更新可能不更新
                pause.pause();
            }
        }
        inline void PopMicroQueue(T& value, uint32_t nNowReadIndex) {
            atomic_backoff pause;
            Circle* pReadCircle;
            while (true){
                pReadCircle = m_pRead;
                atomic_thread_fence(std::memory_order_acquire);
                switch (pReadCircle->PopPosition(value, nNowReadIndex)) {
                case 0:{
                    return;
                }
                case 1: {
                    //need read next circle
                    while (m_nReadCircle == m_nWriteCircle) {
                        pause.pause();
                    }
                    m_pRead = m_pCircle[++m_nReadCircle];
                    pReadCircle->ReleasePool();
                    break;
                }
                default:
                    pause.pause();
                    break;
                }
            }
        }
    };
public:
    CCLockfreeQueue() {
        uint32_t nSetBeginIndex = Traits::CCLockfreeQueueStartIndex;
        m_nReadIndex = nSetBeginIndex;
        m_nPreWriteIndex = nSetBeginIndex;
#ifdef USE_QUICKPOP
        m_nPreReadIndex = m_nReadIndex;
#endif
        if ((Traits::BlockDefaultPerSize & (Traits::BlockDefaultPerSize - 1)) != 0) {
            printf("Traits::BlockDefaultPerSize is not power(2) error!\n");
            exit(0);
        }
        if ((Traits::ThreadWriteIndexModeIndex & (Traits::ThreadWriteIndexModeIndex - 1)) != 0) {
            printf("Traits::ThreadWriteIndexModeIndex is not power(2) error!\n");
            exit(0);
        }
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

#ifdef USE_QUICKPOP
    bool Pop(T& value) {
        //must be read first
        uint32_t nNowReadIndex = m_nReadIndex;
        //禁止write的读取在read之前读取
        atomic_signal_fence(std::memory_order_acquire);
        uint32_t nPreWriteIndex = m_nPreWriteIndex;
        if (nNowReadIndex == nPreWriteIndex) {
            return false;
        }
        //read after
        uint32_t nPreReadIndex = CCLockfreeInterlockedIncrement(&m_nPreReadIndex);
        if (nPreReadIndex == nPreWriteIndex) {
            //Queue is empty
            CCLockfreeInterlockedDecrementNoCheckReturn(&m_nPreReadIndex);
            return false;
        }
        //首先判断自己是否溢出, 默认前提是不可能写满max_uint32_t个队列
        bool bReadMoreUIINT32 = false;
        bool bWriteMoreUIINT32 = false;

        uint32_t nRead = 0;

        if (nPreReadIndex < nNowReadIndex) {
            bReadMoreUIINT32 = true;
        }
        if (nPreWriteIndex < nNowReadIndex) {
            bWriteMoreUIINT32 = true;
        }
        //一共4种情况
        if (bReadMoreUIINT32 && !bWriteMoreUIINT32) {
            //no more data read
            CCLockfreeInterlockedDecrementNoCheckReturn(&m_nPreReadIndex);
            return false;
    }
        else if (!bReadMoreUIINT32 && bWriteMoreUIINT32) {
            //肯定可读
            nRead = CCLockfreeInterlockedIncrement(&m_nReadIndex);
        }
        else {
            if (nPreReadIndex < nPreWriteIndex) {
                //肯定可读
                nRead = CCLockfreeInterlockedIncrement(&m_nReadIndex);
            }
            else {
                //no more data read
                CCLockfreeInterlockedDecrementNoCheckReturn(&m_nPreReadIndex);
                return false;
            }
        }
        m_queue[nRead % Traits::ThreadWriteIndexModeIndex].PopMicroQueue(value, nRead);
        return true;
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
#ifdef USE_QUICKPOP
    volatile uint32_t                                           m_nPreReadIndex;
#endif
    volatile uint32_t                                           m_nReadIndex;

    MicroQueue                                                  m_queue[Traits::ThreadWriteIndexModeIndex];
};

#else
//采用空间换时间的方法
template<class T, class Traits = CCLockfreeQueueFunc, class ObjectBaseClass = CCLockfreeObject<Traits>>
class CCLockfreeQueue : public ObjectBaseClass {
public:
    struct OddEvenBlock {
        struct StoreData {
            T                           m_pData;
            std::atomic<uint8_t>        m_bWrite;
        };
        StoreData*                      m_pPool[2];
        volatile OddEvenBlock*          m_pNext;
    };
    struct AllocBlock {
        struct StoreData {
            T                           m_pData;
            std::atomic<uint8_t>        m_bWrite;
        };
        uint32_t                m_nBeginIndex = 0;
        uint32_t                m_nSize = 0;
        std::atomic<bool>       m_bInit = { false };
        StoreData*              m_pPool = nullptr;
        ~AllocBlock() {
            if (m_pPool != nullptr) {
                Traits::free(m_pPool);
            }
        }
        void Init(uint32_t nBeginIndex, uint32_t nPreSize, uint8_t nTimes) {
            switch (nTimes) {
            case 1:
            case 2: {
                if (m_nSize == 0) {
                    m_nSize = Traits::BlockDefaultPerSize;
                    m_pPool = (StoreData*)Traits::malloc(m_nSize * sizeof(StoreData));
                }
                break;
            }
            default:
                uint32_t nSize = nPreSize * nTimes;
                if (m_nSize < nSize) {
                    m_nSize = nSize;
                    Traits::free(m_pPool);
                    m_pPool = (StoreData*)Traits::malloc(m_nSize * sizeof(StoreData));
                }
                break;
            }

            //初始化 必然会有store writeindex 那边做release保证其他地方拿到的beginidnex正确
            m_nBeginIndex = nBeginIndex;
            memset(m_pPool, 0, sizeof(StoreData) * m_nSize);
            m_bInit.store(true, std::memory_order_release);
        }
        inline void PushLocation(const T& value, uint32_t nPreWriteLocation) {
            StoreData& node = m_pPool[nPreWriteLocation - m_nBeginIndex];
            node.m_pData = value;
            node.m_bWrite.store(0x11, std::memory_order_release);
        }
        inline void PopLocation(T& value, uint32_t nPreWriteLocation) {
            atomic_backoff bPause;
            StoreData& node = m_pPool[nPreWriteLocation - m_nBeginIndex];
            while (!(node.m_bWrite.load(std::memory_order_acquire) & 0x01)) {
                bPause.pause();
            }
            value = node.m_pData;
            node.m_bWrite.store(0x10, std::memory_order_relaxed);
        }
        inline void IsReadEmpty() {
            atomic_backoff bPauseWriteFinish;
            uint32_t i = 0;
            while (i < m_nSize) {
                if (m_pPool[i].m_bWrite.load(std::memory_order_relaxed) == 0x10) {
                    i++;
                }
                else {
                    bPauseWriteFinish.pause();
                }
            }

            //set no init
            m_bInit.store(false, std::memory_order_relaxed);
        }
    };
    struct Block {
    public:
        struct StoreData {
            T                           m_pData;
            std::atomic<uint8_t>        m_bWrite;
        };
        std::atomic<Block*>     m_pNext;
        uint32_t                m_nBeginIndex;
        uint32_t                m_nSize;
        StoreData*              m_pPool;
    public:
        static Block* CreateBlock(uint32_t nSize) {
            Block* pBlock = (Block*)Traits::malloc(sizeof(Block));
            pBlock->m_pPool = (StoreData*)Traits::malloc(nSize * sizeof(StoreData));
            pBlock->m_nSize = nSize;
            return pBlock;
        }
        static void ReleaseBlock(Block* p) {
            p->ReleasePool();
            Traits::free(p);
        }
        inline void ReleasePool() {
            if (m_pPool) {
                Traits::free(m_pPool);
                m_pPool = nullptr;
            }
        }
        void Init(uint32_t nBeginIndex = 0) {
            //初始化 必然会有store writeindex 那边做release保证其他地方拿到的beginidnex正确
            m_nBeginIndex = nBeginIndex;
            m_pNext.store(nullptr, std::memory_order_relaxed);
            memset(m_pPool, 0, sizeof(StoreData) * m_nSize);
        }
        //must can full call
        inline void IsWriteFull() {
            atomic_backoff bPauseWriteFinish;
            uint32_t i = 0;
            while (i < m_nSize) {
                if (m_pPool[i].m_bWrite.load(std::memory_order_relaxed) & 0x10) {
                    i++;
                }
                else {
                    bPauseWriteFinish.pause();
                }
            }
        }
        inline void PushLocation(const T& value, uint32_t nPreWriteLocation) {
            StoreData& node = m_pPool[nPreWriteLocation - m_nBeginIndex];
            node.m_pData = value;
            node.m_bWrite.store(0x11, std::memory_order_release);
        }
        inline void IsReadEmpty() {
            atomic_backoff bPauseWriteFinish;
            uint32_t i = 0;
            while (i < m_nSize) {
                if (m_pPool[i].m_bWrite.load(std::memory_order_relaxed) == 0x10) {
                    i++;
                }
                else {
                    bPauseWriteFinish.pause();
                }
            }
        }
        inline void PopLocation(T& value, uint32_t nPreWriteLocation) {
            atomic_backoff bPause;
            StoreData& node = m_pPool[nPreWriteLocation - m_nBeginIndex];
            while (!(node.m_bWrite.load(std::memory_order_acquire) & 0x01)) {
                bPause.pause();
            }
            value = node.m_pData;
            node.m_bWrite.store(0x10, std::memory_order_relaxed);
        }
    };

    class MicroQueue {
    public:
        MicroQueue() {

        }
        virtual ~MicroQueue() {
            Block* p = m_pHead.load();
            while (p != nullptr) {
                Block* pDel = p;
                p = p->m_pNext;
                Block::ReleaseBlock(pDel);
            }
        }
        void Init(CCLockfreeQueue* pQueue) {
            m_pQueue = pQueue;
            uint32_t nSetBeginIndex = (Traits::CCLockfreeQueueStartIndex / Traits::BlockDefaultPerSize / Traits::ThreadWriteIndexModeIndex) * Traits::BlockDefaultPerSize;
            Block* pBlock = Block::CreateBlock(Traits::BlockDefaultPerSize);
            pBlock->Init(nSetBeginIndex);
            m_pWrite = pBlock;
            m_pRead = pBlock;
            m_pHead = pBlock;
            m_pRevertBlock = nullptr;

            m_nCircleWriteIndex = 0;
            m_nCircleReadIndex = 0;
            m_szBlock[0].Init(nSetBeginIndex, Traits::BlockDefaultPerSize, 1);
        }
        inline void CreateNewBlock(const T& value, uint32_t nPreWriteLocation, Block* pWriteBlock) {
            Block* pGetBlock = m_pRevertBlock.exchange(nullptr, std::memory_order_relaxed);
            if (pGetBlock == nullptr) {
                pGetBlock = Block::CreateBlock(pWriteBlock->m_nSize * 2);
            }
            pGetBlock->Init(nPreWriteLocation);
            //change write block, 这边需要release，因为读的时候读取到next需要同步next的信息
            pWriteBlock->m_pNext.store(pGetBlock, std::memory_order_release);

            pGetBlock->PushLocation(value, nPreWriteLocation);
            //wait to preindex write finish
            pWriteBlock->IsWriteFull();
            atomic_backoff bPauseWriteFinish;
            Block* pReadyWrite = nullptr;
            for (;;) {
                pReadyWrite = pWriteBlock;
                //if know how to 
                if (m_pWrite.compare_exchange_strong(pReadyWrite, pGetBlock, std::memory_order_release, std::memory_order_relaxed)) {
                    break;
                }
                bPauseWriteFinish.pause();
            }
        }
        void PushPosition(const T& value, uint32_t nPreWriteIndex) {
            uint32_t nPreWriteLocation = nPreWriteIndex / Traits::ThreadWriteIndexModeIndex;
            Block* pWriteBlock = m_pWrite.load(std::memory_order_acquire);
            if (nPreWriteLocation == 0 && pWriteBlock->m_nBeginIndex != 0) {
                CreateNewBlock(value, nPreWriteLocation, pWriteBlock);
                return;
            }
            uint32_t nCheckValue = 0;
            Block* pRes = pWriteBlock;
            if (pRes->m_nBeginIndex > nPreWriteLocation) {
                nCheckValue = pRes->m_nBeginIndex;
            }
            else {
                nCheckValue = pRes->m_nBeginIndex;
            }
            do {
                uint32_t nSize = pWriteBlock->m_nSize;
                uint32_t nBeginIndex = pWriteBlock->m_nBeginIndex;
                uint32_t nDis = nPreWriteLocation - nBeginIndex;
                if (nDis == nSize) {
                    CreateNewBlock(value, nPreWriteLocation, pWriteBlock);
                    return;
                }
                else if (nDis < nSize) {
                    break;
                }
                else {
                    atomic_backoff bPause;
                    Block* pNextBlock = pWriteBlock->m_pNext.load(std::memory_order_acquire);
                    while (pNextBlock == nullptr) {
                        bPause.pause();
                        pNextBlock = pWriteBlock->m_pNext.load(std::memory_order_acquire);
                    }
                    pWriteBlock = pNextBlock;
                }
            } while (true);
            pWriteBlock->PushLocation(value, nPreWriteLocation);
        }
        void PopPosition(T& value, uint32_t nReadIndex) {
            uint32_t nReadLocation = nReadIndex / Traits::ThreadWriteIndexModeIndex;
            Block* pReadBlock = m_pRead.load(std::memory_order_acquire);
            atomic_backoff bPauseGetNextFinish;
            Block* pRes = pReadBlock;
            do {
                uint32_t nDis = nReadLocation - pReadBlock->m_nBeginIndex;
                if (nDis == pReadBlock->m_nSize || (nReadLocation == 0 && pReadBlock->m_nBeginIndex != 0)) {
                    //read first use cpu time
                    Block* pNextBlock = pReadBlock->m_pNext.load(std::memory_order_acquire);
                    while (pNextBlock == nullptr) {
                        bPauseGetNextFinish.pause();
                        pNextBlock = pReadBlock->m_pNext.load(std::memory_order_acquire);
                    }
                    pNextBlock->PopLocation(value, nReadLocation);

                    //wait to read finish
                    pReadBlock->IsReadEmpty();
                    atomic_backoff bPauseReadFinish;
                    Block* pReadyRead = nullptr;
                    for (;;) {
                        pReadyRead = pReadBlock;
                        //if know how to 
                        if (m_pRead.compare_exchange_strong(pReadyRead, pNextBlock, std::memory_order_release, std::memory_order_relaxed)) {
                            break;
                        }
                        bPauseReadFinish.pause();
                    }
                    Block* pBlock = m_pRevertBlock.exchange(pReadBlock, std::memory_order_relaxed);
                    if (pBlock != nullptr) {
                        pBlock->ReleasePool();
                    }
                    return;
                }
                else if (nDis < pReadBlock->m_nSize) {
                    //inside
                    break;
                }
                else {
                    Block* pNextBlock = pReadBlock->m_pNext.load(std::memory_order_acquire);
                    while (pNextBlock == nullptr) {
                        bPauseGetNextFinish.pause();
                        pNextBlock = pReadBlock->m_pNext.load(std::memory_order_acquire);
                    }
                    pReadBlock = pNextBlock;
                }
            } while (true);
            pReadBlock->PopLocation(value, nReadLocation);
        }
        void PushPositionIndex(const T& value, uint32_t nPreWriteIndex) {
            int8_t nFindPoint = 0;
            uint32_t nPreWriteLocation = nPreWriteIndex / Traits::ThreadWriteIndexModeIndex;
            uint8_t nCircleWriteIndex = m_nCircleWriteIndex.load(std::memory_order_acquire);
            AllocBlock* pWriteBlock = &m_szBlock[nCircleWriteIndex % Traits::SpaceToAllocaBlockSize];
            do {
                uint32_t nSize = pWriteBlock->m_nSize;
                uint32_t nBeginIndex = pWriteBlock->m_nBeginIndex;
                uint32_t nDis = nPreWriteLocation - nBeginIndex;
                if (nDis < nSize) {
                    break;
                }
                else if (nDis == nSize || (nPreWriteLocation == 0 && pWriteBlock->m_nBeginIndex != 0)) {
                    AllocBlock* pNextWriteBlock = &m_szBlock[(nCircleWriteIndex + 1) % Traits::SpaceToAllocaBlockSize];
                    //如果已经初始化,代表队列已经满了,一直等待到读取出去
                    while (pNextWriteBlock->m_bInit.load(std::memory_order_relaxed)) {
                        CCSwitchToThread();
                    }
                    //
                    uint8_t nDis = nCircleWriteIndex - m_nCircleReadIndex.load(std::memory_order_relaxed);
                    pNextWriteBlock->Init(nPreWriteLocation, nSize, pow(2, nDis));
                    m_nCircleWriteIndex.fetch_add(1, std::memory_order_release);
                    pWriteBlock = pNextWriteBlock;
                    break;
                }
                else {
                    if (nFindPoint == 0) {
                        nFindPoint = nDis & 0x80000000 ? -1 : 1;
                    }
                    nCircleWriteIndex += nFindPoint;
                    //往后查找
                    pWriteBlock = &m_szBlock[nCircleWriteIndex % Traits::SpaceToAllocaBlockSize];
                    if (!pWriteBlock->m_bInit.load(std::memory_order_acquire)) {
                        //找不到，换方向重新查找一遍
                        nFindPoint *= -1;
                        nCircleWriteIndex = m_nCircleWriteIndex.load(std::memory_order_acquire);
                        pWriteBlock = &m_szBlock[nCircleWriteIndex % Traits::SpaceToAllocaBlockSize];
                    }
                }
            } while (true);
            pWriteBlock->PushLocation(value, nPreWriteLocation);
        }

        void PopPositionIndex(T& value, uint32_t nReadIndex) {
            uint32_t nReadLocation = nReadIndex / Traits::ThreadWriteIndexModeIndex;
            uint8_t nCircleReadIndex = m_nCircleReadIndex.load(std::memory_order_acquire);
            AllocBlock* pReadBlock = &m_szBlock[nCircleReadIndex % Traits::SpaceToAllocaBlockSize];
            atomic_backoff bPauseGetNextFinish;
            do {
                uint32_t nDis = nReadLocation - pReadBlock->m_nBeginIndex;
                if (nDis == pReadBlock->m_nSize || (nReadLocation == 0 && pReadBlock->m_nBeginIndex != 0)) {
                    AllocBlock& nextWriteBlock = m_szBlock[(nCircleReadIndex + 1) % Traits::SpaceToAllocaBlockSize];
                    //read first use cpu time
                    while (!nextWriteBlock.m_bInit.load(std::memory_order_acquire)) {
                        bPauseGetNextFinish.pause();
                    }
                    //先读
                    nextWriteBlock.PopLocation(value, nReadLocation);

                    //wait to read finish
                    pReadBlock->IsReadEmpty();
                    uint8_t nSetReadIndex = nCircleReadIndex;
                    while (!m_nCircleReadIndex.compare_exchange_strong(nSetReadIndex, nSetReadIndex + 1, std::memory_order_acquire, std::memory_order_relaxed)) {
                        nSetReadIndex = nCircleReadIndex;
                    }
                    return;
                }
                else if (nDis < pReadBlock->m_nSize) {
                    //inside
                    break;
                }
                else {
                    pReadBlock = &m_szBlock[++nCircleReadIndex % Traits::SpaceToAllocaBlockSize];
                    //wait init finish
                    if (!pReadBlock->m_bInit.load(std::memory_order_acquire)) {
                        nCircleReadIndex = m_nCircleReadIndex.load(std::memory_order_acquire);
                        pReadBlock = &m_szBlock[nCircleReadIndex % Traits::SpaceToAllocaBlockSize];
                    }
                }
            } while (true);
            pReadBlock->PopLocation(value, nReadLocation);
        }
    protected:
        std::atomic<Block*>         m_pRevertBlock;
        std::atomic<Block*>         m_pHead;
        std::atomic<Block*>         m_pRead;
        std::atomic<Block*>         m_pWrite;
        CCLockfreeQueue*            m_pQueue;

        //默认8
        AllocBlock                  m_szBlock[Traits::SpaceToAllocaBlockSize];
        std::atomic<uint8_t>        m_nCircleReadIndex;
        std::atomic<uint8_t>        m_nCircleWriteIndex;
    };
public:
    CCLockfreeQueue() {
        uint32_t nSetBeginIndex = (Traits::CCLockfreeQueueStartIndex / Traits::BlockDefaultPerSize / Traits::ThreadWriteIndexModeIndex) * Traits::BlockDefaultPerSize * Traits::ThreadWriteIndexModeIndex;
        m_nReadIndex = nSetBeginIndex;
        m_nPreReadIndex = nSetBeginIndex;
        m_nPreWriteIndex = nSetBeginIndex;
        for (int i = 0; i < Traits::ThreadWriteIndexModeIndex; i++) {
            m_array[i].Init(this);
        }
    }
    virtual ~CCLockfreeQueue() {
    }
    void Push(const T& value) {
        uint32_t nPreWriteIndex = m_nPreWriteIndex.fetch_add(1, std::memory_order_relaxed);
        m_array[nPreWriteIndex%Traits::ThreadWriteIndexModeIndex].PushPositionIndex(value, nPreWriteIndex);
    }
    bool Pop(T& value) {
        //must be read first
        uint32_t nNowReadIndex = m_nReadIndex.load(std::memory_order_relaxed);
        uint32_t nPreWriteIndex = m_nPreWriteIndex.load(std::memory_order_relaxed);
        if (nNowReadIndex == nPreWriteIndex) {
            return false;
        }
        //read after
        uint32_t nPreReadIndex = m_nPreReadIndex.fetch_add(1, std::memory_order_relaxed);
        if (nPreReadIndex == nPreWriteIndex) {
            //Queue is empty
            m_nPreReadIndex.fetch_sub(1, std::memory_order_relaxed);
            return false;
        }
        //首先判断自己是否溢出, 默认前提是不可能写满max_uint32_t个队列
        bool bReadMoreUIINT32 = false;
        bool bWriteMoreUIINT32 = false;

        uint32_t nRead = 0;

        if (nPreReadIndex < nNowReadIndex) {
            bReadMoreUIINT32 = true;
        }
        if (nPreWriteIndex < nNowReadIndex) {
            bWriteMoreUIINT32 = true;
        }
        //一共4种情况
        if (bReadMoreUIINT32 && !bWriteMoreUIINT32) {
            //no more data read
            m_nPreReadIndex.fetch_sub(1, std::memory_order_relaxed);
            return false;
        }
        else if (!bReadMoreUIINT32 && bWriteMoreUIINT32) {
            //肯定可读
            nRead = m_nReadIndex.fetch_add(1, std::memory_order_relaxed);
        }
        else {
            if (nPreReadIndex < nPreWriteIndex) {
                //肯定可读
                nRead = m_nReadIndex.fetch_add(1, std::memory_order_relaxed);
            }
            else {
                //no more data read
                m_nPreReadIndex.fetch_sub(1, std::memory_order_relaxed);
                return false;
            }
        }
        m_array[nRead%Traits::ThreadWriteIndexModeIndex].PopPositionIndex(value, nRead);
        return true;
    }
    inline uint32_t GetReadIndex() {
        return m_nReadIndex.load(std::memory_order_relaxed);
    }
    inline uint32_t GetPreWriteIndex() {
        return m_nPreWriteIndex.load(std::memory_order_relaxed);
    }
    uint32_t GetSize() {
        return m_nPreWriteIndex.load(std::memory_order_relaxed) - m_nReadIndex.load(std::memory_order_relaxed);
    }
protected:
    MicroQueue                                                  m_array[Traits::ThreadWriteIndexModeIndex];
    std::atomic<uint32_t>                                       m_nPreReadIndex;
    std::atomic<uint32_t>                                       m_nReadIndex;
    std::atomic<uint32_t>                                       m_nPreWriteIndex;
};

#endif
