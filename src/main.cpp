#ifndef __MAIN_CPP__
#define __MAIN_CPP__

#include "global.h"

// miner version string (for pool statistic)
#ifdef __WIN32__
char* minerVersionString = _strdup("jhProtominer v0.1c");
#else
char* minerVersionString = _strdup("jhProtominer v0.1c-Linux");
#endif

minerSettings_t minerSettings = {0};

xptClient_t* xptClient = NULL;
CRITICAL_SECTION cs_xptClient;
volatile uint32 monitorCurrentBlockHeight; // used to notify worker threads of new block data

CBlockProvider* bprovider = NULL;

workDataSource_t workDataSource;

uint32 uniqueMerkleSeedGenerator = 0;
uint32 miningStartTime = 0;

void jhProtominer_submitShare(minerProtosharesBlock_t* block)
{
	printf("Share found! (BlockHeight: %d)\n", block->height);
	EnterCriticalSection(&cs_xptClient);
	if( xptClient == NULL )
	{
		printf("Share submission failed - No connection to server\n");
		LeaveCriticalSection(&cs_xptClient);
		return;
	}
	// submit block
	xptShareToSubmit_t* xptShare = (xptShareToSubmit_t*)malloc(sizeof(xptShareToSubmit_t));
	memset(xptShare, 0x00, sizeof(xptShareToSubmit_t));
	xptShare->algorithm = ALGORITHM_PROTOSHARES;
	xptShare->version = block->version;
	xptShare->nTime = block->nTime;
	xptShare->nonce = block->nonce;
	xptShare->nBits = block->nBits;
	xptShare->nBirthdayA = block->birthdayA;
	xptShare->nBirthdayB = block->birthdayB;
	memcpy(xptShare->prevBlockHash, block->prevBlockHash, 32);
	memcpy(xptShare->merkleRoot, block->merkleRoot, 32);
	memcpy(xptShare->merkleRootOriginal, block->merkleRootOriginal, 32);
	//userExtraNonceLength = min(userExtraNonceLength, 16);
	sint32 userExtraNonceLength = sizeof(uint32);
	uint8* userExtraNonceData = (uint8*)&block->uniqueMerkleSeed;
	xptShare->userExtraNonceLength = userExtraNonceLength;
	memcpy(xptShare->userExtraNonceData, userExtraNonceData, userExtraNonceLength);
	xptClient_foundShare(xptClient, xptShare);
	LeaveCriticalSection(&cs_xptClient);
}

/*
 * Reads data from the xpt connection state and writes it to the universal workDataSource struct
 */
void jhProtominer_getWorkFromXPTConnection(xptClient_t* xptClient)
{
	EnterCriticalSection(&workDataSource.cs_work);
	workDataSource.height = xptClient->blockWorkInfo.height;
	workDataSource.version = xptClient->blockWorkInfo.version;
	//uint32 timeBias = time(NULL) - xptClient->blockWorkInfo.timeWork;
	workDataSource.nTime = xptClient->blockWorkInfo.nTime;// + timeBias;
	workDataSource.nBits = xptClient->blockWorkInfo.nBits;
	memcpy(workDataSource.merkleRootOriginal, xptClient->blockWorkInfo.merkleRoot, 32);
	memcpy(workDataSource.prevBlockHash, xptClient->blockWorkInfo.prevBlockHash, 32);
	memcpy(workDataSource.target, xptClient->blockWorkInfo.target, 32);
	memcpy(workDataSource.targetShare, xptClient->blockWorkInfo.targetShare, 32);

	workDataSource.coinBase1Size = xptClient->blockWorkInfo.coinBase1Size;
	workDataSource.coinBase2Size = xptClient->blockWorkInfo.coinBase2Size;
	memcpy(workDataSource.coinBase1, xptClient->blockWorkInfo.coinBase1, xptClient->blockWorkInfo.coinBase1Size);
	memcpy(workDataSource.coinBase2, xptClient->blockWorkInfo.coinBase2, xptClient->blockWorkInfo.coinBase2Size);

	// get hashes
	if( xptClient->blockWorkInfo.txHashCount >= 256 )
	{
		printf("Too many transaction hashes\n");
		workDataSource.txHashCount = 0;
	}
	else
		workDataSource.txHashCount = xptClient->blockWorkInfo.txHashCount;
	for(uint32 i=0; i<xptClient->blockWorkInfo.txHashCount; i++)
		memcpy(workDataSource.txHash+32*(i+1), xptClient->blockWorkInfo.txHashes+32*i, 32);
	//// generate unique work from custom extra nonce
	//uint32 userExtraNonce = xpc->coinbaseSeed;
	//xpc->coinbaseSeed++;
	//bitclient_generateTxHash(sizeof(uint32), (uint8*)&userExtraNonce, xpc->xptClient->blockWorkInfo.coinBase1Size, xpc->xptClient->blockWorkInfo.coinBase1, xpc->xptClient->blockWorkInfo.coinBase2Size, xpc->xptClient->blockWorkInfo.coinBase2, xpc->xptClient->blockWorkInfo.txHashes);
	//bitclient_calculateMerkleRoot(xpc->xptClient->blockWorkInfo.txHashes, xpc->xptClient->blockWorkInfo.txHashCount+1, workData->merkleRoot);
	//workData->errorCode = 0;
	//workData->shouldTryAgain = false;
	//xpc->timeCacheClear = GetTickCount() + CACHE_TIME_WORKER;
	//xptProxyWorkCache_add(workData->merkleRoot, workData->merkleRootOriginal, sizeof(uint32), (uint8*)&userExtraNonce);
	LeaveCriticalSection(&workDataSource.cs_work);
	monitorCurrentBlockHeight = workDataSource.height;
}

