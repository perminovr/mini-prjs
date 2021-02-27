/*******************************************************************************
  * @file    fb_iec850mmsm.c
  * @author  Пискунов С.Н.
  * @version v 3.5.11.1
  * @date    12.02.2020
  * @description - методы для работы с протоколом IEC61850 MMS Client через ФБ Codesys
  *
  *****************************************************************************/

#include <pthread.h>

#include "prjconf.h"

#if defined(PLATFORM_ELSYMA)
	#include "IoDrvElsyItf.h"
	//#include "IoDrvElsyDep.h"

#elif defined(PLATFORM_ELSYTMK)
	#include "IoDrvElsytmItf.h"
	//#include "IoDrvElsytmDep.h"
#endif

#include "CmpStd.h"
#include "CmpItf.h"
#include "CmpErrors.h"

#include "logserv.h"

#include "fb_iec850mmsm.h"
#include "mms_client.h"



/* ******************************************************************************************************
	FUN *************************************************************************************************
   ******************************************************************************************************
*/



static void GetLastOperationResult(const char *_fname_, MMSClientData_t *data, MCRes_t *res, IedClientError *err)
{
	if (data) {
		MCRes_t reslocal;
		IedClientError errlocal;
		MCRes_t *pr = (res)? res : &reslocal;
		IedClientError *pe = (err)? err : &errlocal;
		*pr = MMSClient_GetLastOperationResult(data, pe);
		log_write(TM_TRACE,"(FUN) mmsclient_%s -- result: \"%s\" // err: \"%s\"", 
				_fname_, MCRes_to_String(*pr), SysErr_to_String(*pe));
	}
}



void CDECL CDECL_EXT getlastoperationresultsys(getlastoperationresultsys_struct *p)
{
	GetLastOperationResult("getlastoperationresultsys", (MMSClientData_t *)p->id, 
			(MCRes_t *)&p->GetLastOperationResultSys, (IedClientError *)p->err);
}



void CDECL CDECL_EXT getnextbasetypemmsval(getnextbasetypemmsval_struct *p)
{
	MCRes_t res = MMSClient_GetNextBaseTypeMMSVal((MMSClientData_t *)p->id, (MMSVal_t *)p->dest);
	GetLastOperationResult("getnextbasetypemmsval", (MMSClientData_t *)p->id, 0, 0);
	p->GetNextBaseTypeMMSVal = res;
}



void CDECL CDECL_EXT getselecteddataset(getselecteddataset_struct *p)
{
	MCRes_t res = MMSClient_GetSelectedDataset((MMSClientData_t *)p->id, (MMSVal_t *)p->dest);
	GetLastOperationResult("getselecteddataset", (MMSClientData_t *)p->id, 0, 0);
	p->GetSelectedDataset = res;
}



void CDECL CDECL_EXT getreportscountsys(getreportscountsys_struct *p)
{
	int res = MMSClient_GetReportsCount((MMSClientData_t *)p->id);
	GetLastOperationResult("getreportscountsys", (MMSClientData_t *)p->id, 0, 0);
	*p->result = res;
	p->GetReportsCountSys = mcsrOk;
}



void CDECL CDECL_EXT getreportincludes(getreportincludes_struct *p)
{
	int ind_size = (int)(*(p->ind_size));
	MCRes_t res = MMSClient_GetReportIncludesIdx((MMSClientData_t *)p->id, (uint8_t *)p->indeces, &ind_size);
	*(p->ind_size) = (RTS_IEC_INT)ind_size;
	GetLastOperationResult("getreportincludes", (MMSClientData_t *)p->id, 0, 0);
	p->GetReportIncludes = res;
}



void CDECL CDECL_EXT setuprcbparams(setuprcbparams_struct *p)
{
	RcbParams *prm = &p->rcb;
	MMSRcbParam_t param = {
		.trgops = {
			.dataChg = prm->trgOpt.dataChg,
			.qualityChg = prm->trgOpt.qualityChg,
			.dataUpd = prm->trgOpt.dataUpd,
			.intgr = prm->trgOpt.intgr,
			.gi = prm->trgOpt.gi,
			.trans = prm->trgOpt.trans,
		},
		.ena = prm->ena,
		.bufTm = prm->bufTm,
		.intgrTm = prm->intgrTm,
		.gi = prm->gi,
		.purgeBuf = prm->purgeBuf,
		.entryId = prm->entryId,
		._force = p->force
	};
	MCRes_t res = MMSClient_SetupRcbParams((MMSClientData_t *)p->id, &param);
	GetLastOperationResult("setuprcbparams", (MMSClientData_t *)p->id, 0, 0);
	p->SetupRcbParams = res;
}



