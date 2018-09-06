#ifndef CCCONTAINUNITTEMPLATE_H
#define CCCONTAINUNITTEMPLATE_H

#include <stdio.h>

//!
//线程工作函数定义
typedef void (*CCLOCKFREEQUEUE_THREAD_START_ROUTINE)(void* lpThreadParameter);

enum CCContainUnitStatus {
    CCContainUnitStatus_Ready,
    CCContainUnitStatus_Wait,
    CCContainUnitStatus_Finish
};

template<typename T, typename Container> class CCContainUnitThread;
template<class T, class Container>
class CCContainUnit {
public:
    typedef CCContainUnitThread<T, Container> UnitThread;
public:
    virtual ~CCContainUnit() {
        if (m_pNode) {
            delete[]m_pNode;
        }
    }
    UnitThread* GetUnitThread() {
        return m_pUnitThread;
    }
    virtual void Init(UnitThread* pThread, Container* pContainer, uint32_t nIndex, uint32_t nCount) {
        m_pUnitThread = pThread;
        m_pContainer = pContainer;
        m_nIndex = nIndex;
        m_nCount = nCount;
        m_pNode = new T[nCount];
        for (uint32_t i = 0; i < nCount; i++) {
            m_pNode[i].InitUint(nIndex, i);
        }
    }
    virtual T* GetPushCtx() {
        uint32_t lRet = m_nPushTime++;
        if (lRet >= m_nCount)
            return nullptr;
        return getPushCtx(lRet);
    }
    virtual T* GetPushCtxNoNull() {
        return getPushCtx(m_nPushTime++);
    }
    inline T* getPushCtx(uint32_t lRet) {
        lRet = lRet % m_nCount;
        T* pRet = &m_pNode[lRet];
        pRet->Send();
        return pRet;
    }
    virtual bool CheckIsSuccess() {
        for (uint32_t i = 0; i < m_nCount; i++) {
            if (!m_pNode[i].IsSendReceiveSame())
                return false;
        }
        return true;
    }
    virtual void Receive(T* pNode) {
        m_pNode[pNode->GetCheckReceiveNumber()].Received();
    }
    Container* GetContainer() {
        return m_pContainer;
    }
    uint32_t GetPushTimes() {
        return m_nPushTime;
    }
protected:
    uint32_t                                m_nIndex = 0;
    uint32_t                                m_nCount = 0;
    uint32_t                                m_nPushTime = 0;
    T*                                      m_pNode = nullptr;
    Container*                              m_pContainer = nullptr;
    UnitThread*                             m_pUnitThread = nullptr;
};

template<class T, class Container>
class CCContainUnitThread {
public:
    virtual ~CCContainUnitThread() {
        if (m_pUint)
            delete[]m_pUint;
    }
    virtual void Init(Container* pContainer, uint32_t nThreadCount, uint32_t nCount) {
        m_pContainer = pContainer;
        m_nThreadCount = nThreadCount;
        m_pUint = createCCContainUnit(nThreadCount);
        for (uint32_t i = 0; i < nThreadCount; i++) {
            m_pUint[i].Init(this, pContainer, i, nCount);
        }
    }
    virtual bool CheckIsSuccess() {
        for (uint32_t i = 0; i < m_nThreadCount; i++) {
            if (!m_pUint[i].CheckIsSuccess())
                return false;
        }
        return true;
    }
    virtual void Receive(T* pNode) {
        m_pUint[pNode->GetCheckIndex()].Receive(pNode);
    }
    uint32_t GetPopSleepData() {
        return m_nNoPopData.load(std::memory_order_relaxed);
    }
    uint32_t GetPushSleepData() {
        return m_nNoPushData.load(std::memory_order_relaxed);
    }
    void NoPopData() {
        m_nNoPopData.fetch_add(1);
    }
    void NoPushData() {
        m_nNoPushData.fetch_add(1);
    }
    Container* GetContainer() {
        return m_pContainer;
    }
    CCContainUnit<T, Container>* GetContainUintByIndex(uint32_t nIndex) {
        return &m_pUint[nIndex];
    }
    void SetTimeFinish() {
        m_statusTimeFinish = CCContainUnitStatus_Finish;
    }
    void SetTimeWait() {
        m_statusTimeFinish = CCContainUnitStatus_Wait;
    }
    void SetTimeReady() {
        m_statusTimeFinish = CCContainUnitStatus_Ready;
    }
    CCContainUnitStatus GetTimeStatus() {
        return m_statusTimeFinish;
    }
    uint32_t GetSendTimes() {
        uint32_t nRetValue = 0;
        for (uint32_t i = 0; i < m_nThreadCount; i++) {
            nRetValue += m_pUint[i].GetPushTimes();
        }
        return nRetValue;
    }
    uint32_t GetContainSize() {
        return m_pContainer->GetSize();
    }
protected:
    virtual CCContainUnit<T, Container>* createCCContainUnit(uint32_t nCount) {
        return new CCContainUnit<T, Container>[nCount];
    }
protected:
    uint32_t                        m_nThreadCount = 0;
    CCContainUnit<T, Container>*    m_pUint = nullptr;
    Container*                      m_pContainer = nullptr;
    std::atomic<uint32_t>           m_nNoPushData = { 0 };
    std::atomic<uint32_t>           m_nNoPopData = { 0 };
    CCContainUnitStatus             m_statusTimeFinish = CCContainUnitStatus_Ready;
};

