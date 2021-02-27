
#include "iec104gw.h"


static void Iec104MSlave_addToList(Iec104M_t *self, Iec104MSlave_t *slave, int asdu)
{
	if (self->slaves.isRedund) {
		if (asdu && !slave->asdu) {
			for (int i = 0; i < self->slaves.size ; ++i) {
				Iec104MSlave_t *s = &self->slaves.self[i];
				if (s->asdu) {
					if (s->asdu == asdu) {
						Iec104MSlave_t *it = s->list.head;
						while (it->list.next) { // thru all nodes != 0
							it = it->list.next;
						}
						it->list.next = slave;
						slave->list.head = s->list.head;
						slave->list.next = 0;
						slave->asdu = asdu;
						slave->isActive = false;
						return;
					}
				}
			}
		}
	}
	// first
	slave->list.head = slave;
	slave->list.next = 0;
	slave->asdu = asdu;
	slave->isActive = true;
}


static void handleServiceEvent(Iec104M_t *self, eServiceEvent event, CS104_Connection con)
{
	Iec104gCommon_t *cmn = self->base.common;
	Iec104MSlave_t *slave = CS104_Connection_getUserData(con);
	if (slave->isActive && slave->asdu) {
		switch (event) {
			case SERVICE_EVENT_CONNECTION_OPENED:
			case SERVICE_EVENT_CONNECTION_CLOSED:
			case SERVICE_EVENT_CONNECTION_STARTDT:
			case SERVICE_EVENT_CONNECTION_STOPDT: {
				Iec10XStructedAsdu_t asdu;
				Iec10XAsduObject_t object;
				object.addr = IEC104GW_LINK_DIAG_BASE + slave->asdu;
				object.payloadSize = 5; // 4 BSI + 1 QDS
				bzero(object.payload, object.payloadSize);
				object.payload[0] = (uint8_t)event;
				object.next = 0;
				asdu.type = M_BO_NA_1;
				asdu.classif = 1;
				asdu.cot = CS101_COT_SPONTANEOUS;
				asdu.addr = self->base.servAsduAddr;
				asdu.objects = &object;
				cmn->slave.sendAsdu(cmn->slave.self, makeStaticAsdu(&self->base.alp, &asdu));
			} break;
			default: {} break;
		}
	}
}

#ifdef DEBUG_COUNTERS
static volatile uint64_t iec104m_systicks = 0;
static volatile uint64_t iec104m_frames_rx = 0;
static volatile uint64_t iec104m_asduo_rx = 0;
#endif
static bool asduReceivedHandler(void* parameter, void *connection, CS101_ASDU asdu)
{
	Iec104M_t *self = (Iec104M_t *)parameter;
	Iec104gCommon_t *cmn = self->base.common;
	CS104_Connection con = (CS104_Connection)connection;
	Iec104MSlave_t *slave = CS104_Connection_getUserData(con);
	int asdu_addr = CS101_ASDU_getCA(asdu);
	if (!slave->asdu) {
		Iec104MSlave_addToList(self, slave, asdu_addr);
		handleServiceEvent(self, SERVICE_EVENT_CONNECTION_STARTDT, slave->con);
	}
	if (slave->isActive && slave->asdu == asdu_addr) {
		#ifdef DEBUG_COUNTERS
			iec104m_frames_rx++;
			iec104m_asduo_rx += CS101_ASDU_getNumberOfElements(asdu);
			if (!iec104m_systicks || ((getsystick()-iec104m_systicks) >= 1000000L)) {
				iec104m_systicks = getsystick();
				printf("systicks %llu frames_rx %llu asduo_rx %llu\n", iec104m_systicks/1000000L, iec104m_frames_rx, iec104m_asduo_rx);
			}
		#endif
		cmn->slave.sendAsdu(cmn->slave.self, asdu);
	}
	return true;
}


static void linkLayerStateChangedHandler(void *parameter, CS104_Connection connection, CS104_ConnectionEvent newState)
{
	Iec104M_t *self = (Iec104M_t *)parameter;
	eServiceEvent event = (eServiceEvent)((uint8_t)newState + 1);
	handleServiceEvent(self, event, connection);
}


static void Iec104M_sendAsdu(void *data, CS101_ASDU asdu)
{
	Iec104M_t *self = (Iec104M_t *)data;
	for (int i = 0; i < self->slaves.size; ++i) {
		CS104_Connection con = self->slaves.self[i].con;
		bool res = CS104_Connection_sendASDU(con, asdu);
		if (!res) {
			// todo error
		}
	}
}


static Iec104MSlave_t *Iec104MSlave_getActive(Iec104MSlaveList_t *list)
{
	for (Iec104MSlave_t *it = list->head; it; it = it->list.next) {
		if (it->isActive) return it;
	}
	return 0;
}


static inline void Iec104M_handleLinkCommand(eServiceEvent cmd, Iec104MSlave_t *slave, Iec104M_t *self)
{
	Iec104MSlaveList_t *list = &slave->list;
	for (Iec104MSlave_t *it = list->head; it; it = it->list.next) {
		CS104_Connection con = it->con;
		switch (cmd) {
			case SERVICE_EVENT_CONNECTION_OPENED: {
				if (!CS104_Connection_getState(con))
					CS104_Connection_connectAsync(con, 1000);
			} break;
			case SERVICE_EVENT_CONNECTION_CLOSED: {
				if (CS104_Connection_getState(con))
					CS104_Connection_close(con);
			} break;
			case SERVICE_EVENT_CONNECTION_STARTDT: {
				if (CS104_Connection_getState(con))
					CS104_Connection_sendStartDT(con);
			} break;
			case SERVICE_EVENT_CONNECTION_STOPDT: {
				if (CS104_Connection_getState(con))
					CS104_Connection_sendStopDT(con);
			} break;
			default: {} break;
		}
	}
}


