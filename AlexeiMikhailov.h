#pragma once

#include <cstdlib>
#include <vector>
#include <limits>
#include <cmath>
#include <set>
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <Windows.h>

namespace AlexeiMikhailov
{
# pragma region NAMEOF MACRO

	/// Preprocessor macro which helps to debug allocator intrinsics.
	///	Set it to 0x00 to optimize performance. 
	/// Set it to 0x01 to investigate the problem. 
# define DEBUG_CUSTOM_ALLOCATOR 0x00

# pragma endregion

# pragma region NAMEOF MACRO

/// Preprocessor macro which infers name of argument as raw string literal (pointer to char const).
/// This macro uses lower_snake_case code style to be similar to standard C `sizeof` keyword to keep consistency. 
# define nameof(x) #x // NOLINT

# pragma endregion



# pragma region SIZE LITERAL CONSTEXPR

# ifdef _WIN64
	/// Constexpr version of size literal.
	[[deprecated]]
	constexpr size_t operator "" _sz(const unsigned long long n)
	{
		return n;
	}
# else
/// Constexpr version of size literal. Performs extra conversion. Do not use it :)
	[[deprecated]]
	constexpr size_t operator "" _sz(const unsigned long long n)
	{
		return static_cast<size_t>(n);
	}
# endif

# pragma endregion



# pragma region SIZE TYPES

	using byte_t = unsigned __int8;
	using err_t = unsigned __int8;

# ifdef _WIN64
	using const_size_t = const unsigned __int64;
	using word_t = unsigned __int64;
	using addr_t = unsigned __int64;
	using const_diff_t = const __int64;
	constexpr size_t word_size = sizeof(word_t);
	constexpr size_t addr_size = sizeof(addr_t);
	constexpr size_t bits_per_word = word_size * 8ui64;
	constexpr size_t sign_bit_mask = 0x8000000000000000;
# else
	using const_size_t = const unsigned __int32;
	using word_t = unsigned __int32;
	using addr_t = unsigned __int32;
	using const_diff_t = const __int32;
	constexpr size_t word_size = sizeof(word_t);
	constexpr size_t addr_size = sizeof(addr_t);
	constexpr size_t bits_per_word = word_size * 8ui32;
	constexpr size_t sign_bit_mask = 0x80000000;
# endif

# pragma endregion



# pragma region SIZE LITERAL MACRO

# ifdef _WIN64

	/// Preprocessor macro which replaces suffix-less integer literal to be sizeof-ready. 
	///	This macro uses different code style to be similar to standard C `sizeof` keyword to keep consistency. 
#	define word_literal(x) ((const_size_t)(x##ui64)) // NOLINT

	/// Preprocessor macro which replaces suffix-less integer literal to be sizeof-ready. 
	///	This macro uses different code style to be similar to standard C `sizeof` keyword to keep consistency. 
#	define size_literal(x) ((const_size_t)(x##ui64)) // NOLINT

	/// Preprocessor macro which replaces suffix-less integer literal to be sizeof-ready. 
	///	This macro uses lower_snake_case code style to be similar to standard C `sizeof` keyword to keep consistency. 
#	define diff_literal(x) (const_diff_t(x##ui64)) // NOLINT
# else
	/// Preprocessor macro which replaces suffix-less integer literal to be sizeof-ready. 
	///	This macro uses lower_snake_case code style to be similar to standard C `sizeof` keyword to keep consistency. 
#	define word_literal(x) (const_size_t(x##ui32)) // NOLINT

	/// Preprocessor macro which replaces suffix-less integer literal to be sizeof-ready. 
	///	This macro uses lower_snake_case code style to be similar to standard C `sizeof` keyword to keep consistency. 
#	define size_literal(x) (const_size_t(x##ui32)) // NOLINT

	/// Preprocessor macro which replaces suffix-less integer literal to be sizeof-ready. 
	///	This macro uses lower_snake_case code style to be similar to standard C `sizeof` keyword to keep consistency. 
#	define diff_literal(x) (const_diff_t(x##ui32)) // NOLINT
# endif

# pragma endregion

	inline size_t align(size_t size)
	{
# ifdef _WIN64
		return size + 0b00000000'00000000'00000000'00000000'00000000'00000000'00000000'00000111 &
			0b11111111'11111111'11111111'11111111'11111111'11111111'11111111'11111000;
# else
		return size + 0b00000000'00000000'00000000'00000011 &
			0b11111111'11111111'11111111'11111100;
# endif
	}


	inline size_t align(size_t size, size_t alignment)
	{
		if (alignment == 0)
		{
			throw std::invalid_argument("Alignment must be more than zero. ");
		}

		--alignment;
		return size + alignment & ~alignment;
	}

