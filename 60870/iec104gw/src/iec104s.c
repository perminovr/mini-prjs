
#include "iec104gw.h"


static inline void serviceCommandHandler(Iec104S_t *self, CS101_ASDU asdu)
{
	char objBuf[128];
	Iec104gCommon_t *cmn = self->base.common;
	int size = CS101_ASDU_getNumberOfElements(asdu);
	for (int i = 0; i < size; ++i) {
		bzero(objBuf, sizeof(objBuf));
		InformationObject io = CS101_ASDU_getElementEx(asdu, (InformationObject)objBuf, i);
		int objAddr = InformationObject_getObjectAddress(io);
		TypeID type = InformationObject_getType(io);
		switch (type) {
			case C_BO_NA_1: {
				cmn->master.handleCommand(cmn->master.self, objAddr, BitString32_getValue((BitString32)io));
			} break;
			default: {} break;
		}
	}
}


static bool asduHandler(void* parameter, IMasterConnection connection, CS101_ASDU asdu)
{
	Iec104S_t *self = (Iec104S_t *)parameter;
	Iec104gCommon_t *cmn = self->base.common;
	int asduAddr = CS101_ASDU_getCA(asdu);
	if (asduAddr == self->base.servAsduAddr) {
		serviceCommandHandler(self, asdu);
	} else {
		cmn->master.sendAsdu(cmn->master.self, asdu);
	}
	return true;
}


static void Iec104S_sendAsdu(void *data, CS101_ASDU asdu)
{
	Iec104S_t *self = (Iec104S_t *)data;
	int maxConnections = CS104_Slave_getMaxConnections(self->self);
	MasterConnection *cons = CS104_Slave_getConnections(self->self);

	char ipAddr[32];
	int asdu_addr = CS101_ASDU_getCA(asdu);
	for (int i = 0; i < maxConnections; ++i) {
		if (CS104_Slave_getIpAddr(cons[i], ipAddr)) {
			bool isServAsduAddr = (asdu_addr == self->base.servAsduAddr);
			bool isServIpAddr = (strcmp(self->servIp, ipAddr) == 0);
			if ((isServAsduAddr && isServIpAddr) || (!isServAsduAddr && !isServIpAddr)) {
				IMasterConnection icon = CS104_Slave_getIMasterConnection(cons[i]);
				IMasterConnection_sendASDU(icon, asdu);
			}
		}
	}
}


void connectionEventHandler(void *parameter, IMasterConnection connection, CS104_PeerConnectionEvent event)
{
	Iec104S_t *self = (Iec104S_t *)parameter;
	if (connection) {
		self->isReady = true;
	}
}


int Iec104S_init(Iec104S_t *self, Iec104SPrms_t *prms)
{
	self->base.servAsduAddr = prms->servAsduAddr;
	strcpy(self->servIp, prms->servIp);
	// inet_aton(prms->servIp, (struct in_addr *)&self->base.servIp);
    CS104_Slave slave = self->self = CS104_Slave_create(prms->maxConnections, prms->maxLowPrioQueueSize, prms->maxHighPrioQueueSize, &prms->apciParams, 0);
    CS104_Slave_setLocalAddress(slave, prms->addr.ip);
	CS104_Slave_setLocalPort(slave, prms->addr.port);
    CS104_Slave_setServerMode(slave, CS104_MODE_CONNECTION_IS_REDUNDANCY_GROUP);
	CS104_Slave_setConnectionEventHandler(slave, connectionEventHandler, self);
    CS104_Slave_setASDUHandler(slave, asduHandler, self);
	return 0;
}


void Iec104S_run(Iec104S_t *self)
{
	CS104_Slave_startThreadless(self->self);
}


Iec104S_t *Iec104S_create(Iec104gCommon_t *common)
{
	Iec104S_t *self = calloc(1, sizeof(Iec104S_t));
	self->base.common = common;
	common->slave.sendAsdu = Iec104S_sendAsdu;
	common->slave.self = self;
	return self;
}


void Iec104S_free(Iec104S_t *self)
{
    CS104_Slave_stop(self->self);
    CS104_Slave_destroy(self->self);
	free(self);
}