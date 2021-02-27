#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <sched.h>
#include <unistd.h>

#include "hal_time.h"
#include "hal_thread.h"
#include "hal_serial.h"
#include "iec60870_master.h"
#include "iec60870_common.h"
#include "cs104_slave.h"
#include "cs101_master.h"


#define IEC10XCONST_STACK_SIZE	65535


#ifndef IEC10XGW_DIR
#define IEC10XGW_DIR 		"/home/root/CoDeSysSP/iec10Xgw"
#endif
#define IEC10XGW_CFG 		IEC10XGW_DIR"/iec10Xgw.cfg"
#define IEC10XGW_DATABASE 	IEC10XGW_DIR"/iec10Xgw.db"


typedef struct {
    CS101_AppLayerParameters parameters;
    uint8_t *asdu;
    int asduHeaderLength;
    uint8_t *payload;
    int payloadSize;
} Iec10XAsdu_t;


typedef struct Iec10XAsduObject_s Iec10XAsduObject_t;
struct Iec10XAsduObject_s {
	int addr;
	uint8_t payload[128];
	int payloadSize;
	Iec10XAsduObject_t *next;
};


typedef struct {
	IEC60870_5_TypeID type;
	int classif;
	CS101_CauseOfTransmission cot;
	int addr;
	Iec10XAsduObject_t *objects;
} Iec10XStructedAsdu_t;

// PARAMS

typedef struct {
	char ip[16];
	uint16_t port;
} SlotIPv4_t;

typedef struct {
	bool isThread;
	int rtpriority;
	int sleeptime;
} Iec10XCmnPrms_t;

typedef struct {
	Iec10XCmnPrms_t cmn;
	SlotIPv4_t addr;
	int maxConnections;
	int maxLowPrioQueueSize;
	int maxHighPrioQueueSize;
	struct sCS104_APCIParameters apciParams;
} Iec104SPrms_t;

typedef struct {
	Iec10XCmnPrms_t cmn;
	struct {
		char name[32];		// "/dev/ttyX"
		int baudRate;		// ... 9600 ... 115200 ...
		uint8_t dataBits;	// 5-8
		char parity; 		// 'N', 'E', 'O'
		uint8_t stopBits;	// 1, 2
	} slot;
	struct {
		int size;
		int delay;
		int *addrs;
	} slaves;
	int sleepTime;
	struct sLinkLayerParameters llp;
	struct sCS101_AppLayerParameters alp;
} Iec101MPrms_t;

// COMMON

typedef struct {
	struct {
		void *self;
		void (*sendAsdu)(void *self, CS101_ASDU asdu);
	} master;
	struct {
		void *self;
		void (*sendAsdu)(void *self, CS101_ASDU asdu);
	} slave;
} Iec10XCommon_t;

// BASE

typedef struct {
	Iec10XCommon_t *common;
	struct sCS101_AppLayerParameters alp;
} Iec10XBase_t;

// SLAVE

typedef struct {
	Iec10XBase_t base;
	CS104_Slave self;
	bool isReady;
	char servIp[16];
} Iec104S_t;

// MASTER

typedef struct AsduQue_s AsduQue_t;
struct AsduQue_s {
	sCS101_StaticASDU self;
	int asduAddr;
	AsduQue_t *next;
};

typedef struct {
	Iec10XBase_t base;
	SerialPort port;
	CS101_Master self;
	struct {
		int size;
		int delay;
		int *addrs;
	} slaves;
	uint64_t lastPollTime;
	int currSlaveIdx;
	AsduQue_t *asduQue;
} Iec101M_t;


extern Iec104S_t *Iec104S_create(Iec10XCommon_t *common);
extern Iec101M_t *Iec101M_create(Iec10XCommon_t *common);

extern void Iec104S_free(Iec104S_t *self);
extern void Iec101M_free(Iec101M_t *self);

extern int Iec104S_init(Iec104S_t *self, Iec104SPrms_t *prms);
extern int Iec101M_init(Iec101M_t *self, Iec101MPrms_t *prms);

extern void Iec104S_run(Iec104S_t *self);
extern void Iec101M_run(Iec101M_t *self);

CS101_ASDU makeStaticAsdu(CS101_AppLayerParameters parameters, const Iec10XStructedAsdu_t *strAsdu);


#ifdef __linux__
#include <syscall.h>
#if defined(_syscall0)
	_syscall0(pid_t, gettid)
#elif defined(__NR_gettid)
	static inline pid_t gettid(void) { return syscall(__NR_gettid); }
#else
	#warning "use pid as tid"
	static inline pid_t gettid(void) { return getpid(); }
#endif
#endif