	enum class allocate_result : unsigned __int8
	{
		ok,
		busy_arena,
		busy_bucket,
		out_of_memory
	};

	class bucket_arena;
	class bucket;

	class bucket_arena final
	{
		size_t _block_size;
		size_t _capacity;
		word_t** _free_start;
		word_t** _free_border;
		word_t** _free_end;
		byte_t* _heap_start;
		byte_t* _heap_end;
		word_t* _xchg_index_entry;

#if DEBUG_CUSTOM_ALLOCATOR
		ptrdiff_t _heap_size_in_bytes;
#endif

		bucket_arena* _self;

	public:
		bucket* owner;
		bucket_arena* prev;
		bucket_arena* next;

		explicit bucket_arena(size_t block_size,
			size_t capacity,
			word_t** free_start,
			byte_t* heap_start) :
			_block_size(block_size),
			_capacity(capacity),
			_free_start(free_start),
			_free_border(free_start),
			_free_end(nullptr),
			_heap_start(heap_start),
			_heap_end(nullptr),

# if DEBUG_CUSTOM_ALLOCATOR
			_heap_size_in_bytes(diff_literal(0)),
# endif

			_self(this),
			owner(nullptr),
			prev(nullptr),
			next(nullptr)
		{
			_xchg_index_entry = new word_t();
		}

		~bucket_arena()
		{
			// Declare that
			// resource pre-acquisition and passing as ctor arg
			// means ownership. 
			::free(_free_start);
			::free(_heap_start);
			delete _xchg_index_entry;
		}

		bucket_arena(const bucket_arena&) = delete;
		bucket_arena(bucket_arena&&) = delete;
		bucket_arena operator=(const bucket_arena&) = delete;
		bucket_arena operator=(bucket_arena&&) = delete;

		bool is_busy() const
		{
			return _free_border == _free_end;
		}

		static allocate_result acquire_space(size_t block_size,
			size_t capacity,
			word_t**& out_free_start,
			byte_t*& out_heap_start)
		{
			out_free_start = static_cast<word_t**>(malloc(capacity * addr_size));
			out_heap_start = static_cast<byte_t*> (malloc(capacity * (3 * addr_size + block_size)));

			if (out_free_start && out_heap_start)
			{
				return allocate_result::ok;
			}
			else
			{
				return allocate_result::out_of_memory;
			}
		}

		void format()
		{
			static word_t** idx_item = nullptr;
			static byte_t* heap_ptr = nullptr;
			static size_t i = 0u;

			idx_item = _free_start;
			heap_ptr = _heap_start;

			for (i = size_literal(0); i < _capacity; ++i)
			{
				// Declare that both of allocated areas are valid.
				// So, dereferencing is safe. 
				*idx_item = reinterpret_cast<word_t*>(heap_ptr);

				memcpy(heap_ptr, &owner, addr_size);
				heap_ptr += addr_size;
				memcpy(heap_ptr, &_self, addr_size);
				heap_ptr += addr_size;
				memcpy(heap_ptr, &idx_item, addr_size);
				heap_ptr += addr_size;

# if DEBUG_CUSTOM_ALLOCATOR
				memset(heap_ptr, 0xa1, _block_size);
# endif

				heap_ptr += _block_size;
				++idx_item;
			}

			_free_end = idx_item;
			_heap_end = heap_ptr;

#if DEBUG_CUSTOM_ALLOCATOR
			std::cout << std::setfill('_') << std::hex;
			std::cout << "Allocated arena with block size of '0x" << std::setw(4) << _block_size << "' and capacity '0x" << std::setw(4) << _capacity << "'. " <<
				"IDX: '0x" << std::setw(16) << (addr_t)_free_start << "' , '0x" << std::setw(16) << (addr_t)_free_end << "', " <<
				"DATA: '0x" << std::setw(16) << (addr_t)_heap_start << "' , '0x" << std::setw(16) << (addr_t)_heap_end << "'" << std::endl;
			_heap_size_in_bytes = _heap_end - _heap_start;
#endif
		}

		allocate_result allocate(void** result)
		{
# if DEBUG_CUSTOM_ALLOCATOR
			if (_free_border > _free_end)
			{
				throw std::out_of_range("Index border is too high. ");
			}
# endif

			if (_free_border == _free_end)
			{
				return allocate_result::busy_arena;
			}

			*result = *_free_border + size_literal(3);
			++_free_border;

			return allocate_result::ok;
		}