void CDECL CDECL_EXT getreportentryid(getreportentryid_struct *p)
{
	uint64_t result;
	MCRes_t res = MMSClient_GetReportEntryId((MMSClientData_t *)p->id, &result);
	GetLastOperationResult("getreportentryid", (MMSClientData_t *)p->id, 0, 0);
	*p->entryId = result;
	p->GetReportEntryId = res;
}



void CDECL CDECL_EXT selectnextdataset(selectnextdataset_struct *p)
{
	MCRes_t res = MMSClient_SelectNextDataset((MMSClientData_t *)p->id);
	GetLastOperationResult("selectnextdataset", (MMSClientData_t *)p->id, 0, 0);
	p->SelectNextDataset = res;
}



void CDECL CDECL_EXT getobjectbusyflag(getobjectbusyflag_struct *p)
{
	int res = MMSClient_isBusy((MMSClientData_t *)p->id);
	GetLastOperationResult("getobjectbusyflag", (MMSClientData_t *)p->id, 0, 0);
	*p->result = res;
	p->GetObjectBusyFlag = mcsrOk;
}



void CDECL CDECL_EXT setupsys(setupsys_struct *p)
{
	MCRes_t res = MMSClient_Setup((MMSClientData_t *)p->id, (MMSVal_t *)p->data);
	GetLastOperationResult("setupsys", (MMSClientData_t *)p->id, 0, 0);
	p->SetupSys = res;
}



void CDECL CDECL_EXT getlastread(getlastread_struct *p)
{
	MCRes_t res = MMSClient_ReadLastObject((MMSClientData_t *)p->id, (MMSVal_t *)p->dest);
	GetLastOperationResult("getlastread", (MMSClientData_t *)p->id, 0, 0);
	p->GetLastRead = res;
}



void CDECL CDECL_EXT setupinitcontrolsys(setupinitcontrolsys_struct *p)
{
	MCRes_t res = MMSClient_SetupInitControl((MMSClientData_t *)p->id, p->useConstantT, p->interlockCheck, p->synchroCheck);
	GetLastOperationResult("setupinitcontrolsys", (MMSClientData_t *)p->id, 0, 0);
	p->SetupInitControlSys = res;
}



void CDECL CDECL_EXT setorigincontrol(setorigincontrol_struct *p)
{
	MCRes_t res = MMSClient_SetOriginControl((MMSClientData_t *)p->id, p->orIdent, p->orCat);
	GetLastOperationResult("setorigincontrol", (MMSClientData_t *)p->id, 0, 0);
	p->SetOriginControl = res;
}



void CDECL CDECL_EXT getcontroltypesys(getcontroltypesys_struct *p)
{
	MCRes_t res = MMSClient_GetControlType((MMSClientData_t *)p->id, (MMSVal_Type_t *)p->dest);
	GetLastOperationResult("getcontroltypesys", (MMSClientData_t *)p->id, 0, 0);
	p->GetControlTypeSys = res;
}



void CDECL CDECL_EXT setupcancelcontrol(setupcancelcontrol_struct *p)
{
	MCRes_t res = MMSClient_SetupCancelControl((MMSClientData_t *)p->id);
	GetLastOperationResult("setupcancelcontrol", (MMSClientData_t *)p->id, 0, 0);
	p->SetupCancelControl = res;
}



/* ******************************************************************************************************
	MMS VALUES ******************************************************************************************
   ******************************************************************************************************
*/



void CDECL CDECL_EXT getstructelem(getstructelem_struct *p)
{
	MCRes_t res = MMSClient_GetStructElem((MMSVal_t*)p->structure, p->index, (MMSVal_t*)p->dest);
	log_write(TM_TRACE,"(FUN) mmsclient_getstructelem -- result: \"%s\"", MCRes_to_String(res));
	p->GetStructElem = res;
}



void CDECL CDECL_EXT setstructelem(setstructelem_struct *p)
{
	MCRes_t res = MMSClient_SetStructElem((MMSVal_t*)p->structure, p->index, (MMSVal_t*)p->value);
	log_write(TM_TRACE,"(FUN) mmsclient_setstructelem -- result: \"%s\"", MCRes_to_String(res));
	p->SetStructElem = res;
}



