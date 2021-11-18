#include <iostream>
#include <iomanip>
#include <deque>

/* OneshotLocator, Denis Perevalov
   Идея в том, что память выделяется ненадолго и через некоторое время освобождается.
*/

using namespace std;
namespace  DenisPerevalov
{
	typedef unsigned long long uint64;
	typedef unsigned int uint32;
	typedef unsigned char uint8;
	typedef long long int64;
	typedef int int32;
	typedef char int8;

	static const uint32 maxn = 36;  //2^36 > 30Gb
	static const uint32 maxn1 = maxn - 1;
	static const uint64 highbit = uint64(1u) << maxn1;

	
	// Allocation table
	const uint8 table[maxn] =
	{
		16,16,16,16,16,16,16,16,16,16,		//0..9    1 b  .. 512 b,        1 Kb .. 16 Kb
		16,16,16,16,25,25,25,25,25,25,  //10..19  1 Kb .. 512 Kb    
		25,25,25,25,30,30,30,30,30,30,  //20..29  1 Mb .. 512 Mb    
		33,33,33,33,34,35               //30..35  1 Gb .. 32 Gb  
	};

	uint32 log2_up(uint64 v) {
		uint64 u = 1;
		uint8 log = 0;
		while (u < v) {
			u <<= 1;
			log++;
		}
		return log;
	}

	
	struct Page;

	struct Item {
		Page *page;
		static void setup(Item* it, Page *page) {
			it->page = page;
		}
	};
	
	struct Page {
		uint32 counter = 0;
		uint8* Data = nullptr;
		uint8 table_index = 0;

		uint64 FreeSize = 0;
		uint8* RecPos = nullptr;

		static Page* create_page(uint64 size, uint8 table_index) {
			size += sizeof(Item);		//Increase to be able to keep at least one item
			uint8* Data0 = (uint8*)malloc(size);
			if (Data0) {
				return new Page(size, table_index, Data0);
			}
			return nullptr;		//Can't allocate
		}
		
		Page() {}
	protected:
		Page(uint64 size, uint8 table_index0, uint8* Data0) {
			FreeSize = size;
			table_index = table_index0;
			Data = RecPos = Data0;
		}
	public:
		~Page() {
			free(Data);
		}

		bool can_alloc(size_t size) {
			return size + sizeof(Item) <= FreeSize;
		}

		void* alloc(size_t size) {		// You must check can_alloc() before calling this
			Item *it = (Item*)RecPos;
			Item::setup(it, this);
			uint8* data = RecPos + sizeof(Item);
			RecPos += size + sizeof(Item);
			FreeSize -= size + sizeof(Item);
			counter++;
			return data;
		}

		bool free_one_item() {	//delete one element, returns true if need delete the page
			counter--;
			return (counter <= 0);
		}	
	};

	
	// Oneshotlocator class
	
	class Oneshotlocator
	{
	public:
		
		Oneshotlocator() {
			for (int i = 0; i < maxn; i++) {
				FreePages[i] = nullptr;
			}

			uint64 bytes = 1;
			for (uint8 i = 0; i < maxn; i++) {
				pow2[i] = bytes;
				bytes <<= 1;
			}
		}
		
		~Oneshotlocator() {
		}
	public:
		
		void* Allocate(size_t size, size_t /*alignment*/) {
			uint8 log = log2_up(size);
			uint8 i = table[log];
			Page* &page = FreePages[i];
			if (page && page->can_alloc(size)) {
				return page->alloc(size);
			}
			else {
				page = Page::create_page(pow2[i], i);
				return (page) ? page->alloc(size) : nullptr;
			}
		}

		
		void Free(void* ptr) {
			if (ptr) {
				Item* it = (Item*)ptr - 1;
				Page* page = it->page;
				if (page->free_one_item()) {	//if returns true - then need to delete page
					uint8 i = page->table_index;
					if (FreePages[i] == page) {
						FreePages[i] = nullptr;		
					}
					delete page;		
				}
			}
		}

	private:
		// Pages
		Page *FreePages[maxn];		//heads of the pages lists (though we don't maintaining list structure for now)

		// Table of 2^i
		uint64 pow2[maxn];
	};
}