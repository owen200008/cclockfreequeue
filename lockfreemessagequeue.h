#ifndef CCBASIC_LOCKFREEMESSAGEQUEUE_H
#define CCBASIC_LOCKFREEMESSAGEQUEUE_H

#include "concurrentqueue_alter/concurrentqueue.h"
#include <string.h>

template<size_t defaultBLOCKSize = 32>
class CBasicConcurrentQueueTraits : public moodycamel::ConcurrentQueueDefaultTraits
{
public:
	static const size_t BLOCK_SIZE = defaultBLOCKSize;
	static const size_t EXPLICIT_BLOCK_EMPTY_COUNTER_THRESHOLD = defaultBLOCKSize;
	//! (2的指数幂)最大分配次数, 这个值越大对象占用内存越大 sizeof（指针） * BASICQUEUE_MAX_ALLOCTIMES
	static const uint32_t BASICQUEUE_MAX_ALLOCTIMES = 16;
	//! (2的指数幂)每次分配增大的倍数
	static const size_t BASICLOCFREEQUEUE_ALLOCMULTYTIMES = 4;

	template<class... _Types>
	static inline void Trace(const char* pData, _Types&&... _Args){
#ifdef _DEBUG
		printf(pData, std::forward<_Types>(_Args)...);
#endif
	}
};

//nBlockSize必须为2的指数幂
template<class T, size_t nBlockSize = 64, class Traits = CBasicConcurrentQueueTraits<nBlockSize>>
class CLockFreeMessageQueue : public moodycamel::ConcurrentQueue<T, Traits>
{
public:
	typedef typename moodycamel::ConcurrentQueue<T, Traits>::Block LockFreeMsgBlock;

	struct AllocateIndexData
	{
		std::atomic<size_t> initialBlockPoolIndex;
		LockFreeMsgBlock* initialBlockPool;
		size_t initialBlockPoolSize;
		AllocateIndexData(int blockCount) : initialBlockPoolIndex(0){
			initialBlockPoolSize = blockCount;
			initialBlockPool = moodycamel::ConcurrentQueue<T, Traits>::create_array(blockCount);
			for (size_t i = 0; i < initialBlockPoolSize; ++i) {
				initialBlockPool[i].dynamicallyAllocated = false;
			}
		}
		~AllocateIndexData(){
			moodycamel::ConcurrentQueue<T, Traits>::destroy_array(initialBlockPool, initialBlockPoolSize);
		}
		LockFreeMsgBlock* GetBlock(){
			if (initialBlockPoolIndex.load(std::memory_order_relaxed) >= initialBlockPoolSize) {
				return nullptr;
			}
			auto index = initialBlockPoolIndex.fetch_add(1, std::memory_order_relaxed);
			return index < initialBlockPoolSize ? (initialBlockPool + index) : nullptr;
		}
	};
	CLockFreeMessageQueue(size_t capacity = nBlockSize) : moodycamel::ConcurrentQueue<T, Traits>(capacity),
		m_lock(0)
	{
		memset(m_pMaxAllocTimes, 0, Traits::BASICQUEUE_MAX_ALLOCTIMES * sizeof(AllocateIndexData*));

		m_nAllocateIndex = 0;
		m_pMaxAllocTimes[0] = new AllocateIndexData(capacity);

		moodycamel::ConcurrentQueue<T, Traits>::BindReleaseAfter([&]()->void{
			for (int i = 0; i < Traits::BASICQUEUE_MAX_ALLOCTIMES; i++){
				if (nullptr != m_pMaxAllocTimes[i])
					delete m_pMaxAllocTimes[i];
			}
		});
	}
	virtual ~CLockFreeMessageQueue(){
	}
	virtual LockFreeMsgBlock* ChildCreateBlock(){
		int nAllocateIndex = m_nAllocateIndex;
		if (nAllocateIndex >= Traits::BASICQUEUE_MAX_ALLOCTIMES)
			return nullptr;
		AllocateIndexData* pData = m_pMaxAllocTimes[nAllocateIndex];
		LockFreeMsgBlock* pRet = pData->GetBlock();
		if (pRet)
			return pRet;
		if (nAllocateIndex + 1 >= Traits::BASICQUEUE_MAX_ALLOCTIMES)
			return nullptr;
		while (m_lock.exchange(1)){};
		if (nAllocateIndex != m_nAllocateIndex){
			m_lock.exchange(0);
			return ChildCreateBlock();
		}
		m_pMaxAllocTimes[++m_nAllocateIndex] = new AllocateIndexData(pData->initialBlockPoolSize * Traits::BASICLOCFREEQUEUE_ALLOCMULTYTIMES);
		m_lock.exchange(0);
		Traits::Trace("expand_queue:%d\n", pData->initialBlockPoolSize * Traits::BASICLOCFREEQUEUE_ALLOCMULTYTIMES * sizeof(T));
		return ChildCreateBlock();
	}
protected:
	int															m_nAllocateIndex;
	AllocateIndexData*											m_pMaxAllocTimes[Traits::BASICQUEUE_MAX_ALLOCTIMES];
	std::atomic<char>											m_lock;
};

#endif