void CDECL CDECL_EXT getstructelemptr(getstructelemptr_struct *p)
{
	MCRes_t res = MMSClient_GetMMSValPtr((MMSVal_t*)p->structure, p->index, (MMSValPtr_t*)p->dest);
	log_write(TM_TRACE,"(FUN) mmsclient_getstructelemptr -- result: \"%s\"", MCRes_to_String(res));
	p->GetStructElemPtr = res;
}



void CDECL CDECL_EXT getstructelembyptr(getstructelembyptr_struct *p)
{
	MCRes_t res = MMSClient_GetMMSValByPtr((MMSValPtr_t*)p->ptr);
	log_write(TM_TRACE,"(FUN) mmsclient_getstructelembyptr -- result: \"%s\"", MCRes_to_String(res));
	p->GetStructElemByPtr = res;
}



void CDECL CDECL_EXT setstructelembyptr(setstructelembyptr_struct *p)
{
	MCRes_t res = MMSClient_SetMMSValByPtr((MMSValPtr_t*)p->ptr);
	log_write(TM_TRACE,"(FUN) mmsclient_setstructelembyptr -- result: \"%s\"", MCRes_to_String(res));
	p->SetStructElemByPtr = res;
}



void CDECL CDECL_EXT getsimplestruct(getsimplestruct_struct *p)
{
	int buf_size = (int)(*(p->buf_size));
	MCRes_t res = MMSClient_GetSimpleStruct((MMSVal_t *)p->structure, (uint8_t *)p->buf, &buf_size);
	*(p->buf_size) = (RTS_IEC_INT)buf_size;
	log_write(TM_TRACE,"(FUN) mmsclient_getsimplestruct -- result: \"%s\"", MCRes_to_String(res));
	p->GetSimpleStruct = res;
}



/* ******************************************************************************************************
	FB **************************************************************************************************
   ******************************************************************************************************
*/ 



typedef struct fb_iec61850_s fb_iec61850_t;


struct fb_iec61850_s {
	MMSClient_t *client;
	uint32_t key;
	uint8_t init;
};



#define DEFAULT_DEFINITIONS 						\
	fb_iec61850_t *priv;							\
	mmsclient_struct *fb = p->pInstance;			\
	priv = (fb_iec61850_t *)fb->priv;			


#define DEFAULT_DEF_AND_CHECK(res,m)				\
	fb_iec61850_t *priv;							\
	mmsclient_struct *fb = p->pInstance;			\
	priv = (fb_iec61850_t *)fb->priv;				\
	const char *_fname_ = m;						\
	if (!priv) {									\
		log_write(TM_ERROR,"(FB) %s: mem error", _fname_);	\
		res = mcsrSysErr;							\
		return;										\
	}



static void MMSClientData_cb(MCCbRes_t result, MMSClientData_t *data, void *user)
{
	fb_iec61850_t __attribute__((unused)) *priv = (fb_iec61850_t *)user;
	log_write(TM_TRACE, "MMSClientData_cb (%d) start with result: \"%s\"", 
			priv->key, MCCbRes_to_String(result));
    switch (result) {
        case mccrReadOk: {
        } break;
        case mccrReadFail: {
        } break;
        case mccrWriteOk: {
        } break;
        case mccrWriteFail: {
        } break;
        default: {
        } break;
    }
}



static void MMSClient_cb(MCCbRes_t result, MMSClient_t *client, void *user)
{
	fb_iec61850_t __attribute__((unused)) *priv = (fb_iec61850_t *)user;
	log_write(TM_TRACE, "MMSClient_cb (%d) start with result: \"%s\"", 
			priv->key, MCCbRes_to_String(result));
    switch (result) {
        case mccrChgState: {
			log_write(TM_TRACE, "Link is %s", (MMSClient_GetConnectState(client))?
					"up" : "down");
        } break;
        default: {
        } break;
    }
}



static MCRes_t ConnectToServer(MMSClient_t *client, const char *ip, const char *_fname_)
{
	IedClientError err;
	MCRes_t res;
	if (ip) {
		res = MMSClient_SetupConnectByIP(client, ip, &err);
	} else {
		res = MMSClient_SetupConnect(client, &err);
	}
	if (res != mcsrOk) {
		log_write(TM_ERROR,"(FB) %s -- result: \"%s\" // err: \"%s\"", 
				_fname_, MCRes_to_String(res), SysErr_to_String(err));
	}
	return res;
}



void CDECL CDECL_EXT mmsclient__main(mmsclient_main_struct *fb)
{
}



