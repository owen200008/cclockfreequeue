#pragma once
#include <stdlib.h>
#include <functional>
#include "headdefine.h"

unsigned long GetCheckTickTime();

class CBasicCalcUseTime {
public:
    CBasicCalcUseTime();
    virtual ~CBasicCalcUseTime();

    void Init(const std::function<void(DWORD dwUse, DWORD dwTotalUse)>& callback = nullptr);

    void StartCalc();

    void EncCalc();
    void EncCalc(const std::function<void(DWORD dwUse, DWORD dwTotalUse)>& callback);

    //手动调用回调
    void CallbackLastData();

    void ResetData();

    bool IsTimeEnough(DWORD dwUseTime, const std::function<void(DWORD dwUse, DWORD dwTotalUse)>& callback);
protected:
    bool    m_bStart = false;
    DWORD   m_dwBegin = 0;
    DWORD   m_dwUseTime = 0;
    DWORD   m_dwTotalUseTime = 0;
    std::function<void(DWORD dwUse, DWORD dwTotalUse)>  m_callback = nullptr;
};
//定义使用的宏
#define CreateCalcUseTime(calcName, callback, bStart)\
CBasicCalcUseTime calcName;\
calcName.Init(callback);\
if(bStart)\
    calcName.StartCalc();

#define StartCalcUseTime(calcName)\
calcName.StartCalc();

#define EndCalcUseTime(calcName)\
calcName.EncCalc();
#define EndCalcUseTimeCallback(calcName, callback)\
calcName.EncCalc(callback);

#define CallbackUseTime(calcName)\
calcName.CallbackLastData();

#define IsTimeEnoughUseTime(calcName, nUseTime, callback)\
calcName.IsTimeEnough(nUseTime, callback)
