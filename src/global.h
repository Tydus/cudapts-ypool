#ifndef __XPT_GLOBAL_H__
#define __XPT_GLOBAL_H__

#include "main_poolminer_export.hpp"

#include<stdio.h>
#include<time.h>
#include<stdlib.h>

#ifdef __WIN32__
#pragma comment(lib,"Ws2_32.lib")
#include<Winsock2.h>
#include<ws2tcpip.h>
#else
#include"win.h" // port from windows
#endif


#include"jhlib.h" // slim version of jh library


// connection info for xpt
typedef struct
{
	char* ip;
	uint16 port;
	char* authUser;
	char* authPass;
}generalRequestTarget_t;

#include"xptServer.h"
#include"xptClient.h"

#include"sha2.h"

#include"transaction.h"

// global settings for miner
typedef struct
{
	generalRequestTarget_t requestTarget;
}minerSettings_t;

extern minerSettings_t minerSettings;

#define PROTOSHARE_MEM_512		(0)
#define PROTOSHARE_MEM_256		(1)
#define PROTOSHARE_MEM_128		(2)
#define PROTOSHARE_MEM_32		(3)
#define PROTOSHARE_MEM_8		(4)

// block data struct

typedef struct
{
	// block header data (relevant for midhash)
	uint32	version;
	uint8	prevBlockHash[32];
	uint8	merkleRoot[32];
	uint32	nTime;
	uint32	nBits;
	uint32	nonce;
	// birthday collision
	uint32	birthdayA;
	uint32	birthdayB;
	uint32	uniqueMerkleSeed;

	uint32	height;
	uint8	merkleRootOriginal[32]; // used to identify work
	uint8	target[32];
	uint8	targetShare[32];
}minerProtosharesBlock_t;

void jhProtominer_submitShare(minerProtosharesBlock_t* block);

// stats
extern volatile uint64_t totalCollisionCount;
extern volatile uint64_t totalShareCount;

extern volatile uint32 monitorCurrentBlockHeight;

extern CBlockProvider* bprovider;

struct workDataSource_t
{
	CRITICAL_SECTION cs_work;
	uint32	algorithm;
	// block data
	uint32	version;
	uint32	height;
	uint32	nBits;
	uint32	nTime;
	uint8	merkleRootOriginal[32]; // used to identify work
	uint8	prevBlockHash[32];
	uint8	target[32];
	uint8	targetShare[32];
	// extra nonce info
	uint8	coinBase1[1024];
	uint8	coinBase2[1024];
	uint16	coinBase1Size;
	uint16	coinBase2Size;
	// transaction hashes
	uint8	txHash[32*4096];
	uint32	txHashCount;
};


typedef struct
{
	char* workername;
	char* workerpass;
	char* host;
	sint32 port;
	sint32 numThreads;
	// GPU / OpenCL options

	// mode option
	uint32 mode;
}commandlineInput_t;



extern workDataSource_t workDataSource;
extern commandlineInput_t commandlineInput;
extern uint32 uniqueMerkleSeedGenerator;
extern uint32 miningStartTime;



#endif
