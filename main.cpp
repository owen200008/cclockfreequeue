#include "cclockfreequeue.h"
#include "lockfreemessagequeue.h"
#include "debug.h"
#include "cccontaintemplate.h"
#include <atomic>


struct ctx_message {
    uint32_t        m_nCtxID;
    uint32_t        m_nIndex;
    uint32_t        m_nReceived;
    uint32_t        m_nSend;
    ctx_message() {
        memset(this, 0, sizeof(ctx_message));
    }
    ctx_message(uint32_t ctxid) {
        memset(this, 0, sizeof(ctx_message));
        m_nCtxID = ctxid;
    }
    ~ctx_message() {
    }

    void InitUint(uint32_t nIndex, uint32_t nValue) {
        m_nCtxID = nValue;
        m_nIndex = nIndex;
    }
    uint32_t GetCheckIndex() {
        return m_nIndex;
    }
    uint32_t GetCheckReceiveNumber() {
        return m_nCtxID;
    }
    void Send() {
        m_nSend++;
    }
    void Received() {
        m_nReceived++;
    }
    bool IsSendReceiveSame() {
        return m_nReceived == m_nSend && m_nSend != 0;
    }
};



/////////////////////////////////////////////////////////////////////////////////////////////////////////////
class CLockFreeMessageQueuePushPop : public CLockFreeMessageQueue<ctx_message> {
public:
    bool Push(ctx_message& msg) {
        return enqueue(msg);
    }
    bool Pop(ctx_message& msg) {
        return try_dequeue(msg);
    }
};
CLockFreeMessageQueuePushPop conMsgQueue;
/////////////////////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////////////////////////////
template<class msg, class Queue>
bool BenchmarkQueue(Queue& q, int nRepeatTimes, int nMinThread, int nMaxThread) {
    bool bRet = true;
    auto pCBasicQueueArrayMode = new CContainUnitThreadRunMode<msg, Queue>(&q, TIMES_FAST, nRepeatTimes);
    bRet &= pCBasicQueueArrayMode->PowerOfTwoThreadCountTest(PushContentFunc<msg, Queue>, PopContentFunc<ctx_message, Queue>, nMaxThread, nMinThread);
    delete pCBasicQueueArrayMode;
    if (bRet == false)
        return bRet;
    return bRet;
}

template<class msg, class Queue>
bool BenchmarkQueueTime(Queue& q, int nTotalTimes, int nPushThread, int nPopThread) {
    bool bRet = true;
    auto pCBasicQueueArrayMode = new CContainUnitThreadRunModeTime<msg, Queue>(&q, TIMES_FAST, nTotalTimes);
    bRet &= pCBasicQueueArrayMode->PowerOfTwoThreadCountTest(nPushThread, nPopThread);
    delete pCBasicQueueArrayMode;
    if (bRet == false)
        return bRet;
    return bRet;
}

int main(int argc, char* argv[]){
    {
        CCLockfreeQueue<ctx_message> basicQueue;
        int nTimes = 5;
        int nRepeatTimes = 5;
        int nMinThread = 1;
        int nMaxThread = 8;
        //Ä¬ÈÏ1·ÖÖÓ
        int nHeavyTestTime = 60 * 1000;
        if (argc >= 2) {
            argv[1];
        }
        if (nTimes <= 0 || nTimes > 100)
            nTimes = 3;
        if (nRepeatTimes <= 0 || nRepeatTimes >= 100)
            nRepeatTimes = 5;
        if (nMinThread <= 0 || nMinThread >= 32)
            nMinThread = 1;
        if (nMaxThread <= 0 || nMaxThread >= 256)
            nMaxThread = 8;
        if (nHeavyTestTime < 1000)
            nHeavyTestTime = 60 * 1000;
        printf("/*************************************************************************/\n");
        printf("Start CCLockfreeQueue\n");
        for (int i = 0; i < nTimes; i++) {
            if (!BenchmarkQueue<ctx_message, CCLockfreeQueue<ctx_message>>(basicQueue, nRepeatTimes, nMinThread, nMaxThread)) {
                printf("check fail!\n");
                break;
            }
        }
        printf("/*************************************************************************/\n");
        printf("Start CLockFreeMessageQueuePushPop\n");
        for (int i = 0; i < nTimes; i++) {
            if (!BenchmarkQueue<ctx_message, CLockFreeMessageQueuePushPop>(conMsgQueue, nRepeatTimes, nMinThread, nMaxThread)) {
                printf("check fail!\n");
                break;
            }
        }
        printf("/*************************************************************************/\n");
        printf("Start CCLockfreeQueue heavy\n");
        if (!BenchmarkQueueTime<ctx_message, CCLockfreeQueue<ctx_message>>(basicQueue, nHeavyTestTime, nMinThread, nMinThread)) {
            printf("check fail!\n");
        }
        printf("/*************************************************************************/\n");
    }
    
	getchar();
	return 0;
}

