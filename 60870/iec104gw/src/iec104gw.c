#include "iec104gw.h"
#include "minIni.h"

const char *CONFIG_NAME;

typedef struct {
	Iec104gCommon_t *iec104cmn;
	Iec104S_t *iec104s;
	Iec104M_t *iec104m;
} Iec104Gw_t;

#define INIINIT \
	char buf[128];

#define INIGET(sec,prm,defop,ndefop) \
	ini_gets(sec,prm,"",buf,sizeof(buf),CONFIG_NAME); \
	if (strlen(buf) == 0) defop; \
	else ndefop;


static void Iec104Prms_parseCmn(Iec104gCmnPrms_t *cmn)
{
	INIINIT
	bzero(cmn, sizeof(Iec104gCmnPrms_t));
	INIGET("common", "thread",
		cmn->isThread = 0,
		cmn->isThread = atoi(buf));
	INIGET("common", "rtpriority",
		cmn->rtpriority = 99,
		cmn->rtpriority = atoi(buf));
	INIGET("common", "mate_ip1",
		strcpy(cmn->mateAddr1.ip, ""),
		strncpy(cmn->mateAddr1.ip, buf, sizeof(cmn->mateAddr1.ip)-1));
	INIGET("common", "mate_ip2",
		strcpy(cmn->mateAddr2.ip, ""),
		strncpy(cmn->mateAddr2.ip, buf, sizeof(cmn->mateAddr2.ip)-1));
	INIGET("common", "mate_port1",
		cmn->mateAddr1.port = 0,
		cmn->mateAddr1.port = atoi(buf));
	INIGET("common", "mate_port2",
		cmn->mateAddr2.port = 0,
		cmn->mateAddr2.port = atoi(buf));
}


static void Iec104Prms_parseS(Iec104gCmnPrms_t *cmn, Iec104SPrms_t *prms)
{
	INIINIT
	bzero(prms, sizeof(Iec104SPrms_t));
	INIGET("iec104s", "addr",
		strcpy(prms->addr.ip, "127.0.0.1"),
		strncpy(prms->addr.ip, buf, sizeof(prms->addr.ip)-1));
	INIGET("iec104s", "port",
		prms->addr.port = 0,
		prms->addr.port = atoi(buf));
	INIGET("iec104s", "servAsduAddr",
		prms->servAsduAddr = 0,
		prms->servAsduAddr = atoi(buf));
	INIGET("iec104s", "servIp",
		strcpy(prms->servIp, "127.0.0.1"),
		strcpy(prms->servIp, buf));
	INIGET("iec104s", "maxConnections",
		prms->maxConnections = 0,
		prms->maxConnections = atoi(buf));
	INIGET("iec104s", "maxLowPrioQueueSize",
		prms->maxLowPrioQueueSize = 10,
		prms->maxLowPrioQueueSize = atoi(buf));
	INIGET("iec104s", "maxHighPrioQueueSize",
		prms->maxHighPrioQueueSize = 10,
		prms->maxHighPrioQueueSize = atoi(buf));
	INIGET("iec104s", "k",
		prms->apciParams.k = 12,
		prms->apciParams.k = atoi(buf));
	INIGET("iec104s", "w",
		prms->apciParams.w = 8,
		prms->apciParams.w = atoi(buf));
	INIGET("iec104s", "t0",
		prms->apciParams.t0 = 10,
		prms->apciParams.t0 = atoi(buf));
	INIGET("iec104s", "t1",
		prms->apciParams.t1 = 15,
		prms->apciParams.t1 = atoi(buf));
	INIGET("iec104s", "t2",
		prms->apciParams.t2 = 10,
		prms->apciParams.t2 = atoi(buf));
	INIGET("iec104s", "t3",
		prms->apciParams.t3 = 20,
		prms->apciParams.t3 = atoi(buf));

	memcpy(&prms->cmn, cmn, sizeof(Iec104gCmnPrms_t));
}


