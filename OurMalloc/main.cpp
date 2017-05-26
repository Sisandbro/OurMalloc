#include <iostream>
#include <list>
#include <map>
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

size_t NumberOfChunks = 0;

typedef std::list<ChunkPointer> ChunkList;

ChunkList smallMemoryBin[64];  //bin[0]是unsortBin
ChunkList fastBin;

std::map<size_t, ChunkPointer> largeBin;	//创建一颗红黑树


//功能：将申请数量向8对齐，若小于16，直接返回16
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

//功能：使用Malloc前必须调用，提交进程专属的64M页面
void myMallocInit(void) {
	heapMemory = VirtualAlloc(NULL, 1 << 26, MEM_COMMIT, \
		PAGE_EXECUTE_READWRITE);
	hMemoryLeft = (1 << 26) - 8;
}

//放入smallbin
void putInSmallBin(ChunkPointer thisChunk) {
	int index = thisChunk->size CHUNK_SIZE_MASK / 8 - 1;
	smallMemoryBin[index].push_front(thisChunk);
}

//放入largebin
void putInLargeBin(ChunkPointer thisChunk) {
	largeBin.insert(std::map<size_t, ChunkPointer>::value_type(thisChunk->size CHUNK_SIZE_MASK, thisChunk));
}

//功能：得到下一个Chunk
ChunkPointer getNextChunk(ChunkPointer thisChunk) {
	ChunkPointer nextChunk = (ChunkPointer)((char *)thisChunk + (thisChunk->size CHUNK_SIZE_MASK) + 8);
	return nextChunk;
}

//功能：将边缘Chunk合并进TopChunk
int mergeEdgeChunkToTopChunk(ChunkPointer chunkToMerge) {
	heapMemory = (PVOID)chunkToMerge;
	hMemoryLeft = hMemoryLeft + (chunkToMerge->size CHUNK_SIZE_MASK) + 8;

	//清零
	ZeroMemory((PVOID)heapMemory, (edgeChunk->size CHUNK_SIZE_MASK) + 8);

	edgeChunk = (ChunkPointer)(ChunkPointer)((char *)edgeChunk - edgeChunk->preSize - 8);
	return 0;
}

//功能：判断下一个Chunk是否使用中
int nextChunkIsInUse(ChunkPointer thisChunk) {
	ChunkPointer nextChunk = getNextChunk(thisChunk);
	if (nextChunk != edgeChunk) {					//只有非边缘Chunk才有可能有下一个Chunk，才能得到P标记位
		ChunkPointer nextNextChunk = getNextChunk(nextChunk);
		if (nextNextChunk->size PRE_USE_MASK) {
			return 1;
		}
		else {
			return 0;								//P标志位为0 说明下一个Chunk不在使用
		}
	}
	else {
		return 1;									//若下一个Chunk为边缘块，则必然在使用中（应该是）
	}
}

//功能：将这个块向前合并，注意先确定前一个块是否为空闲，并根据情况选择是否修改边缘块地址
//返回：返回合并后的Chunk
ChunkPointer mergeThisChunkToPreChunk(ChunkPointer thisChunk) {
	size_t mergedChunkSize = (thisChunk->size CHUNK_SIZE_MASK) + thisChunk->preSize + 8;		//合并之后的实际大小为这个数据块的大小加8Bsize位加前一个块的大小
	if (thisChunk == edgeChunk) {				
		edgeChunk = (ChunkPointer)((char *)edgeChunk - edgeChunk->preSize - 8);
	}
	thisChunk = (ChunkPointer)((char *)thisChunk - thisChunk->preSize - 8);	 //此块地址移动到前一个块地址
	thisChunk->size = (thisChunk->size CHUNK_SIGN_MASK) + mergedChunkSize;   //更新新块的size并补充标志位
	if (thisChunk != edgeChunk) {
		ChunkPointer nextChunk = (ChunkPointer)((char *)thisChunk + (thisChunk->size CHUNK_SIZE_MASK) + 8);
		nextChunk->preSize = mergedChunkSize;								 //如果不是边缘块，应更新下一个Chunk的preSize
	}

	return thisChunk;
}


//功能：向前递归合并，直至合并完前方全部空闲方块, 若前方不空闲，则返回本身
ChunkPointer mergeThisChuckForwardUntilInUseChunk(ChunkPointer thisChunk) {
	ChunkPointer tempChunk = thisChunk;
	if (tempChunk->size PRE_USE_MASK) {
		return tempChunk;
	}
	else {
		tempChunk = mergeThisChunkToPreChunk(tempChunk);
		tempChunk = mergeThisChuckForwardUntilInUseChunk(tempChunk);
		return tempChunk;
	}
}

