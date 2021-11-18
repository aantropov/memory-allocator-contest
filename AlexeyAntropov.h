#include <memory>
#include <vector>
#include <cstdlib>
#include <memory>
#include <stdint.h>
#include <cassert>
#define InvalidIndexUINT64 UINT64_MAX

extern size_t TotalUsedSpace;

namespace AlexeyAntropov
{
	namespace Sailor::Memory
	{
		namespace Internal
		{
			class PoolAllocator
			{
			public:

				PoolAllocator(size_t startPageSize = 8000) : m_pageSize(startPageSize) {}

				struct Header
				{
					size_t m_next = InvalidIndexUINT64;
					size_t m_nextFree = InvalidIndexUINT64;
					size_t m_prev = InvalidIndexUINT64;
					size_t m_prevFree = InvalidIndexUINT64;
					size_t m_pageIndex = 0;
					size_t m_size = 0;
					bool bIsFree : 1;
					uint8_t m_meta;
				};

				class Page
				{
				public:

					size_t m_totalSize = InvalidIndexUINT64;
					size_t m_occupiedSpace = InvalidIndexUINT64;
					void* m_pData = nullptr;
					size_t m_firstFree = InvalidIndexUINT64;
					size_t m_first = InvalidIndexUINT64;
					bool bIsInFreeList = true;

					bool IsEmpty() const { return m_occupiedSpace == sizeof(Header); }
					inline Header* MoveHeader(Header* block, int64_t shift);

					void* Allocate(size_t size, size_t alignment);
					void Free(void* pData);

					void Clear();

					size_t GetMinAllowedEmptySpace() const;
				};

				void* Allocate(size_t size, size_t alignment);
				void Free(void* ptr);

				size_t GetOccupiedSpace() const
				{
					return 0;
				}
				~PoolAllocator();

			private:

				bool RequestPage(Page& page, size_t size, size_t pageIndex) const;

				const size_t m_pageSize = 4096;

				std::vector<Page> m_pages;
				std::vector<size_t> m_freeList;
				std::vector<size_t> m_emptyPages;
			};

			class SmallPoolAllocator
			{
			public:

				struct SmallHeader
				{
					uint16_t m_pageIndex = 0;
					uint16_t m_id = 0;
					uint8_t m_size = 0;
					uint8_t m_meta = 0;
				};

				class SmallPage
				{
				public:

					static constexpr  uint32_t m_size = 65536;

					SmallPage() = default;
					SmallPage(uint8_t blockSize, uint16_t pageIndex);

					uint16_t m_numAllocs = 0;
					uint16_t m_pageIndex = 0;
					void* m_pData = nullptr;
					uint8_t m_blockSize = 0;
					bool m_bIsInFreeList = true;
					std::vector<uint16_t> m_freeList;

					void* Allocate();
					void Free(void* ptr);
					void Clear();

					size_t GetMaxBlocksNum() const;
					uint16_t GetOccupiedSpace() const;

					bool IsFull() const;
					bool IsEmpty() const;
				};

				SmallPoolAllocator(uint8_t blockSize) : m_blockSize(blockSize) {}
				~SmallPoolAllocator();

				bool RequestPage(SmallPage& page, uint8_t blockSize, uint16_t pageIndex) const;

				void* Allocate();
				void Free(void* ptr);

			private:

				uint8_t m_blockSize = 0;
				std::vector<SmallPage> m_pages;
				std::vector<uint16_t> m_freeList;
				std::vector<uint16_t> m_emptyPages;
			};
		}

		class HeapAllocator
		{
		public:

			HeapAllocator();
			void* Allocate(size_t size, size_t alignment);
			void Free(void* ptr);

		private:

			inline size_t CalculateAlignedSize(size_t blockSize) const;
			std::vector<std::unique_ptr<Internal::SmallPoolAllocator>> m_smallAllocators;
			Internal::PoolAllocator m_allocator;
		};

#include <cstdlib>
#include <memory>
#include <stdint.h>
#include <cassert>

		using namespace Sailor;
		using namespace Sailor::Memory;
		using namespace Sailor::Memory::Internal;
		using namespace std;

		using SmallPage = Memory::Internal::SmallPoolAllocator::SmallPage;
		using Page = Memory::Internal::PoolAllocator::Page;
		using Header = Memory::Internal::PoolAllocator::Header;

#define SAILOR_SMALLEST_DATA_SIZE (255ull + sizeof(Header))

#define ShiftPtr(ptr, numBytes) (void*)(&(((uint8_t*)ptr)[numBytes]))
#define Offset(to, from) ((int64_t)to - (int64_t)from)
#define GetHeaderPtr(pPage, pBlock, member) (Header*)( pBlock->#member != InvalidIndexUINT64 ? ShiftPtr(pPage, pBlock->#member) : nullptr)

