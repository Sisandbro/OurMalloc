#include <iostream>
#include <list>
#include <map>
#include <Windows.h>
#include <mutex>
#include <algorithm> 
#include <thread>

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

typedef std::list<ChunkPointer> ChunkList;
//const
LPVOID headOfMemory;

//�ٽ���Դ
ChunkList smallMemoryBin[64];  //bin[0]��unsortBin
ChunkList fastBin;
LPVOID heapMemory;
size_t hMemoryLeft;
ChunkPointer edgeChunk = NULL;
std::map<size_t, ChunkPointer> largeBin;	//����һ�ź����

//std::mutex cleanUnsortBinLock;
//Bin������
std::mutex largeBinLock;
std::mutex smallBinLock;
std::mutex fastBinLock;
//CAS����ϵ��
std::mutex CASAtomic;
bool compareAndSwapNumber(size_t *reg, size_t oldValue, size_t newValue) {
	CASAtomic.lock();
	if (*reg = oldValue) {
		*reg = newValue;
		CASAtomic.unlock();
		return true;
	}
	else {
		CASAtomic.unlock();
		return false;
	}
}

bool compareAndSwapChunkPointer(ChunkPointer * reg, ChunkPointer oldValue, ChunkPointer newValue) {
	CASAtomic.lock();
	if (*reg = oldValue) {
		*reg = newValue;
		CASAtomic.unlock();
		return true;
	}
	else {
		CASAtomic.unlock();
		return false;
	}
}
//����һ����ջ���Ӧ����û�������
//bool compareAndPushList(ChunkList tempList, ChunkPointer oldChunk, ChunkPointer newChunk) {
//	CASAtomic.lock();
//	if (*tempList.begin() == oldChunk) {
//		tempList.push_front(newChunk);
//		CASAtomic.unlock();
//		return true;
//	}
//	else {
//		CASAtomic.unlock();
//		return false;
//	}
//}

//bool compareAndPopList(ChunkList tempList, ChunkPointer oldChunk) {
//	CASAtomic.lock();
//	ChunkPointer tempChunk= *tempList.end();
//	if (*tempList.end() == oldChunk) {
//		tempList.pop_back();
//		CASAtomic.unlock();
//		return tempChunk;
//	}
//	else {
//		CASAtomic.unlock();
//		return false;
//	}
//}



//���ܣ�������������8���룬��С��16��ֱ�ӷ���16
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

//���ܣ�ʹ��Mallocǰ������ã��ύ����ר����64Mҳ��
void myMallocInit(void) {
	heapMemory = VirtualAlloc(NULL, 1 << 26, MEM_COMMIT, \
		PAGE_EXECUTE_READWRITE);
	headOfMemory = heapMemory;
	hMemoryLeft = (1 << 26) - 8;
}

//����smallbin
void putInSmallBin(ChunkPointer thisChunk) {
	int index = thisChunk->size CHUNK_SIZE_MASK / 8 - 1;
	ChunkPointer oldChunk;
	//do {
	//	oldChunk = *smallMemoryBin[index].begin();
	//} while (!compareAndPushList(smallMemoryBin[index],oldChunk,thisChunk));
	smallBinLock.lock();
	smallMemoryBin[index].push_front(thisChunk);
	smallBinLock.unlock();
}

//����largebin
void putInLargeBin(ChunkPointer thisChunk) {
	largeBinLock.lock();
	largeBin.insert(std::map<size_t, ChunkPointer>::value_type(thisChunk->size CHUNK_SIZE_MASK, thisChunk));
	largeBinLock.unlock();
}

//���ܣ��õ���һ��Chunk
ChunkPointer getNextChunk(ChunkPointer thisChunk) {
	ChunkPointer nextChunk = (ChunkPointer)((char *)thisChunk + (thisChunk->size CHUNK_SIZE_MASK) + 8);
	return nextChunk;
}

