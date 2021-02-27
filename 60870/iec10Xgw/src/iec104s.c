
#include "iec10Xgw.h"


static bool asduHandler(void* parameter, IMasterConnection connection, CS101_ASDU asdu)
{
	Iec104S_t *self = (Iec104S_t *)parameter;
	Iec10XCommon_t *cmn = self->base.common;
	cmn->master.sendAsdu(cmn->master.self, asdu);
	return true;
}


static void Iec104S_sendAsdu(void *data, CS101_ASDU asdu)
{
	Iec104S_t *self = (Iec104S_t *)data;
	int maxConnections = CS104_Slave_getMaxConnections(self->self);
	MasterConnection *cons = CS104_Slave_getConnections(self->self);

	for (int i = 0; i < maxConnections; ++i) {
		IMasterConnection icon = CS104_Slave_getIMasterConnection(cons[i]);
		if (icon) IMasterConnection_sendASDU(icon, asdu);
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


Iec104S_t *Iec104S_create(Iec10XCommon_t *common)
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