void jhProtominer_xptQueryWorkLoop()
{
	xptClient = NULL;
	uint32 timerPrintDetails = GetTickCount() + 8000;
	while( true )
	{
		uint32 currentTick = GetTickCount();
		if( currentTick >= timerPrintDetails )
		{
			// print details only when connected
			if( xptClient )
			{
				uint32 passedSeconds = time(NULL) - miningStartTime;
				double collisionsPerMinute = 0.0;
				if( passedSeconds > 5 )
				{
					collisionsPerMinute = (double)totalCollisionCount / (double)passedSeconds * 60.0;
				}
				printf("collisions/min: %.4lf Shares total: %d\n", collisionsPerMinute, totalShareCount);
			}
			timerPrintDetails = currentTick + 8000;
		}
		// check stats
		if( xptClient )
		{
			EnterCriticalSection(&cs_xptClient);
			xptClient_process(xptClient);
			if( xptClient->disconnected )
			{
				// mark work as invalid
				EnterCriticalSection(&workDataSource.cs_work);
				workDataSource.height = 0;
				monitorCurrentBlockHeight = 0;
				LeaveCriticalSection(&workDataSource.cs_work);
				// we lost connection :(
				printf("Connection to server lost - Reconnect in 45 seconds\n");
				xptClient_free(xptClient);
				xptClient = NULL;
				LeaveCriticalSection(&cs_xptClient);
				Sleep(45000);
			}
			else
			{
				// is protoshare algorithm?
				if( xptClient->clientState == XPT_CLIENT_STATE_LOGGED_IN && xptClient->algorithm != ALGORITHM_PROTOSHARES )
				{
					printf("The miner is configured to use a different algorithm.\n");
					printf("Make sure you miner login details are correct\n");
					// force disconnect
					xptClient_free(xptClient);
					xptClient = NULL;
				}
				else if( xptClient->blockWorkInfo.height != workDataSource.height )
				{
					// update work
					jhProtominer_getWorkFromXPTConnection(xptClient);
				}
				LeaveCriticalSection(&cs_xptClient);
				Sleep(1);
			}
		}
		else
		{
			// initiate new connection
			EnterCriticalSection(&cs_xptClient);
			xptClient = xptClient_connect(&minerSettings.requestTarget, 0);
			if( xptClient == NULL )
			{
				LeaveCriticalSection(&cs_xptClient);
				printf("Connection attempt failed, retry in 45 seconds\n");
				Sleep(45000);
			}
			else
			{
				LeaveCriticalSection(&cs_xptClient);
				printf("Connected to server using x.pushthrough(xpt) protocol\n");
				miningStartTime = (uint32)time(NULL);
				totalCollisionCount = 0;
			}

		}
	}
}


commandlineInput_t commandlineInput;

void jhProtominer_printHelp()
{
	puts("Usage: jhProtominer.exe [options]");
	puts("Options:");
	puts("   -o, -O                        The miner will connect to this url");
	puts("                                     You can specifiy an port after the url using -o url:port");
	puts("   -u                            The username (workername) used for login");
	puts("   -p                            The password used for login");
	puts("   -t <device_id>                The id of cuda device for mining (default 0)");
	puts("                                 For most efficient mining, set to number of CPU cores");
	puts("Example usage:");
	puts("   jhProtominer.exe -o http://poolurl.com:10034 -u workername.pts_1 -p workerpass -t 4");
}