		bool PoolAllocator::RequestPage(Page& page, size_t size, size_t pageIndex) const
		{
			page.m_totalSize = size;
			page.m_occupiedSpace = sizeof(Header);
			page.m_pData = malloc(size);
			page.m_firstFree = 0;
			page.m_first = 0;
			page.bIsInFreeList = true;

			if (!page.m_pData)
			{
				return false;
			}

			Header* firstFree = static_cast<Header*>(page.m_pData);

			firstFree->bIsFree = 1;
			firstFree->m_next = InvalidIndexUINT64;
			firstFree->m_nextFree = InvalidIndexUINT64;
			firstFree->m_prev = InvalidIndexUINT64;
			firstFree->m_prevFree = InvalidIndexUINT64;
			firstFree->m_pageIndex = pageIndex;
			firstFree->m_size = size - sizeof(Header);

			return true;
		}

		void Page::Clear()
		{
			free(m_pData);
			m_pData = nullptr;
			m_totalSize = m_occupiedSpace = 0;
		}

		size_t PoolAllocator::Page::GetMinAllowedEmptySpace() const
		{
			return (std::min)(2048ull, (std::max)((size_t)(m_totalSize * 0.05f), SAILOR_SMALLEST_DATA_SIZE * 2ull));
		}

		Header* Page::MoveHeader(Header* block, int64_t shift)
		{
			const size_t headerSize = sizeof(Header);

			Header* pPrevFree = static_cast<Header*>(block->m_prevFree != InvalidIndexUINT64 ? ShiftPtr(m_pData, block->m_prevFree) : nullptr);
			Header* pPrev = static_cast<Header*>(block->m_prev != InvalidIndexUINT64 ? ShiftPtr(m_pData, block->m_prev) : nullptr);
			Header* pNextFree = static_cast<Header*>(block->m_nextFree != InvalidIndexUINT64 ? ShiftPtr(m_pData, block->m_nextFree) : nullptr);
			Header* pNext = static_cast<Header*>(block->m_next != InvalidIndexUINT64 ? ShiftPtr(m_pData, block->m_next) : nullptr);

			Header* oldBlock = block;
			block = static_cast<Header*>(ShiftPtr(oldBlock, shift));
			memmove(block, oldBlock, headerSize);

			block->m_size -= shift;

			// we merge free space to left block
			if (pPrev && pPrev != block)
			{
				pPrev->m_size += shift;
				pPrev->m_next += shift;
			}

			if (pNext && pNext != block)
			{
				pNext->m_prev += shift;
			}

			if (pPrevFree && pPrevFree != block)
			{
				pPrevFree->m_nextFree += shift;
			}

			if (pNextFree && pNextFree != block)
			{
				pNextFree->m_prevFree += shift;
			}

			// that's the first block
			if (!pPrev)
			{
				m_first = Offset(block, m_pData);
			}

			if (block->bIsFree && !pPrevFree)
			{
				m_firstFree = Offset(block, m_pData);
			}

			return block;
		}

