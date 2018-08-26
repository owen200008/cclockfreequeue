#include "debug.h"


unsigned long GetCheckTickTime() {
#ifdef _WIN32
    LARGE_INTEGER lFreq, lCounter;
    QueryPerformanceFrequency(&lFreq);
    QueryPerformanceCounter(&lCounter);
    return unsigned long((((double)lCounter.QuadPart) / lFreq.QuadPart) * 1000);
#else

    struct timeval tp;
    gettimeofday(&tp, NULL);
    return tp.tv_sec * 1000 + tp.tv_usec / 1000;
#endif
}
//////////////////////////////////////////////////////////////////////////////////////////////////
CBasicCalcUseTime::CBasicCalcUseTime() {
}
void CBasicCalcUseTime::Init(const std::function<void(DWORD dwUse, DWORD dwTotalUse)>& callback) {
    m_callback = callback;
}

CBasicCalcUseTime::~CBasicCalcUseTime() {
    if (m_bStart) {
        EncCalc();
    }
}

void CBasicCalcUseTime::StartCalc() {
    m_dwBegin = GetCheckTickTime();
    m_bStart = true;
}

void CBasicCalcUseTime::EncCalc() {
    EncCalc(m_callback);
}

void CBasicCalcUseTime::EncCalc(const std::function<void(DWORD dwUse, DWORD dwTotalUse)>& callback) {
    if (m_bStart) {
        m_dwUseTime = GetCheckTickTime() - m_dwBegin;
        m_dwTotalUseTime += m_dwUseTime;
        m_bStart = false;

        if (callback != nullptr) {
            callback(m_dwUseTime, m_dwTotalUseTime);
        }
    }
}

//手动调用回调
void CBasicCalcUseTime::CallbackLastData() {
    if (m_callback != nullptr) {
        m_callback(m_dwUseTime, m_dwTotalUseTime);
    }
}

void CBasicCalcUseTime::ResetData() {
    m_dwUseTime = 0;
    m_dwTotalUseTime = 0;
}

bool CBasicCalcUseTime::IsTimeEnough(DWORD dwUseTime, const std::function<void(DWORD dwUse, DWORD dwTotalUse)>& callback) {
    if (m_bStart) {
        DWORD dwUse = GetCheckTickTime() - m_dwBegin;
        bool bRet = (dwUse >= dwUseTime);
        if (bRet && callback != nullptr) {
            callback(dwUse, m_dwTotalUseTime + dwUse);
        }
        return bRet;
    }
    return false;
}