//功能：设置此块为空闲状态
int setThisChunkUnuse(ChunkPointer thisChunk) {
	if (thisChunk != edgeChunk) {
		ChunkPointer nextChunk = getNextChunk(thisChunk);
		nextChunk->size = nextChunk->size SET_PRE_UNUSE;
		return 1;
	}
	else {
		return 0;
	}
}

//功能：在fastbin里找到一个符合要求的块
ChunkPointer searchFastBin(size_t sizeOfChunk) {
	ChunkList::iterator it;
	for (it = fastBin.begin(); it != fastBin.end(); it++) {
		ChunkPointer tempChunkPointer = *it;
		if (tempChunkPointer->size == sizeOfChunk) {
			fastBin.erase(it);
			return tempChunkPointer;
		}
	}
	return NULL;
}

//功能：在smallbin里找到一个符合要求的块
ChunkPointer searchSmallBin(size_t sizeOfChunk) {
	int index = (sizeOfChunk / 8) - 1;      //可以通过简单的哈希从块大小得到bin索引
	if (smallMemoryBin[index].empty()) {
		return NULL;
	}
	else {
		ChunkList::iterator it = smallMemoryBin[index].end();   //从尾部得到一个块
		smallMemoryBin[index].pop_back();						//删除尾部
		ChunkPointer tempChunkPointer = *it;

		return tempChunkPointer;
	}
}

//功能：在largebin里按最优分配算法找一个最符合要求的块并切割
ChunkPointer searchLargeBin(size_t sizeOfChunk) {
	std::map<size_t, ChunkPointer>::iterator it;

	for (it = largeBin.begin(); it != largeBin.end(); it++) {
		ChunkPointer tempChunkPointer = it->second;
		if (tempChunkPointer->size CHUNK_SIZE_MASK >= sizeOfChunk) {
			size_t leftChunkSize = (tempChunkPointer->size CHUNK_SIZE_MASK) - 8 - sizeOfChunk; //被切割走数据块大小和标志位大小
			if (leftChunkSize < 16) {														   //为了防止以后出bug，剩下的块太小的话就都分给人家
				largeBin.erase(it);
				return tempChunkPointer;
			}
			//创建剩下部分的块信息
			ChunkPointer leftChunkPointer = (ChunkPointer)((char *)tempChunkPointer + 8 + sizeOfChunk);
			leftChunkPointer->size = leftChunkSize SET_PRE_IN_USE;
			leftChunkPointer->preSize = sizeOfChunk;

			largeBin.erase(it);
			if (leftChunkSize > 512)
				putInLargeBin(leftChunkPointer);
			else {
				putInSmallBin(leftChunkPointer);
			}

			return tempChunkPointer;
		}
	}

	return NULL;
}



//功能：整理fastbin并连接入bin[0]
//合并思路：遍历所有fastbin chunk将其下一个块的preUse置为0，然后重新遍历所有找preUse为0的。向前递归合并。
void cleanFastBin(void) {
	ChunkList::iterator it;
	//修改标志过程
	for (it = fastBin.begin(); it != fastBin.end(); it++) {
		ChunkPointer tempChunkPointer = *it;
		ChunkPointer nextChunkPointer = getNextChunk(tempChunkPointer);
		nextChunkPointer->size = nextChunkPointer->size SET_PRE_UNUSE;
	}
	//合并过程
	for (it = fastBin.begin(); it != fastBin.end(); it++) {
		ChunkPointer tempChunkPointer = *it;
		if (tempChunkPointer->size != 0) { //如果size为0 说明此块已经被合并进其他块了 
			tempChunkPointer = mergeThisChuckForwardUntilInUseChunk(tempChunkPointer);					 //无论前方是否有空闲都尝试向前合并结果加入bin[0]
			smallMemoryBin[0].push_front(tempChunkPointer);
			ZeroMemory((PVOID)((char*)tempChunkPointer + 8), (tempChunkPointer->size CHUNK_SIZE_MASK));  //仅仅把数据块清空
		}
	}

	//合并所有的fastbin块后 将fastbin清空
	fastBin.clear();
}

//功能：整理unsortbin进入smallbin和largebin
void cleanUnsortBin(void) {
	ChunkList::iterator it;

	for (it = smallMemoryBin[0].begin(); it != smallMemoryBin[0].end(); it++) {
		ChunkPointer tempChunkPointer = *it;
		if (tempChunkPointer->size CHUNK_SIZE_MASK <= 512 && tempChunkPointer->size CHUNK_SIZE_MASK > 0) {
			putInSmallBin(tempChunkPointer);
		}
		else {
			putInLargeBin(tempChunkPointer);
		}
	}

	smallMemoryBin[0].clear();
}

//