//���ܣ�����ԵChunk�ϲ���TopChunk
int mergeEdgeChunkToTopChunk(ChunkPointer chunkToMerge) {
	heapMemory = (PVOID)chunkToMerge;

	size_t oldMemLeft;
	do {
		oldMemLeft = hMemoryLeft;
	} while (!compareAndSwapNumber(&hMemoryLeft, oldMemLeft, oldMemLeft + (chunkToMerge->size CHUNK_SIZE_MASK) + 8));

	//hMemoryLeft = hMemoryLeft + (chunkToMerge->size CHUNK_SIZE_MASK) + 8;

	//����
	ZeroMemory((PVOID)heapMemory, (edgeChunk->size CHUNK_SIZE_MASK) + 8);
	if (((char *)edgeChunk - edgeChunk->preSize - 8) < (char *)headOfMemory) {
		edgeChunk = NULL;
	}
	else {
		edgeChunk = (ChunkPointer)(ChunkPointer)((char *)edgeChunk - edgeChunk->preSize - 8);
	}
	return 0;
}

//���ܣ��ж���һ��Chunk�Ƿ�ʹ����
int nextChunkIsInUse(ChunkPointer thisChunk) {
	ChunkPointer nextChunk = getNextChunk(thisChunk);
	if (nextChunk != edgeChunk) {					//ֻ�зǱ�ԵChunk���п�������һ��Chunk�����ܵõ�P���λ
		ChunkPointer nextNextChunk = getNextChunk(nextChunk);
		if (nextNextChunk->size PRE_USE_MASK) {
			return 1;
		}
		else {
			return 0;								//P��־λΪ0 ˵����һ��Chunk����ʹ��
		}
	}
	else {
		return 1;									//����һ��ChunkΪ��Ե�飬���Ȼ��ʹ���У�Ӧ���ǣ�
	}
}

//���ܣ����������ǰ�ϲ���ע����ȷ��ǰһ�����Ƿ�Ϊ���У����������ѡ���Ƿ��޸ı�Ե���ַ
//���أ����غϲ����Chunk
ChunkPointer mergeThisChunkToPreChunk(ChunkPointer thisChunk) {
	size_t mergedChunkSize = (thisChunk->size CHUNK_SIZE_MASK) + thisChunk->preSize + 8;		//�ϲ�֮���ʵ�ʴ�СΪ������ݿ�Ĵ�С��8Bsizeλ��ǰһ����Ĵ�С
	if (thisChunk == edgeChunk) {	
		ChunkPointer oldChunkPointer;
		do {
			oldChunkPointer = edgeChunk;
		} while (!compareAndSwapChunkPointer(&edgeChunk, oldChunkPointer,\
			(ChunkPointer)((char *)oldChunkPointer - oldChunkPointer->preSize - 8)));
		//edgeChunk = (ChunkPointer)((char *)edgeChunk - edgeChunk->preSize - 8);
	}
	thisChunk = (ChunkPointer)((char *)thisChunk - thisChunk->preSize - 8);	 //�˿��ַ�ƶ���ǰһ�����ַ
	thisChunk->size = (thisChunk->size CHUNK_SIGN_MASK) + mergedChunkSize;   //�����¿��size�������־λ
	if (thisChunk != edgeChunk) {
		ChunkPointer nextChunk = (ChunkPointer)((char *)thisChunk + (thisChunk->size CHUNK_SIZE_MASK) + 8);
		nextChunk->preSize = mergedChunkSize;								 //������Ǳ�Ե�飬Ӧ������һ��Chunk��preSize
	}

	return thisChunk;
}


//���ܣ���ǰ�ݹ�ϲ���ֱ���ϲ���ǰ��ȫ�����з���, ��ǰ�������У��򷵻ر���
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

//���ܣ����ô˿�Ϊ����״̬
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

//���ܣ���fastbin���ҵ�һ������Ҫ��Ŀ�
ChunkPointer searchFastBin(size_t sizeOfChunk) {
	ChunkList::iterator it;
	fastBinLock.lock();
	for (it = fastBin.begin(); it != fastBin.end(); it++) {
		ChunkPointer tempChunkPointer = *it;
		if (tempChunkPointer->size == sizeOfChunk) {
			fastBin.erase(it);
			return tempChunkPointer;
		}
	}
	fastBinLock.unlock();
	return NULL;
}

//���ܣ���smallbin���ҵ�һ������Ҫ��Ŀ�
ChunkPointer searchSmallBin(size_t sizeOfChunk) {
	int index = (sizeOfChunk / 8) - 1;      //����ͨ���򵥵Ĺ�ϣ�ӿ��С�õ�bin����
	if (smallMemoryBin[index].empty()) {
		return NULL;
	}
	else {
		ChunkList::iterator it = smallMemoryBin[index].end();   //��β���õ�һ����
		smallBinLock.lock();
		smallMemoryBin[index].pop_back();						//ɾ��β��
		smallBinLock.unlock();
		ChunkPointer tempChunkPointer = *it;

		return tempChunkPointer;
	}
}