		void* Page::Allocate(size_t size, size_t alignment)
		{
			if (m_firstFree == InvalidIndexUINT64)
			{
				return nullptr;
			}

			const size_t headerSize = sizeof(Header);

			Header* freeBlock = static_cast<Header*>(ShiftPtr(m_pData, m_firstFree));
			do
			{
				assert(freeBlock->bIsFree);

				void* pOldStartData = ShiftPtr(freeBlock, headerSize);
				void* pNewStartData = pOldStartData;

				size_t roomSize = freeBlock->m_size;

				//we sure that we can place
				if (std::align(alignment, size, pNewStartData, roomSize))
				{
					Header* pPrevFree = static_cast<Header*>(freeBlock->m_prevFree != InvalidIndexUINT64 ? ShiftPtr(m_pData, freeBlock->m_prevFree) : nullptr);
					Header* pNextFree = static_cast<Header*>(freeBlock->m_nextFree != InvalidIndexUINT64 ? ShiftPtr(m_pData, freeBlock->m_nextFree) : nullptr);
					Header* pNext = static_cast<Header*>(freeBlock->m_next != InvalidIndexUINT64 ? ShiftPtr(m_pData, freeBlock->m_next) : nullptr);
					Header* pPrev = static_cast<Header*>(freeBlock->m_prev != InvalidIndexUINT64 ? ShiftPtr(m_pData, freeBlock->m_prev) : nullptr);

					// we have extra free space left after alignment
					const int64_t freeSpaceLeft = Offset(pNewStartData, pOldStartData);
					if (freeSpaceLeft > 0)
					{
						freeBlock = MoveHeader(freeBlock, freeSpaceLeft);

						//we never shift the first block
						assert(pPrev);
						m_occupiedSpace += pPrev->bIsFree ? -freeSpaceLeft : freeSpaceLeft;
					}

					const size_t freeSpaceRight = freeBlock->m_size - size;

					//try to merge freespace right
					if (freeSpaceRight > SAILOR_SMALLEST_DATA_SIZE)
					{
						//create new free block
						Header* pNewFreeBlock = static_cast<Header*>(ShiftPtr(freeBlock, size + headerSize));
						const size_t newBlock = Offset(pNewFreeBlock, m_pData);

						pNewFreeBlock->bIsFree = 1;
						pNewFreeBlock->m_size = freeSpaceRight - headerSize;
						pNewFreeBlock->m_prev = Offset(freeBlock, m_pData);

						pNewFreeBlock->m_prevFree = freeBlock->m_prevFree;
						pNewFreeBlock->m_nextFree = freeBlock->m_nextFree;
						pNewFreeBlock->m_next = freeBlock->m_next;
						pNewFreeBlock->m_pageIndex = freeBlock->m_pageIndex;

						m_occupiedSpace += headerSize;

						freeBlock->m_size = size;

						if (!pPrevFree)
						{
							m_firstFree = newBlock;
						}
						else
						{
							pPrevFree->m_nextFree = newBlock;
						}

						if (pNextFree)
						{
							pNextFree->m_prevFree = newBlock;
						}

						freeBlock->m_next = newBlock;
						if (pNext)
						{
							pNext->m_prev = newBlock;
						}
					}
					else
					{
						if (!pPrevFree)
						{
							m_firstFree = freeBlock->m_nextFree;
						}
						else
						{
							pPrevFree->m_nextFree = freeBlock->m_nextFree;
						}

						if (pNextFree)
						{
							pNextFree->m_prevFree = freeBlock->m_prevFree;
						}
					}

					freeBlock->m_nextFree = InvalidIndexUINT64;
					freeBlock->m_prevFree = InvalidIndexUINT64;
					freeBlock->bIsFree = 0;

					m_occupiedSpace += freeBlock->m_size;

					return pNewStartData;
				}

				Header* pNextFree = static_cast<Header*>(freeBlock->m_nextFree != InvalidIndexUINT64 ? ShiftPtr(m_pData, freeBlock->m_nextFree) : nullptr);
				freeBlock = pNextFree;
			} while (freeBlock);

			return nullptr;
		}