template<class T, class Container>
void PushFunc(void* p) {
    CCContainUnit<T, Container>* pTest = (CCContainUnit<T, Container>*)p;
    Container* pContainer = pTest->GetContainer();
    while (true) {
        T* pRet = pTest->GetPushCtx();
        if (pRet == nullptr)
            break;
        pContainer->Push(pRet);
    }
}

template<class T, class Container>
void PopFunc(void* p) {
    CCContainUnitThread<T, Container>* pTest = (CCContainUnitThread<T, Container>*)p;
    Container* pContainer = pTest->GetContainer();
    while (true) {
        T* p = (T*)pContainer->Pop();
        if (p == nullptr)
            break;
        pTest->Receive(p);
    }
}

template<class T, class Container>
void PushContentFunc(void* p) {
    CCContainUnit<T, Container>* pTest = (CCContainUnit<T, Container>*)p;
    Container* pContainer = pTest->GetContainer();
    while (true) {
        T* pRet = pTest->GetPushCtx();
        if (pRet == nullptr)
            break;
        pContainer->Push(*pRet);
    }
}

template<class T, class Container>
void PopContentFunc(void* p) {
    CCContainUnitThread<T, Container>* pTest = (CCContainUnitThread<T, Container>*)p;
    Container* pContainer = pTest->GetContainer();
    T node;
    uint32_t nReadIndex = 0;
    while (true) {
        if (!pContainer->Pop(node))
            break;
        pTest->Receive(&node);
    }
}


template<class T, class Container>
void PushContentNoNullFunc(void* p) {
    CCContainUnit<T, Container>* pTest = (CCContainUnit<T, Container>*)p;
    CCContainUnitThread<T, Container>* pThreadTest = pTest->GetUnitThread();
    Container* pContainer = pTest->GetContainer();
    CCContainUnitStatus status = pThreadTest->GetTimeStatus();
    while (status != CCContainUnitStatus_Finish) {
        switch (status) {
        case CCContainUnitStatus_Ready:
            pContainer->Push(*pTest->GetPushCtxNoNull());
            break;
        case CCContainUnitStatus_Wait:
            pThreadTest->NoPushData();
            CCSleep(1);
            break;
        }
        status = pThreadTest->GetTimeStatus();
    }
}

template<class T, class Container>
void PopContentNoNullFunc(void* p) {
    CCContainUnitThread<T, Container>* pTest = (CCContainUnitThread<T, Container>*)p;
    Container* pContainer = pTest->GetContainer();
    T node;
    while (pTest->GetTimeStatus() != CCContainUnitStatus_Finish) {
        if (!pContainer->Pop(node)) {
            pTest->NoPopData();
            CCSleep(1);
            continue;
        }
        pTest->Receive(&node);
    }
    //pop all data
    while (true) {
        if (!pContainer->Pop(node)) {
            break;
        }
        pTest->Receive(&node);
    }
}

