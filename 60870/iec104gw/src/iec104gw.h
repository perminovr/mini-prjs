#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <sched.h>

#include "hal_time.h"
#include "hal_thread.h"
#include "iec60870_master.h"
#include "iec60870_common.h"
#include "cs104_slave.h"
#include "cs104_connection.h"


#define IEC10XCONST_STACK_SIZE	65535


#ifndef IEC104GW_DIR
#define IEC104GW_DIR 		"/home/root/CoDeSysSP/iec104gw"
#endif
#define IEC104GW_CFG 		"iec104gw.cfg"

// service asdu offsets
#define IEC104GW_LINK_CMD_BASE			0		// link cmd: con/disc/startDT/stopDT
#define IEC104GW_CONREDUND_CMD_BASE		500		// slaves redund cmd: № of slave or ip
#define IEC104GW_LINK_DIAG_BASE			1000	// link cmd status
#define IEC104GW_CONREDUND_DIAG_BASE	1500	// slaves redund cmd status

// UTILS

#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})

typedef struct que_s que_t;
typedef struct qitem_s qitem_t;

struct qitem_s {
	struct qitem_s *next;
	struct qitem_s *prev;
	que_t *pque;
	uint8_t onHandling;
};

struct que_s {
	int size;
	int maxSize;
	qitem_t *head;
	qitem_t *tail;
	int onHandling;
	pthread_mutex_t mu;
	qitem_t *ihandling;
};

typedef struct {
	pthread_mutex_t self;
	pthread_t pth;
} smart_mutex_t;

// ASDU

typedef struct {
    CS101_AppLayerParameters parameters;
    uint8_t *asdu;
    int asduHeaderLength;
    uint8_t *payload;
    int payloadSize;
} Iec10XAsdu_t; // lib asdu struct copy

typedef struct Iec10XAsduObject_s Iec10XAsduObject_t;
struct Iec10XAsduObject_s {
	int addr;				// IOA
	uint8_t payload[32];	// values / ts / etc
	int payloadSize;		// sizeof payload
	Iec10XAsduObject_t *next;	// next IO
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
	SlotIPv4_t mateAddr1;
	SlotIPv4_t mateAddr2;
	bool isThread;
	int rtpriority;
} Iec104gCmnPrms_t;

typedef struct {
	Iec104gCmnPrms_t cmn;
	SlotIPv4_t addr;
	int servAsduAddr;
	char servIp[16];
	int maxConnections;
	int maxLowPrioQueueSize;
	int maxHighPrioQueueSize;
	struct sCS104_APCIParameters apciParams;
} Iec104SPrms_t;

typedef struct {
	SlotIPv4_t addr;
	int asdu;
} Iec104MPrmSlave_t;

typedef struct {
	Iec104gCmnPrms_t cmn;
	struct {
		char addr[16];
	} slot;
	struct {
		int size;
		int isRedund;
		Iec104MPrmSlave_t *self;
	} slaves;
	int servAsduAddr;
	struct sCS101_AppLayerParameters alp;
	struct sCS104_APCIParameters apciParams;
} Iec104MPrms_t;

typedef enum {
	SERVICE_EVENT_NDEF,
    SERVICE_EVENT_CONNECTION_OPENED,
    SERVICE_EVENT_CONNECTION_CLOSED,
    SERVICE_EVENT_CONNECTION_STARTDT,
    SERVICE_EVENT_CONNECTION_STOPDT,
} eServiceEvent;

// COMMON

typedef struct {
	struct {
		void *self;
		void (*sendAsdu)(void *self, CS101_ASDU asdu);
		void (*handleCommand)(void *self, int addr, uint32_t cmd);
	} master;
	struct {
		void *self;
		void (*sendAsdu)(void *self, CS101_ASDU asdu);
	} slave;
} Iec104gCommon_t;

// BASE

typedef struct {
	Iec104gCommon_t *common;
	struct sCS101_AppLayerParameters alp;
	int servAsduAddr;
} Iec104gBase_t;

// SLAVE

typedef struct {
	Iec104gBase_t base;
	CS104_Slave self;
	bool isReady;
	char servIp[16];
} Iec104S_t;

// MASTER

typedef struct Iec104MSlave_s Iec104MSlave_t;

typedef struct {
	Iec104MSlave_t *head;
	Iec104MSlave_t *next;
} Iec104MSlaveList_t;

struct Iec104MSlave_s {
	uint32_t addr;
	int asdu;
	bool isActive;
	CS104_Connection con;
	Iec104MSlaveList_t list;
};

typedef struct {
	Iec104gBase_t base;
	char addr[16];
	struct {
		int size;
		int isRedund;
		Iec104MSlave_t *self;
	} slaves;
} Iec104M_t;


Iec104S_t *Iec104S_create(Iec104gCommon_t *common);
Iec104M_t *Iec104M_create(Iec104gCommon_t *common);

void Iec104S_free(Iec104S_t *self);
void Iec104M_free(Iec104M_t *self);

int Iec104S_init(Iec104S_t *self, Iec104SPrms_t *prms);
int Iec104M_init(Iec104M_t *self, Iec104MPrms_t *prms);

void Iec104S_run(Iec104S_t *self);
void Iec104M_run(Iec104M_t *self);

CS101_ASDU makeStaticAsdu(CS101_AppLayerParameters parameters, const Iec10XStructedAsdu_t *strAsdu);


que_t *que_init(que_t *pque, int maxSize);
que_t *que_deinit(que_t *pque);
int que_get_free_size(que_t *pque);
void que_clear(que_t *pque, void (*free_func)(qitem_t *));
//
void que_add_head(que_t *pque, qitem_t *item);
qitem_t *que_get_tail(que_t *pque);
//
qitem_t *que_get_next_for_handling(que_t *pque);
qitem_t *que_set_last_for_handling(que_t *pque);
//
qitem_t *que_del_tail(que_t *pque);
qitem_t *que_del_first_handled(que_t *pque);
void que_clear_handled(que_t *pque, void (*free_func)(qitem_t *));


void smart_mutex_lock(smart_mutex_t *smutex);
void smart_mutex_unlock(smart_mutex_t *smutex);


//#define DEBUG_COUNTERS

#ifdef DEBUG_COUNTERS
uint64_t getsystick(void);
#endif


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