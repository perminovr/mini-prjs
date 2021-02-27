#include "iec10Xgw.h"
#include "minIni.h"

const char *CONFIG_NAME;

typedef struct {
	Iec10XCommon_t *iec10xcmn;
	Iec104S_t *iec104s;
	Iec101M_t *iec101m;
} Iec10xGw_t;

#define INIINIT \
	char buf[128];

#define INIGET(sec,prm,defop,ndefop) \
	ini_gets(sec,prm,"",buf,sizeof(buf),CONFIG_NAME); \
	if (strlen(buf) == 0) defop; \
	else ndefop;


static void Iec10XPrms_parseCmn(Iec10XCmnPrms_t *cmn)
{
	INIINIT
	bzero(cmn, sizeof(Iec10XCmnPrms_t));
	INIGET("common", "thread",
		cmn->isThread = 0,
		cmn->isThread = atoi(buf));
	INIGET("common", "rtpriority",
		cmn->rtpriority = 99,
		cmn->rtpriority = atoi(buf));
	INIGET("common", "sleeptime",
		cmn->sleeptime = 10,
		cmn->sleeptime = atoi(buf));
}


static void Iec10XPrms_parse104(Iec10XCmnPrms_t *cmn, Iec104SPrms_t *prms)
{
	INIINIT
	bzero(prms, sizeof(Iec104SPrms_t));
	INIGET("iec104s", "addr",
		strcpy(prms->addr.ip, "127.0.0.1"),
		strncpy(prms->addr.ip, buf, sizeof(prms->addr.ip)-1));
	INIGET("iec104s", "port",
		prms->addr.port = 0,
		prms->addr.port = atoi(buf));
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

	memcpy(&prms->cmn, cmn, sizeof(Iec10XCmnPrms_t));
}


static void Iec10XPrms_parse101(Iec10XCmnPrms_t *cmn, Iec101MPrms_t *prms)
{
	INIINIT
	bzero(prms, sizeof(Iec101MPrms_t));
	INIGET("iec101m", "port",
		strcpy(prms->port.name, "/dev/ttyS1"),
		strncpy(prms->port.name, buf, sizeof(prms->port.name)-1));
	INIGET("iec101m", "baud",
		prms->port.baudRate = 9600,
		prms->port.baudRate = atoi(buf));
	INIGET("iec101m", "dbits",
		prms->port.dataBits = 8,
		prms->port.dataBits = atoi(buf));
	INIGET("iec101m", "sbits",
		prms->port.stopBits = 1,
		prms->port.stopBits = atoi(buf));
	INIGET("iec101m", "parity",
		prms->port.parity = 'N',
		prms->port.parity = buf[0]);
	INIGET("iec101m", "slaves",
		prms->slaves.size = 1,
		prms->slaves.size = atoi(buf));
	{
		int i;
		char slave[16];
		prms->slaves.addrs = calloc(prms->slaves.size, sizeof(int));
		for (i = 0; i < prms->slaves.size; ++i) {
			sprintf(slave, "aslave%d", i+1);
			INIGET("iec101m", slave,
				prms->slaves.addrs[i] = i+1,
				prms->slaves.addrs[i] = atoi(buf));
		}
	}
	INIGET("iec101m", "delay",
		prms->slaves.delay = 100,
		prms->slaves.delay = atoi(buf));
	INIGET("iec101m", "addressLength",
		prms->llp.addressLength = 1,
		prms->llp.addressLength = atoi(buf));
	INIGET("iec101m", "timeoutForAck",
		prms->llp.timeoutForAck = 200,
		prms->llp.timeoutForAck = atoi(buf));
	INIGET("iec101m", "timeoutRepeat",
		prms->llp.timeoutRepeat = 1000,
		prms->llp.timeoutRepeat = atoi(buf));
	INIGET("iec101m", "useSingleCharACK",
		prms->llp.useSingleCharACK = 1,
		prms->llp.useSingleCharACK = atoi(buf));
	INIGET("iec101m", "sizeOfTypeId",
		prms->alp.sizeOfTypeId = 1,
		prms->alp.sizeOfTypeId = atoi(buf));
	INIGET("iec101m", "sizeOfVSQ",
		prms->alp.sizeOfVSQ = 1,
		prms->alp.sizeOfVSQ = atoi(buf));
	INIGET("iec101m", "sizeOfCOT",
		prms->alp.sizeOfCOT = 2,
		prms->alp.sizeOfCOT = atoi(buf));
	INIGET("iec101m", "originatorAddress",
		prms->alp.originatorAddress = 0,
		prms->alp.originatorAddress = atoi(buf));
	INIGET("iec101m", "sizeOfCA",
		prms->alp.sizeOfCA = 2,
		prms->alp.sizeOfCA = atoi(buf));
	INIGET("iec101m", "sizeOfIOA",
		prms->alp.sizeOfIOA = 3,
		prms->alp.sizeOfIOA = atoi(buf));
	INIGET("iec101m", "maxSizeOfASDU",
		prms->alp.maxSizeOfASDU = 249,
		prms->alp.maxSizeOfASDU = atoi(buf));

	memcpy(&prms->cmn, cmn, sizeof(Iec10XCmnPrms_t));
}