static void Iec104Prms_parseM(Iec104gCmnPrms_t *cmn, Iec104MPrms_t *prms)
{
	INIINIT
	bzero(prms, sizeof(Iec104MPrms_t));
	INIGET("iec104m", "addr",
		strcpy(prms->slot.addr, "127.0.0.1"),
		strncpy(prms->slot.addr, buf, sizeof(prms->slot.addr)-1));
	INIGET("iec104m", "slaves_redund",
		prms->slaves.isRedund = 0,
		prms->slaves.isRedund = atoi(buf));
	INIGET("iec104m", "slaves",
		prms->slaves.size = 1,
		prms->slaves.size = atoi(buf));
	{
		int i;
		char slave[32];
		prms->slaves.self = calloc(prms->slaves.size, sizeof(Iec104MPrmSlave_t));
		for (i = 0; i < prms->slaves.size; ++i) {
			sprintf(slave, "slave%d_ip", i+1);
			INIGET("iec104m", slave,
				strcpy(prms->slaves.self[i].addr.ip, "127.0.0.1"),
				strncpy(prms->slaves.self[i].addr.ip, buf, sizeof(prms->slaves.self[i].addr.ip)-1));
			sprintf(slave, "slave%d_port", i+1);
			INIGET("iec104m", slave,
				prms->slaves.self[i].addr.port = 0,
				prms->slaves.self[i].addr.port = atoi(buf));
			sprintf(slave, "slave%d_asdu", i+1);
			INIGET("iec104m", slave,
				prms->slaves.self[i].asdu = 0,
				prms->slaves.self[i].asdu = atoi(buf));
		}
	}
	INIGET("iec104m", "sizeOfTypeId",
		prms->alp.sizeOfTypeId = 1,
		prms->alp.sizeOfTypeId = atoi(buf));
	INIGET("iec104m", "sizeOfVSQ",
		prms->alp.sizeOfVSQ = 1,
		prms->alp.sizeOfVSQ = atoi(buf));
	INIGET("iec104m", "sizeOfCOT",
		prms->alp.sizeOfCOT = 2,
		prms->alp.sizeOfCOT = atoi(buf));
	INIGET("iec104m", "originatorAddress",
		prms->alp.originatorAddress = 0,
		prms->alp.originatorAddress = atoi(buf));
	INIGET("iec104m", "sizeOfCA",
		prms->alp.sizeOfCA = 2,
		prms->alp.sizeOfCA = atoi(buf));
	INIGET("iec104m", "sizeOfIOA",
		prms->alp.sizeOfIOA = 3,
		prms->alp.sizeOfIOA = atoi(buf));
	INIGET("iec104m", "maxSizeOfASDU",
		prms->alp.maxSizeOfASDU = 249,
		prms->alp.maxSizeOfASDU = atoi(buf));
	INIGET("iec104m", "k",
		prms->apciParams.k = 12,
		prms->apciParams.k = atoi(buf));
	INIGET("iec104m", "w",
		prms->apciParams.w = 8,
		prms->apciParams.w = atoi(buf));
	INIGET("iec104m", "t0",
		prms->apciParams.t0 = 10,
		prms->apciParams.t0 = atoi(buf));
	INIGET("iec104m", "t1",
		prms->apciParams.t1 = 15,
		prms->apciParams.t1 = atoi(buf));
	INIGET("iec104m", "t2",
		prms->apciParams.t2 = 10,
		prms->apciParams.t2 = atoi(buf));
	INIGET("iec104m", "t3",
		prms->apciParams.t3 = 20,
		prms->apciParams.t3 = atoi(buf));
	INIGET("iec104s", "servAsduAddr",
		prms->servAsduAddr = 0,
		prms->servAsduAddr = atoi(buf));

	memcpy(&prms->cmn, cmn, sizeof(Iec104gCmnPrms_t));
}


static Iec104gCommon_t *Iec104gCommon_create()
{
	Iec104gCommon_t *cmn = (Iec104gCommon_t *)calloc(1, sizeof(Iec104gCommon_t));
	return cmn;
}