ChunkPointer newChunkFromTopChunk(size_t sizeOfMemoryInBytes) {
	size_t realDataSize = alignTo(sizeOfMemoryInBytes, MALLOC_ALIGNMENT);
	if (sizeOfMemoryInBytes > 0 && sizeOfMemoryInBytes<= hMemoryLeft) {
		ChunkPointer newChunk = (ChunkPointer)heapMemory;

		heapMemory = (char *)heapMemory + realDataSize + 8;
		hMemoryLeft = hMemoryLeft - realDataSize - 8;   //消耗已提交页共数据块+数据大小标志区
		if (edgeChunk) {								//如果存在edgeChunk说明并非第一次分配内存,edgeChunk将是前一个块,并且前一个块要么在fastBin里要么还在使用
			newChunk->preSize = edgeChunk->size;
			newChunk->size = newChunk->size SET_PRE_IN_USE;
		}
		else {
			newChunk->preSize = 0;
			newChunk->size = newChunk->size SET_PRE_UNUSE;
		}

		newChunk->size = realDataSize;
		edgeChunk = newChunk;
		if (NumberOfChunks == 0) {
			newChunk->size = newChunk->size SET_PRE_IN_USE;
		}
		NumberOfChunks++;
		return newChunk;				//返回新分配块的数据块地址
	}
	else {
		return NULL;
	}
}
//MYMALLOC
char * myMalloc(size_t sizeOfMemoryInBytes) {
	size_t realDataSize;
	realDataSize = alignTo(sizeOfMemoryInBytes, MALLOC_ALIGNMENT);

	if (sizeOfMemoryInBytes > 0 && sizeOfMemoryInBytes <= MAX_FAST_SIZE) {				//若在max_fast范围内，优先查找fastbin
		ChunkPointer fastBinResult;
		if (fastBinResult = searchFastBin(realDataSize)) {
			return (char*)fastBinResult + 8;											//找到就返回数据块
		}
	}

	if (sizeOfMemoryInBytes > 0 && sizeOfMemoryInBytes <= SMALL_BIN_MAX_SIZE) {			//没能在fastbin找到的情况下，在smallBin中寻找
		ChunkPointer smallBinResult;
		if (smallBinResult = searchSmallBin(realDataSize)) {							
			return (char *)smallBinResult + 8;											//找到则返回
		}
		else {
			cleanFastBin();																//若没有找到，则整理fastBin
			if (smallMemoryBin[0].size() == 1 && ((* smallMemoryBin[0].begin()) ->size CHUNK_SIZE_MASK) >= realDataSize) {											//若unsortBin只有一块，那直接切割最方便
				ChunkPointer unsortBinResult = * smallMemoryBin[0].begin();
				size_t leftChunkSize = (unsortBinResult->size CHUNK_SIZE_MASK) - 8 - realDataSize; //被切割走数据块大小和标志位大小
				//size_t theChunkSign = unsortBinResult->size CHUNK_SIGN_MASK;
				//size_t theChunkPreSize = unsortBinResult->preSize;
				if (leftChunkSize < 16) {														   //为了防止以后出bug，剩下的块太小的话就都分给人家
					smallMemoryBin[0].pop_back();
					return (char *)unsortBinResult + 8;
				}
																								   //创建剩下部分的块信息
				ChunkPointer leftChunkPoint = (ChunkPointer)((char *)unsortBinResult + 8 + realDataSize);
				leftChunkPoint->size = leftChunkSize SET_PRE_IN_USE;
				leftChunkPoint->preSize = realDataSize;

				smallMemoryBin[0].pop_back();													   //将切割后的换回unsortbin
				smallMemoryBin[0].push_front(leftChunkPoint);


				unsortBinResult->size = realDataSize + unsortBinResult->size CHUNK_SIGN_MASK;      //更新切割出块的信息 仅有size变了

				return (char *)unsortBinResult + 8;
			}
			else {
				cleanUnsortBin();
				if (smallBinResult = searchSmallBin(realDataSize)) {							   //这里我认为再次搜索一次smallbin比较好
					return (char *)smallBinResult + 8;
				}
				ChunkPointer largeBinResult;
				if (largeBinResult = searchLargeBin(realDataSize)) {
					return (char *)largeBinResult + 8;
				}
				else {
					return (char*)newChunkFromTopChunk(realDataSize) + 8;
				}

			}
		}
	}
	else {
		ChunkPointer aLargeChunkToFind;
		aLargeChunkToFind = searchLargeBin(sizeOfMemoryInBytes);
		if (aLargeChunkToFind) {
			return (char *)aLargeChunkToFind + 8;
		}
		else {
			return (char *)newChunkFromTopChunk(sizeOfMemoryInBytes) + 8;
		}
	}

	return NULL;
}