//���ܣ���largebin�ﰴ���ŷ����㷨��һ�������Ҫ��Ŀ鲢�и�
ChunkPointer searchLargeBin(size_t sizeOfChunk) {
	std::map<size_t, ChunkPointer>::iterator it;
	largeBinLock.lock();
	for (it = largeBin.begin(); it != largeBin.end(); it++) {
		ChunkPointer tempChunkPointer = it->second;
		if ((tempChunkPointer->size CHUNK_SIZE_MASK) >= sizeOfChunk) {
			size_t leftChunkSize = (tempChunkPointer->size CHUNK_SIZE_MASK) - 8 - sizeOfChunk; //���и������ݿ��С�ͱ�־λ��С
			if (leftChunkSize < 16) {														   //Ϊ�˷�ֹ�Ժ��bug��ʣ�µĿ�̫С�Ļ��Ͷ��ָ��˼�
				largeBin.erase(it);
				largeBinLock.unlock();
				return tempChunkPointer;
			}
			//����ʣ�²��ֵĿ���Ϣ
			ChunkPointer leftChunkPointer = (ChunkPointer)((char *)tempChunkPointer + 8 + sizeOfChunk);
			leftChunkPointer->size = leftChunkSize SET_PRE_IN_USE;
			leftChunkPointer->preSize = sizeOfChunk;

			largeBin.erase(it);
			if (leftChunkSize > 512)
				putInLargeBin(leftChunkPointer);
			else {
				putInSmallBin(leftChunkPointer);
			}
			largeBinLock.unlock();
			return tempChunkPointer;
		}
	}
	largeBinLock.unlock();
	return NULL;
}



//���ܣ�����fastbin��������bin[0]
//�ϲ�˼·����������fastbin chunk������һ�����preUse��Ϊ0��Ȼ�����±���������preUseΪ0�ġ���ǰ�ݹ�ϲ���
void cleanFastBin(void) {
	ChunkList::iterator it;
	fastBinLock.lock();
	smallBinLock.lock();
	//�޸ı�־����
	for (it = fastBin.begin(); it != fastBin.end(); it++) {
		ChunkPointer tempChunkPointer = *it;
		ChunkPointer nextChunkPointer = getNextChunk(tempChunkPointer);
		nextChunkPointer->size = nextChunkPointer->size SET_PRE_UNUSE;
	}
	//�ϲ�����
	for (it = fastBin.begin(); it != fastBin.end(); it++) {
		ChunkPointer tempChunkPointer = *it;
		if (tempChunkPointer->size != 0) { //���sizeΪ0 ˵���˿��Ѿ����ϲ����������� 
			tempChunkPointer = mergeThisChuckForwardUntilInUseChunk(tempChunkPointer);					 //����ǰ���Ƿ��п��ж�������ǰ�ϲ��������bin[0]
			smallMemoryBin[0].push_front(tempChunkPointer);
			ZeroMemory((PVOID)((char*)tempChunkPointer + 8), (tempChunkPointer->size CHUNK_SIZE_MASK));  //���������ݿ����
		}
	}
	//�洢
	//for (it = fastBin.begin(); it != fastBin.end(); it++) {
	//	ChunkPointer tempChunk
	//}
	smallMemoryBin[0].erase(unique(smallMemoryBin[0].begin(), smallMemoryBin[0].end()), smallMemoryBin[0].end());
	//�ϲ����е�fastbin��� ��fastbin���
	fastBin.clear();
	smallBinLock.unlock();
	fastBinLock.unlock();
}

//���ܣ�����unsortbin����smallbin��largebin
void cleanUnsortBin(void) {
	ChunkList::iterator it;
	smallBinLock.lock();
	largeBinLock.lock();

	for (it = smallMemoryBin[0].begin(); it != smallMemoryBin[0].end(); it++) {
		ChunkPointer tempChunkPointer = *it;
		if (tempChunkPointer->size CHUNK_SIZE_MASK <= 512 && (tempChunkPointer->size CHUNK_SIZE_MASK) > 0) {
			putInSmallBin(tempChunkPointer);
		}
		else {
			putInLargeBin(tempChunkPointer);
		}
	}

	smallMemoryBin[0].clear();
	smallBinLock.unlock();
	largeBinLock.unlock();
}