		void Page::Free(void* pData)
		{
			const int32_t headerSize = sizeof(Header);
			Header* block = static_cast<Header*>(ShiftPtr(pData, -headerSize));
			Header* pFirstFree = static_cast<Header*>(m_firstFree != InvalidIndexUINT64 ? ShiftPtr(m_pData, m_firstFree) : nullptr);

			Header* pPrev = static_cast<Header*>(block->m_prev != InvalidIndexUINT64 ? ShiftPtr(m_pData, block->m_prev) : nullptr);
			Header* pNext = static_cast<Header*>(block->m_next != InvalidIndexUINT64 ? ShiftPtr(m_pData, block->m_next) : nullptr);

			assert(!block->bIsFree);

			const bool bShouldMergeRight = pNext && pNext->bIsFree;
			const bool bShouldMergeLeft = pPrev && pPrev->bIsFree;

			block->bIsFree = 1;
			block->m_nextFree = InvalidIndexUINT64;
			block->m_prevFree = InvalidIndexUINT64;

			// merge freespace
			if (bShouldMergeRight && !bShouldMergeLeft)
			{
				m_occupiedSpace -= sizeof(Header);

				if (!pNext->bIsFree)
				{
					m_occupiedSpace -= pNext->m_size;
				}

				const size_t blockAddr = Offset(block, m_pData);
				Header* pNextNextFree = static_cast<Header*>(pNext->m_nextFree != InvalidIndexUINT64 ? ShiftPtr(m_pData, pNext->m_nextFree) : nullptr);
				Header* pNextPrevFree = static_cast<Header*>(pNext->m_prevFree != InvalidIndexUINT64 ? ShiftPtr(m_pData, pNext->m_prevFree) : nullptr);
				Header* pNextNext = static_cast<Header*>(pNext->m_next != InvalidIndexUINT64 ? ShiftPtr(m_pData, pNext->m_next) : nullptr);

				if (pNextPrevFree)
				{
					pNextPrevFree->m_nextFree = blockAddr;
				}
				else
				{
					m_firstFree = blockAddr;
				}

				if (pNextNextFree)
				{
					pNextNextFree->m_prevFree = blockAddr;
				}

				block->m_size += pNext->m_size + headerSize;
				block->m_next = pNext->m_next;

				block->m_prevFree = pNext->m_prevFree;
				block->m_nextFree = pNext->m_nextFree;

				if (pNextNext)
				{
					pNextNext->m_prev = blockAddr;
				}
			}
			else if (bShouldMergeLeft && !bShouldMergeRight)
			{
				m_occupiedSpace -= sizeof(Header) + block->m_size;
				pPrev->m_size += block->m_size + headerSize;
				if (pNext)
				{
					pNext->m_prev = Offset(pPrev, m_pData);
				}

				pPrev->m_next = block->m_next;
			}
			else if (bShouldMergeRight && bShouldMergeLeft)
			{
				m_occupiedSpace -= sizeof(Header) * 2 + block->m_size;

				if (!pNext->bIsFree)
				{
					m_occupiedSpace -= pNext->m_size;
				}

				// Merge left first
				pPrev->m_size += block->m_size + headerSize;
				pNext->m_prev = Offset(pPrev, m_pData);
				pPrev->m_next = Offset(pNext, m_pData);

				Header* pNextNextFree = static_cast<Header*>(pNext->m_nextFree != InvalidIndexUINT64 ? ShiftPtr(m_pData, pNext->m_nextFree) : nullptr);
				Header* pNextPrevFree = static_cast<Header*>(pNext->m_prevFree != InvalidIndexUINT64 ? ShiftPtr(m_pData, pNext->m_prevFree) : nullptr);
				Header* pNextNext = static_cast<Header*>(pNext->m_next != InvalidIndexUINT64 ? ShiftPtr(m_pData, pNext->m_next) : nullptr);

				if (pNextPrevFree)
				{
					pNextPrevFree->m_nextFree = pNext->m_nextFree;
				}
				else
				{
					m_firstFree = pNext->m_nextFree;
				}

				if (pNextNextFree)
				{
					pNextNextFree->m_prevFree = pNext->m_prevFree;
				}

				pPrev->m_size += pNext->m_size + headerSize;
				pPrev->m_next = pNext->m_next;

				if (pNextNext)
				{
					pNextNext->m_prev = Offset(pPrev, m_pData);
				}
			}
			else
			{
				m_occupiedSpace -= block->m_size;

				const size_t blockAddress = Offset(block, m_pData);
				if (pFirstFree)
				{
					// largest block is first
					if (pFirstFree->m_size > block->m_size)
					{
						Header* pFirstFreeNextFree = static_cast<Header*>(pFirstFree->m_nextFree != InvalidIndexUINT64 ? ShiftPtr(m_pData, pFirstFree->m_nextFree) : nullptr);

						block->m_prevFree = m_firstFree;
						block->m_nextFree = pFirstFree->m_nextFree;
						pFirstFree->m_nextFree = blockAddress;

						if (pFirstFreeNextFree)
						{
							pFirstFreeNextFree->m_prevFree = blockAddress;
						}

						return;
					}
					pFirstFree->m_prevFree = blockAddress;
				}

				block->m_nextFree = m_firstFree;
				m_firstFree = blockAddress;
			}
		}

