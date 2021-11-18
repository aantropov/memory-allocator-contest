#include <unordered_map>

namespace  AntonShatalov
{

	typedef unsigned long long uint64;
	typedef unsigned int uint32;
	typedef unsigned char uint8;
	typedef long long int64;
	typedef int int32;
	typedef char int8;

	static const uint32 uint32_max = 0u - 1u;

	//#define ENABLE_DEBUG
	//#define DISABLE_POOLS_CLEAR
	//#define ENABLE_OUTPUT
	//#define DISABLE_DIRECT_MALLOC
	//#define ENABLE_MEMORY_TRACKING

	//defined min and max give best performance
#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

	template <class T> T logOfTwoCeil(T value)
	{
		if constexpr (sizeof(T) > 4)
		{
#ifdef _WIN64
			unsigned long index;
			if (_BitScanReverse64(&index, (uint64)value))
			{
				T result = index;
				if ((value & ~((T)1 << index)) > 0)
					++result;

				return result;
			}
			else
			{
				return (T)0;
			}
#else
			unsigned long index;
			T result; //don't touch that
			if (_BitScanReverse(&index, (uint32)(value >> 32)))
			{
				result = index + 32;
			}
			else
			{
				if (_BitScanReverse(&index, (uint32)(value & 0x00000000FFFFFFFF)))
					result = index;
				else
					return (T)0;
			}

			if ((value & ~((T)1 << index)) > 0)
				++result;

			return result;
#endif
		}
		else
		{
			unsigned long index;
			if (_BitScanReverse(&index, (uint32)value))
			{
				T result = index;
				if ((value & ~((T)1 << index)) > 0)
					++result;

				return result;
			}
			else
			{
				return (T)0;
			}
		}
	}

	std::string sizeToString(size_t size, uint32 width = 2)
	{
		static const char METRICS[5][3] = { "bt", "Kb", "Mb", "Gb", "Tb" };
		uint32 idx = 0;

		while (size >= (1 << 20))
		{
			size >>= 10;
			idx++;
		}

		char buf[64];
		if (size >= (1 << 10))
		{
			sprintf_s(buf, 64, "%.*f", 2, (double)size / (double)(1 << 10));
			return std::string(buf) + " " + METRICS[idx + 1];
		}
		else
		{
			sprintf_s(buf, 64, "%d", (int)size);
			return std::string(buf) + " " + METRICS[idx];
		}
	}