void jhProtominer_parseCommandline(int argc, char **argv)
{
	sint32 cIdx = 1;
	while( cIdx < argc )
	{
		char* argument = argv[cIdx];
		cIdx++;
		if( memcmp(argument, "-o", 3)==0 || memcmp(argument, "-O", 3)==0 )
		{
			// -o
			if( cIdx >= argc )
			{
				printf("Missing URL after -o option\n");
				exit(0);
			}
			if( strstr(argv[cIdx], "http://") )
				commandlineInput.host = _strdup(strstr(argv[cIdx], "http://")+7);
			else
				commandlineInput.host = _strdup(argv[cIdx]);
			char* portStr = strstr(commandlineInput.host, ":");
			if( portStr )
			{
				*portStr = '\0';
				commandlineInput.port = atoi(portStr+1);
			}
			cIdx++;
		}
		else if( memcmp(argument, "-u", 3)==0 )
		{
			// -u
			if( cIdx >= argc )
			{
				printf("Missing username/workername after -u option\n");
				exit(0);
			}
			commandlineInput.workername = _strdup(argv[cIdx]);
			cIdx++;
		}
		else if( memcmp(argument, "-p", 3)==0 )
		{
			// -p
			if( cIdx >= argc )
			{
				printf("Missing password after -p option\n");
				exit(0);
			}
			commandlineInput.workerpass = _strdup(argv[cIdx]);
			cIdx++;
		}
		else if( memcmp(argument, "-t", 3)==0 )
		{
			// -t
			if( cIdx >= argc )
			{
				printf("Missing thread number after -t option\n");
				exit(0);
			}
			commandlineInput.numThreads = atoi(argv[cIdx]);
			if( commandlineInput.numThreads < 0 || commandlineInput.numThreads > 128 )
			{
				printf("-t parameter out of range");
				exit(0);
			}
			cIdx++;
		}
		else if( memcmp(argument, "-help", 6)==0 || memcmp(argument, "--help", 7)==0 )
		{
			jhProtominer_printHelp();
			exit(0);
		}
		else
		{
			printf("'%s' is an unknown option.\nType jhPrimeminer.exe --help for more info\n", argument);
			exit(-1);
		}
	}
	if( argc <= 1 )
	{
		jhProtominer_printHelp();
		exit(0);
	}
}


int main(int argc, char** argv)
{
	commandlineInput.host = _strdup("ypool.net");
	srand(GetTickCount());
	commandlineInput.port = 8080 + (rand()%8); // use random port between 8080 and 8088
	SYSTEM_INFO sysinfo;
	GetSystemInfo( &sysinfo );
	commandlineInput.numThreads = sysinfo.dwNumberOfProcessors;
	commandlineInput.numThreads = min(max(commandlineInput.numThreads, 20), 0);
	jhProtominer_parseCommandline(argc, argv);
	printf("\xC9\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xBB\n");
	printf("\xBA  jhProtominer (v0.1e)                            \xBA\n");
	printf("\xBA  author: jh                                      \xBA\n");
	printf("\xBA  http://ypool.net                                \xBA\n");
	printf("\xC8\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xCD\xBC\n");
	printf("Launching miner...\n");
	printf("Using gpu %d\n", commandlineInput.numThreads);
	// set priority to below normal
	SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
	// init winsock
	WSADATA wsa;
	WSAStartup(MAKEWORD(2,2),&wsa);
	// get IP of pool url (default ypool.net)
	char* poolURL = commandlineInput.host;//"ypool.net";
	hostent* hostInfo = gethostbyname(poolURL);
	if( hostInfo == NULL )
	{
		printf("Cannot resolve '%s'. Is it a valid URL?\n", poolURL);
		exit(-1);
	}
	void** ipListPtr = (void**)hostInfo->h_addr_list;
	uint32 ip = 0xFFFFFFFF;
	if( ipListPtr[0] )
	{
		ip = *(uint32*)ipListPtr[0];
	}
	char* ipText = (char*)malloc(32);
	sprintf(ipText, "%d.%d.%d.%d", ((ip>>0)&0xFF), ((ip>>8)&0xFF), ((ip>>16)&0xFF), ((ip>>24)&0xFF));
	// init work source
	InitializeCriticalSection(&workDataSource.cs_work);
	InitializeCriticalSection(&cs_xptClient);
	// setup connection info
	minerSettings.requestTarget.ip = ipText;
	minerSettings.requestTarget.port = commandlineInput.port;
	minerSettings.requestTarget.authUser = commandlineInput.workername;//"jh00.pts_1";
	minerSettings.requestTarget.authPass = commandlineInput.workerpass;//"x";

    main2();

	// Configure Block Provider
	bprovider = createBlockProvider();

	// start miner threads
	for(uint32 i = 0; i <= commandlineInput.numThreads; i++) {
        start_worker_thread(i, bprovider);
    }

	jhProtominer_xptQueryWorkLoop();
	return 0;
}

#endif
