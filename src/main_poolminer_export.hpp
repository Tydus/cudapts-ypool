#ifndef __MAIN_POOL_EXPORT_H__
#define __MAIN_POOL_EXPORT_H__

#if defined(__MINGW64__)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#include <stdint.h>

typedef struct {
  // comments: BYTES <index> + <length>
  int32_t nVersion;            // 0+4
  uint8_t hashPrevBlock[32];       // 4+32
  uint8_t hashMerkleRoot[32];      // 36+32
  uint32_t  nTime;               // 68+4
  uint32_t  nBits;               // 72+4
  uint32_t  nNonce;              // 76+4
  uint32_t  birthdayA;          // 80+32+4 (uint32_t)
  uint32_t  birthdayB;          // 84+32+4 (uint32_t)
  uint8_t   targetShare[32];
} blockHeader_t;              // = 80+32+8 bytes header (80 default + 8 birthdayA&B + 32 target)

class CBlockProvider {
public:
  CBlockProvider() { }
  virtual ~CBlockProvider() { }
  virtual blockHeader_t* getBlock(unsigned int thread_id) = 0;
  virtual void submitBlock(blockHeader_t* block, unsigned int thread_id) = 0;
};

extern int main2();
extern void start_worker_thread(int n, CBlockProvider *bprovider);
extern CBlockProvider *createBlockProvider();

#endif
