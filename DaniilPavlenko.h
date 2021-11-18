#pragma once

#include <cstdlib>
#include <vector>
#include <limits>
#include <cmath>
#include <set>

namespace Daniil_MultisetBlock_impl
{
    // each allocation 2 times bigger then previous
    // so if first malloc size = element size:
    // max mallocs = log(max elements)
    static constexpr std::size_t max_exp_mallocs = 128;
    // Allocator info for each separately given memory space
    // that is stored together with data
    struct Header {
        // minimum bytes of space for new header (excluding size of Header itself)
        static constexpr std::size_t min_bytes = 16;

        // size of memory space excluding size of header
        std::size_t m_size;
        // Pointers to neighbouring Headers (stored for merges)
        // nullptr if this Header is at the start/end of allocation
        Header* m_next;
        Header* m_prev;
        bool m_bIsOccupied;

        // Shrinks Header to provided size and returns a ponter to a new Header of freed space
        // or returns nullptr if not enough space for new header + min_bytes
        inline Header* Insert(std::size_t);
        // Get pointer to data
        inline void* Data() { return (reinterpret_cast<char*>(this) + sizeof(Header)); }
        // Get Header from pointer to data
        static inline Header* FromDataPtr(void* data_ptr)
        {
            return reinterpret_cast<Header*>(reinterpret_cast<char*>(data_ptr) - sizeof(Header));
        }
    };
    // Comparison functor for multiset
    class HeaderLess {
    public:
        inline bool operator()(const Header* a, const Header* b) const
        {
            return (a->m_size < b->m_size);
        }
    };
    Header* Header::Insert(std::size_t new_size) {
        if (m_size < (min_bytes + new_size + sizeof(Header)))
        {
            return nullptr;
        }
        Header* new_header = reinterpret_cast<Header*>(reinterpret_cast<char*>(Data()) + new_size);
        new_header->m_size = m_size - new_size - sizeof(Header);
        new_header->m_prev = this;
        new_header->m_bIsOccupied = false;
        if (m_next)
        {
            m_next->m_prev = new_header;
            new_header->m_next = m_next;
            m_next = new_header;
        }
        else new_header->m_next = nullptr;
        m_size = new_size;
        return new_header;
    }
    // Pool allocator for fast multiset operations
    template<class T>
    class SetAllocator {

        struct Chunk
        {
            Chunk* prev_free_chunk;
        };
        class MemoryBlock
        {
            static constexpr std::size_t size = sizeof(T);
            char data[size * max_exp_mallocs];
        public:

            MemoryBlock* next;
            MemoryBlock(MemoryBlock* next) : next(next) {}
            T* GetChunk(std::size_t index)
            {
                return reinterpret_cast<T*>(&data[size * index]);
            }
        };
        Chunk* firstFreeChunk = nullptr;
        MemoryBlock* firstMemoryBlock = nullptr;
        std::size_t currentIndex = max_exp_mallocs;
    public:
        typedef std::size_t size_type;
        typedef std::ptrdiff_t difference_type;
        typedef T* pointer;
        typedef const T* const_pointer;
        typedef T& reference;
        typedef const T& const_reference;
        typedef T value_type;
        SetAllocator() = default;
        SetAllocator(SetAllocator&& alloc) = delete;
        SetAllocator(const SetAllocator& alloc) = delete;
        SetAllocator operator =(SetAllocator&& alloc) = delete;
        SetAllocator operator =(const SetAllocator& alloc) = delete;
        ~SetAllocator()
        {
            while (firstMemoryBlock) {
                MemoryBlock* block = firstMemoryBlock;
                firstMemoryBlock = block->next;
                delete block;
            }
        }
        T* allocate(std::size_t n = 1)
        {
            if (n != 1)
            {
                throw std::bad_alloc();
            }
            if (firstFreeChunk) {
                Chunk* chunk = firstFreeChunk;
                firstFreeChunk = chunk->prev_free_chunk;
                return reinterpret_cast<T*>(chunk);
            }
            if (currentIndex >= max_exp_mallocs) {
                firstMemoryBlock = new MemoryBlock(firstMemoryBlock);
                currentIndex = 0;
            }
            return firstMemoryBlock->GetChunk(currentIndex++);
        }
        void deallocate(T* p, std::size_t n = 1)
        {
            Chunk* chunk = reinterpret_cast<Chunk*>(p);
            chunk->prev_free_chunk = firstFreeChunk;
            firstFreeChunk = chunk;
        }
    };
}