void CDECL CDECL_EXT mmsclient__getconnectstate(mmsclient_getconnectstate_struct *p)
{
	DEFAULT_DEFINITIONS
	if (!priv) {
		log_write(TM_ERROR,"(FB) mmsclient__getconnectstate: mem error");
		p->GetConnectState = 0;
		return;
	}
	int res = MMSClient_GetConnectState(priv->client);
	log_write(TM_TRACE,"(FB) mmsclient__getconnectstate -- result: %d", res);
	p->GetConnectState = res;
}



void CDECL CDECL_EXT mmsclient__connect(mmsclient_connect_struct *p)
{
	DEFAULT_DEF_AND_CHECK(p->Connect, "mmsclient__connect")

	MCRes_t res = ConnectToServer(priv->client, 0, _fname_);
	log_write(TM_TRACE,"(FB) %s -- result: \"%s\"", _fname_, MCRes_to_String(res));
	p->Connect = res;
}



void CDECL CDECL_EXT mmsclient__connectbyip(mmsclient_connectbyip_struct *p)
{
	DEFAULT_DEF_AND_CHECK(p->ConnectByIP, "mmsclient__connectbyip")

	MCRes_t res = ConnectToServer(priv->client, (const char*)p->ip, _fname_);
	log_write(TM_TRACE,"(FB) %s -- result: \"%s\"", _fname_, MCRes_to_String(res));
	p->ConnectByIP = res;
}



void CDECL CDECL_EXT mmsclient__disconnect(mmsclient_disconnect_struct *p)
{
	DEFAULT_DEF_AND_CHECK(p->Disconnect, "mmsclient__disconnect")

	IedClientError err;
	MCRes_t res = MMSClient_Disconnect(priv->client, &err);
	if (res != mcsrOk) {
		log_write(TM_ERROR,"(FB) %s -- result: \"%s\" // err: \"%s\"", 
				_fname_, MCRes_to_String(res), SysErr_to_String(err));
		p->Disconnect = res;
		return;
	}
	log_write(TM_TRACE,"(FB) %s -- result: \"%s\" // err: \"%s\"", 
			_fname_, MCRes_to_String(res), SysErr_to_String(err));
	p->Disconnect = res;
}



void CDECL CDECL_EXT mmsclient__init(mmsclient_init_struct *p)
{
	DEFAULT_DEF_AND_CHECK(p->Init, "mmsclient__init")

	p->Init = mcsrOk;
	if (!priv->init) {
		priv->client = MMSClient_FindById((int)fb->key);
		if (!priv->client) {
			log_write(TM_ERROR,"(FB) %s: couldn't find id %d", _fname_, (int)fb->key);
			p->Init = mcsrInvalidArg;
			return;
		} else {
			log_write(TM_NOTE,"(FB) %s: client with id %d found", _fname_, (int)fb->key);
		}

		MMSClient_SetupCallback(priv->client, MMSClient_cb, priv);
		for (MMSClientData_t *cd = MMSClient_GetData(priv->client); cd; cd = MMSClient_NextData(cd)) {
			MMSClientData_SetupCallback(cd, MMSClientData_cb, priv);
		}

		MCRes_t res = mcsrOk;
		if (p->connect) {
			res = ConnectToServer(priv->client, 0, _fname_);
			log_write(TM_TRACE,"(FB) %s -- result: \"%s\"", _fname_, MCRes_to_String(res));
		}

		if (res == mcsrOk)
			priv->init = 1;
		p->Init = res;
	}
}



void CDECL CDECL_EXT mmsclient__fb_init(mmsclient_fb_init_struct *p)
{
	DEFAULT_DEFINITIONS

	fb->priv = (RTS_IEC_BYTE *)calloc(1, sizeof(fb_iec61850_t));
	if (!fb->priv) {
		log_write(TM_ERROR,"(FB) mmsclient__fb_init: couldn't calloc mem");
		return;
	}
	priv = (fb_iec61850_t *)fb->priv;

	fb->key = p->key;
	priv->key = (uint32_t)p->key;
	
	//log_write(TM_NOTE,"(FB) mmsclient__fb_init: done ok");
}



void CDECL CDECL_EXT mmsclient__fb_exit(mmsclient_fb_exit_struct *p)
{
	DEFAULT_DEFINITIONS
	if (priv)
		free(priv);
	//log_write(TM_NOTE,"(FB) mmsclient__fb_exit: done ok");
}
