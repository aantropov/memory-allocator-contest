#pragma once
#include <vector>
#include <mutex>
#include <iostream>
#include <thread>
namespace OlegApanasik
{
    class TMemoryAllocator;
    struct Header
    {
        size_t _memPieceSize = 0;
        size_t _memBlockIndex = 0;
        size_t _memBlockPosition = 0;
    };
    class TMemoryBlockAllocator
    {
    public:
        size_t _memPieceSize = 0;
        size_t _memBlockSize = 0;
        size_t _memBlockIndex = 0;
        std::vector<size_t> _occupiedMemoryPositions;
        std::mutex _populateMutex;
        char* _data = nullptr;
    public:
        TMemoryBlockAllocator(const size_t& in_mem_piece_size, const size_t& in_mem_block_size, const size_t& in_mem_block_index)
        {
            _memPieceSize = in_mem_piece_size;
            _memBlockSize = in_mem_block_size;
            _memBlockIndex = in_mem_block_index;
            const size_t total_block_size = (_memPieceSize + sizeof(Header)) * _memBlockSize;
            _occupiedMemoryPositions.reserve(_memBlockSize);
            _data = static_cast<char*>(malloc(total_block_size));
            if (_memBlockSize > 8192)
            {
                PopulateLayoutParallel();
            }
            else
            {
                PopulateLayout();
            }
        }
        ~TMemoryBlockAllocator()
        {
            free(_data);
        }
        void* ReserveMemoryPiece()
        {
            void* memory_piece = _data + _occupiedMemoryPositions.back() + sizeof(Header);
            _occupiedMemoryPositions.pop_back();
            return memory_piece;
        }
        void FreeMemoryPiece(size_t& in_mem_block_position)
        {
            _occupiedMemoryPositions.push_back(in_mem_block_position);
        }
        bool IsFree() const
        {
            return !_occupiedMemoryPositions.empty();
        }
    private:
        unsigned int clamp_threads_count(const unsigned int& in_count, const unsigned int& in_min) const
        {
            return in_count > in_min ? in_count : in_min;
        }
        void PopulateLayout()
        {
            for (size_t index = 0; index < _memBlockSize; index++)
            {
                const size_t position = index * (_memPieceSize + sizeof(Header));
                Header* header = reinterpret_cast<Header*>(static_cast<char*>(_data) + position);
                header->_memPieceSize = _memPieceSize;
                header->_memBlockPosition = position;
                header->_memBlockIndex = _memBlockIndex;
                _occupiedMemoryPositions.push_back(position);
            }
        }
        void LayoutWorker(size_t in_start_index, size_t in_end_index)
        {
            std::vector<size_t> worker_temp_positions;
            worker_temp_positions.reserve(in_end_index - in_start_index);
            for (in_start_index; in_start_index < in_end_index; in_start_index++)
            {
                const size_t memory_position = in_start_index * (_memPieceSize + sizeof(Header));
                Header* header = reinterpret_cast<Header*>(_data + memory_position);
                header->_memPieceSize = _memPieceSize;
                header->_memBlockPosition = memory_position;
                header->_memBlockIndex = _memBlockIndex;
                worker_temp_positions.push_back(memory_position);
            }
            _populateMutex.lock();
            _occupiedMemoryPositions.insert(_occupiedMemoryPositions.end(), worker_temp_positions.begin(), worker_temp_positions.end());
            _populateMutex.unlock();
        }
        void PopulateLayoutParallel()
        {
            unsigned int threads_count = std::thread::hardware_concurrency() / 4;
            threads_count = clamp_threads_count(threads_count, 2);
            const size_t mem_block_size_per_thread = _memBlockSize / threads_count;
            size_t start_index = 0;
            size_t end_index = 0;
            std::vector<std::thread*> workers;
            workers.reserve(threads_count);
            for (unsigned int i = 0; i < threads_count; i++)
            {
                end_index = start_index + mem_block_size_per_thread;
                std::thread* worker_thread = new std::thread(&TMemoryBlockAllocator::LayoutWorker, this, start_index, end_index);
                start_index = end_index;
                workers.push_back(worker_thread);
            }
            for (unsigned int i = 0; i < threads_count; i++)
            {
                workers[i]->join();
            }
        }
    };
    class TMemoryAllocator
    {
    private:
        std::vector<std::vector<TMemoryBlockAllocator*>> _memBuckets;
        std::vector<std::vector<size_t>> _freeMemBuckets;
        std::vector<size_t> _memBlockSizes;
        size_t _bucketsCount = 0;
        size_t _minStartMemoryBlockSize = 0;
        size_t _maxStartMemoryBlockSize = 0;
        size_t _maxMemoryExpandedAllocationSize = 0;
        size_t _memoryPreallocatedBlocksPerBucket = 0;
        size_t _memoryBucketSize = 0;
        size_t _alignMachineWord = 0;
        size_t _maxMemoryBlockExpandedSize = 0;
    public:
        TMemoryAllocator()
        {
            _maxMemoryBlockExpandedSize = 1048576;
            _maxMemoryExpandedAllocationSize = 1073741824;
            _minStartMemoryBlockSize = 1;
            _maxStartMemoryBlockSize = 512;
            _memoryPreallocatedBlocksPerBucket = 16;
            _memoryBucketSize = 16;
            _alignMachineWord = sizeof(intptr_t) / 4 + 1;
            _memBlockSizes.reserve(32);
            _memBuckets.reserve(32);
            _freeMemBuckets.reserve(32);
        }
        void Release()
        {
            for (auto& _memBucket : _memBuckets)
            {
                for (auto& j : _memBucket)
                {
                    delete j;
                }
            }
            _memBuckets.clear();
            _freeMemBuckets.clear();
            _memBlockSizes.clear();
            _bucketsCount = 0;
        }
        ~TMemoryAllocator()
        {
            Release();
        }
        void* Allocate(size_t in_memory_allocation_size, size_t in_alignment)
        {
            const size_t& align_size = align(in_memory_allocation_size);
            void* memory_allocation_block = nullptr;
            const size_t& bucket_index = getBucketIndex(align_size);
            if (TMemoryBlockAllocator* found_mem_block = findFreeBlock(bucket_index, align_size))
            {
                memory_allocation_block = found_mem_block->ReserveMemoryPiece();
                const size_t mem_block_index = _memBuckets[bucket_index].size();
                if (!found_mem_block->IsFree())
                {
                    RemoveBlockFromBucket(bucket_index, mem_block_index);
                }
            }
            else
            {
                TMemoryBlockAllocator* requested_mem_block = requestMemoryFromOS(bucket_index, align_size);
                const size_t mem_block_index = _memBuckets[bucket_index].size();
                _freeMemBuckets[bucket_index].emplace_back(mem_block_index);
                _memBuckets[bucket_index].emplace_back(requested_mem_block);
                memory_allocation_block = requested_mem_block->ReserveMemoryPiece();
                if (!requested_mem_block->IsFree())
                {
                    RemoveBlockFromBucket(bucket_index, mem_block_index);
                }
            }
            return memory_allocation_block;
        }
        void Reserve()
        {
            ReserveBuckets(_memoryBucketSize - 1, 2);
            for (size_t mem_block_size = sizeof(intptr_t); mem_block_size <= _minStartMemoryBlockSize; mem_block_size <<= 1)
            {
                size_t bucket_index = getBucketIndex(mem_block_size);
                for (size_t i = 0; i < _memoryPreallocatedBlocksPerBucket; i++)
                {
                    TMemoryBlockAllocator* requested_mem_block = requestMemoryFromOS(bucket_index, mem_block_size);
                    const size_t mem_block_index = _memBuckets[bucket_index].size();
                    _freeMemBuckets[bucket_index].emplace_back(mem_block_index);
                    _memBuckets[bucket_index].emplace_back(requested_mem_block);
                }
            }
        }
        void Free(void* in_data)
        {
            Header* header = reinterpret_cast<Header*>(static_cast<char*>(in_data) - sizeof(Header));
            size_t& mem_block_index = header->_memBlockIndex;
            size_t& mem_block_position = header->_memBlockPosition;
            const size_t& bucket_index = getBucketIndex(header->_memPieceSize);
            TMemoryBlockAllocator* mem_block = _memBuckets[bucket_index][mem_block_index];
            if (!mem_block->IsFree())
            {
                _freeMemBuckets[bucket_index].emplace_back(mem_block_index);
            }
            mem_block->FreeMemoryPiece(mem_block_position);
        }
    private:
        void RemoveBlockFromBucket(const size_t& in_bucket_index, const size_t& in_mem_block_index)
        {
            const size_t mem_bucket_size = _freeMemBuckets[in_bucket_index].size();
            if (in_mem_block_index < mem_bucket_size || (in_mem_block_index == 0 && mem_bucket_size > 1))
            {
                std::swap(_freeMemBuckets[in_bucket_index][mem_bucket_size - 1], _freeMemBuckets[in_bucket_index][in_mem_block_index]);
            }
            _freeMemBuckets[in_bucket_index].pop_back();
        }
        void ReserveBuckets(const size_t& in_upper_index, const size_t& in_mem_piece_size)
        {
            const size_t new_mem_block_size = clamp(_maxMemoryExpandedAllocationSize / in_mem_piece_size, _minStartMemoryBlockSize, _maxStartMemoryBlockSize);
            for (_bucketsCount; _bucketsCount <= in_upper_index; _bucketsCount++)
            {
                _memBuckets.emplace_back();
                _freeMemBuckets.emplace_back();
                _memBuckets[_bucketsCount].reserve(_memoryBucketSize);
                _freeMemBuckets[_bucketsCount].reserve(_memoryBucketSize);
                _memBlockSizes.emplace_back(new_mem_block_size);
            }
            _bucketsCount = in_upper_index + 1;
        }
        size_t clamp(const size_t& in_value, const size_t& in_min, const size_t& in_max) const
        {
            return in_value < in_min ? in_min : (in_value > in_max ? in_max : in_value);
        }
        size_t align(size_t in_size) const
        {
            if (in_size <= sizeof(intptr_t))
            {
                in_size = sizeof(intptr_t);
            }
            else
            {
                in_size--;
                in_size |= in_size >> 1;
                in_size |= in_size >> 2;
                in_size |= in_size >> 4;
                in_size |= in_size >> 8;
                in_size++;
            }
            return in_size;
        }
        size_t getBucketIndex(size_t in_size) const
        {
            return static_cast<size_t>(log(in_size) / log(2)) - _alignMachineWord;
        }
        TMemoryBlockAllocator* requestMemoryFromOS(const size_t& in_bucket_index, const size_t& in_mem_piece_size)
        {
            if (in_bucket_index + 1 > _bucketsCount)
            {
                ReserveBuckets(in_bucket_index, in_mem_piece_size);
            }
            size_t& memory_block_size = _memBlockSizes[in_bucket_index];
            const size_t& mem_block_index = _memBuckets[in_bucket_index].size();
            auto* block = new TMemoryBlockAllocator(in_mem_piece_size, memory_block_size, mem_block_index);
            const size_t expanded_memory_block_size = memory_block_size * 2;
            if (expanded_memory_block_size * in_mem_piece_size <= _maxMemoryExpandedAllocationSize && expanded_memory_block_size <= _maxMemoryBlockExpandedSize)
            {
                memory_block_size = expanded_memory_block_size;
            }
            return block;
        }
        TMemoryBlockAllocator* findFreeBlock(const size_t& in_bucket_index, const size_t& in_mem_piece_size)
        {
            if (in_bucket_index + 1 > _bucketsCount)
            {
                ReserveBuckets(in_bucket_index, in_mem_piece_size);
            }
            if (_freeMemBuckets[in_bucket_index].empty())
            {
                return nullptr;
            }
            const size_t& mem_block_index = _freeMemBuckets[in_bucket_index].back();
            return _memBuckets[in_bucket_index][mem_block_index];
        }
    };
}