	template <bool X64_BIT, int MAX_LEVEL> class TPoolTree
	{
	private:
		template <bool X64_BIT_BRANCH> struct FHelper;
		template <> struct FHelper<true>
		{
			typedef uint64 NodeType;

			inline static unsigned char bitScanForward(unsigned long* index, NodeType mask)
			{
				return _BitScanForward64(index, mask);
			}
		};
		template <> struct FHelper<false>
		{
			typedef uint32 NodeType;

			inline static unsigned char bitScanForward(unsigned long* index, NodeType mask)
			{
				return _BitScanForward(index, mask);
			}
		};
		typedef typename FHelper<X64_BIT>::NodeType NodeType;

		inline static unsigned char bitScanForward(unsigned long* index, NodeType mask)
		{
			return FHelper<X64_BIT>::bitScanForward(index, mask);
		}

		static const int NODE_CAPACITY_BIT = X64_BIT ? 6 : 5;
		static const uint32 NODE_CAPACITY = 1u << NODE_CAPACITY_BIT;
		static const NodeType NODE_INIT_VALUE = X64_BIT ? 0xFFFFFFFFFFFFFFFF : 0xFFFFFFFF;
		static const int MAXIMAL_ALLOCS_SUPPORTED_BIT = NODE_CAPACITY_BIT * (MAX_LEVEL + 1);

	public:
		TPoolTree(const TPoolTree&) = delete;
		TPoolTree& operator = (const TPoolTree&) = delete;

		inline TPoolTree(TPoolTree&& other)
		{
			m_memory = nullptr;
			m_tree = nullptr;
			m_lastBite = nullptr;
			operator= (std::move(other));
		}

		inline TPoolTree& operator = (TPoolTree&& other)
		{
			clear();

			m_memory = other.m_memory;
			m_lastBite = other.m_lastBite;
			m_tree = other.m_tree;
			m_ptrDeviserBit = other.m_ptrDeviserBit;
			m_cachedNodeNum = other.m_cachedNodeNum;
			m_cachedNodeValue = other.m_cachedNodeValue;
			for (int i = 0; i <= MAX_LEVEL; ++i)
				m_levels[i] = other.m_levels[i];

			other.m_memory = nullptr;
			other.m_lastBite = nullptr;
			other.m_tree = nullptr;

			m_maximalCount = other.m_maximalCount;
			m_curCount = other.m_curCount;

#ifdef ENABLE_DEBUG
			m_levels[MAX_LEVEL + 1] = other.m_levels[MAX_LEVEL + 1];
#endif
			return *this;
		}

		inline TPoolTree(size_t allocSize, uint32 maximalCount)
		{
			m_ptrDeviserBit = (int)logOfTwoCeil(allocSize);

			m_curCount = 0;
			maximalCount = min(maximalCount, (1u << MAXIMAL_ALLOCS_SUPPORTED_BIT));
			m_maximalCount = maximalCount;
			m_memory = (uint8*)malloc((size_t)maximalCount << m_ptrDeviserBit);
			m_lastBite = m_memory + ((size_t)maximalCount << m_ptrDeviserBit) - 1;

			uint32 count = 0;
			for (int i = 0; i <= MAX_LEVEL; ++i)
			{
				m_levels[i] = count;

				uint32 offset = (NODE_CAPACITY_BIT * (MAX_LEVEL - i));
				uint32 allocsCount = (maximalCount + (1 << offset) - 1) >> offset;
				uint32 nodesCount = (allocsCount + NODE_CAPACITY - 1) >> NODE_CAPACITY_BIT;
				count += nodesCount;
			}
#ifdef ENABLE_DEBUG
			m_levels[MAX_LEVEL + 1] = count;
#endif

			m_tree = (NodeType*)malloc(sizeof(NodeType) * count);

			for (int i = 0; i <= MAX_LEVEL; ++i)
			{
				uint32 offset = (NODE_CAPACITY_BIT * (MAX_LEVEL - i));
				uint32 allocsCount = (maximalCount + (1 << offset) - 1) >> offset;
				uint32 nodesCount = (allocsCount + NODE_CAPACITY - 1) >> NODE_CAPACITY_BIT;

				for (uint32 j = 0; j < nodesCount; ++j)
				{
					uint32 step = min(allocsCount, NODE_CAPACITY);
					m_tree[m_levels[i] + j] = NODE_INIT_VALUE >> (NODE_CAPACITY - step);
					allocsCount -= step;
				}
#ifdef ENABLE_DEBUG
				if (allocsCount > 0)
					__debugbreak();
#endif
			}

			m_cachedNodeNum = m_levels[MAX_LEVEL];
			m_cachedNodeValue = m_tree[m_cachedNodeNum];
		}

		inline ~TPoolTree()
		{
			clear();
		}

		inline void clear()
		{
			if (m_memory)
			{
				free(m_memory);
				m_memory = nullptr;
				m_lastBite = nullptr;
			}
			if (m_tree)
			{
				free(m_tree);
				m_tree = nullptr;
			}
		}

		inline uint8* alloc()
		{
			if (!valid())
				return nullptr;

			uint32 offset = 0;
			if (consume<0>(offset) == EConsume::Fail)
				return nullptr;

#ifdef ENABLE_DEBUG
			if (offset >= m_maximalCount)
				__debugbreak();
#endif

			++m_curCount;
			return m_memory + (offset << m_ptrDeviserBit);
		}

		inline void dealloc(uint8* ptr)
		{
#ifdef ENABLE_DEBUG
			if (!contains(ptr))
				__debugbreak();
#endif

			uint32 offset = (uint32)((ptr - m_memory) >> m_ptrDeviserBit);
			giveBack<0>(offset);
			--m_curCount;
		}

		inline bool contains(uint8* ptr) const
		{
			return ptr >= m_memory && ptr <= m_lastBite;
		}

		inline uint8* getFirstBite() const
		{
			return m_memory;
		}

		inline uint8* getLastBite() const
		{
			return m_lastBite;
		}

		inline uint32 getMaximalAllocsCount() const
		{
			return m_maximalCount;
		}

		inline int getPtrDiviserBit() const
		{
			return m_ptrDeviserBit;
		}

		inline bool empty() const
		{
			return m_curCount == 0;
		}

		inline bool valid() const
		{
			return m_memory;
		}

		inline size_t getMemorySize() const
		{
			return (size_t)(m_lastBite - m_memory + 1);
		}

		constexpr static int GetMaximalAllocsSupportedBit()
		{
			return MAXIMAL_ALLOCS_SUPPORTED_BIT;
		}

#ifdef ENABLE_MEMORY_TRACKING
		inline uint32 getCurrentAllocationsCount() const
		{
			return m_curCount;
		}
#endif

	private:
		enum class EConsume : uint32
		{
			Fail,
			Full,
			Ok,
		};

		enum class EGiveBack : uint32
		{
			WasFull,
			Ok,
		};

		uint8* m_memory;
		uint8* m_lastBite;
		NodeType* m_tree;
#ifdef ENABLE_DEBUG
		uint32 m_levels[MAX_LEVEL + 2];
#else
		uint32 m_levels[MAX_LEVEL + 1];
#endif
		int m_ptrDeviserBit;
		uint32 m_cachedNodeNum;
		NodeType m_cachedNodeValue;
		uint32 m_maximalCount;
		uint32 m_curCount;

		template<int CALL_LEVEL> inline EConsume consume(uint32& offset)
		{
			unsigned long subIndex;

			//try to use cached node
			if constexpr (CALL_LEVEL == 0)
			{
				if (bitScanForward(&subIndex, m_cachedNodeValue) > 0)
				{
					m_cachedNodeValue &= ~(1llu << subIndex);

					if (m_cachedNodeValue > 0)
					{
						offset = ((m_cachedNodeNum - m_levels[MAX_LEVEL]) << NODE_CAPACITY_BIT) | subIndex;
						m_tree[m_cachedNodeNum] = m_cachedNodeValue;
						return EConsume::Ok;
					}
				}
			}

			//normal allocation
			uint32 num = m_levels[CALL_LEVEL] + offset;
#ifdef ENABLE_DEBUG
			if (num < m_levels[CALL_LEVEL] || num >= m_levels[CALL_LEVEL + 1])
				__debugbreak();
#endif

			NodeType node = m_tree[num];

			if (bitScanForward(&subIndex, node) > 0)
			{
				offset = (offset << NODE_CAPACITY_BIT) | subIndex;

				if constexpr (CALL_LEVEL < MAX_LEVEL)
				{
					EConsume result = consume<CALL_LEVEL + 1>(offset);

					if (result == EConsume::Full)
					{
						node &= ~(1llu << subIndex);
						m_tree[num] = node;

						return node == 0 ? EConsume::Full : EConsume::Ok;
					}
					else
					{
						return result;
					}
				}
				else
				{
					node &= ~(1llu << subIndex);
					m_tree[num] = node;

					m_cachedNodeValue = node;
					m_cachedNodeNum = num;

					return node == 0 ? EConsume::Full : EConsume::Ok;
				}
			}
			else
			{
				return EConsume::Fail;
			}
		}

		template<int CALL_LEVEL> inline EGiveBack giveBack(const uint32& offset)
		{
			if constexpr (CALL_LEVEL < MAX_LEVEL)
			{
				if (giveBack<CALL_LEVEL + 1>(offset) == EGiveBack::WasFull)
				{
					constexpr uint32 SUB_OFFSET = NODE_CAPACITY_BIT * (MAX_LEVEL - CALL_LEVEL);
					unsigned long subIndex = (offset & ((NODE_CAPACITY - 1) << SUB_OFFSET)) >> SUB_OFFSET;

					uint32 num = offset >> (SUB_OFFSET + NODE_CAPACITY_BIT);
					num += m_levels[CALL_LEVEL];
#ifdef ENABLE_DEBUG
					if (num < m_levels[CALL_LEVEL] || num >= m_levels[CALL_LEVEL + 1])
						__debugbreak();
#endif

					NodeType node = m_tree[num];
					EGiveBack result = node == 0 ? EGiveBack::WasFull : EGiveBack::Ok;
#ifdef ENABLE_DEBUG
					if (node & (1llu << subIndex))
						__debugbreak();
#endif
					node |= (1llu << subIndex);
					m_tree[num] = node;

					return result;
				}
				else
				{
					return EGiveBack::Ok;
				}
			}
			else
			{
				constexpr uint32 SUB_OFFSET = NODE_CAPACITY_BIT * (MAX_LEVEL - CALL_LEVEL);
				unsigned long subIndex = (offset & ((NODE_CAPACITY - 1) << SUB_OFFSET)) >> SUB_OFFSET;

				uint32 num = offset >> (NODE_CAPACITY_BIT + SUB_OFFSET);
				num += m_levels[CALL_LEVEL];
#ifdef ENABLE_DEBUG
				if (num < m_levels[CALL_LEVEL] || num >= m_levels[CALL_LEVEL + 1])
					__debugbreak();
#endif

				NodeType node = m_tree[num];
				EGiveBack result = node == 0 ? EGiveBack::WasFull : EGiveBack::Ok;
				node |= (1llu << subIndex);
				m_tree[num] = node;

				return result;
			}
		}
	};