static Iec10XCommon_t *Iec10XCommon_create()
{
	Iec10XCommon_t *cmn = (Iec10XCommon_t *)calloc(1, sizeof(Iec10XCommon_t));
	return cmn;
}


static void Iec10XCommon_free(Iec10XCommon_t *self)
{
	free(self);
}


static Iec10xGw_t *this;
static Iec10XCmnPrms_t *iec10x_cmn;
static Iec104SPrms_t *iec104s_prms;
static Iec101MPrms_t *iec101m_prms;


static void connectionsHandling()
{
	int i;
	MasterConnection *cons = CS104_Slave_getConnections(this->iec104s->self);
	for (i = 0; i < iec104s_prms->maxConnections; ++i) {
		CS104_Slave_masterConnectionHandling(cons[i]);
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
	pfds[i].fd = CS101_Master_getFd(this->iec101m->self);
	pfds[i].events = (pfds[i].fd >= 0)? POLLIN : 0;
	i++;
	return i;
}


static void *MainLoop(void *unused)
{
	int i;
	uint64_t ts;
	bool handleConnections;

	int pfds_size = 1 + iec104s_prms->maxConnections + 1;
	struct pollfd *pfds = calloc(pfds_size, sizeof(struct pollfd));

	{
		struct sched_param schp;
		bzero(&schp, sizeof(struct sched_param));
		schp.sched_priority = iec10x_cmn->rtpriority;
		if (iec10x_cmn->isThread) {
			pid_t pid = gettid();
			prctl(PR_SET_NAME, "iec10Xgw");
			pthread_setschedparam(pid, SCHED_RR, &schp);
		} else {
			pid_t pid = getpid();
			sched_setscheduler(pid, SCHED_RR, &schp);
		}
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
		int pres = poll(pfds, pfds_size, iec10x_cmn->sleeptime);
		//
		// cts = Hal_getTimeInMs();
		//
		if (Hal_getTimeInMs() >= ts+100) { // handle connections/timeouts
			handleConnections = true;
			continue;
		}
		else if (pres == 0) {
			Iec101M_run(this->iec101m);
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
						Iec101M_run(this->iec101m);
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


int main()
{
	CONFIG_NAME = (argc > 1)?
		argv[1] : IEC104GW_CFG;
	sprintf(buf, "%s/%s", IEC104GW_DIR, CONFIG_NAME);
	CONFIG_NAME = buf;

	this = (Iec10xGw_t *)calloc(1, sizeof(Iec10xGw_t));
	iec10x_cmn = (Iec10XCmnPrms_t *)calloc(1, sizeof(Iec10XCmnPrms_t));
	iec104s_prms = (Iec104SPrms_t *)calloc(1, sizeof(Iec104SPrms_t));
	iec101m_prms = (Iec101MPrms_t *)calloc(1, sizeof(Iec101MPrms_t));

	this->iec10xcmn = Iec10XCommon_create();
	this->iec104s = Iec104S_create(this->iec10xcmn);
	this->iec101m = Iec101M_create(this->iec10xcmn);

	Iec10XPrms_parseCmn(iec10x_cmn);
	Iec10XPrms_parse104(iec10x_cmn, iec104s_prms);
	Iec10XPrms_parse101(iec10x_cmn, iec101m_prms);

	Iec104S_init(this->iec104s, iec104s_prms);
	Iec101M_init(this->iec101m, iec101m_prms);

	Iec104S_run(this->iec104s);
	//Iec101M_run(this->iec101m);

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