//use time
#define PrintLockfreeUseTime(calcName, totalPerformance)\
[&](DWORD dwUseTime, DWORD dwTotalUseTime){\
    printf("P:%12.3f/ms Time:%5d/%5d F:%s\n", (double)(totalPerformance)/dwTotalUseTime, dwUseTime, dwTotalUseTime, calcName);\
}

template<class T, class Container>
class CContainUnitThreadRunMode {
public:
    CContainUnitThreadRunMode(Container* p, uint32_t nMaxCountTimeFast, uint32_t nRepeatTimes) {
        m_nMaxCountTimesFast = nMaxCountTimeFast;
        m_nRepeatTimes = nRepeatTimes;
        m_pContain = p;
    }
    virtual ~CContainUnitThreadRunMode(){
        
    }

    template<class F>
    bool RepeatCCContainUnitThread(uint32_t nThreadCount, uint32_t maxPushTimes, F func) {
        uint32_t nReceiveThreadCount = nThreadCount;
        for (uint32_t i = 0; i < m_nRepeatTimes; i++) {
            auto p = createUnitThread();
            p->Init(m_pContain, nThreadCount, maxPushTimes);
            if (!func(p, nReceiveThreadCount)) {
                delete p;
                return false;
            }
            delete p;
        }
        return true;
    }

    bool PowerOfTwoThreadCountImpl(uint32_t nThreadCount, CCLOCKFREEQUEUE_THREAD_START_ROUTINE lpStartAddressPush, CCLOCKFREEQUEUE_THREAD_START_ROUTINE lpStartAddressPop) {
        bool bRet = true;
        char szBuf[2][32];
        ccsnprintf(szBuf[0], 32, "ThreadCount(%d) Push", nThreadCount);
        CreateCalcUseTime(begin, PrintLockfreeUseTime(szBuf[0], m_nMaxCountTimesFast / nThreadCount * m_nRepeatTimes * nThreadCount), false);
        ccsnprintf(szBuf[1], 32, "ThreadCount(%d) Pop", nThreadCount);
        CreateCalcUseTime(beginrece, PrintLockfreeUseTime(szBuf[1], m_nMaxCountTimesFast / nThreadCount * m_nRepeatTimes * nThreadCount), false);
        RepeatCCContainUnitThread(nThreadCount, m_nMaxCountTimesFast / nThreadCount, [&](CCContainUnitThread<T, Container>* pMgr, uint32_t nReceiveThreadCount) {
            bool bFuncRet = true;
            std::thread* pPushThread = new std::thread[nThreadCount];
            StartCalcUseTime(begin);
            for (uint32_t j = 0; j < nThreadCount; j++) {
                pPushThread[j] = std::thread(lpStartAddressPush, pMgr->GetContainUintByIndex(j));
            }
            for (uint32_t j = 0; j < nThreadCount; j++) {
                pPushThread[j].join();
            }
            EndCalcUseTimeCallback(begin, nullptr);
            StartCalcUseTime(beginrece);
            for (uint32_t j = 0; j < nReceiveThreadCount; j++) {
                pPushThread[j] = std::thread(lpStartAddressPop, pMgr);
            }
            for (uint32_t j = 0; j < nReceiveThreadCount; j++) {
                pPushThread[j].join();
            }
            EndCalcUseTimeCallback(beginrece, nullptr);
            if (!pMgr->CheckIsSuccess()) {
                bFuncRet = false;
                printf("check fail\n");
            }
            delete[]pPushThread;
            return bFuncRet;
        });
        CallbackUseTime(begin);
        CallbackUseTime(beginrece);
        return bRet;
    }

    bool PowerOfTwoThreadCountTest(CCLOCKFREEQUEUE_THREAD_START_ROUTINE lpStartAddressPush, CCLOCKFREEQUEUE_THREAD_START_ROUTINE lpStartAddressPop, uint32_t nMaxThreadCount = 8, uint32_t nMinThreadCount = 1) {
        for (uint32_t nThreadCount = nMinThreadCount; nThreadCount <= nMaxThreadCount; nThreadCount *= 2) {
            if (!PowerOfTwoThreadCountImpl(nThreadCount, lpStartAddressPush, lpStartAddressPop))
                return false;
        }
        return true;
    }

protected:
    virtual CCContainUnitThread<T, Container>* createUnitThread() {
        return new CCContainUnitThread<T, Container>();
    }
protected:
    Container * m_pContain;
    uint32_t            m_nRepeatTimes;
    uint32_t            m_nMaxCountTimesFast;
};