//MYFREE
int myFree(char * memToFree) {

	if (memToFree == NULL)
		return 0;

	ChunkPointer tempChunkPointer = (ChunkPointer)(memToFree - 8);  //数据块前偏移8位才是Chunk头



	if (tempChunkPointer->size <= MAX_FAST_SIZE && tempChunkPointer != edgeChunk) {		//小于64B并且不是边缘块的才会被加入fastBin
		fastBin.push_front(tempChunkPointer);
		return 1;
	}
	else {
		if (tempChunkPointer->size PRE_USE_MASK) {				
			//如果前一个块没有空闲，那就直接考虑此块是否为边缘块
			if (tempChunkPointer == edgeChunk) {
				mergeEdgeChunkToTopChunk(tempChunkPointer);	//如果是，合并回TopChunk
			}
			else {
				//判断下一个块是否在使用，若空闲合并，并加入bin[0]，
				if (nextChunkIsInUse(tempChunkPointer)) {

				}
				else {
					ChunkPointer nextChunk = getNextChunk(tempChunkPointer);
					nextChunk = mergeThisChunkToPreChunk(nextChunk);
					if (nextChunk == tempChunkPointer) {					//加入Bin中并设置为空闲
						smallMemoryBin[0].push_front(tempChunkPointer);
						setThisChunkUnuse(tempChunkPointer);
					}
					else {
						//如果合并后的指针不相同，则出错
					}
				}
			}
		}
		else {
			//如果前一个块空闲则合并两个块
			tempChunkPointer = mergeThisChunkToPreChunk(tempChunkPointer);

			if (tempChunkPointer == edgeChunk) {
				mergeEdgeChunkToTopChunk(tempChunkPointer);
			}
			else {
				//判断下一个块是否在使用，若空闲合并，并加入bin[0]，
				if (nextChunkIsInUse(tempChunkPointer)) {

				}
				else {
					ChunkPointer nextChunk = getNextChunk(tempChunkPointer);
					nextChunk = mergeThisChunkToPreChunk(nextChunk);
					if (nextChunk == tempChunkPointer) {					//加入Bin中并设置为空闲
						smallMemoryBin[0].push_front(tempChunkPointer);
						setThisChunkUnuse(tempChunkPointer);	
					}
					else {
						//如果合并后的指针不相同，则出错
					}
				}

				//重构前代码，不要删，下方代码应该和上方代码功能相同。功能：判断下一个块是否在使用，若空闲合并，并加入bin[0]，
				//ChunkPointer nextChunk = (ChunkPointer)((char *)tempChunkPointer + (tempChunkPointer->size CHUNK_SIZE_MASK) + 8);
				//if (nextChunk != edgeChunk) {
				//	ChunkPointer nextNextChunk = (ChunkPointer)((char *)nextChunk + (nextChunk->size CHUNK_SIZE_MASK) + 8);
				//	if (nextNextChunk->size PRE_USE_MASK) {

				//	}
				//	else {
				//		tempChunkPointer->size = (tempChunkPointer->size CHUNK_SIGN_MASK) + (tempChunkPointer->size CHUNK_SIZE_MASK) + nextNextChunk->preSize;
				//		smallMemoryBin[0].push_back(tempChunkPointer);
				//		nextNextChunk->size = nextNextChunk->size SET_PRE_UNUSE;
				//	}
				//}
			}

			if (tempChunkPointer->size > FASTBIN_CONSOLIDATION_THRESHOLD) {
				//Merging FastBin
				cleanFastBin();
			}

			return 2;
		}
	}


	//First Free Version 
	//if (edgeChunk == tempChunkPointer) {                            //如果是边缘Chunk，则合并回原始堆

	//	if (tempChunkPointer->preSize == 0) {										//若这是唯一一个Chunk了则edgeChunk为NULL
	//		edgeChunk = NULL;
	//	}
	//	else {
	//		edgeChunk = (ChunkPointer)((char *)edgeChunk - edgeChunk->preSize - 8); //edgeChunk指向下一个最接近TopChunk的Chunk
	//	}

	//	hMemoryLeft = hMemoryLeft + 8 + tempChunkPointer->size;					//更新原始堆信息
	//	heapMemory = (char *)heapMemory - tempChunkPointer->size - 8;
	//	ZeroMemory((PVOID)heapMemory, tempChunkPointer->size + 8);				//清空块内存
	//}
	//else {																		//如果不是边缘Chunk
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
	testMemory = myMalloc(1024);
	myFree(testMemory);
	testMemory = myMalloc(512);
	myFree(testMemory);
	testMemory = myMalloc(1024000);
	myFree(testMemory);
	std::cin.get();
	return 0;
}