namespace DaniilPavlenko {
    using namespace Daniil_MultisetBlock_impl;
    class FastAllocator {
    public:

        FastAllocator& operator=(const FastAllocator&) = delete;
        FastAllocator(const FastAllocator&) = delete;

        FastAllocator& operator=(FastAllocator&&) = default;
        FastAllocator(FastAllocator&&) = default;


        FastAllocator()
        {
            m_mallocPtrs.reserve(max_exp_mallocs);
            void* block_ptr = malloc(blocksize);
            m_mallocPtrs.push_back(block_ptr);
            m_free_space.insert(InitBlock(block_ptr));
        }
        virtual ~FastAllocator()
        {
            for (auto& ptr : m_mallocPtrs)
            {
                std::free(ptr);
            }
        }

        void* Allocate(std::size_t size, std::size_t allignment)
        {
            if (size == 0)
            {
                return nullptr;
            }
            Header wanted_header;
            wanted_header.m_size = size - 1;
            auto found_iterator = m_free_space.upper_bound(&wanted_header);
            if (found_iterator != m_free_space.end())
            {
                Header* hdr = *found_iterator;
                m_free_space.erase(found_iterator);
                Header* leftovers = hdr->Insert(size);
                hdr->m_bIsOccupied = true;
                if (leftovers)
                {
                    m_free_space.insert(leftovers);
                }
                return hdr->Data();
            }
            std::size_t new_size = (size_t)blocksize * (size_t)std::pow(2, (m_mallocPtrs.size()));
            if (new_size < (size + sizeof(Header)))
            {
                new_size = size + sizeof(Header);
            }
            void* new_memory_ptr = malloc(new_size);

        	if(!new_memory_ptr)
        	{
                return nullptr;
        	}
            m_mallocPtrs.push_back(new_memory_ptr);
            Header* new_block_header = reinterpret_cast<Header*>(new_memory_ptr);
            new_block_header->m_bIsOccupied = true;
            new_block_header->m_prev = nullptr;
            new_block_header->m_next = nullptr;
            new_block_header->m_size = new_size - sizeof(Header);
            Header* leftovers = new_block_header->Insert(size);
            if (leftovers)
            {
                m_free_space.insert(leftovers);
            }
            return new_block_header->Data();
        }
        void Free(void* ptr)
        {
        	if(!ptr)
        	{
                return;
        	}
        	
            Header* freed_header = Header::FromDataPtr(ptr);
            freed_header->m_bIsOccupied = false;
            if (freed_header->m_next && !freed_header->m_next->m_bIsOccupied)
            {
                freed_header->m_size += freed_header->m_next->m_size + sizeof(Header);
                freed_header->m_next = freed_header->m_next->m_next;
                m_free_space.erase(freed_header->m_next);
            }
            if (freed_header->m_prev && !freed_header->m_prev->m_bIsOccupied)
            {
                freed_header->m_prev->m_size += freed_header->m_size + sizeof(Header);
                freed_header->m_prev->m_next = freed_header->m_next;
                m_free_space.erase(freed_header);
            }
        }
    private:
        inline Header* InitBlock(void* block_ptr)
        {
            Header* initial_header = reinterpret_cast<Header*>(block_ptr);
            initial_header->m_bIsOccupied = false;
            initial_header->m_prev = nullptr;
            initial_header->m_next = nullptr;
            initial_header->m_size = blocksize - sizeof(Header);
            return initial_header;
        }

        // Pointers to headers with unoccupied space
        std::multiset<Header*, HeaderLess, SetAllocator<Header*>> m_free_space{};
        // Pointers to every separately allocated memory
        std::vector<void*> m_mallocPtrs{};
        // Each allocation has size equal to blocksize*2**numAllocations (doubles each time)
        std::size_t blocksize = 1024 * 1;
    };
   
}