		void* PoolAllocator::Allocate(size_t size, size_t alignment)
		{
			for (size_t i = 0; i < m_freeList.size(); i++)
			{
				auto& page = m_pages[m_freeList[i]];
				const size_t realEmptySpace = page.m_totalSize - page.m_occupiedSpace;

				if (realEmptySpace > size)
				{
					if (void* res = page.Allocate(size, alignment))
					{
						if (realEmptySpace < page.GetMinAllowedEmptySpace())
						{
							page.bIsInFreeList = false;
							std::iter_swap(m_freeList.begin() + i, m_freeList.end() - 1);
							m_freeList.pop_back();
						}
						else if (i != 0)
						{
							// push current matching block into the beginning of the free list
							std::iter_swap(m_freeList.begin(), m_freeList.begin() + i);
						}
						return res;
					}
				}
			}

			const size_t quadraticGrow = (size_t)pow(2.0, (double)m_pages.size());
			const size_t neededPlace = size + sizeof(Header);
			const size_t maxBlockSize = 1024ull * 1024ull * 1024ull * 1u;
			const size_t newPageSize = (std::max)(neededPlace, (std::min)(maxBlockSize, quadraticGrow * m_pageSize));

			size_t index = m_pages.size();

			if (!m_emptyPages.empty())
			{
				index = m_emptyPages[m_emptyPages.size() - 1];
				if (!RequestPage(m_pages[index], newPageSize, index))
				{
					return nullptr;
				}
				m_emptyPages.pop_back();
			}
			else
			{
				Page newPage;
				if (!RequestPage(newPage, newPageSize, index))
				{
					return nullptr;
				}

				m_pages.emplace_back(std::move(newPage));
			}

			if (newPageSize <= maxBlockSize)
			{
				m_freeList.push_back(index);
			}

			return m_pages[index].Allocate(size, alignment);
		}

		void PoolAllocator::Free(void* ptr)
		{
			const int32_t headerSize = sizeof(Header);
			Header* block = static_cast<Header*>(ShiftPtr(ptr, -headerSize));

			Page& page = m_pages[block->m_pageIndex];

			page.Free(ptr);

			if (!page.bIsInFreeList && (page.m_totalSize - page.m_occupiedSpace) > page.GetMinAllowedEmptySpace())
			{
				m_freeList.push_back(block->m_pageIndex);
				page.bIsInFreeList = true;
			}

			if (false && page.IsEmpty() && m_freeList.size() > 1)
			{
				m_emptyPages.push_back(block->m_pageIndex);

				auto it = std::find(m_freeList.begin(), m_freeList.end(), block->m_pageIndex);

				std::iter_swap(m_freeList.end() - 1, it);
				m_freeList.pop_back();
				page.Clear();
			}
		}

		PoolAllocator::~PoolAllocator()
		{
			for (auto& page : m_pages)
			{
				page.Clear();
			}

			m_freeList.clear();
			m_pages.clear();
		}

		bool SmallPoolAllocator::RequestPage(SmallPage& page, uint8_t blockSize, uint16_t pageIndex) const
		{
			page = SmallPage(blockSize, pageIndex);
			if (page.m_pData = malloc(page.m_size))
			{
				return true;
			}

			return false;
		}

		SmallPoolAllocator::SmallPage::SmallPage(uint8_t blockSize, uint16_t pageIndex)
		{
			m_blockSize = blockSize;
			m_pageIndex = pageIndex;

			m_freeList.reserve(GetMaxBlocksNum());

			for (uint16_t i = 1; i < GetMaxBlocksNum(); i++)
			{
				m_freeList.emplace_back(i);
			}
		}

		void* SmallPoolAllocator::SmallPage::Allocate()
		{
			if (m_freeList.size() == 0)
			{
				return nullptr;
			}

			uint16_t id = m_freeList[m_freeList.size() - 1];
			m_freeList.pop_back();

			SmallHeader* header = (SmallHeader*)ShiftPtr(m_pData, (int32_t)id * m_blockSize - (int32_t)sizeof(SmallHeader));
			header->m_id = id;
			header->m_pageIndex = m_pageIndex;
			header->m_size = m_blockSize;

			m_numAllocs++;

			return ShiftPtr(m_pData, id * m_blockSize);
		}

		void SmallPoolAllocator::SmallPage::Free(void* ptr)
		{
			SmallHeader* header = (SmallHeader*)(((uint8_t*)ptr) - sizeof(SmallHeader));

			m_numAllocs--;

			m_freeList.push_back(header->m_id);
		}

		void SmallPoolAllocator::SmallPage::Clear()
		{
			delete m_pData;
			m_pData = nullptr;
		}

		size_t SmallPoolAllocator::SmallPage::GetMaxBlocksNum() const
		{
			return m_size / (uint32_t)m_blockSize;
		}

		bool SmallPoolAllocator::SmallPage::IsEmpty() const
		{
			return m_numAllocs == 0;
		}

		uint16_t SmallPoolAllocator::SmallPage::GetOccupiedSpace() const
		{
			return m_size - (uint16_t)m_freeList.size() * m_blockSize;
		}

