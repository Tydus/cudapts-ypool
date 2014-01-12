//===
// by xolokram/TB
// 2013
//===

#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <map>
#include <inttypes.h>
#include <sys/mman.h>

#include "main_poolminer.hpp"
#include "global.h"

#if defined(__GNUG__) && !defined(__MINGW__)
#include <sys/time.h> //depr?
#include <sys/resource.h>
#elif defined(__MINGW32__) || defined(__MINGW64__)
#include <windows.h>
#endif

#define VERSION_MAJOR 0
#define VERSION_MINOR 8
#define VERSION_EXT "GPU0.2 <experimental>"

#define MAX_THREADS 64

/*********************************
 * global variables, structs and extern functions
 *********************************/

int COLLISION_TABLE_BITS;
bool use_avxsse4;
size_t thread_num_max;
static bool running;

/* bleah this shouldn't be global so we can run one instance
 * on all GPUs. */
int gpu_device_id = 0;

/*********************************
 * class CBlockProviderGW to (incl. SUBMIT_BLOCK)
 *********************************/

class CBlockProviderGW : public CBlockProvider {
public:

	CBlockProviderGW() : CBlockProvider() {}

  virtual ~CBlockProviderGW() { /* TODO */ }

	virtual blockHeader_t* getBlock(unsigned int thread_id) {
		blockHeader_t* block = NULL;
		EnterCriticalSection(&workDataSource.cs_work);
		if( workDataSource.height > 0 ) {
            // ======== xpt ========
            minerProtosharesBlock_t minerProtosharesBlock = {0};
			// get work data
			minerProtosharesBlock.version = workDataSource.version;
			//minerProtosharesBlock.nTime = workDataSource.nTime;
			minerProtosharesBlock.nTime = (uint32)time(NULL);
			minerProtosharesBlock.nBits = workDataSource.nBits;
			minerProtosharesBlock.nonce = 0;
			minerProtosharesBlock.height = workDataSource.height;
			memcpy(minerProtosharesBlock.merkleRootOriginal, workDataSource.merkleRootOriginal, 32);
			memcpy(minerProtosharesBlock.prevBlockHash, workDataSource.prevBlockHash, 32);
			memcpy(minerProtosharesBlock.targetShare, workDataSource.targetShare, 32);
			minerProtosharesBlock.uniqueMerkleSeed = uniqueMerkleSeedGenerator;
			uniqueMerkleSeedGenerator++;
			// generate merkle root transaction
			bitclient_generateTxHash(sizeof(uint32), (uint8*)&minerProtosharesBlock.uniqueMerkleSeed, workDataSource.coinBase1Size, workDataSource.coinBase1, workDataSource.coinBase2Size, workDataSource.coinBase2, workDataSource.txHash);
			bitclient_calculateMerkleRoot(workDataSource.txHash, workDataSource.txHashCount+1, minerProtosharesBlock.merkleRoot);
            // ======== local ========
			block = new blockHeader_t;
            block->nVersion = workDataSource.version;
            block->nTime = (uint32_t)time(NULL);
            block->nBits = workDataSource.nBits;
            block->nNonce = 0;
			memcpy(block->hashPrevBlock, workDataSource.prevBlockHash, 32);
			memcpy(block->targetShare, workDataSource.targetShare, 32);
            memcpy(block->hashMerkleRoot, minerProtosharesBlock.merkleRoot, 32);

            _merkleRootReg.insert(std::make_pair(
                        std::vector<uint8_t>(block->hashMerkleRoot, block->hashMerkleRoot + 32), minerProtosharesBlock));
        }
		LeaveCriticalSection(&workDataSource.cs_work);
		return block;
	}

	void submitBlock(blockHeader_t *block, unsigned int thread_id) {
        blockHeader_t submitblock; //!
        memcpy((unsigned char*)&submitblock, (unsigned char*)block, 88);
        std::cout << "[WORKER] collision found: " << submitblock.birthdayA << " <-> " << submitblock.birthdayB << " #" << totalCollisionCount << " @ " << submitblock.nTime << " by " << thread_id << std::endl;
        totalShareCount += 1;

        std::vector<uint8_t> key(submitblock.hashMerkleRoot, submitblock.hashMerkleRoot + 32);
        minerProtosharesBlock_t& b = _merkleRootReg[key];

        b.birthdayA = submitblock.birthdayA;
        b.birthdayB = submitblock.birthdayB;

        jhProtominer_submitShare(&b);

        _merkleRootReg.erase(key);
	}

protected:

    std::map<std::vector<uint8_t>, minerProtosharesBlock_t> _merkleRootReg;

};

CBlockProvider *createBlockProvider()
{
    return new CBlockProviderGW();
}

/*********************************
 * multi-threading
 *********************************/

class CWorkerThread { // worker=miner
public:

	CWorkerThread(unsigned int id, CBlockProvider *bprovider)
		: _id(id), _bprovider(bprovider), _thread(&CWorkerThread::run, this) {

	}

  template<int COLLISION_TABLE_SIZE, int COLLISION_KEY_MASK, int CTABLE_BITS, SHAMODE shamode>
	void mineloop() {
		blockHeader_t* thrblock = NULL;
		while (running) {
			thrblock = _bprovider->getBlock(_id);
			if (thrblock != NULL) {
			  protoshares_process_512<COLLISION_TABLE_SIZE,COLLISION_KEY_MASK,CTABLE_BITS,shamode>(thrblock, _bprovider, _id, _gpu, _hashblock);
			} else {
				boost::this_thread::sleep(boost::posix_time::seconds(1));
            }
		}
	}

  template<SHAMODE shamode>
	void mineloop_start() {
	  mineloop<(1<<21),(0xFFFFFFFF<<(32-(32-21))),21,shamode>();
	}

  void run() {
    std::cout << "[WORKER" << _id << "] starting" << std::endl;

    /* Ensure that thread is pinned to its allocation */
    _hashblock = (uint64_t *)malloc(sizeof(uint64_t) * GPUHasher::N_RESULTS);
    _gpu = new GPUHasher(gpu_device_id);
    _gpu->Initialize();

        std::cout << "[WORKER" << _id << "] GoGoGo!" << std::endl;
        boost::this_thread::sleep(boost::posix_time::seconds(1));
        if (use_avxsse4)
            mineloop_start<AVXSSE4>(); // <-- work loop
        else
            mineloop_start<SPHLIB>(); // ^
        std::cout << "[WORKER" << _id << "] Bye Bye!" << std::endl;
    }

protected:
  unsigned int _id;
  CBlockProvider  *_bprovider;
  GPUHasher *_gpu;
  uint64_t *_hashblock;
  boost::thread _thread;
};

void start_worker_thread(int n, CBlockProvider *bprovider)
{
    CWorkerThread *worker = new CWorkerThread(n, bprovider);
}

/*********************************
 * exit / end / shutdown
 *********************************/

void exit_handler() {
	//cleanup for not-retarded OS
	running = false;
}

#if defined(__MINGW32__) || defined(__MINGW64__)

//#define WIN32_LEAN_AND_MEAN
//#include <windows.h>

BOOL WINAPI ctrl_handler(DWORD dwCtrlType) {
	//'special' cleanup for windows
	switch(dwCtrlType) {
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT: {
			running = false;
		} break;
		default: break;
	}
	return FALSE;
}

#elif defined(__GNUG__) && !defined(__APPLE__)

static sighandler_t set_signal_handler (int signum, sighandler_t signalhandler) {
  struct sigaction new_sig, old_sig;
  new_sig.sa_handler = signalhandler;
  sigemptyset (&new_sig.sa_mask);
  new_sig.sa_flags = SA_RESTART;
  if (sigaction (signum, &new_sig, &old_sig) < 0)
    return SIG_ERR;
  return old_sig.sa_handler;
}

void ctrl_handler(int signum) {
  exit(1);
}

#endif

/*********************************
* main - this is where it begins
*********************************/
int main2()
{

  running = true;

#if defined(__MINGW32__) || defined(__MINGW64__)
  SetConsoleCtrlHandler(ctrl_handler, TRUE);
#elif defined(__GNUG__) && !defined(__APPLE__)
  set_signal_handler(SIGINT, ctrl_handler);
#endif

	const int atexit_res = std::atexit(exit_handler);
	if (atexit_res != 0)
		std::cerr << "atexit registration failed, shutdown will be dirty!" << std::endl;

	// init everything:
	thread_num_max = 1; //GetArg("-genproclimit", 1); // what about boost's hardware_concurrency() ?
    // ??? Use last gpu device only
	gpu_device_id = commandlineInput.numThreads;
	COLLISION_TABLE_BITS = 21;

	if (thread_num_max == 0 || thread_num_max > MAX_THREADS)
	{
		std::cerr << "usage: " << "current maximum supported number of threads = " << MAX_THREADS << std::endl;
		return EXIT_FAILURE;
	}

	// end:
	return EXIT_SUCCESS;
}

/*********************************
 * and this is where it ends
 *********************************/