		void free(void* address)
		{
			static word_t** index_ptr = nullptr;
			index_ptr = *(static_cast<word_t***>(address) - size_literal(1));

# if DEBUG_CUSTOM_ALLOCATOR
			auto pattern_ptr = static_cast<byte_t*>(address);
			auto pattern_end = static_cast<byte_t*>(address) + _block_size;

			while (pattern_ptr != pattern_end)
			{
				if (reinterpret_cast<addr_t>(pattern_ptr) & size_literal(1))
				{
					memset(pattern_ptr++, 0xee, size_literal(1));
				}
				else
				{
					memset(pattern_ptr++, 0xf3, size_literal(1));
				}
			}

			if (address < _heap_start + addr_size)
			{
				throw std::out_of_range("Data address is too low. ");
			}

			if (address >= _heap_end)
			{
				throw std::out_of_range("Data address is too high. ");
			}

			if (_free_border <= _free_start)
			{
				throw std::out_of_range("Index border is too low. ");
			}

			if (index_ptr >= _free_border)
			{
				throw std::out_of_range("Index address is greater than index border. It may point to free indices. ");
			}
# endif

			--_free_border;

			memcpy(_xchg_index_entry, _free_border, sizeof(word_t));
			memcpy(_free_border, index_ptr, sizeof(word_t));
			memcpy(index_ptr, _xchg_index_entry, sizeof(word_t));

			memcpy(_xchg_index_entry, *_free_border + size_literal(2), sizeof(word_t));
			memcpy(*_free_border + size_literal(2), *index_ptr + size_literal(2), sizeof(word_t));
			memcpy(*index_ptr + size_literal(2), _xchg_index_entry, sizeof(word_t));
		}
	};

	class bucket final
	{
		size_t _block_size;
		size_t _current_arena_capacity = size_literal(0x100);
		bucket_arena* _root;
		bucket_arena* _head;
		size_t _count;

	public:
		bucket(size_t block_size,
			size_t capacity,
			bucket_arena* root) :
			_block_size(block_size),
			_root(root),
			_head(root),
			_count(size_literal(1))
		{
			_root->owner = this;
		}

		~bucket()
		{
			static bucket_arena* arena = nullptr;
			arena = _root;

			while (arena != _head)
			{
				static bucket_arena* next = nullptr;
				next = arena->next;
				delete arena;
				arena = next;
			}

			delete _head;
		}

		bucket(const bucket&) = delete;
		bucket(bucket&&) = delete;
		bucket& operator=(const bucket&) = delete;
		bucket& operator=(bucket&&) = delete;

		size_t get_count() const
		{
			static size_t root_to_head_count = 0u;
			static size_t head_to_root_count = 0u;
			static bucket_arena* arena = nullptr;

			root_to_head_count = size_literal(1);
			arena = _root;

			while (arena->next)
			{
				++root_to_head_count;
				if (root_to_head_count > _count)
				{
					throw std::logic_error("Chain count is more than expected. ");
				}

				arena = arena->next;
			}

			arena = _head;
			while (arena->prev)
			{
				++head_to_root_count;
				if (head_to_root_count > _count)
				{
					throw std::logic_error("Chain count is more than expected. ");
				}

				arena = arena->prev;
			}

# if DEBUG_CUSTOM_ALLOCATOR
			if (root_to_head_count != head_to_root_count)
			{
				throw std::logic_error("Node chain is broken. Root to head count is not equal to head to root count. ");
			}
# endif

			return root_to_head_count;
		}

		bool bFull = false;
		void* allocate()
		{
			static void* result = nullptr;
			static bucket_arena* last_free_arena = nullptr;
			static allocate_result error = allocate_result::ok;

			result = nullptr;

			// Drive from head to last free arena
			last_free_arena = _head;
			error = last_free_arena->allocate(&result);

			while (error != allocate_result::ok)
			{
				if (last_free_arena->prev)
				{
					last_free_arena = last_free_arena->prev;
					error = last_free_arena->allocate(&result);
				}
				else
				{
					error = allocate_result::busy_bucket;
					break;
				}
			}

			if (error == allocate_result::busy_bucket)
			{
				if (bFull)
				{
					return nullptr;
				} 

				_current_arena_capacity <<= 1;
				
				static word_t** free_start = nullptr;
				static byte_t* heap_start = nullptr;
				auto res = bucket_arena::acquire_space(_block_size,
					_current_arena_capacity,
					free_start,
					heap_start);

				if (res == allocate_result::ok)
				{
					static bucket_arena* arena;
					arena = new bucket_arena(_block_size,
						_current_arena_capacity,
						free_start,
						heap_start);
					arena->owner = this;
					arena->format();
					arena->prev = _head;
					arena->next = nullptr;
					_head->next = arena;
					_head = arena;
					++_count;

# if DEBUG_CUSTOM_ALLOCATOR
					if (_count != get_count())
					{
						throw std::logic_error("Node chain is broken. ");
					}

					error = _head->allocate(&result);

					if (error != allocate_result::ok)
					{
						throw std::logic_error("Unexpected program flow. ");
					}
# else
					_head->allocate(&result);
# endif
				}
				else if (res == allocate_result::out_of_memory)
				{
					bFull = true;
					return nullptr;
				}
			}
			else
			{
# if DEBUG_CUSTOM_ALLOCATOR
				if (error != allocate_result::ok)
				{
					throw std::logic_error("Unexpected program flow. ");
				}
# endif
			}

			return result;
		}