		bool SmallPoolAllocator::SmallPage::IsFull() const
		{
			return m_freeList.empty();
		}

		void* SmallPoolAllocator::Allocate()
		{
			for (size_t i = 0; i < m_freeList.size(); i++)
			{
				auto& page = m_pages[m_freeList[i]];

				if (void* res = page.Allocate())
				{
					if (page.IsFull())
					{
						page.m_bIsInFreeList = false;
						std::iter_swap(m_freeList.begin() + i, m_freeList.end() - 1);
						m_freeList.pop_back();
					}
					return res;
				}
			}

			uint16_t index = (uint16_t)m_pages.size();

			if (!m_emptyPages.empty())
			{
				index = m_emptyPages[m_emptyPages.size() - 1];
				if (!RequestPage(m_pages[index], m_blockSize, index))
				{
					return nullptr;
				}
				m_emptyPages.pop_back();
			}
			else
			{
				SmallPage newPage;
				if (!RequestPage(newPage, m_blockSize, index))
				{
					return nullptr;
				}

				m_pages.emplace_back(std::move(newPage));
			}

			m_freeList.push_back(index);
			return m_pages[index].Allocate();
		}

		void SmallPoolAllocator::Free(void* ptr)
		{
			const int32_t headerSize = sizeof(SmallHeader);
			SmallHeader* block = (SmallHeader*)ShiftPtr(ptr, -headerSize);

			SmallPage& page = m_pages[block->m_pageIndex];
			const uint16_t blockIndex = block->m_pageIndex;

			page.Free(ptr);

			assert(page.m_pData != nullptr);

			if (!page.m_bIsInFreeList)
			{
				m_freeList.push_back(block->m_pageIndex);
				page.m_bIsInFreeList = true;
			}

			if (false && page.IsEmpty() && m_freeList.size() > 1)
			{
				m_emptyPages.push_back(blockIndex);

				if (page.m_bIsInFreeList)
				{
					auto it = std::find(m_freeList.begin(), m_freeList.end(), blockIndex);
					std::iter_swap(m_freeList.end() - 1, it);
					m_freeList.pop_back();
				}

				page.Clear();
			}
		}

		SmallPoolAllocator::~SmallPoolAllocator()
		{
			for (auto& page : m_pages)
			{
				page.Clear();
			}
		}

		HeapAllocator::HeapAllocator()
		{
			for (uint8_t i = 0; i < 255; i++)
			{
				m_smallAllocators.push_back(nullptr);
			}

			for (uint8_t i = 0; i < 255; i++)
			{
				const uint16_t alignedSize = (uint16_t)CalculateAlignedSize(i + 1);
				if (alignedSize < 256 && m_smallAllocators[alignedSize] == nullptr)
				{
					m_smallAllocators[alignedSize] = make_unique<SmallPoolAllocator>((uint8_t)alignedSize);
				}
			}
		}

		void* HeapAllocator::Allocate(size_t size, size_t alignment)
		{
			size_t alignedSize = CalculateAlignedSize(size);

			bool bSmallAllocator = alignedSize < 256 && size < 256;

			void* res = nullptr;
			if (bSmallAllocator)
			{
				res = m_smallAllocators[alignedSize]->Allocate();
			}
			else
			{
				res = m_allocator.Allocate(size, alignment);
			}

			if (!res)
			{
				return nullptr;
			}

			const int32_t headerSize = sizeof(SmallPoolAllocator::SmallHeader);
			SmallPoolAllocator::SmallHeader* block = (SmallPoolAllocator::SmallHeader*)ShiftPtr(res, -headerSize);

			block->m_meta = !bSmallAllocator;

			return res;
		}

		void HeapAllocator::Free(void* ptr)
		{
			if (!ptr)
			{
				return;
			}

			const int32_t headerSize = sizeof(SmallPoolAllocator::SmallHeader);
			SmallPoolAllocator::SmallHeader* block = (SmallPoolAllocator::SmallHeader*)ShiftPtr(ptr, -headerSize);

			if (block->m_meta == 1)
			{
				m_allocator.Free(ptr);
			}
			else
			{
				m_smallAllocators[block->m_size]->Free(ptr);
			}
		}

		size_t HeapAllocator::CalculateAlignedSize(size_t blockSize) const
		{
			const size_t alignment = 8;
			size_t fullData = sizeof(SmallPoolAllocator::SmallHeader) + blockSize;
			return fullData + (fullData % alignment == 0 ? 0 : (alignment - fullData % alignment));
		}
	}
}