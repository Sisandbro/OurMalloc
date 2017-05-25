#include <iostream>
#include <list>
#include <Windows.h>
#include <atomic>

//32Bit system size_t is 4 bytes
#ifndef INTERNAL_SIZE_T 
#define INTERNAL_SIZE_T size_t 
#endif
#define SIZE_SZ                (sizeof(INTERNAL_SIZE_T)) 
//Align to 8 bytes 
#ifndef MALLOC_ALIGNMENT 
#define MALLOC_ALIGNMENT       (2 * SIZE_SZ) 
#endif

#define MAX_FAST_SIZE 64
#define SMALL_BIN_MAX_SIZE 512

typedef struct myChunk {
	size_t preSize;
	size_t size;
}*ChunkPointer;

LPVOID heapMemory;
size_t hMemoryLeft;
ChunkPointer edgeChunk;


typedef std::list<ChunkPointer> ChunkList;

ChunkList smallMemoryBin[64];
ChunkList fastBin;

size_t alignTo(size_t fromNum,size_t toNum) {
	size_t temp = fromNum % toNum;
	size_t resultNumber = (toNum - temp + fromNum);
	if (resultNumber < 16) {
		return 16;
	}
	else {
		return resultNumber;
	}
}

void myMallocInit(void) {
	heapMemory = VirtualAlloc(NULL, 1 << 16, MEM_COMMIT, \
		PAGE_EXECUTE_READWRITE);
	hMemoryLeft = 1 << 16;
}

char * myMalloc(size_t sizeOfMemoryInBytes) {
	if (sizeOfMemoryInBytes > 0 && sizeOfMemoryInBytes <= SMALL_BIN_MAX_SIZE) {
		ChunkPointer newChunk = (ChunkPointer)heapMemory;
		size_t realDataSize = alignTo(sizeOfMemoryInBytes, MALLOC_ALIGNMENT);
		heapMemory =  (char *)heapMemory + realDataSize + 8;
		hMemoryLeft = hMemoryLeft - realDataSize - 8;   //消耗已提交页共数据块+数据大小标志区
		newChunk->preSize = 0;							//TODO: 如何得到前一个块的大小
		newChunk->size = realDataSize;
		edgeChunk = newChunk;
		return (char *)newChunk + 8;					//返回新分配块的数据块地址

	}

	return NULL;
}

int * myFree(char * memToFree) {

	if (memToFree == NULL)
		return 0;

	ChunkPointer tempChunkPointer = (ChunkPointer)(memToFree - 8);  //数据块前偏移8位才是Chunk头

	if (edgeChunk == tempChunkPointer) {                            //如果是边缘Chunk，则合并回原始堆

		if (tempChunkPointer->preSize == 0) {										//若这是唯一一个Chunk了则edgeChunk为NULL
			edgeChunk = NULL;
		}
		else {
			edgeChunk = (ChunkPointer)((char *)edgeChunk - edgeChunk->preSize - 8); //edgeChunk指向下一个最接近TopChunk的Chunk
		}

		hMemoryLeft = hMemoryLeft + 8 + tempChunkPointer->size;					//更新原始堆信息
		heapMemory = (char *)heapMemory - tempChunkPointer->size - 8;
		ZeroMemory((PVOID)heapMemory, tempChunkPointer->size + 8);				//清空块内存
	}
	else {															//如果不是边缘Chunk
		fastBin.push_back(tempChunkPointer);
	}
	return 0;
}

int main(void) {
	myMallocInit();
	std::cout << alignTo(1, MALLOC_ALIGNMENT) << std::endl;
	std::cout << alignTo(511, MALLOC_ALIGNMENT) << std::endl;
	std::cout << "HelloWorld!" << std::endl;


	char * testMemory = myMalloc(11);

	myFree(testMemory);
	std::cin.get();
	return 0;
}