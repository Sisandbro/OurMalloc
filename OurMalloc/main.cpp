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

#define FASTBIN_CONSOLIDATION_THRESHOLD 1<<16

#define SET_PRE_IN_USE |1
#define SET_PRE_UNUSE &0xFFFFFFFE
#define PRE_USE_MASK &1
#define CHUNK_SIZE_MASK &0xFFFFFFF8
#define CHUNK_SIGN_MASK &7


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
	heapMemory = VirtualAlloc(NULL, 1 << 26, MEM_COMMIT, \
		PAGE_EXECUTE_READWRITE);
	hMemoryLeft = 1 << 26;
}

char * myMalloc(size_t sizeOfMemoryInBytes) {
	if (sizeOfMemoryInBytes > 0 && sizeOfMemoryInBytes <= SMALL_BIN_MAX_SIZE) {
		ChunkPointer newChunk = (ChunkPointer)heapMemory;
		size_t realDataSize = alignTo(sizeOfMemoryInBytes, MALLOC_ALIGNMENT);
		heapMemory =  (char *)heapMemory + realDataSize + 8;
		hMemoryLeft = hMemoryLeft - realDataSize - 8;   //�������ύҳ�����ݿ�+���ݴ�С��־��
		if (edgeChunk) {								//�������edgeChunk˵�����ǵ�һ�η����ڴ�,edgeChunk����ǰһ����,����ǰһ����Ҫô��fastBin��Ҫô����ʹ��
			newChunk->preSize = edgeChunk->size;
			newChunk->size = newChunk->size SET_PRE_IN_USE;
		}
		else {
			newChunk->preSize = 0;
			newChunk->size = newChunk->size SET_PRE_UNUSE;
		}

		newChunk->size = realDataSize;
		edgeChunk = newChunk;
		return (char *)newChunk + 8;					//�����·��������ݿ��ַ

	}

	return NULL;
}

int myFree(char * memToFree) {

	if (memToFree == NULL)
		return 0;

	ChunkPointer tempChunkPointer = (ChunkPointer)(memToFree - 8);  //���ݿ�ǰƫ��8λ����Chunkͷ



	if (tempChunkPointer->size <= MAX_FAST_SIZE && tempChunkPointer != edgeChunk) {
		fastBin.push_back(tempChunkPointer);
		return 1;
	}
	else {
		if (tempChunkPointer->size PRE_USE_MASK) {				//

		}
		else {
			//���ǰһ������кϲ������飬���˿�ΪegdeChunk��Ӧ�ı�edgeChunkָ��
			size_t mergedChunkSize = (tempChunkPointer->size CHUNK_SIZE_MASK) + tempChunkPointer->preSize;
			if (tempChunkPointer == edgeChunk) {
				edgeChunk = (ChunkPointer)((char *)tempChunkPointer - tempChunkPointer->preSize - 8);
			}
			tempChunkPointer = (ChunkPointer)((char *)tempChunkPointer - tempChunkPointer->preSize - 8);
			tempChunkPointer->size = (tempChunkPointer->size CHUNK_SIGN_MASK) + mergedChunkSize;

			if (tempChunkPointer == edgeChunk) {
				heapMemory = (PVOID)tempChunkPointer;
				hMemoryLeft = hMemoryLeft + (edgeChunk->size CHUNK_SIZE_MASK) + 8;
				edgeChunk = (ChunkPointer)((char *)edgeChunk - edgeChunk->preSize - 8);
			}
			else {
				ChunkPointer nextChunk = (ChunkPointer)((char *)tempChunkPointer + (tempChunkPointer->size CHUNK_SIZE_MASK) + 8);
				if (nextChunk != edgeChunk) {
					ChunkPointer nextNextChunk = (ChunkPointer)((char *)nextChunk + (nextChunk->size CHUNK_SIZE_MASK) + 8);
					if (nextNextChunk->size PRE_USE_MASK) {

					}
					else {
						tempChunkPointer->size = (tempChunkPointer->size CHUNK_SIGN_MASK) + (tempChunkPointer->size CHUNK_SIZE_MASK) + nextNextChunk->preSize;
						smallMemoryBin[0].push_back(tempChunkPointer);
						nextNextChunk->size = nextNextChunk->size SET_PRE_UNUSE;
					}
				}
			}

			if (tempChunkPointer->size > FASTBIN_CONSOLIDATION_THRESHOLD) {
				//TODO:Merging FastBin
			}

			return 2;
		}
	}


	//First Free Version 
	//if (edgeChunk == tempChunkPointer) {                            //����Ǳ�ԵChunk����ϲ���ԭʼ��

	//	if (tempChunkPointer->preSize == 0) {										//������Ψһһ��Chunk����edgeChunkΪNULL
	//		edgeChunk = NULL;
	//	}
	//	else {
	//		edgeChunk = (ChunkPointer)((char *)edgeChunk - edgeChunk->preSize - 8); //edgeChunkָ����һ����ӽ�TopChunk��Chunk
	//	}

	//	hMemoryLeft = hMemoryLeft + 8 + tempChunkPointer->size;					//����ԭʼ����Ϣ
	//	heapMemory = (char *)heapMemory - tempChunkPointer->size - 8;
	//	ZeroMemory((PVOID)heapMemory, tempChunkPointer->size + 8);				//��տ��ڴ�
	//}
	//else {																		//������Ǳ�ԵChunk
	//}


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