//use time
#define PrintEffect(calcName, pMgr)\
[&](DWORD dwUseTime, DWORD dwTotalUseTime){\
    uint32_t nContainSize = pMgr->GetContainSize();\
    uint32_t nSendTimes = pMgr->GetSendTimes();\
    printf("Size: %d PushTime: %d Push:%8.3f/ms Pop:%8.3f/ms Time:%5d/%5d F:%s\n", nContainSize, nSendTimes, (double)(nSendTimes)/dwTotalUseTime, (double)(nSendTimes - nContainSize)/dwTotalUseTime, pMgr->GetPushSleepData(), pMgr->GetPopSleepData(), calcName);\
}

template<class T, class Container>
class CContainUnitThreadRunModeTime {
public:
    CContainUnitThreadRunModeTime(Container* p, uint32_t nMaxCountTimeFast, uint32_t nTotalTimes) {
        m_nMaxCountTimesFast = nMaxCountTimeFast;
        m_nTotalTimes = nTotalTimes;
        m_pContain = p;
    }
    virtual ~CContainUnitThreadRunModeTime(){
        
    }

    bool PowerOfTwoThreadCountImpl(uint32_t nPushThreadCount, uint32_t nPopThreadCount) {
        bool bRet = true;
        char szBuf[128];
        ccsnprintf(szBuf, 128, "PushThreadCount(%d) PopThreadCount(%d)", nPushThreadCount, nPopThreadCount);
        CreateCalcUseTime(begin, nullptr, false);

        auto pMgr = createUnitThread();
        pMgr->Init(m_pContain, nPushThreadCount, m_nMaxCountTimesFast / nPushThreadCount);

        std::thread* pPushThread = new std::thread[nPushThreadCount];
        std::thread* pPopThread = new std::thread[nPopThreadCount];
        uint32_t nLastSendTimes = 0;
#define PRINT_TIME 1000;
        uint32_t nCheckTime = PRINT_TIME;
        StartCalcUseTime(begin);
        for (uint32_t j = 0; j < nPushThreadCount; j++) {
            pPushThread[j] = std::thread(PushContentNoNullFunc<T, Container>, pMgr->GetContainUintByIndex(j));
        }
        for (uint32_t j = 0; j < nPopThreadCount; j++) {
            pPopThread[j] = std::thread(PopContentNoNullFunc<T, Container>, pMgr);
        }
        while (!IsTimeEnoughUseTime(begin, m_nTotalTimes, PrintEffect(szBuf, pMgr))) {
            if (!IsTimeEnoughUseTime(begin, nCheckTime, PrintEffect(szBuf, pMgr))) {
                CCSleep(1);
                continue;
            }
            nCheckTime += PRINT_TIME;
            if (pMgr->GetContainSize() > 1024 * 1024 * 10) {
                pMgr->SetTimeWait();
            }
            else {
                pMgr->SetTimeReady();
            }
        }
        pMgr->SetTimeFinish();

        for (uint32_t j = 0; j < nPushThreadCount; j++) {
            pPushThread[j].join();
        }
        for (uint32_t j = 0; j < nPopThreadCount; j++) {
            pPopThread[j].join();
        }
        if (!pMgr->CheckIsSuccess()) {
            printf("check fail\n");
        }
        delete pMgr;
        delete[]pPushThread;
        delete[]pPopThread;

        return bRet;
    }

    bool PowerOfTwoThreadCountTest(uint32_t nPushThreadCount, uint32_t nPopThreadCount) {
        return PowerOfTwoThreadCountImpl(nPushThreadCount, nPopThreadCount);
    }

protected:
    virtual CCContainUnitThread<T, Container>* createUnitThread() {
        return new CCContainUnitThread<T, Container>();
    }
protected:
    Container * m_pContain;
    uint32_t            m_nTotalTimes;
    uint32_t            m_nMaxCountTimesFast;
};

#endif
