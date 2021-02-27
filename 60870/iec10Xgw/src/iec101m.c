
#include "iec10Xgw.h"


static bool asduReceivedHandler(void* parameter, int address, CS101_ASDU asdu)
{
	Iec101M_t *self = (Iec101M_t *)parameter;
	Iec10XCommon_t *cmn = self->base.common;
	cmn->slave.sendAsdu(cmn->slave.self, asdu);
	return true;
}


static void linkLayerStateChangedHandler(void *parameter, int address, LinkLayerState newState)
{
	Iec101M_t *self = (Iec101M_t *)parameter;
	Iec10XCommon_t *cmn = self->base.common;
	uint8_t link = (newState == LL_STATE_AVAILABLE)? 1 : 0;

	Iec10XStructedAsdu_t asdu;
	Iec10XAsduObject_t object;
	object.addr = 0;
	object.payloadSize = 1;
	object.payload[0] = link;
	object.next = 0;
	asdu.type = M_EI_NA_1;
	asdu.classif = 1;
	asdu.cot = CS101_COT_INITIALIZED;
	asdu.addr = address;
	asdu.objects = &object;

	cmn->slave.sendAsdu(cmn->slave.self, makeStaticAsdu(&self->alp, &asdu));
}


static uint8_t getAsduAddr(CS101_ASDU asdu)
{
	Iec10XAsdu_t *masdu = (Iec10XAsdu_t *)asdu;
	CS101_AppLayerParameters parameters = masdu->parameters;
	uint8_t ret = (parameters->sizeOfCOT > 1)?
		(masdu->asdu[4]) : (masdu->asdu[3]);
	return ret;
}


static void Iec101M_sendAsdu(void *data, CS101_ASDU asdu)
{
	Iec101M_t *self = (Iec101M_t *)data;
	Iec10XCommon_t *cmn = self->base.common;
	AsduQue_t *item = (AsduQue_t *)calloc(1, sizeof(AsduQue_t));
	if (item) {
		memcpy(&item->self, asdu, sizeof(sCS101_StaticASDU));
		item->asduAddr = getAsduAddr(asdu);
		if (self->asduQue) {
			for (AsduQue_t *i = self->asduQue; i; i = i->next) {
				if (i->next == 0) { i->next = item; break; }
			}
		} else {
			self->asduQue = item;
		}
	}
}


void Iec101M_run(Iec101M_t *self)
{
	if (self->slaves.size) {
		CS101_Master master = self->self;
		if (self->asduQue) {
			int asduAddr = self->asduQue->asduAddr;
			if (!CS101_Master_hasMessageToSend(master, asduAddr)) {
				CS101_ASDU asdu = (CS101_ASDU)&self->asduQue->self;
				CS101_Master_useSlaveAddress(master, asduAddr);
				CS101_Master_sendASDU(master, asdu);
				AsduQue_t *del = self->asduQue;
				self->asduQue = del->next;
				free(del);
			}
		}
		uint64_t currTime = Hal_getTimeInMs();
		if (currTime > (self->lastPollTime + self->slaves.delay)) {
			self->lastPollTime = currTime;
			CS101_Master_pollSingleSlave(master, self->slaves.addrs[self->currSlaveIdx++]);
			if (self->currSlaveIdx >= self->slaves.size) self->currSlaveIdx = 0;
		}
		CS101_Master_run(master);
	}
}


int Iec101M_init(Iec101M_t *self, Iec101MPrms_t *prms)
{
	int i;
    SerialPort port = self->port = SerialPort_create(
			prms->port.name, prms->port.baudRate, prms->port.dataBits,
			prms->port.parity, prms->port.stopBits);
    CS101_Master master = self->self = CS101_Master_create(port, &prms->llp, &prms->alp, IEC60870_LINK_LAYER_UNBALANCED);
    CS101_Master_setASDUReceivedHandler(master, asduReceivedHandler, self);
	CS101_Master_setLinkLayerStateChanged(master, linkLayerStateChangedHandler, self);

	memcpy(&self->alp, &prms->alp, sizeof(struct sCS101_AppLayerParameters));

	self->slaves.size = prms->slaves.size;
	self->slaves.addrs = calloc(self->slaves.size, sizeof(int));
	self->slaves.delay = prms->slaves.delay;
	memcpy(self->slaves.addrs, prms->slaves.addrs, self->slaves.size*sizeof(int));

	self->sleepTime = prms->sleepTime;

	for (i = 0; i < self->slaves.size ; ++i) {
    	CS101_Master_addSlave(master, self->slaves.addrs[i]);
	}
    SerialPort_open(port);

	return 0;
}


Iec101M_t *Iec101M_create(Iec10XCommon_t *common)
{
	Iec101M_t *self = calloc(1, sizeof(Iec101M_t));
	self->base.common = common;
	common->master.sendAsdu = Iec101M_sendAsdu;
	common->master.self = self;
	return self;
}


void Iec101M_free(Iec101M_t *self)
{
	CS101_Master_stop(self->self);
    CS101_Master_destroy(self->self);
    SerialPort_close(self->port);
    SerialPort_destroy(self->port);
	free(self->slaves.addrs);
	free(self);
}