		void bob(bucket_arena* arena)
		{
			if (arena != _head)
			{
				static bucket_arena* prev;
				static bucket_arena* next;
				prev = arena->prev;
				next = arena->next;

				if (prev)
				{
					prev->next = arena->next;
				}

				if (next)
				{
					next->prev = arena->prev;
				}

				if (arena == _root)
				{
					_root = next;
				}

				_head->next = arena;
				arena->prev = _head;
				arena->next = nullptr;
				_head = arena;
			}

# if DEBUG_CUSTOM_ALLOCATOR
			if (_count != get_count())
			{
				throw std::logic_error("Node chain is broken. ");
			}
# endif
		}
	};
	
	class allocator final
	{
		std::vector<bucket*> _buckets;

	public:
		allocator()
		{
# if DEBUG_CUSTOM_ALLOCATOR
			constexpr auto buffer_size = COORD{ 120, 32766 };
			const auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
			SetConsoleScreenBufferSize(handle, buffer_size);
# endif
		}

		~allocator()
		{
			for (const auto& bucket : _buckets)
			{
				delete bucket;
			}
		}

		allocator(const allocator& allocator) = delete;
		allocator(allocator&& allocator) = delete;
		allocator& operator=(const allocator& allocator) = delete;
		allocator& operator=(allocator&& allocator) = delete;

		void* allocate(size_t size, size_t alignment = size_literal(0))
		{
			size = alignment == 0 ? align(size) : align(size, alignment);

			static size_t size_key = 0u;
# ifdef _WIN64
			size_key = 64ui64 - __lzcnt64(size - size_literal(1));
#else
			size_key = 32ui32 - __lzcnt(size - size_literal(1));
# endif
			size = size_literal(1) << size_key;

			if (_buckets.size() <= size_key)
			{
				_buckets.resize(size_key + 1);
			}

			if (_buckets[size_key])
			{
				return _buckets[size_key]->allocate();
			}

			constexpr size_t initial_arena_capacity = size_literal(0x01);
			static word_t** free_start = nullptr;
			static byte_t* heap_start = nullptr;
			static allocate_result result = allocate_result::ok;
					
			result = bucket_arena::acquire_space(size,
				initial_arena_capacity,
				free_start,
				heap_start);
			if (result == allocate_result::ok)
			{
				static bucket_arena* root_arena = nullptr;
				root_arena = new bucket_arena(size,
					initial_arena_capacity,
					free_start,
					heap_start);

				_buckets[size_key] = new bucket(size,
					initial_arena_capacity,
					root_arena);

				root_arena->owner = _buckets[size_key];
				root_arena->format();

				return _buckets[size_key]->allocate();
			}

			if (result == allocate_result::out_of_memory)
			{
				return nullptr;
			}

			throw std::logic_error("Unexpected program flow. ");
		}

		void free(void* ptr)
		{
			static word_t* word_ptr = nullptr;
			static bucket* bucket = nullptr;
			static bucket_arena* arena = nullptr;

			// Dereferencing is very slow. How to fix it?
			// Ideas: 1. Move ptr instead of refer from address to address.
			//        2. ... no more :)
			word_ptr = static_cast<word_t*>(ptr);
			----word_ptr;
			arena = reinterpret_cast<bucket_arena*>(*word_ptr);
			--word_ptr;
			bucket = reinterpret_cast<AlexeiMikhailov::bucket*>(*word_ptr);

			arena->free(ptr);
			bucket->bob(arena);
		}

		size_t get_occupied_space() const
		{
			throw std::logic_error("Method " nameof(get_occupied_space) " not implemented. ");
		}
	};
	
	class Allocator final
	{
		AlexeiMikhailov::allocator m_impl;

	public:
		void* Allocate(size_t size, size_t alignment)
		{
			return m_impl.allocate(size, alignment);
		}

		void Free(void* ptr)
		{
			if (!ptr)
			{
				return;
			}

			m_impl.free(ptr);
		}
	};
}