//���ܣ���һ�������unsortBin
void putInUnsortBin(ChunkPointer thisChunk) {
	smallBinLock.lock();
	smallMemoryBin[0].push_front(thisChunk);
	setThisChunkUnuse(thisChunk);
	smallBinLock.unlock();
}
//���ܣ���unsortBin��һ�����ʵĿ��и��ֱ�ӷ���
ChunkPointer searchUnsortBin(size_t wannaSize) {
	smallBinLock.lock();
	ChunkPointer unsortBinResult = *smallMemoryBin[0].begin();
	size_t leftChunkSize = (unsortBinResult->size CHUNK_SIZE_MASK) - 8 - wannaSize; //���и������ݿ��С�ͱ�־λ��С
																					   //size_t theChunkSign = unsortBinResult->size CHUNK_SIGN_MASK;
																					   //size_t theChunkPreSize = unsortBinResult->preSize;
	if (leftChunkSize < 16) {														   //Ϊ�˷�ֹ�Ժ��bug��ʣ�µĿ�̫С�Ļ��Ͷ��ָ��˼�
		smallMemoryBin[0].pop_back();
		smallBinLock.unlock();
		return unsortBinResult;
	}
	//����ʣ�²��ֵĿ���Ϣ
	ChunkPointer leftChunkPoint = (ChunkPointer)((char *)unsortBinResult + 8 + wannaSize);
	leftChunkPoint->size = leftChunkSize SET_PRE_IN_USE;
	leftChunkPoint->preSize = wannaSize;

	smallMemoryBin[0].pop_back();													   //���и��Ļ���unsortbin
	smallMemoryBin[0].push_front(leftChunkPoint);


	unsortBinResult->size = wannaSize + unsortBinResult->size CHUNK_SIGN_MASK;      //�����и�������Ϣ ����size����
	smallBinLock.unlock();
	return unsortBinResult;
}