static void handleConRedundDiag(Iec104M_t *self, uint32_t actCon, Iec104MSlave_t *slave)
{
	Iec104gCommon_t *cmn = self->base.common;
	Iec10XStructedAsdu_t asdu;
	Iec10XAsduObject_t object;
	if (slave->asdu) {
		object.addr = IEC104GW_CONREDUND_DIAG_BASE + slave->asdu;
		object.payloadSize = 5; // 4 BSI + 1 QDS
		bzero(object.payload, object.payloadSize);
		memcpy(object.payload, &actCon, sizeof(actCon));
		object.next = 0;
		asdu.type = M_BO_NA_1;
		asdu.classif = 1;
		asdu.cot = CS101_COT_SPONTANEOUS;
		asdu.addr = self->base.servAsduAddr;
		asdu.objects = &object;
		cmn->slave.sendAsdu(cmn->slave.self, makeStaticAsdu(&self->base.alp, &asdu));
	}
}


static inline void Iec104M_handleRedundCommand(uint32_t actCon, Iec104M_t *self, Iec104MSlave_t *slave, Iec104gCommon_t *cmn)
{
	Iec104MSlave_t *it = 0;
	Iec104MSlave_t *active = Iec104MSlave_getActive(&slave->list);
	if (active)
		active->isActive = false;
	uint32_t cnt = 1;
	for (it = slave->list.head; it; it = it->list.next) {
		if (actCon > 255) { // by ip addr
			if (it->addr == actCon) {
				it->isActive = true;
				break;
			}
		} else { // by order number
			if (cnt == actCon) {
				it->isActive = true;
				break;
			}
		}
		cnt++;
	}
	if (it)
		handleConRedundDiag(self, actCon, it);
}


static void Iec104M_handleCommand(void *data, int addr, uint32_t cmd)
{
	Iec104M_t *self = (Iec104M_t *)data;
	Iec104gCommon_t *cmn = self->base.common;
	for (int i = 0; i < self->slaves.size; ++i) {
		Iec104MSlave_t *slave = &self->slaves.self[i];
		if (slave->asdu+IEC104GW_LINK_CMD_BASE == addr) {
			Iec104M_handleLinkCommand((eServiceEvent)cmd, slave, self);
			return;
		} else if (slave->asdu+IEC104GW_CONREDUND_CMD_BASE == addr) {
			Iec104M_handleRedundCommand(cmd, self, slave, cmn);
			return;
		}
	}
}


void Iec104M_SMassageReceived(CS104_Connection con, void *data)
{}


void Iec104M_run(Iec104M_t *self)
{}


int Iec104M_init(Iec104M_t *self, Iec104MPrms_t *prms)
{
	self->base.servAsduAddr = prms->servAsduAddr;

	memcpy(&self->base.alp, &prms->alp, sizeof(struct sCS101_AppLayerParameters));
	strcpy(self->addr, prms->slot.addr);
	self->slaves.size = prms->slaves.size;
	self->slaves.isRedund = prms->slaves.isRedund;

	self->slaves.self = (Iec104MSlave_t *)calloc(prms->slaves.size, sizeof(Iec104MSlave_t));
	for (int i = 0; i < self->slaves.size ; ++i) {
		Iec104MPrmSlave_t *prm = &prms->slaves.self[i];
		CS104_Connection con = CS104_Connection_create(self->addr, prm->addr.ip, prm->addr.port);
		CS104_Connection_setAPCIParameters(con, &prms->apciParams);
		CS104_Connection_setAppLayerParameters(con, &self->base.alp);
		CS104_Connection_setConnectionHandler(con, linkLayerStateChangedHandler, self);
		CS104_Connection_setASDUReceivedHandler(con, asduReceivedHandler, self);
		CS104_Connection_setSMassageReceivedHandler(con, Iec104M_SMassageReceived, self);
		CS104_Connection_setUserData(con, &self->slaves.self[i]);
		//
		inet_aton(prm->addr.ip, (struct in_addr *)&self->slaves.self[i].addr);
		self->slaves.self[i].asdu = 0;
		self->slaves.self[i].con = con;
		Iec104MSlave_addToList(self, &self->slaves.self[i], prm->asdu);
	}

	return 0;
}


Iec104M_t *Iec104M_create(Iec104gCommon_t *common)
{
	Iec104M_t *self = calloc(1, sizeof(Iec104M_t));
	self->base.common = common;
	common->master.handleCommand = Iec104M_handleCommand;
	common->master.sendAsdu = Iec104M_sendAsdu;
	common->master.self = self;
	return self;
}


void Iec104M_free(Iec104M_t *self)
{
	for (int i = 0; i < self->slaves.size; ++i) {
    	CS104_Connection_destroy(self->slaves.self[i].con);
	}
	free(self->slaves.self);
	free(self);
}