static void Iec104gCommon_free(Iec104gCommon_t *self)
{
	free(self);
}


static Iec104Gw_t *this;
static Iec104gCmnPrms_t *iec104_cmn;
static Iec104SPrms_t *iec104s_prms;
static Iec104MPrms_t *iec104m_prms;


static void connectionsHandling()
{
	int i;
	MasterConnection *cons = CS104_Slave_getConnections(this->iec104s->self);
	for (i = 0; i < iec104s_prms->maxConnections; ++i) {
		CS104_Slave_masterConnectionHandling(cons[i]);
	}
	for (i = 0; i < this->iec104m->slaves.size; ++i) {
		CS104_Connection_handleConnection(this->iec104m->slaves.self[i].con);
	}
}


static int pollReconstruct(struct pollfd *pfds)
{
	int i = 0, j = 0;
	pfds[i].fd = CS104_Slave_getServerFd(this->iec104s->self);
	pfds[i].events = (pfds[i].fd >= 0)? POLLIN : 0;
	i++;
	MasterConnection *cons = CS104_Slave_getConnections(this->iec104s->self);
	for (j = 0; j < iec104s_prms->maxConnections; ++j) {
		pfds[i].fd = CS104_Slave_getFd(cons[j]);
		pfds[i].events = (pfds[i].fd >= 0)? POLLIN : 0;
		i++;
	}
	for (j = 0; j < this->iec104m->slaves.size; ++j) {
		pfds[i].fd = CS104_Connection_getFd(this->iec104m->slaves.self[j].con);
		pfds[i].events = (pfds[i].fd >= 0)? POLLIN : 0;
		i++;
	}
	return i;
}


static void *MainLoop(void *unused)
{
	int i;
	uint64_t ts;
	bool handleConnections;

	int pfds_size = 1 + iec104s_prms->maxConnections + iec104m_prms->slaves.size;
	struct pollfd *pfds = calloc(pfds_size, sizeof(struct pollfd));

	{
		struct sched_param schp;
		bzero(&schp, sizeof(struct sched_param));
		schp.sched_priority = iec104_cmn->rtpriority;
		if (iec104_cmn->isThread) {
			pid_t pid = gettid();
			prctl(PR_SET_NAME, "iec104gw");
			pthread_setschedparam(pid, SCHED_RR, &schp);
		} else {
			pid_t pid = getpid();
			sched_setscheduler(pid, SCHED_RR, &schp);
		}
	}

	// connect to slaves
	for (i = 0; i < this->iec104m->slaves.size; ++i) {
		CS104_Connection_connectAsync(this->iec104m->slaves.self[i].con, 1000);
	}

	// volatile uint64_t timeForFault = Hal_getTimeInMs() + 5 * 60 * 1000;

	handleConnections = true;
	for (;;) {
		// uint64_t cts = Hal_getTimeInMs();
		if (handleConnections) {
			ts = Hal_getTimeInMs();
			connectionsHandling();
			pollReconstruct(pfds);
			handleConnections = false;
			// if (timeForFault <= cts) {
			// 	usleep(10*1000*1000);
			// 	int *i = 0;
			// 	*i = 123;
			// 	printf(" result %p %d \n", i, *i);
			// 	exit(0);
			// }
			// printf("handleConnections %llu\n", cts%10000);
		}
		// stay on poll
		int pres = poll(pfds, pfds_size, 50);
		//
		// cts = Hal_getTimeInMs();
		//
		if (pres == 0 || (Hal_getTimeInMs() >= ts+100)) { // handle connections/timeouts
			handleConnections = true;
			continue;
		}
		else if (pres > 0) {
			for (i = 0; i < pfds_size; ++i) {
				bool pollin = (pfds[i].revents & POLLIN) > 0;
				bool pollerr = (pfds[i].revents & (POLLHUP|POLLERR|POLLNVAL)) > 0;
				if (pollerr) {
					handleConnections = true; // pollerr -> handle connections/timeouts on next round
				}
				if (pollin || pollerr) {
					bool ok = true;
					if (i == 0) { // accept
						CS104_Slave_tick(this->iec104s->self);
						// printf("CS104_Slave_tick %llu\n", cts%10000);
					} else if (i <= iec104s_prms->maxConnections) { // slave
						MasterConnection *cons = CS104_Slave_getConnections(this->iec104s->self);
						int offset = 1;
						ok &= CS104_Slave_masterReceiveHandling(cons[i-offset], pollerr);
						// printf("CS104_Slave_masterReceiveHandling %llu\n", cts);
					} else { // master
						int offset = 1+iec104s_prms->maxConnections;
						ok &= CS104_Connection_handleReceive(this->iec104m->slaves.self[i-offset].con, pollerr);
						// printf("CS104_Connection_handleReceive %d %llu\n", i-offset, cts);
					}
					if (!ok) handleConnections = true;
				}
			}
		}
		else {
			// printf("poll error %llu\n", cts%10000);
			Thread_sleep(1);
		}
	}
	free(pfds);
	return 0;
}