ChunkPointer newChunkFromTopChunk(size_t sizeOfMemoryInBytes) {
	size_t realDataSize = alignTo(sizeOfMemoryInBytes, MALLOC_ALIGNMENT);

	if (sizeOfMemoryInBytes > 0 && sizeOfMemoryInBytes<= hMemoryLeft) {
		ChunkPointer newChunk = (ChunkPointer)heapMemory;

		//CAS�����ı�ʣ���ڴ�
		size_t oldHMemoryLeft;
		do {
			oldHMemoryLeft = hMemoryLeft;
		} while (!compareAndSwapNumber(&hMemoryLeft, oldHMemoryLeft, oldHMemoryLeft - realDataSize - 8));

		//hMemoryLeft = hMemoryLeft - realDataSize - 8;   //�������ύҳ�����ݿ�+���ݴ�С��־��

		if (edgeChunk) {								//�������edgeChunk˵�����ǵ�һ�η����ڴ�,edgeChunk����ǰһ����,����ǰһ����Ҫô��fastBin��Ҫô����ʹ��
			newChunk->preSize = edgeChunk->size;
			newChunk->size = realDataSize;
			newChunk->size = newChunk->size SET_PRE_IN_USE;
		}
		else {
			newChunk->preSize = 0;
			newChunk->size = realDataSize;
			newChunk->size = newChunk->size SET_PRE_IN_USE; //���������edgeChunk ˵���ǵ�һ��Chunk��PӦ����Ϊinuse��ֹԽ��
		}

		edgeChunk = newChunk;
		return newChunk;				//�����·��������ݿ��ַ
	}
	else {
		return NULL;
	}
}
//MYMALLOC
char * myMalloc(size_t sizeOfMemoryInBytes) {
	size_t realDataSize;
	realDataSize = alignTo(sizeOfMemoryInBytes, MALLOC_ALIGNMENT);

	if (sizeOfMemoryInBytes > 0 && sizeOfMemoryInBytes <= MAX_FAST_SIZE) {				//����max_fast��Χ�ڣ����Ȳ���fastbin
		ChunkPointer fastBinResult;
		if (fastBinResult = searchFastBin(realDataSize)) {
			return (char*)fastBinResult + 8;											//�ҵ��ͷ������ݿ�
		}
	}

	if (sizeOfMemoryInBytes > 0 && sizeOfMemoryInBytes <= SMALL_BIN_MAX_SIZE) {			//û����fastbin�ҵ�������£���smallBin��Ѱ��
		ChunkPointer smallBinResult;
		if (smallBinResult = searchSmallBin(realDataSize)) {							
			return (char *)smallBinResult + 8;											//�ҵ��򷵻�
		}
		else {
			cleanFastBin();																//��û���ҵ���������fastBin
			if (smallMemoryBin[0].size() == 1 && ((* smallMemoryBin[0].begin()) ->size CHUNK_SIZE_MASK) >= realDataSize) {//��unsortBinֻ��һ�飬��ֱ���и����			
				ChunkPointer unsortBinResult = searchUnsortBin(realDataSize);
				return (char *)unsortBinResult + 8;
			}
			else {
				cleanUnsortBin();
				if (smallBinResult = searchSmallBin(realDataSize)) {							   //��������Ϊ�ٴ�����һ��smallbin�ȽϺ�
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

	ChunkPointer tempChunkPointer = (ChunkPointer)(memToFree - 8);  //���ݿ�ǰƫ��8λ����Chunkͷ



	if (tempChunkPointer->size <= MAX_FAST_SIZE && tempChunkPointer != edgeChunk) {		//С��64B���Ҳ��Ǳ�Ե��ĲŻᱻ����fastBin
		fastBin.push_front(tempChunkPointer);
		return 1;
	}
	else {
		if (tempChunkPointer->size PRE_USE_MASK) {				
			//���ǰһ����û�п��У��Ǿ�ֱ�ӿ��Ǵ˿��Ƿ�Ϊ��Ե��
			if (tempChunkPointer == edgeChunk) {
				mergeEdgeChunkToTopChunk(tempChunkPointer);	//����ǣ��ϲ���TopChunk
			}
			else {
				//�ж���һ�����Ƿ���ʹ�ã������кϲ���������bin[0]��
				if (nextChunkIsInUse(tempChunkPointer)) {
					putInUnsortBin(tempChunkPointer);
				}
				else {
					ChunkPointer nextChunk = getNextChunk(tempChunkPointer);
					nextChunk = mergeThisChunkToPreChunk(nextChunk);
					if (nextChunk == tempChunkPointer) {					//����Bin�в�����Ϊ����
						putInUnsortBin(tempChunkPointer);
					}
					else {
						//����ϲ����ָ�벻��ͬ�������
					}
				}
			}
		}
		else {
			//���ǰһ���������ϲ�������
			tempChunkPointer = mergeThisChunkToPreChunk(tempChunkPointer);

			if (tempChunkPointer == edgeChunk) {
				mergeEdgeChunkToTopChunk(tempChunkPointer);
			}
			else {
				//�ж���һ�����Ƿ���ʹ�ã������кϲ���������bin[0]��
				if (nextChunkIsInUse(tempChunkPointer)) {
					putInUnsortBin(tempChunkPointer);
				}
				else {
					ChunkPointer nextChunk = getNextChunk(tempChunkPointer);
					nextChunk = mergeThisChunkToPreChunk(nextChunk);
					if (nextChunk == tempChunkPointer) {					//����Bin�в�����Ϊ����
						putInUnsortBin(tempChunkPointer);
					}
					else {
						//����ϲ����ָ�벻��ͬ�������
					}
				}
			}

			if (tempChunkPointer->size > FASTBIN_CONSOLIDATION_THRESHOLD) {
				//Merging FastBin
				cleanFastBin();
			}

			return 2;
		}
	}

	return 0;
}
void test(void) {
	char * temp = myMalloc(500);

	myFree(temp);

	char * temp2 = myMalloc(10);

	myFree(temp2);

	char * temp3 = myMalloc(102400);

	myFree(temp3);

}

int main(void) {
	myMallocInit();
	std::cout << alignTo(1, MALLOC_ALIGNMENT) << std::endl;
	std::cout << alignTo(511, MALLOC_ALIGNMENT) << std::endl;
	std::cout << "HelloWorld!" << std::endl;


	//char * testMemory = myMalloc(11);
	for (int i = 0; i < 100; i++) {
		std::thread t(test);
		t.join();
	}

	//myFree(testMemory);
	//testMemory = myMalloc(1024);
	//myFree(testMemory);
	//testMemory = myMalloc(512);
	//myFree(testMemory);
	//testMemory = myMalloc(1024000);
	//myFree(testMemory);
	std::cin.get();
	return 0;
}