	template <bool X64_BIT> class TMultiPoolTree
	{
	private:
		typedef TPoolTree<X64_BIT, X64_BIT ? 2 : 3> FPoolTree;

		class FInnerPool : public FPoolTree
		{
		public:
			FInnerPool(const FInnerPool&) = delete;
			FInnerPool& operator = (const FInnerPool&) = delete;

			inline FInnerPool(FInnerPool&& other) :
				FPoolTree(std::move(*(FPoolTree*)&other))
			{
				m_sizedIndex = other.m_sizedIndex;
				m_availability = other.m_availability;

#ifdef ENABLE_MEMORY_TRACKING
				m_locatedSize = other.m_locatedSize;
				m_pointers = std::move(other.m_pointers);
#endif
			}

			inline FInnerPool& operator = (FInnerPool&& other)
			{
				*(FPoolTree*)this = std::move(*(FPoolTree*)&other);
				m_sizedIndex = other.m_sizedIndex;
				m_availability = other.m_availability;

#ifdef ENABLE_MEMORY_TRACKING
				m_locatedSize = other.m_locatedSize;
				m_pointers = std::move(other.m_pointers);
#endif

				return *this;
			}

			inline FInnerPool(size_t allocSize, uint32 maximalCount, int sizedIndex) :
				FPoolTree(allocSize, maximalCount)
			{
				m_sizedIndex = sizedIndex;
				m_availability = true;

#ifdef ENABLE_MEMORY_TRACKING
				m_locatedSize = 0;
#endif    
			}

			inline int getSizedIndex() const
			{
				return m_sizedIndex;
			}

			inline bool available() const
			{
				return m_availability;
			}

			inline void setAvailability(bool availability)
			{
				m_availability = availability;
			}

#ifdef ENABLE_MEMORY_TRACKING
			inline void registerPointer(uint8* ptr, size_t size)
			{
				m_pointers.insert({ ptr, size });
				m_locatedSize += size;
			}

			inline void unregisterPointer(uint8* ptr)
			{
				auto it = m_pointers.find(ptr);
				m_locatedSize -= it->second;
				m_pointers.erase(it);
			}

			inline size_t getLocatedSize() const
			{
				return m_locatedSize;
			}
#endif

		private:
			int m_sizedIndex;
			bool m_availability;

#ifdef ENABLE_MEMORY_TRACKING
			std::unordered_map<uint8*, size_t> m_pointers;
			size_t m_locatedSize;
#endif
		};

		static const int MIN_ALLOC_SIZE_BIT = 0;
		static const size_t MAXIMAL_MEMORY_SEGMENT = (size_t)0 - 1;
		static const int MINIMAL_POOL_SIZE_BIT = min(18, FInnerPool::GetMaximalAllocsSupportedBit() + MIN_ALLOC_SIZE_BIT);
		static const size_t MINIMAL_POOL_SIZE = 1 << MINIMAL_POOL_SIZE_BIT;
		static const int MAXIMAL_POOL_SIZE_BIT = max(MINIMAL_POOL_SIZE_BIT, 7 + 10 + 10); //128 Mb
		static const size_t MAXIMAL_POOL_SIZE = (size_t)1 << MAXIMAL_POOL_SIZE_BIT;

		static const size_t MAXIMAL_MEMORY_ADDRESS = sizeof(size_t) == sizeof(uint64) ? (1llu << 48) - 1 : 0xFFFFFFFF;
		static const size_t LOOKUP_CAPACITY_REQUIRED = ((uint64)MAXIMAL_MEMORY_ADDRESS + MINIMAL_POOL_SIZE - 1) >> MINIMAL_POOL_SIZE_BIT;
		static const size_t MAXIMAL_INT_VALUE = ((1 << 30) - 1) + (1 << 30); //for 32 bit version

		static const size_t POOL_FORCE_CLEAR_LIMIT = 8 << (10 + 10); //8 Mb

		template <bool WIDE_RANGE> struct FHelper;
		template <> struct FHelper<true>
		{
			typedef int64 LookUpIndexType;
		};
		template <> struct FHelper<false>
		{
			typedef int LookUpIndexType;
		};
		typedef typename FHelper < MAXIMAL_INT_VALUE < LOOKUP_CAPACITY_REQUIRED>::LookUpIndexType LookUpIndexType;

	public:
		TMultiPoolTree(const TMultiPoolTree&) = delete;
		TMultiPoolTree& operator = (const TMultiPoolTree&) = delete;

		inline TMultiPoolTree()
		{
			m_lookUpFirst = 1;
			m_lookUpLast = -1;
			m_lookUpCurCount = 8;
			m_lookUp = nullptr;

			m_sizeds.resize((int)logOfTwoCeil(MAXIMAL_MEMORY_SEGMENT) - MIN_ALLOC_SIZE_BIT);
			for (uint32 i = 0; i < m_sizeds.size(); ++i)
			{
				FSized& sized = m_sizeds[i];
				sized.lastPool = uint32_max;
				sized.validPools = 0;
				sized.totalPools = 0;
			}
		}

		inline ~TMultiPoolTree()
		{
			if (m_lookUp)
				free(m_lookUp);
		}

		uint8* alloc(size_t size, size_t alignment)
		{
			int sizedIndex = max((int)logOfTwoCeil(size) - MIN_ALLOC_SIZE_BIT, 0);
#ifdef ENABLE_DEBUG
			if (sizedIndex >= (int)m_sizeds.size())
				__debugbreak();
#endif

			//try to alloc from last used pool in choosen sized
			FSized& sized = m_sizeds[sizedIndex];
			if (sized.lastPool != uint32_max)
			{
				uint8* ptr = m_pools[sized.lastPool].alloc();
				if (ptr)
				{
#ifdef ENABLE_MEMORY_TRACKING
					m_pools[sized.lastPool].registerPointer(ptr, size);
#endif
					return ptr;
				}
			}

			//try to alloc from any available pool in choosen sized
			while (!sized.availablePools.empty())
			{
				sized.lastPool = sized.availablePools.back();
				uint8* ptr = m_pools[sized.lastPool].alloc();

				if (ptr)
				{
#ifdef ENABLE_MEMORY_TRACKING
					m_pools[sized.lastPool].registerPointer(ptr, size);
#endif
					return ptr;
				}
				else
				{
					sized.availablePools.pop_back();
					m_pools[sized.lastPool].setAvailability(false);
				}
			}

#ifndef DISABLE_DIRECT_MALLOC
			if (size > MAXIMAL_POOL_SIZE)
				return (uint8*)malloc(size);
#endif

			{
				int allocSizeBit = MIN_ALLOC_SIZE_BIT + sizedIndex;
				int totalMemoryBit = min(MAXIMAL_POOL_SIZE_BIT, MINIMAL_POOL_SIZE_BIT + sized.totalPools);
				uint32 allocsCount = (uint32)(1 << max(totalMemoryBit - allocSizeBit, 0));
				FInnerPool newPool = FInnerPool((size_t)1 << allocSizeBit, allocsCount, sizedIndex);

				if (sized.freePools.empty())
				{
					++sized.totalPools;
					sized.lastPool = (uint32)m_pools.size();
					sized.availablePools.push_back(sized.lastPool);

					m_pools.push_back(std::move(newPool));
				}
				else
				{
					sized.lastPool = (uint32)sized.freePools.back();
					sized.availablePools.push_back(sized.lastPool);
					sized.freePools.pop_back();

					m_pools[sized.lastPool] = std::move(newPool);
				}
			}

			FInnerPool& newPool = m_pools[sized.lastPool];
			LookUpIndexType minIndex = (LookUpIndexType)(((size_t)newPool.getFirstBite() >> MINIMAL_POOL_SIZE_BIT) << 1);
			LookUpIndexType maxIndex = (LookUpIndexType)(((size_t)newPool.getLastBite() >> MINIMAL_POOL_SIZE_BIT) << 1);

			if (minIndex < m_lookUpFirst || maxIndex > m_lookUpLast)
			{
				LookUpIndexType mostMin = minIndex;
				LookUpIndexType mostMax = maxIndex;
				if (m_lookUp)
				{
					mostMin = min(mostMin, m_lookUpFirst);
					mostMax = max(mostMax, m_lookUpLast);
				}
				LookUpIndexType width = mostMax - mostMin + 2;
				m_lookUpCurCount = max(m_lookUpCurCount, width);

				LookUpIndexType bestMin = (((mostMin + mostMax - width) >> 2) << 1) + 2; //add 2 as maximal error correction
				bestMin = min(bestMin, mostMin); //compensation here if adding 2 was wrong
#ifdef ENABLE_DEBUG
				LookUpIndexType bestMax = bestMin + width - 2;
				if (bestMin > mostMin || bestMax < mostMax)
					__debugbreak();
#endif

				uint32* newLookUp = (uint32*)malloc(sizeof(uint32) * m_lookUpCurCount);

				LookUpIndexType offset = 0;
				LookUpIndexType count = 0;
				if (m_lookUp)
				{
					offset = m_lookUpFirst - bestMin;
					count = m_lookUpLast - m_lookUpFirst + 2;
					memcpy(newLookUp + offset, m_lookUp, sizeof(uint32) * count);
					free(m_lookUp);
				}

				for (LookUpIndexType i = 0; i < offset; ++i)
					newLookUp[i] = uint32_max;
				offset += count;
				for (LookUpIndexType i = offset; i < m_lookUpCurCount; ++i)
					newLookUp[i] = uint32_max;

				m_lookUp = newLookUp;
				m_lookUpFirst = bestMin;
				m_lookUpLast = m_lookUpFirst + m_lookUpCurCount - 2;

				m_lookUpCurCount = max(m_lookUpCurCount, m_lookUpCurCount << (m_lookUpCurCount < 1024 ? 3 : 1));
				m_lookUpCurCount = min((LookUpIndexType)LOOKUP_CAPACITY_REQUIRED, m_lookUpCurCount);
			}

			++sized.validPools;
			registerPool(sized.lastPool, minIndex, maxIndex);
#ifdef ENABLE_MEMORY_TRACKING
			{
				uint8* ptr = newPool.alloc();
				newPool.registerPointer(ptr, size);
				return ptr;
			}
#else
			return newPool.alloc();
#endif
		}

		inline void dealloc(uint8* ptr)
		{
			LookUpIndexType index = (LookUpIndexType)(((size_t)ptr >> MINIMAL_POOL_SIZE_BIT) << 1);
#ifndef DISABLE_DIRECT_MALLOC
			if (index < m_lookUpFirst || index > m_lookUpLast)
			{
				free(ptr);
				return;
			}
#else
#ifdef ENABLE_DEBUG
			if (index < m_lookUpFirst || index > m_lookUpLast)
				__debugbreak();
#endif
#endif
			index -= m_lookUpFirst;

			for (uint32 i = 0; i < 2; ++i)
			{
				uint32 poolNum = m_lookUp[index + i];
				if (poolNum != uint32_max && m_pools[poolNum].contains(ptr))
				{
					FInnerPool& pool = m_pools[poolNum];
					pool.dealloc(ptr);
#ifdef ENABLE_MEMORY_TRACKING
					pool.unregisterPointer(ptr);
#endif

					FSized& sized = m_sizeds[pool.getSizedIndex()];
#ifndef DISABLE_POOLS_CLEAR
					bool cleared = false;
					if (pool.empty())
					{
						if ((sized.validPools > 1 || pool.getMemorySize() >= POOL_FORCE_CLEAR_LIMIT))
						{
							sized.freePools.push_back(poolNum);
							--sized.validPools;
							unregisterPool(poolNum, (LookUpIndexType)(((size_t)pool.getFirstBite() >> MINIMAL_POOL_SIZE_BIT) << 1), (LookUpIndexType)(((size_t)pool.getLastBite() >> MINIMAL_POOL_SIZE_BIT) << 1));
							pool.clear();
							cleared = true;
						}
					}
					if (!cleared)
#endif
					{
						if (!pool.available())
						{
							pool.setAvailability(true);
							sized.availablePools.push_back(poolNum);
							//          sized.lastPool = poolNum; //we don't do that here
						}
					}

					return;
				}
			}
		}

		inline void debugOutput() const
		{
#ifdef ENABLE_OUTPUT
			std::vector<std::vector<uint32 > > poolsLookUp;
			poolsLookUp.resize(m_sizeds.size());
			for (size_t i = 0; i < m_pools.size(); ++i)
			{
				//!!!!
				if (m_pools[i].valid())
					poolsLookUp[m_pools[i].getSizedIndex()].push_back((uint32)i);
			}

			printf("\n\nMultiPoolTree:\n");
			for (size_t i = 0; i < m_sizeds.size(); ++i)
			{
				const FSized& sized = m_sizeds[i];
				std::vector<uint32>& sizedPools = poolsLookUp[i];

				if (!sizedPools.empty())
				{
					printf("    Sized: %s\n", sizeToString((size_t)1 << (MIN_ALLOC_SIZE_BIT + i)).c_str());

					printf("        Pools %d:\n", (int)sizedPools.size());
					for (size_t j = 0; j < sizedPools.size(); ++j)
					{
						const FInnerPool& pool = m_pools[sizedPools[j]];
						printf("            Pool %d:\n", (int)sizedPools[j]);
						printf("                Valid        : %s\n", pool.valid() ? "yes" : "no");
						printf("                Available    : %s\n", pool.available() ? "yes" : "no");
						printf("                Max allocs   : %d\n", (int)pool.getMaximalAllocsCount());
						printf("                Mem size     : %s\n", sizeToString(pool.getMemorySize()).c_str());
						printf("                Located size : %s\n", sizeToString(pool.getLocatedSize()).c_str());
						printf("                Used size    : %s\n", sizeToString(pool.getCurrentAllocationsCount() * ((size_t)1 << (MIN_ALLOC_SIZE_BIT + i))).c_str());
					}

					printf("        Available %d\n", (int)sized.availablePools.size());
				}
			}

			printf("LookUp:\n");
			printf("    First           : %d\n", (int)(m_lookUpFirst >> 1));
			printf("    Last            : %d\n", (int)(m_lookUpLast >> 1));
			printf("    Width           : %d\n", (int)((m_lookUpLast - m_lookUpFirst + 2) >> 1));
			printf("    Mem size        : %s\n", sizeToString((m_lookUpLast - m_lookUpFirst + 2) * sizeof(uint32)).c_str());
			printf("    Maximal mem size: %s\n", sizeToString((LOOKUP_CAPACITY_REQUIRED * sizeof(uint32) << 1)).c_str());

			size_t count = (m_lookUpLast - m_lookUpFirst + 2) >> 1;
			uint32 storedIndices = 0;
			for (size_t i = 0; i < count; ++i)
			{
				uint32 index0 = m_lookUp[(i << 1) + 0];
				uint32 index1 = m_lookUp[(i << 1) + 1];
				if (index0 != uint32_max)
					++storedIndices;
				if (index1 != uint32_max)
					++storedIndices;
			}

			/*    printf("    Stored indices  : %d\n", (int)storedIndices);
				for (size_t i = 0; i < count; ++i)
				{
				  uint32 index0 = m_lookUp[(i << 1) + 0];
				  uint32 index1 = m_lookUp[(i << 1) + 1];
				  if (index0 != uint32_max || index1 != uint32_max)
				  {
					printf("        %d: ", (int)i);
					if (index0 != uint32_max)
					  printf("%d / ", (int)index0);
					else
					  printf("x / ");
					if (index1 != uint32_max)
					  printf("%d\n", (int)index1);
					else
					  printf("x\n");
				  }
				}*/
#endif
		}

	private:
		struct FSized
		{
			std::vector<uint32> availablePools;
			std::vector<uint32> freePools; //dont' move that. it's connected to availablePools
			uint32 lastPool;
			uint32 validPools;
			int totalPools;
		};

		uint32* m_lookUp;
		LookUpIndexType m_lookUpFirst;
		LookUpIndexType m_lookUpLast;
		LookUpIndexType m_lookUpCurCount;

		std::vector<FInnerPool> m_pools;
		std::vector<FSized> m_sizeds;

		inline void registerPool(uint32 poolNum, LookUpIndexType minIndex, LookUpIndexType maxIndex)
		{
			for (LookUpIndexType i = minIndex; i <= maxIndex; i += 2)
			{
				LookUpIndexType index = i;
#ifdef ENABLE_DEBUG
				if (index < m_lookUpFirst || index > m_lookUpLast)
					__debugbreak();
#endif
				index -= m_lookUpFirst;

				if (m_lookUp[index] == uint32_max)
				{
					m_lookUp[index] = poolNum;
				}
				else
				{
#ifdef ENABLE_DEBUG
					if (m_lookUp[index + 1] != uint32_max)
						__debugbreak();
#endif
					m_lookUp[index + 1] = poolNum;
				}
			}
		}

		inline void unregisterPool(uint32 poolNum, LookUpIndexType minIndex, LookUpIndexType maxIndex)
		{
			for (LookUpIndexType i = minIndex; i <= maxIndex; i += 2)
			{
				LookUpIndexType index = i;
#ifdef ENABLE_DEBUG
				if (index < m_lookUpFirst || index > m_lookUpLast)
					__debugbreak();
#endif
				index -= m_lookUpFirst;

				if (m_lookUp[index] == poolNum)
				{
					m_lookUp[index] = uint32_max;
				}
				else
				{
#ifdef ENABLE_DEBUG
					if (m_lookUp[index + 1] != poolNum)
						__debugbreak();
#endif
					m_lookUp[index + 1] = uint32_max;
				}
			}
		}
	};

	class Ololokator
	{
	public:
		inline Ololokator()
		{

		}

		inline ~Ololokator()
		{
		}

		void* Allocate(size_t size, size_t alignment)
		{
			return m_multiPoolTree.alloc(size, alignment);
		}

		inline void Free(void* ptr)
		{
			m_multiPoolTree.dealloc((uint8*)ptr);
		}

		inline size_t GetOccupiedSpace() const
		{
			return 0;
		}

		inline void debugOutput() const
		{
			m_multiPoolTree.debugOutput();
		}

	private:
#ifdef _WIN64
		static const bool WIN64_BIT = true;
#else
		static const bool WIN64_BIT = false;
#endif
		TMultiPoolTree<WIN64_BIT> m_multiPoolTree;
	};
}