int main(int argc, char **argv)
{
	char buf[256];

	// volatile int flag = 0;
	// while(!flag) {
	// 	sleep(1);
	// }

	CONFIG_NAME = (argc > 1)?
		argv[1] : IEC104GW_CFG;
	sprintf(buf, "%s/%s", IEC104GW_DIR, CONFIG_NAME);
	CONFIG_NAME = buf;

	this = (Iec104Gw_t *)calloc(1, sizeof(Iec104Gw_t));
	iec104_cmn = (Iec104gCmnPrms_t *)calloc(1, sizeof(Iec104gCmnPrms_t));
	iec104s_prms = (Iec104SPrms_t *)calloc(1, sizeof(Iec104SPrms_t));
	iec104m_prms = (Iec104MPrms_t *)calloc(1, sizeof(Iec104MPrms_t));

	this->iec104cmn = Iec104gCommon_create();
	this->iec104s = Iec104S_create(this->iec104cmn);
	this->iec104m = Iec104M_create(this->iec104cmn);

	Iec104Prms_parseCmn(iec104_cmn);
	Iec104Prms_parseS(iec104_cmn, iec104s_prms);
	Iec104Prms_parseM(iec104_cmn, iec104m_prms);

	Iec104S_init(this->iec104s, iec104s_prms);
	Iec104M_init(this->iec104m, iec104m_prms);

	Iec104S_run(this->iec104s);
	Iec104M_run(this->iec104m);

	MainLoop(0);

	return 0;
}


CS101_ASDU makeStaticAsdu(CS101_AppLayerParameters parameters, const Iec10XStructedAsdu_t *strAsdu)
{
	#define HEADER_SIZE 4
	static sCS101_StaticASDU sasdu;
	CS101_ASDU asdu = CS101_ASDU_initializeStatic(&sasdu, parameters, false, strAsdu->cot, 0, strAsdu->addr, false, false);
	if (asdu) {
		Iec10XAsduObject_t *obj;
		Iec10XAsdu_t *masdu = (Iec10XAsdu_t *)asdu;
		masdu->asdu[0] = (uint8_t)strAsdu->type;
		masdu->asdu[1] = (uint8_t)strAsdu->classif;
		uint8_t *asdu_payload = masdu->payload;
		for (obj = strAsdu->objects; obj; obj = obj->next) {
			uint8_t *addr = (uint8_t*)&obj->addr;
			switch (parameters->sizeOfIOA) { // addr of object
				case 3: asdu_payload[2] = addr[2];
				case 2: asdu_payload[1] = addr[1];
				case 1: asdu_payload[0] = addr[0];
					break;
			}
			asdu_payload += parameters->sizeOfIOA;
			memcpy(asdu_payload, obj->payload, obj->payloadSize);
			asdu_payload += obj->payloadSize;
			masdu->payloadSize += parameters->sizeOfIOA + obj->payloadSize;
		}
	}
	return asdu;
}
