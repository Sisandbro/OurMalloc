#include <iostream>
#include <list>
#include <Windows.h>

//32Bit system size_t is 4 bytes
#ifndef INTERNAL_SIZE_T 
#define INTERNAL_SIZE_T size_t 
#endif
#define SIZE_SZ                (sizeof(INTERNAL_SIZE_T)) 
//Align to 8 bytes 
#ifndef MALLOC_ALIGNMENT 
#define MALLOC_ALIGNMENT       (2 * SIZE_SZ) 
#endif

#define SMALL_BIN_MAX_SIZE 512

LPVOID heapMemory;
size_t hMemoryLeft;

typedef struct myChunk {
	size_t preSize;
	size_t size;
}*ChunkPointer;

typedef std::list<ChunkPointer> ChunkList;

ChunkList smallMemoryBin[64];

size_t alignTo(size_t fromNum,size_t toNum) {
	size_t temp = fromNum % toNum;
	return (toNum - temp + fromNum);
}
void myMallocInit(void) {
	heapMemory = VirtualAlloc(NULL, 1 << 16, MEM_COMMIT, \
		PAGE_EXECUTE_READWRITE);
	hMemoryLeft = 1 << 16;
}

char * myMalloc(size_t sizeOfMemoryInBytes) {
	if (sizeOfMemoryInBytes > 0 && sizeOfMemoryInBytes <= 512) {
		ChunkPointer newChunk = (ChunkPointer)heapMemory;
		size_t realDataSize = alignTo(sizeOfMemoryInBytes, MALLOC_ALIGNMENT);
		heapMemory =  (char *)heapMemory + realDataSize + 8;
		hMemoryLeft = hMemoryLeft - realDataSize - 8;
		newChunk->preSize = 0;
		newChunk->size = realDataSize;

		return (char *)newChunk + 8;

	}

	return NULL;
}

int main(void) {
	myMallocInit();
	std::cout << alignTo(1, MALLOC_ALIGNMENT) << std::endl;
	std::cout << alignTo(11, MALLOC_ALIGNMENT) << std::endl;
	std::cout << "HelloWorld!" << std::endl;


	char * testMemory = myMalloc(11);
	std::cin.get();
	return 0;
}