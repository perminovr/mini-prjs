/*******************************************************************************
  * @file    esp8266.c
  * @author  Перминов Р.И.
  * @version v0.0.0.1
  * @date    19.04.2019
  *****************************************************************************/

#include "esp8266.h"
#include "uart.h"
#include <time.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>



#if defined(ESP_LOGGER)
# include "logserv.h"
# define ESP_LOG(t,m,...) log_write(t, "esp: " m,##__VA_ARGS__)
#elif defined(ESP_PRINTF)
# include <stdio.h>
# define ESP_LOG(t,m,...) printf("esp: " m "\n",##__VA_ARGS__)
#else
# define ESP_LOG(t,m,...) {}
#endif



#define ESP_FAIL(x,...) { 					\
	ESP_LOG(TM_ERROR,x,##__VA_ARGS__); 				\
	return ESP_Fail; 						\
}
#define ESP_PARAM_CHECK_FAIL ESP_FAIL



#define ESP_COMMON_CHECK() {				\
	if (_ESP_Driver == 0)					\
		ESP_FAIL("Driver doesn't created");	\
	if (_ESP_Driver->callback == 0)			\
		ESP_FAIL("Callback is null ptr");	\
}



#define ESP_CMD_DONE(x) {					\
		_ESP_ResetProcessType();			\
		_ESP_Driver->callback(&x);			\
		return;								\
}



#define ESP_PORT_SEND_CMD(x) {				\
		UART_WriteStr(x "\r\n");			\
}



#define ESP_EVENTS_NUM			4
#define ESP_EVENT_UART			0
#define ESP_EVENT_RESET			1
#define ESP_EVENT_USER			2
#define ESP_EVENT_KILL			3
#define ESP_EVENT_DSIZE			8



#define ESP_SUPPORTED_BAUDS		{115200, 921600, 9600}
#define ESP_PORT_BUF_SIZE		(2096+1024)
#define ESP_PORT_SPACE_DUR		16

#define ESP_PORT_WAIT_CNT_MAX	40
#define ESP_PORT_SHORT_DELAY	5
#define ESP_PORT_LONG_DELAY		25



#define ESP_RESET_CNT_MAX		5



#define ESP_NET_ADDR_SIZE		16



/*! ---------------------------------------------------------------------------
 * @def: ui32_u
*/
typedef union {
	uint8_t b[4];
	uint32_t d;
} ui32_u;



/*! ---------------------------------------------------------------------------
 * @def: ESP_CommonBuf_t
 *
 * @brief: Структура общего буфера
*/
typedef struct {
    uint8_t data[ESP_PORT_BUF_SIZE];
    uint16_t curPos;
} ESP_CommonBuf_t;



/*! ---------------------------------------------------------------------------
 * @def: ESP_Channel_d_t
 *
 * @brief: Каналы данных
*/
typedef struct {
	ESP_ChannelParams_t prm;
	char isOpened;
} ESP_Channel_d_t;



/*! ---------------------------------------------------------------------------
 * @def: ESP_State_t
 *
 * @brief: Состояние драйвера
*/
typedef enum {
	ESP_State_Created,
	ESP_State_Init,
	ESP_State_Connected,
	ESP_State_Work,
} ESP_State_t;



/*! ---------------------------------------------------------------------------
 * @def: ESP_ProcessData_u
 *
 * @brief: Данные, переданные процессу
*/
typedef union {
	void *data;
	int id;
	uint32_t ip;
	ESP_Init_t init;
	ESP_Channel_t channel;
	ESP_ChannelParams_t ch_params;
	ESP_APList_Sort_e sortType;
	ESP_AccessPoint_t accessPoint;
} ESP_ProcessData_u;



/*! ---------------------------------------------------------------------------
 * @def: ESP_Process_t
 *
 * @brief: Информация процесса
*/
typedef struct {
	ESP_Cmd_e type;
	ESP_ProcessData_u data;
} ESP_Process_t;



/*! ---------------------------------------------------------------------------
 * @def: ESP_Static_u
 *
 * @brief: Данные, передаваемые пользователю
*/
typedef struct {
	int id;
	ESP_ChannelParams_t ch_params;
	ESP_Channel_t recvChannel;
	ESP_AccessPoint_List_t APList;
	ESP_AccessPoint_Node_t accessPoints[ESP_ACCESS_POINTS];
	ESP_ConnectParams_t connParams;
	ESP_AccessPoint_t curAP;
} ESP_Static_t;



/*! ---------------------------------------------------------------------------
 * @def: ESP_Driver_t
 *
 * @brief: Драйвер ESP
*/
typedef struct {
	ESP_State_t state;							/* текущее состояние */
	ESP_Init_t init;							/* структура инициализации */
	ESP_CallBack_t callback;					/* user callback */
	pthread_t mthread;							/* основной поток */
	pthread_mutex_t procMutex;					/* мьютекс процесса */
	pthread_mutex_t killmu;						/* мьютекс обытия завершения потока */
	struct pollfd pfds[ESP_EVENTS_NUM];			/* дескрипторы работы (пользовательский и UART) */
	ESP_Channel_d_t channels[ESP_CHANNELS];		/* каналы передачи данных */
	ESP_CommonBuf_t recvBuffer;					/* буфер приема UART */
	ESP_CommonBuf_t sendBuffer;					/* буфер передачи UART */
	ESP_Process_t curProc;						/* текущий процесс */
	ESP_Static_t staticData;					/* "статические" данные, отдаваемые пользователю */
	uint8_t errToResetCnt;						/* счетчик ошибок обращения к esp до переинициализации @ref ESP_RESET_CNT_MAX */
	uint32_t cmdToSuccessCnt;					/* счетчик установки команд на исполенение до первого успешного выполнения (для печати логов)   */
} ESP_Driver_t;



ESP_Driver_t *_ESP_Driver;



static void DumpHex(const void* data, size_t size);



char *strnstr(const char *s1, const char *s2, size_t len)
{
	const char *ss2 = s2;
	char *ret = 0;
	char *tmp;

	size_t i;
	for (i = 0; i < len; ++i) {
		if ((*ss2)) {
			tmp = (char*)&(s1[i]);
			if ((*tmp) == (*ss2)) {
				ss2++;
				if (!ret)
					ret = tmp;
			} else {
				if (ret) {
					i -= ss2 - s2;
					ret = 0;
				}
				ss2 = s2;
			}
		} else {
			break;
		}
	}

	if ((*ss2) == 0)
		return ret;

	return 0;
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_RaiseEvent
 *
 * @brief: Сигнал наступления события
 *
 * input parameters
 * fd - дексриптор события
 *
 * output parameters
 *
 * return результат выполнения:
 * 		!= ESP_EVENT_DSIZE - ошибка
*/
static inline int _ESP_RaiseEvent(int fd)
{
	uint8_t buf = 1;
	return write(fd, &buf, ESP_EVENT_DSIZE);
}
#define _ESP_RaiseUserEvent() _ESP_RaiseEvent(_ESP_Driver->pfds[ESP_EVENT_USER].fd)
#define _ESP_RaiseKillEvent() _ESP_RaiseEvent(_ESP_Driver->pfds[ESP_EVENT_KILL].fd)
#define _ESP_RaiseResetEvent() _ESP_RaiseEvent(_ESP_Driver->pfds[ESP_EVENT_RESET].fd)



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_EndEvent
 *
 * @brief: Выключение сигнала (прием события)
 *
 * input parameters
 * fd - дексриптор события
 *
 * output parameters
 *
 * return результат выполнения:
 * 		!= ESP_EVENT_DSIZE - ошибка
*/
static inline int _ESP_EndEvent(int fd)
{
	uint8_t buf[8];
	return read(fd, &buf, ESP_EVENT_DSIZE);
}
#define _ESP_EndUserEvent() _ESP_EndEvent(_ESP_Driver->pfds[ESP_EVENT_USER].fd)
#define _ESP_EndKillEvent() _ESP_EndEvent(_ESP_Driver->pfds[ESP_EVENT_KILL].fd)
#define _ESP_EndResetEvent() _ESP_EndEvent(_ESP_Driver->pfds[ESP_EVENT_RESET].fd)



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_GetProcessType
 *
 * @brief: Потокобезопасный возврат типа исполняемого процесса
 *
 * input parameters
 *
 * output parameters
 *
 * return тип процесса
*/
static ESP_Cmd_e _ESP_GetProcessType(void)
{
	ESP_Cmd_e ret;
	pthread_mutex_lock(&_ESP_Driver->procMutex);
	ret = _ESP_Driver->curProc.type;
	pthread_mutex_unlock(&_ESP_Driver->procMutex);
	return ret;
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_SetProcessType
 *
 * @brief: Потокобезопасная установка типа исполняемого процесса
 *
 * input parameters
 *
 * output parameters
 *
 * no return
*/
static inline void _ESP_SetProcessType(ESP_Cmd_e type)
{
	pthread_mutex_lock(&_ESP_Driver->procMutex);
	_ESP_Driver->curProc.type = type;
	pthread_mutex_unlock(&_ESP_Driver->procMutex);
}
#define _ESP_ResetProcessType() _ESP_SetProcessType(ESP_Cmd_No)



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_PrintProcessWarn
 *
 * @brief: Защищенный принт логов: повторы одного и того же лога
 * 		будут опущены
 *
 * input parameters
 *
 * output parameters
 *
 * no return
*/
#define _ESP_PrintProcessWarn(format,...) { 					\
	switch (_ESP_Driver->cmdToSuccessCnt) {							\
		case 0:													\
			ESP_LOG(TM_WARN,format,##__VA_ARGS__);				\
			break;												\
		case 1:													\
			ESP_LOG(TM_WARN,format" ...",##__VA_ARGS__);		\
			break;												\
		default: 												\
			break;												\
	}															\
	_ESP_Driver->cmdToSuccessCnt++;									\
	if (!_ESP_Driver->cmdToSuccessCnt)								\
		_ESP_Driver->cmdToSuccessCnt = 1;							\
}



// /*! ---------------------------------------------------------------------------
//  * @fn: _ESP_SetupResetChip
//  *
//  * @brief: Установка команды на сброс ESP
//  *
//  * input parameters
//  *
//  * output parameters
//  *
//  * no return
//  *
// */
// static void _ESP_SetupResetChip(void)
// {
// 	ESP_LOG(TM_WARN, "CHIP WILL BE RESETED!");
// 	_ESP_RaiseResetEvent();
// }



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_SetProcess
 *
 * @brief: Передача команды и аргументов основному потоку
 *
 * input parameters
 *
 * output parameters
 *
 * return результат выполнения @ref ESP_Result_e
 *
*/
static ESP_Result_e _ESP_SetProcess(ESP_Cmd_e type, const void *data, size_t dataSize)
{
	/* нельзя прервать операцию */
	ESP_Cmd_e currCmd = _ESP_GetProcessType();
	if (currCmd != ESP_Cmd_No) {
		_ESP_PrintProcessWarn("%s %d %d", "_ESP_SetProcess drv is busy", 
			type, currCmd);
		return ESP_Fail;
	}

	/* здесь поток не использует curProc */
	_ESP_Driver->curProc.type = type;
	memcpy(&_ESP_Driver->curProc.data, data, dataSize);
	_ESP_Driver->cmdToSuccessCnt = 0;

	if (_ESP_RaiseUserEvent() != ESP_EVENT_DSIZE) {
		_ESP_Driver->curProc.type = ESP_Cmd_No;
		ESP_FAIL("_ESP_SetProcess raise event fail");
	}
	return ESP_Ok;
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_RecvBuf_Reset
 *
 * @brief: Сброс буфера приема UART
 *
 * input parameters
 *
 * output parameters
 *
 * no return
 *
*/
static inline void _ESP_RecvBuf_Reset(void)
{
	memset(&_ESP_Driver->recvBuffer, 0, sizeof(ESP_CommonBuf_t));
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_PortDataRcvDone
 *
 * @brief: Завершение приема данных с ESP
 *
 * input parameters
 *
 * output parameters
 *
 * no return
 *
*/
static inline void _ESP_PortDataRcvDone(ESP_Channel_t *channel, ESP_Result_e res)
{
	ESP_CallBack_Data_t cb_data = {
			.type = ESP_Cmd_Receive,
			.res = res,
			.data = channel
	};

	_ESP_Driver->callback(&cb_data);
}
#define _ESP_PortDataRcvDone_Ok(x) _ESP_PortDataRcvDone(x, ESP_Ok)
#define _ESP_PortDataRcvDone_Fail(x) _ESP_PortDataRcvDone(x, ESP_Fail)



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_PortRead
 *
 * @brief: Чтение данных с порта
 *
 * input parameters
 *
 * output parameters
 *
 * no return
*/
static void _ESP_PortRead(void)
{
	if (_ESP_Driver->recvBuffer.curPos >= ESP_PORT_BUF_SIZE) {
		ESP_LOG(TM_ERROR, "recvBuffer overrun: reset");
		_ESP_RecvBuf_Reset();
	}
	//UART_WaitSpace(ESP_PORT_SPACE_DUR);
	size_t size = UART_GetAvailableBytes();
	size_t available = ESP_PORT_BUF_SIZE - _ESP_Driver->recvBuffer.curPos;
	if (size > available) {
		size = available;
	}
	int res = UART_ReadNonBlocking(
			_ESP_Driver->recvBuffer.data + _ESP_Driver->recvBuffer.curPos,
			size
	);
	if (res > 0) {
		_ESP_Driver->recvBuffer.curPos += res;
	}
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_PortHandleRecvdEvent
 *
 * @brief: Анализ и извлечение данных и с порта
 *
 * NOTE: выполняется следующая последовательность:
 * 		- Поиск ключа +IPD
 * 		- Извлечение метаданных
 * 		- Извлечение данных:
 * 			* если данные готовы - отправка в callback
 * 			* если данные не готовы:
 * 				** расчет остатка
 * 				** определение места под остаток
 * 				** ожидание остатка
 * 				** отправка в callback
 *
 * input parameters
 *
 * output parameters
 *
 * no return
 *
*/
static void _ESP_PortHandleRecvdEvent(void)
{
	ESP_Channel_t *channel = &_ESP_Driver->staticData.recvChannel;
	int cntExit = 0;
	char *it;

esp_porthandlerecvdevent_start:
	// exit after 5 frames
	if (cntExit++ > 5)
		return;

	// extract one frame	******************************************************************************
	if ( 
		_ESP_Driver->state >= ESP_State_Connected &&
		(it = strnstr((char*)_ESP_Driver->recvBuffer.data, "+IPD,", _ESP_Driver->recvBuffer.curPos))		
	) {
		// extract metadata
		ui32_u uip;
		uint16_t port;
		int id;
		uint32_t len;
		char tmp = 0;
		// exmpl: +IPD,1,274,192.168.1.202,58006:
		if ( (sscanf(
				it, "+IPD,%d,%u,%hhu.%hhu.%hhu.%hhu,%hu%c",
				&id, &len, &uip.b[0], &uip.b[1], &uip.b[2], &uip.b[3], &port, &tmp) == 8) &&
				tmp == ':'
		) {
			// extract data
			if (len < ESP_PORT_BUF_SIZE) {
				channel->id = id;
				channel->data.buf = 0;
				channel->data.size = (int)len;
				channel->data.remoteIP = uip.d;
				channel->data.remotePort = port;
				// compute frame params
				char *data_start = 0;
				char *next_frame = 0;
				size_t remainder = 0;
				size_t recvd = 0;
				uint16_t tail = 0;
				{
					char metadata[64];
					sprintf(metadata, "+IPD,%d,%u,%hhu.%hhu.%hhu.%hhu,%hu:",
							id, len, uip.b[0], uip.b[1], uip.b[2], uip.b[3], port);
					data_start = it + strlen(metadata);
					recvd = (size_t)(((char*)_ESP_Driver->recvBuffer.data + _ESP_Driver->recvBuffer.curPos) - data_start);
					// protect from errors of sscanf
					if (recvd > ESP_PORT_BUF_SIZE) {
						ESP_LOG(TM_ERROR, "Failed to receive data // recvd = %lu", recvd);
						goto esp_porthandlerecvdevent_call_fail;
					}
					// analize for next frame
					if ((size_t)len < recvd) {
						tail = (uint16_t)recvd - (uint16_t)len;
						next_frame = data_start + tail;
					} else {
						remainder = (size_t)len - recvd;
					}
				}				
				// remainder out of buf size
				if (remainder > (ESP_PORT_BUF_SIZE - _ESP_Driver->recvBuffer.curPos)) {
					memcpy(_ESP_Driver->recvBuffer.data, data_start, recvd);
					_ESP_Driver->recvBuffer.curPos = (uint16_t)recvd;
					data_start = (char*)_ESP_Driver->recvBuffer.data;
				}
				// data is ready
				else if ( !remainder ) {
					goto esp_porthandlerecvdevent_call_ok;
				}
				// need to wait
				int cnt = 0;
				while(1) {
					UART_WaitSpace(ESP_PORT_SPACE_DUR);
					if (UART_GetAvailableBytes() >= remainder) {
						// ready - get remainder
						int res = UART_ReadNonBlocking(
								_ESP_Driver->recvBuffer.data + _ESP_Driver->recvBuffer.curPos,
								remainder
						);
						if (res > 0) {
							_ESP_Driver->recvBuffer.curPos += res;
							goto esp_porthandlerecvdevent_call_ok;
						} else {
							ESP_LOG(TM_ERROR, "Failed to receive data // remainder = %d", res);
							goto esp_porthandlerecvdevent_call_fail;
						}
					}
					if (++cnt > ESP_PORT_WAIT_CNT_MAX) {
						ESP_LOG(TM_WARN, "Failed to receive data: a lot of spaces // remainder = %lu", remainder);
						return;
					}
				}
esp_porthandlerecvdevent_call_ok:
				channel->data.buf = (void*)data_start;
				_ESP_PortDataRcvDone_Ok(channel);
				// save data if need
				if (next_frame) {					
					memcpy(_ESP_Driver->recvBuffer.data, next_frame, tail);
					_ESP_Driver->recvBuffer.curPos = tail;
					memset(_ESP_Driver->recvBuffer.data + _ESP_Driver->recvBuffer.curPos, 0, 
							ESP_PORT_BUF_SIZE - _ESP_Driver->recvBuffer.curPos);
					goto esp_porthandlerecvdevent_start;
				} 
				// or reset all
				else {
					_ESP_RecvBuf_Reset();
				}
				return;
			} else {
				ESP_LOG(TM_ERROR, "Failed to receive data: oversize");
				goto esp_porthandlerecvdevent_call_fail;
			}
		}
		return;

esp_porthandlerecvdevent_call_fail:
		_ESP_RecvBuf_Reset();
		_ESP_PortDataRcvDone_Fail(0);
		return;
	}
	// // check for disconnect	******************************************************************************
	// else if ( 
	// 	(it = strnstr((char*)_ESP_Driver->recvBuffer.data, "\r\nWIFI DISCONNECT\r\n", _ESP_Driver->recvBuffer.curPos)) 
	// ) {
	// 	ESP_LOG(TM_WARN, "Disconnected from AP");
	// 	_ESP_RecvBuf_Reset();
	// 	_ESP_Driver->state = ESP_State_Init;
	// }
	// // reset chip if has disconnected	******************************************************************
	// else if ( 
	// 	_ESP_Driver->state == ESP_State_Init &&
	// 	(it = strnstr((char*)_ESP_Driver->recvBuffer.data, "WIFI GOT IP\r\n", _ESP_Driver->recvBuffer.curPos))		
	// ) {
	// 	ESP_LOG(TM_WARN, "AP was detected after disconnect");
	// 	_ESP_RecvBuf_Reset();
	// 	_ESP_SetupResetChip();
	// }
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_PortRecvWork
 *
 * @brief: Работа с портом по приему
 *
 * input parameters
 *
 * output parameters
 *
 * no return
 *
*/
static inline void _ESP_PortRecvWork(void)
{
	_ESP_PortRead();
	_ESP_PortHandleRecvdEvent();
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_AnsWaiting
 *
 * @brief: Ожидание ответа на команду
 *
 * NOTE: Пробует принять данные с UART с тайм-аутом ESP_PORT_WAIT_CNT_MAX раз
 *
 * input parameters
 * @param expected - ожидаемые строки в ответе. строки должны быть разделены \0,
 * 		последняя строка должна дополняться еще одним \0 
 * 		(например, expected="abc\0def\0")
 * @param timeout - время на прием данных с UART, мс
 * @param clr - флаг на очистку буфера приема до начала стадии ожидания
 *
 * output parameters
 *
 * return результат выполнения @ref ESP_Result_e
 *
*/
static ESP_Result_e _ESP_AnsWaiting(const char *expected, uint16_t timeout, uint8_t clr)
{	
	const char *cur;
	int i, rc;
	size_t res;

	if (clr)
		_ESP_RecvBuf_Reset();

	for (i = 0; i < ESP_PORT_WAIT_CNT_MAX;) {
		poll(&_ESP_Driver->pfds[ESP_EVENT_UART], 1, timeout);
		res = UART_GetAvailableBytes();
		if (res) {
			if (_ESP_Driver->recvBuffer.curPos + res >= ESP_PORT_BUF_SIZE) {
				ESP_LOG(TM_ERROR, "AnsWaiting overrun");
				ESP_LOG(TM_ERROR, "AnsWaiting failed, dump (%u):", _ESP_Driver->recvBuffer.curPos + res);
				DumpHex(_ESP_Driver->recvBuffer.data, ESP_PORT_BUF_SIZE);
				_ESP_RecvBuf_Reset();
				return ESP_Fail;
			}
			rc = UART_ReadNonBlocking(_ESP_Driver->recvBuffer.data + _ESP_Driver->recvBuffer.curPos, res);
			if (rc == (int)res) {
				_ESP_Driver->recvBuffer.curPos += res;
				for (cur = expected; *cur; cur += strlen(cur)+1) {
					if ( strnstr((char*)_ESP_Driver->recvBuffer.data, cur, _ESP_Driver->recvBuffer.curPos) ) {
						return ESP_Ok;
					}
				}
			} else {
				ESP_FAIL("AnsWaiting rc (%d) != res (%u)", rc, res);
			}
		} else {
			i++;
		}
	}

	for (cur = expected; *cur; cur += strlen(cur)+1) {
		ESP_LOG(TM_ERROR, "AnsWaiting expected one of: %s", cur);
	}
	ESP_LOG(TM_ERROR, "AnsWaiting failed, dump (%u):", _ESP_Driver->recvBuffer.curPos);
	DumpHex(_ESP_Driver->recvBuffer.data, _ESP_Driver->recvBuffer.curPos);
	
	if (_ESP_Driver->state == ESP_State_Work) {
		// сброс модуля
		_ESP_Driver->errToResetCnt++;
		if (_ESP_Driver->errToResetCnt > ESP_RESET_CNT_MAX) {
			_ESP_Driver->errToResetCnt = 0;
			// _ESP_SetupResetChip();
		}
	}

	return ESP_Fail;
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_CheckConnectStatus
 *
 * @brief: Проверка статуса подключения ESP к ТД
 *
 * NOTE:
 * 		- Проверка статуса соединения: AT+CWJAP_CUR? (-> No ap / +CWJAP_CUR:)
 * 			* если соединение есть, то для получения:
 * 				** части параметров: взять с AT+CWJAP_CUR?
 * 				** всех параметров: AT+CWLAP (-> найти свою ТД)
 *
 * input parameters
 *
 * output parameters
 * @param status - статус соединения:
 * 		0 - нет соединения
 * 		1 - подключен к ТД
 *
 * return результат выполнения @ref ESP_Result_e
 *
*/
static ESP_Result_e _ESP_CheckConnectStatus(int *status)
{
	ESP_PORT_SEND_CMD("AT+CWJAP_CUR?");
	// OK follows after +CWJAP_CUR: / No AP
	if (_ESP_AnsWaiting("\r\nOK\r\n", ESP_PORT_LONG_DELAY, 1) != ESP_Ok) {
		ESP_FAIL("Couldn't get AT+CWJAP_CUR?");
	}

	char *it;
	it = strnstr((char*)_ESP_Driver->recvBuffer.data, "+CWJAP_CUR:", _ESP_Driver->recvBuffer.curPos);
	if (it) {
		*status = 1;
		return ESP_Ok;
	}
	it = strnstr((char*)_ESP_Driver->recvBuffer.data, "No AP\r\n", _ESP_Driver->recvBuffer.curPos);
	if (it) {
		*status = 0;
		return ESP_Ok;
	}

	ESP_FAIL("Not expected answer on AT+CWJAP_CUR?");
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_GetAccessPointList
 *
 * @brief: Получение списка видимых ТД
 *
 * input parameters
 *
 * output parameters
 * @param ret - список ТД
 *
 * return результат выполнения @ref ESP_Result_e
 *
*/
static ESP_Result_e _ESP_GetAccessPointList(ESP_AccessPoint_List_t **ret)
{
	ESP_AccessPoint_List_t *list = &_ESP_Driver->staticData.APList;
	ESP_AccessPoint_Node_t *accessPoints = _ESP_Driver->staticData.accessPoints;

	memset(accessPoints, 0, sizeof(ESP_AccessPoint_Node_t)*ESP_ACCESS_POINTS);
	memset(list, 0, sizeof(ESP_AccessPoint_List_t));
	list->head = accessPoints;

	*ret = 0;

	ESP_PORT_SEND_CMD("AT+CWLAP");
	if (_ESP_AnsWaiting("\r\nOK\r\n\0", ESP_PORT_LONG_DELAY * 10, 1) != ESP_Ok) {
		ESP_FAIL("Couldn't get AT+CWLAP");
	}

	ESP_AccessPoint_Node_t *node;
	size_t len;
	char *start, *end;
	char *it = (char*)_ESP_Driver->recvBuffer.data;
	char tmp[64];
	while (1) {
		it = strnstr(it, "+CWLAP:(", _ESP_Driver->recvBuffer.curPos);
		if (it) {
			node = &accessPoints[list->count];
			// exmpl: +CWLAP:(0,"WIFI_HMI",-35,"64:70:02:70:6d:f6",4,10,0,0,0,7,1)
			start = it;
			// enc
			int enc;
			if (
					sscanf(start, "+CWLAP:(%d,\"",
							&enc
					) != 1
			) {
				goto esp_apl_getlist_find_next;
			}
			node->ap.enc = (ESP_AP_ENC_e)enc;
			// name
			sprintf(tmp, "+CWLAP:(%d,\"", node->ap.enc);
			start += strlen(tmp);
			end = strstr(start, "\"");
			if (!end)
				goto esp_apl_getlist_find_next;
			len = end - start;
			len = (len <= ESP_AP_NAME_MAX_SIZE)? len : ESP_AP_NAME_MAX_SIZE;
			strncpy(node->ap.name, start, len);
			node->ap.name[ESP_AP_NAME_MAX_SIZE] = 0;
			// rssi
			start = end + 2; // ",
			int rssi;
			if (
					sscanf(start, "%d,\"",
							&rssi
					) != 1
			) {
				goto esp_apl_getlist_find_next;
			}
			node->ap.RSSI = rssi;
			// mac
			sprintf(tmp, "%d,\"", node->ap.RSSI);
			start += strlen(tmp);
			end = strstr(start, "\"");
			if (!end)
				goto esp_apl_getlist_find_next;
			len = end - start;
			len = (len <= ESP_AP_MAC_SIZE)? len : ESP_AP_MAC_SIZE;
			strncpy(node->ap.MAC, start, len);
			node->ap.MAC[ESP_AP_MAC_SIZE] = 0;
			// other
			start = end + 2; // ",
			int channel, freq_off, freq_cali, pc, gc, bgn, wps;
			if (
					sscanf(
							start, "%d,%d,%d,%d,%d,%d,%d",
							&channel,
							&freq_off,
							&freq_cali,
							&pc,
							&gc,
							&bgn,
							&wps
					) != 7
			) {
				goto esp_apl_getlist_find_next;
			}
			node->ap.channel = channel;
			node->ap.freq_off = freq_off;
			node->ap.freq_cali = freq_cali;
			node->ap.pc = (ESP_AP_CIPHER_e)pc;
			node->ap.gc = (ESP_AP_CIPHER_e)gc;
			node->ap.bgn.val = (uint8_t)bgn;
			node->ap.wps = (ESP_AP_WPS_e)wps;
			// passwd
			node->ap.password[0] = 0;
			// next
			node->next = 0;
			if (list->count > 0)
				accessPoints[list->count-1].next = node;
			list->count++;	
esp_apl_getlist_find_next:
			it++;
			if (list->count >= ESP_ACCESS_POINTS) {
				break;
			}
		} else {
			break;
		}
	}

	*ret = list;

	return ESP_Ok;
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_GetConnectParams
 *
 * @brief: Получение параметров соединения с ТД
 *
 * input parameters
 *
 * output parameters
 * @param ret - параметры соединения
 *
 * return результат выполнения @ref ESP_Result_e
 *
*/
static ESP_Result_e _ESP_GetConnectParams(ESP_ConnectParams_t **ret)
{
	ESP_ConnectParams_t *params = &_ESP_Driver->staticData.connParams;
	ESP_AccessPoint_t *apoint = &_ESP_Driver->staticData.curAP;

	memset(params, 0, sizeof(ESP_ConnectParams_t));
	params->ap = apoint;

	*ret = 0;

	{ // извлечение имени и MAC ТД
		char *start = 0, *end = 0;
		size_t len;
		start = strnstr((char*)_ESP_Driver->recvBuffer.data, "+CWJAP_CUR:\"", _ESP_Driver->recvBuffer.curPos);	// 12
		// exmpl: +CWJAP_CUR:"WIFI_HMI","64:70:02:70:6d:f6",4,-37,0
		if (start) {
			start += 12;
			end = strstr(start, "\"");
			if (end) {
				len = end - start;
				len = (len <= ESP_AP_NAME_MAX_SIZE)? len : ESP_AP_NAME_MAX_SIZE;
				strncpy(apoint->name, start, len);
				apoint->name[ESP_AP_NAME_MAX_SIZE] = 0;
				// mac
				start = end + 3; // ","
				end = strstr(start, "\"");
				if (end) {
					len = end - start;
					len = (len <= ESP_AP_MAC_SIZE)? len : ESP_AP_MAC_SIZE;
					strncpy(apoint->MAC, start, len);
					apoint->MAC[ESP_AP_MAC_SIZE] = 0;
				}
			}
		}
		if (!start || !end)
			ESP_FAIL("Couldn't find name/MAC of AP");
	}

	// получение остальных параметров ТД
	ESP_AccessPoint_List_t *list;
	if (_ESP_GetAccessPointList(&list) != ESP_Ok)
		return ESP_Fail;

	int i = 0;
	ESP_AccessPoint_Node_t *node;
	for (node = list->head; node; node = node->next) {
		if ( strcmp(node->ap.MAC, apoint->MAC) == 0 ) {
			memcpy(apoint, &node->ap, sizeof(ESP_AccessPoint_t));
			break;
		}
		i++;
	}

	if (i == list->count) {
		return ESP_Fail;
	}

	{ // режим работы
		ESP_PORT_SEND_CMD("AT+CWDHCP_CUR?");
		if (_ESP_AnsWaiting("\r\nOK\r\n\0", ESP_PORT_SHORT_DELAY, 1) != ESP_Ok) {
			ESP_FAIL("Couldn't get AT+CWDHCP_CUR?");
		}
		int mode;
		if (
				sscanf((char*)_ESP_Driver->recvBuffer.data, "+CWDHCP_CUR:%d",
						&mode
				) != 1
		) {
			return ESP_Fail;
		}
		// mask 0x1 => ESP DHCP station
		params->nmode = (mode & 0x1)? ESP_NM_DHCP : ESP_NM_Static;
	}

	{ // ip, mask, gw
		ESP_PORT_SEND_CMD("AT+CIPSTA_CUR?");
		if (_ESP_AnsWaiting("\r\nOK\r\n\0", ESP_PORT_SHORT_DELAY, 1) != ESP_Ok) {
			ESP_FAIL("Couldn't get AT+CIPSTA_CUR?");
		}
		char *it;
		ui32_u uip;
		it = strnstr((char*)_ESP_Driver->recvBuffer.data, "+CIPSTA_CUR:ip:\"", _ESP_Driver->recvBuffer.curPos); // 16
		// exmpl: +CIPSTA_CUR:ip:"192.168.1.100"
		if (it) {
			it += 16;
			if ( sscanf(
					it, "%hhu.%hhu.%hhu.%hhu\"",
					&uip.b[0], &uip.b[1], &uip.b[2], &uip.b[3]) == 4
			) {
				params->ip = uip.d;
			}
		}
		it = strnstr((char*)_ESP_Driver->recvBuffer.data, "+CIPSTA_CUR:gateway:\"", _ESP_Driver->recvBuffer.curPos); // 21
		// exmpl: +CIPSTA_CUR:gateway:"192.168.1.1"
		if (it) {
			it += 21;
			if ( sscanf(
					it, "%hhu.%hhu.%hhu.%hhu\"",
					&uip.b[0], &uip.b[1], &uip.b[2], &uip.b[3]) == 4
			) {
				params->gateway = uip.d;
			}
		}
		it = strnstr((char*)_ESP_Driver->recvBuffer.data, "+CIPSTA_CUR:netmask:\"", _ESP_Driver->recvBuffer.curPos); // 21
		// exmpl: +CIPSTA_CUR:netmask:"255.255.255.0"
		if (it) {
			it += 21;
			if ( sscanf(
					it, "%hhu.%hhu.%hhu.%hhu\"",
					&uip.b[0], &uip.b[1], &uip.b[2], &uip.b[3]) == 4
			) {
				params->mask = uip.d;
			}
		}

	}

	*ret = params;

	return ESP_Ok;
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_CheckGetConnectParams
 *
 * @brief: Получение параметров соединения с ТД
 *
 * input parameters
 *
 * output parameters
 * @param ret - параметры соединения
 *
 * return результат выполнения @ref ESP_Result_e
 *
*/
static ESP_Result_e _ESP_CheckGetConnectParams(ESP_ConnectParams_t **ret)
{
	*ret = 0;

	int status = 0;
	if (_ESP_CheckConnectStatus(&status) != ESP_Ok)
		return ESP_Fail;

	if (!status)
		return ESP_Ok;

	return _ESP_GetConnectParams(ret);
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_ChannelClose
 *
 * @brief: Закрыть канал передачи данных
 *
 * input parameters
 *
 * output parameters
 * @param ret - id канала
 *
 * return результат выполнения @ref ESP_Result_e
 *
*/
static ESP_Result_e _ESP_ChannelClose(int **ret)
{
	int *pret = &_ESP_Driver->staticData.id;
	const int *id = &_ESP_Driver->curProc.data.id;

	memset(pret, 0, sizeof(int));

	*ret = 0;

	if (_ESP_Driver->channels[*id].isOpened) {
		char tmp[32];
		_ESP_Driver->channels[*id].isOpened = 0;
		sprintf(tmp, "AT+CIPCLOSE=%d\r\n", *id);
		UART_WriteStr(tmp);
		const char *expected = 
			"\r\nOK\r\n\0"
			"UNLINK\r\n\r\nERROR\r\n\0";
		if (_ESP_AnsWaiting(expected, ESP_PORT_SHORT_DELAY, 1) != ESP_Ok) {
			ESP_FAIL("Couldn't set AT+CIPCLOSE=%d", *id);
		}		
	}

	memcpy(pret, id, sizeof(int));
	*ret = pret;
	return ESP_Ok;
}



#ifdef ESP_HARD_CONTROL
/*! ---------------------------------------------------------------------------
 * @fn: _ESP_CmdInit_BlockFlashing
 *
 * @brief: Блокировка прошивки ESP
 *
 * input parameters
 *
 * output parameters
 *
 * return результат выполнения @ref ESP_Result_e
 *
*/
static ESP_Result_e _ESP_CmdInit_BlockFlashing(void)
{
	int fd_io0 = open(ESP_PIN_IO0, O_WRONLY);
	if (fd_io0 > 0) {
		int res = write(fd_io0, "1", 1);
		close(fd_io0);
		if (res != 1) {
			ESP_FAIL("Couldn't ctl power of esp");
		}
	} else {
		ESP_FAIL("Couldn't open en pin");
	}
	usleep(1000);
	return ESP_Ok;
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_ChipReset
 *
 * @brief: Сброс ESP
 *
 * input parameters
 *
 * output parameters
 *
 * return 0 - ok / 1 - error
 *
*/
static ESP_Result_e _ESP_ChipReset(void)
{
	int fd_rst = open(ESP_PIN_RST, O_WRONLY);
	if (fd_rst > 0) {
		if (write(fd_rst, "0", 1) != 1) {
			ESP_LOG(TM_ERROR, "Couldn't reset esp");
			close(fd_rst);
			return 1;
		}
		usleep(1000);
		if (write(fd_rst, "1", 1) != 1) {
			ESP_LOG(TM_ERROR, "Couldn't up esp");
			close(fd_rst);
			return 1;
		}
		close(fd_rst);
		return 0;
	}
	ESP_LOG(TM_ERROR, "Couldn't open reset pin");
	return 1;
}
#endif



#ifdef ESP_HARD_CONTROL
/*! ---------------------------------------------------------------------------
 * @fn: _ESP_CmdInit_Autonegotiation
 *
 * @brief: Автонастройка скорости работы порта с ESP
 *
 * NOTE:
 * 		- Подбор скорости (@ref ESP_SUPPORTED_BAUDS):
 * 			* Сброс ESP (-> ready)
 * 		- Переход на 1М: AT+UART_CUR=921600,8,1,0,0 (-> OK)
 * 		- Перевод порта на 1М
 *
 * input parameters
 *
 * output parameters
 *
 * return результат выполнения @ref ESP_Result_e
 *
*/
static ESP_Result_e _ESP_CmdInit_Autonegotiation(void)
{
	int i;
	static const uint32_t bauds[] = ESP_SUPPORTED_BAUDS;
	static const int bauds_size = sizeof(bauds) / sizeof(uint32_t);
	UART_Params_t uart_params = {0};
	for (i = 0; i < bauds_size; ++i) {
		uart_params.baudRate = bauds[i];
		UART_SetParams(&uart_params);		
		_ESP_ChipReset();
		if (_ESP_AnsWaiting("ready\0", ESP_PORT_LONG_DELAY * 3, 1) == ESP_Ok) {
			ESP_PORT_SEND_CMD("\r\n" "AT+UART_CUR=921600,8,1,0,0");
			if (_ESP_AnsWaiting("\r\nOK\r\n\0", ESP_PORT_SHORT_DELAY, 1) == ESP_Ok) {
				// set port baudrate
				uart_params.baudRate = 921600;
				if (UART_SetParams(&uart_params) == UART_Ok) {
					return ESP_Ok;
				} else {
					ESP_LOG(TM_ERROR, "Couldn't set port baud");
				}
			} else {
				ESP_LOG(TM_ERROR, "Couldn't set esp baud");
			}
			return ESP_Fail;
		}
	}
	ESP_FAIL("Couldn't autonegotiate esp");
}
#else



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_CmdInit_Autonegotiation
 *
 * @brief: Автонастройка скорости работы порта с ESP
 *
 * NOTE:
 * 		- Переход на 1М: AT+UART_CUR=921600,8,1,0,0 (-> OK)
 * 		- Перевод порта на 1М
 *
 * input parameters
 *
 * output parameters
 *
 * return результат выполнения @ref ESP_Result_e
 *
*/
static ESP_Result_e _ESP_CmdInit_Autonegotiation(void)
{
	UART_Params_t uart_params = {0};

	// 1st try
	uart_params.baudRate = 115200;
	if (UART_SetParams(&uart_params) != UART_Ok) {
		goto esp_cmdinitautonegotiation_exit;
	}
	ESP_PORT_SEND_CMD("\r\n" "AT+UART_CUR=921600,8,1,0,0");
	usleep(100000);

	// 2nd try
	uart_params.baudRate = 921600;
	if (UART_SetParams(&uart_params) != UART_Ok) {
		goto esp_cmdinitautonegotiation_exit;
	}
	ESP_PORT_SEND_CMD("\r\n" "AT+UART_CUR=921600,8,1,0,0");
	usleep(100000);

	return ESP_Ok;

esp_cmdinitautonegotiation_exit:
	ESP_FAIL("Couldn't set esp baud");
}
#endif /* ESP_HARD_CONTROL */



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_CmdInit_SetParams
 *
 * @brief: Установка параметров работы ESP
 *
 * NOTE:
 * 		- Установка параметров: ATE0, AT+CWMODE_DEF=1, AT+CIPMUX=1, AT+CIPDINFO=1
 *
 * input parameters
 *
 * output parameters
 *
 * return результат выполнения @ref ESP_Result_e
 *
*/
static ESP_Result_e _ESP_CmdInit_SetParams(void)
{
	ESP_PORT_SEND_CMD("ATE0");
	if (_ESP_AnsWaiting("\r\nOK\r\n\0", ESP_PORT_SHORT_DELAY, 1) != ESP_Ok) {
		ESP_FAIL("Couldn't set ATE0");
	}

	ESP_PORT_SEND_CMD("AT+CWMODE_DEF=1");
	if (_ESP_AnsWaiting("\r\nOK\r\n\0", ESP_PORT_SHORT_DELAY, 1) != ESP_Ok) {
		ESP_FAIL("Couldn't set AT+CWMODE_DEF=1");
	}

	ESP_PORT_SEND_CMD("AT+CIPMUX=1");
	if (_ESP_AnsWaiting("\r\nOK\r\n\0", ESP_PORT_SHORT_DELAY, 1) != ESP_Ok) {
		ESP_FAIL("Couldn't set AT+CIPMUX=1");
	}

	ESP_PORT_SEND_CMD("AT+CIPDINFO=1");
	if (_ESP_AnsWaiting("\r\nOK\r\n\0", ESP_PORT_SHORT_DELAY, 1) != ESP_Ok) {
		ESP_FAIL("Couldn't set AT+CIPDINFO=1");
	}

	ESP_PORT_SEND_CMD("AT+CWLAPOPT=1,2047");
	if (_ESP_AnsWaiting("\r\nOK\r\n\0", ESP_PORT_SHORT_DELAY, 1) != ESP_Ok) {
		ESP_FAIL("Couldn't set AT+CIPDINFO=1");
	}

	ESP_Init_t *init = &_ESP_Driver->curProc.data.init;
	switch (init->nmode) {
		case ESP_NM_DHCP: {
			ESP_PORT_SEND_CMD("AT+CWDHCP_CUR=1,1");
			if (_ESP_AnsWaiting("\r\nOK\r\n\0", ESP_PORT_SHORT_DELAY, 1) != ESP_Ok) {
				ESP_FAIL("Couldn't set AT+CWDHCP_CUR=1,1");
			}
		} break;
		case ESP_NM_Static: {
			char tmp[128];
			char ip[ESP_NET_ADDR_SIZE], gw[ESP_NET_ADDR_SIZE], mask[ESP_NET_ADDR_SIZE];
			strcpy(ip, ip_to_str(init->ip));
			strcpy(gw, ip_to_str(init->gateway));
			strcpy(mask, ip_to_str(init->mask));
			sprintf(tmp, "AT+CIPSTA_CUR=\"%s\",\"%s\",\"%s\"\r\n", ip, gw, mask);
			UART_WriteStr(tmp);
			if (_ESP_AnsWaiting("\r\nOK\r\n\0", ESP_PORT_SHORT_DELAY, 1) != ESP_Ok) {
				ESP_FAIL("Couldn't set AT+CWDHCP_CUR=1,1");
			}
		} break;
	}

	return ESP_Ok;
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_CmdConnect_GetParams
 *
 * @brief: Получение параметров соединения с ТД
 *
 * input parameters
 *
 * output parameters
 * @param ret - параметры соединения
 *
 * return результат выполнения @ref ESP_Result_e
 *
*/
static ESP_Result_e _ESP_CmdConnect_GetParams(ESP_ConnectParams_t **ret)
{
	return _ESP_CheckGetConnectParams(ret);
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_CmdConParams_GetParams
 *
 * @brief: Получение параметров соединения с ТД
 *
 * input parameters
 *
 * output parameters
 * @param ret - параметры соединения
 *
 * return результат выполнения @ref ESP_Result_e
 *
*/
static ESP_Result_e _ESP_CmdConParams_GetParams(ESP_ConnectParams_t **ret)
{
	return _ESP_CheckGetConnectParams(ret);
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_CmdConnect_GetParams
 *
 * @brief: Получение параметров соединения с ТД
 *
 * input parameters
 *
 * output parameters
 * @param ret - параметры соединения
 *
 * return результат выполнения @ref ESP_Result_e
 *
*/
static ESP_Result_e _ESP_CmdConnect_Connect(void)
{
	ESP_AccessPoint_t *apoint = &_ESP_Driver->curProc.data.accessPoint;

	char tmp[128];
	sprintf(tmp, "AT+CWJAP_DEF=\"%s\",\"%s\"\r\n", apoint->name, apoint->password);
	UART_WriteStr(tmp);
	if (_ESP_AnsWaiting("WIFI GOT IP\0", ESP_PORT_LONG_DELAY * 20, 1) == ESP_Ok) {
		if (_ESP_AnsWaiting("\r\nOK\r\n\0", ESP_PORT_LONG_DELAY * 20, 0) == ESP_Ok) {
			return ESP_Ok;
		}
	}
	ESP_FAIL("Couldn't connect to \"%s\"", apoint->name);
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_CmdOpen_Close
 *
 * @brief: Закрыть канал передачи данных
 *
 * input parameters
 *
 * output parameters
 *
 * return результат выполнения @ref ESP_Result_e
 *
*/
static ESP_Result_e _ESP_CmdOpen_Close()
{
	int *id;
	return _ESP_ChannelClose(&id);
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_CmdOpen_Open_UDP
 *
 * @brief: Открыть UDP канал
 *
 * input parameters
 *
 * output parameters
 *
 * return результат выполнения @ref ESP_Result_e
 *
*/
static ESP_Result_e _ESP_CmdOpen_Open_UDP(const ESP_ChannelParams_t *params)
{
	char *type = "UDP";
	char tmp[64];

	sprintf(tmp, "AT+CIPSTART=%d,\"%s\",\"0.0.0.0\",0,%hu,2\r\n",
			params->id, type, params->tprms.udp.ownPort);

	UART_WriteStr(tmp);
	if (_ESP_AnsWaiting("\r\nOK\r\n\0", ESP_PORT_SHORT_DELAY, 1) != ESP_Ok) {
		ESP_FAIL("Couldn't open channel id=%d, type=%s, port=%hu",
				params->id, type, params->tprms.udp.ownPort);
	}
	return ESP_Ok;
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_CmdOpen_Open
 *
 * @brief: Открыть канал передачи данных
 *
 * input parameters
 *
 * output parameters
 *
 * return результат выполнения @ref ESP_Result_e
 *
*/
static ESP_Result_e _ESP_CmdOpen_Open(ESP_ChannelParams_t **ret)
{
	ESP_ChannelParams_t *pret = &_ESP_Driver->staticData.ch_params;
	const ESP_ChannelParams_t *params = &_ESP_Driver->curProc.data.ch_params;

	memset(pret, 0, sizeof(ESP_ChannelParams_t));

	*ret = 0;

	ESP_Result_e res;
	switch (params->type) {
		case ESP_ChT_UDP: {
			res = _ESP_CmdOpen_Open_UDP(params);
		} break;
		default: {
			ESP_FAIL("Unsupported channel type");
		} break;
	}

	if (res == ESP_Ok) {
		_ESP_Driver->channels[params->id].isOpened = 1;
		memcpy(pret, params, sizeof(ESP_ChannelParams_t));
		*ret = pret;
		return ESP_Ok;
	} else {
		return ESP_Fail;
	}
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_CmdClose_Close
 *
 * @brief: Закрыть канал передачи данных
 *
 * input parameters
 *
 * output parameters
 * @param ret - id канала
 *
 * return результат выполнения @ref ESP_Result_e
 *
*/
static ESP_Result_e _ESP_CmdClose_Close(int **id)
{
	return _ESP_ChannelClose(id);
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_CmdSend_Send
 *
 * @brief: Передача данных
 *
 * input parameters
 *
 * output parameters
 * @param ret - id канала
 *
 * return результат выполнения @ref ESP_Result_e
 *
*/
static ESP_Result_e _ESP_CmdSend_Send(int **ret)
{
	int *pret = &_ESP_Driver->staticData.id;
	const ESP_Channel_t *channel = &_ESP_Driver->curProc.data.channel;

	memset(pret, 0, sizeof(int));

	*ret = 0;

	char tmp[64];
	ui32_u uip = {.d = channel->data.remoteIP};
	sprintf(tmp, "AT+CIPSEND=%d,%hu,\"%hhu.%hhu.%hhu.%hhu\",%hu\r\n",
			channel->id, _ESP_Driver->sendBuffer.curPos,
			uip.b[0], uip.b[1], uip.b[2], uip.b[3], channel->data.remotePort);
	UART_WriteStr(tmp);
	if (_ESP_AnsWaiting("\r\nOK\r\n>\0", (ESP_PORT_SHORT_DELAY/5), 1) != ESP_Ok) {
		ESP_FAIL("Couldn't set AT+CIPSEND");
	}

	int res = UART_Write(_ESP_Driver->sendBuffer.data, _ESP_Driver->sendBuffer.curPos);
	if (
			res != (int)_ESP_Driver->sendBuffer.curPos
			||
			_ESP_AnsWaiting("SEND OK\r\n\0", ESP_PORT_LONG_DELAY, 1) != ESP_Ok
	) {
		ESP_FAIL("Failed to send data: id=%d, ip=%hhu.%hhu.%hhu.%hhu, port=%hu, size=%hu",
				channel->id, uip.b[0], uip.b[1], uip.b[2], uip.b[3],
				channel->data.remotePort,
				_ESP_Driver->sendBuffer.curPos);
	}

	memcpy(pret, &channel->id, sizeof(int));
	*ret = pret;
	return ESP_Ok;
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_CmdIGMPJoin_Join
 *
 * @brief: Присоединиться к мультикаст группе
 *
 * input parameters
 *
 * output parameters
 *
 * return результат выполнения @ref ESP_Result_e
 *
*/
static ESP_Result_e _ESP_CmdIGMPJoin_Join(void)
{
	const char *ip = ip_to_str(_ESP_Driver->curProc.data.ip);

	char tmp[128];
	sprintf(tmp, "AT+CIPIGMPJ=\"%s\"\r\n", ip);
	UART_WriteStr(tmp);
	if (_ESP_AnsWaiting("\r\nOK\r\n\0", ESP_PORT_SHORT_DELAY, 1) != ESP_Ok) {
		ESP_FAIL("Couldn't set AT+CIPIGMPJ");
	}
	return ESP_Ok;
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_CmdIGMPLeave_Leave
 *
 * @brief: Выйти из мультикаст группы
 *
 * input parameters
 *
 * output parameters
 *
 * return результат выполнения @ref ESP_Result_e
 *
*/
static ESP_Result_e _ESP_CmdIGMPLeave_Leave(void)
{
	const char *ip = ip_to_str(_ESP_Driver->curProc.data.ip);

	char tmp[128];
	sprintf(tmp, "AT+CIPIGMPL=\"%s\"\r\n", ip);
	UART_WriteStr(tmp);
	if (_ESP_AnsWaiting("\r\nOK\r\n", ESP_PORT_SHORT_DELAY, 1) != ESP_Ok) {
		ESP_FAIL("Couldn't set AT+CIPIGMPL");
	}
	return ESP_Ok;
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_CmdInit
 *
 * @brief: Инициализация ESP
 *
 * NOTE:
 * 		- Включение
 * 		- Подбор скорости
 * 		- Установка параметров
 * 		- Проверка статуса соединения
 * 		- Вызов callback в зависимости от статуса соединения
 *
 * input parameters
 *
 * output parameters
 *
 * no return
 *
*/
static void _ESP_CmdInit(void)
{
	ESP_CallBack_Data_t cb_data = {
			.type = _ESP_Driver->curProc.type,
			.res = ESP_Fail,
			.data = 0
	};	

#ifdef ESP_HARD_CONTROL
	if (_ESP_CmdInit_BlockFlashing() != ESP_Ok)
		ESP_CMD_DONE(cb_data);
#endif

	if (_ESP_CmdInit_Autonegotiation() != ESP_Ok)
		ESP_CMD_DONE(cb_data);

	if (_ESP_CmdInit_SetParams() != ESP_Ok)
		ESP_CMD_DONE(cb_data);

	int status = 0;
	if (_ESP_CheckConnectStatus(&status) != ESP_Ok)
		ESP_CMD_DONE(cb_data);

	_ESP_Driver->state = ESP_State_Init;

	if (status) {
		if (_ESP_GetConnectParams((ESP_ConnectParams_t **)(&cb_data.data)) != ESP_Ok)
			ESP_CMD_DONE(cb_data);
		cb_data.type = ESP_Cmd_Connect;
		_ESP_Driver->state = ESP_State_Connected;
	}

	cb_data.res = ESP_Ok;
	ESP_CMD_DONE(cb_data);
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_CmdFind
 *
 * @brief: Поиск ТД
 *
 * input parameters
 *
 * output parameters
 *
 * no return
 *
*/
static void _ESP_CmdFind(void)
{
	ESP_CallBack_Data_t cb_data = {
			.type = _ESP_Driver->curProc.type,
			.res = ESP_Fail,
			.data = 0
	};

	if (_ESP_GetAccessPointList((ESP_AccessPoint_List_t **)(&cb_data.data)) != ESP_Ok) {
		ESP_CMD_DONE(cb_data);
	}

	ESP_AccessPoint_List_t *list = (ESP_AccessPoint_List_t *)cb_data.data;
	list->sortType = _ESP_Driver->curProc.data.sortType;

	ESP_SortAccessPointList(list);

	cb_data.res = ESP_Ok;
	ESP_CMD_DONE(cb_data);
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_CmdConnect
 *
 * @brief: Подключение к ТД
 *
 * input parameters
 *
 * output parameters
 *
 * no return
 *
*/
static void _ESP_CmdConnect(void)
{
	ESP_CallBack_Data_t cb_data = {
			.type = _ESP_Driver->curProc.type,
			.res = ESP_Fail,
			.data = 0
	};

	_ESP_Driver->state = ESP_State_Init;

	if (_ESP_CmdConnect_Connect() != ESP_Ok)
		ESP_CMD_DONE(cb_data);

	if (_ESP_CmdConnect_GetParams((ESP_ConnectParams_t **)(&cb_data.data)) != ESP_Ok)
		ESP_CMD_DONE(cb_data);

	_ESP_Driver->state = ESP_State_Connected;

	cb_data.res = ESP_Ok;
	ESP_CMD_DONE(cb_data);
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_CmdConParams
 *
 * @brief: Получение параметров подключения к ТД
 *
 * input parameters
 *
 * output parameters
 *
 * no return
 *
*/
static void _ESP_CmdConParams(void)
{
	ESP_CallBack_Data_t cb_data = {
			.type = _ESP_Driver->curProc.type,
			.res = ESP_Fail,
			.data = 0
	};

	if (_ESP_CmdConParams_GetParams((ESP_ConnectParams_t **)(&cb_data.data)) != ESP_Ok)
		ESP_CMD_DONE(cb_data);

	cb_data.res = ESP_Ok;
	ESP_CMD_DONE(cb_data);
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_CmdOpen
 *
 * @brief: Открытие канала передачи данных
 *
 * input parameters
 *
 * output parameters
 *
 * no return
 *
*/
static void _ESP_CmdOpen(void)
{
	ESP_CallBack_Data_t cb_data = {
			.type = _ESP_Driver->curProc.type,
			.res = ESP_Fail,
			.data = 0
	};

	if (_ESP_CmdOpen_Close() != ESP_Ok)
		ESP_CMD_DONE(cb_data);

	if (_ESP_CmdOpen_Open((ESP_ChannelParams_t **)(&cb_data.data)) != ESP_Ok)
		ESP_CMD_DONE(cb_data);

	cb_data.res = ESP_Ok;
	ESP_CMD_DONE(cb_data);
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_CmdClose
 *
 * @brief: Открытие канала передачи данных
 *
 * input parameters
 *
 * output parameters
 *
 * no return
 *
*/
static void _ESP_CmdClose(void)
{
	ESP_CallBack_Data_t cb_data = {
			.type = _ESP_Driver->curProc.type,
			.res = ESP_Fail,
			.data = 0
	};

	if (_ESP_CmdClose_Close((int **)(&cb_data.data)) != ESP_Ok)
		ESP_CMD_DONE(cb_data);

	{ // все каналы закрыты
		int i;
		for (i = 0; i < ESP_CHANNELS; ++i)
			if (_ESP_Driver->channels[i].isOpened)
				break;
		if (i == ESP_CHANNELS) {
			_ESP_Driver->state = (_ESP_Driver->state >= ESP_State_Connected)? 
				ESP_State_Connected : _ESP_Driver->state;
		}
	}

	cb_data.res = ESP_Ok;
	ESP_CMD_DONE(cb_data);
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_CmdSend
 *
 * @brief: Передача данных
 *
 * input parameters
 *
 * output parameters
 *
 * no return
 *
*/
static void _ESP_CmdSend(void)
{
	ESP_CallBack_Data_t cb_data = {
			.type = _ESP_Driver->curProc.type,
			.res = ESP_Fail,
			.data = 0
	};

	if (_ESP_CmdSend_Send((int **)(&cb_data.data)) != ESP_Ok)
		ESP_CMD_DONE(cb_data);

	_ESP_Driver->state = ESP_State_Work;

	cb_data.res = ESP_Ok;
	ESP_CMD_DONE(cb_data);
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_CmdIGMPJoin
 *
 * @brief: Присоединение к мультикаст группе
 *
 * input parameters
 *
 * output parameters
 *
 * no return
 *
*/
static void _ESP_CmdIGMPJoin(void)
{
	ESP_CallBack_Data_t cb_data = {
			.type = _ESP_Driver->curProc.type,
			.res = ESP_Fail,
			.data = 0
	};

	if (_ESP_CmdIGMPJoin_Join() != ESP_Ok)
		ESP_CMD_DONE(cb_data);

	cb_data.res = ESP_Ok;
	ESP_CMD_DONE(cb_data);
}



/*! ---------------------------------------------------------------------------
 * @fn: _ESP_CmdIGMPLeave
 *
 * @brief: Выход из мультикаст группы
 *
 * input parameters
 *
 * output parameters
 *
 * no return
 *
*/
static void _ESP_CmdIGMPLeave(void)
{
	ESP_CallBack_Data_t cb_data = {
			.type = _ESP_Driver->curProc.type,
			.res = ESP_Fail,
			.data = 0
	};

	if (_ESP_CmdIGMPLeave_Leave() != ESP_Ok)
		ESP_CMD_DONE(cb_data);

	cb_data.res = ESP_Ok;
	ESP_CMD_DONE(cb_data);
}



static inline void _ESP_UserCmdWork(void)
{
	switch (_ESP_Driver->curProc.type)
	{
		case ESP_Cmd_Init: {
			_ESP_CmdInit();
		} break;
		case ESP_Cmd_Find: {
			_ESP_CmdFind();
		} break;
		case ESP_Cmd_Connect: {
			_ESP_CmdConnect();
		} break;
		case ESP_Cmd_ConParams: {
			_ESP_CmdConParams();
		} break;
		case ESP_Cmd_Open: {
			_ESP_CmdOpen();
		} break;
		case ESP_Cmd_Close: {
			_ESP_CmdClose();
		} break;
		case ESP_Cmd_Send: {
			_ESP_CmdSend();
		} break;
		case ESP_Cmd_IGMPJoin: {
			_ESP_CmdIGMPJoin();
		} break;
		case ESP_Cmd_IGMPLeave: {
			_ESP_CmdIGMPLeave();
		} break;
		default: break;
	}
}



/*! ---------------------------------------------------------------------------
 * @fn: ESP_Main
 *
 * @brief: Основной поток драйвера ESP
 *
 * input parameters
 *
 * output parameters
 *
 * no return
*/
void *ESP_Main(void *data)
{
	int pollres, i;
	while (1)
	{
		/* ожидание команды или данных с порта */
		pollres = poll(_ESP_Driver->pfds, ESP_EVENTS_NUM, -1);
		if (pollres > 0) {
			for (i = 0; i < ESP_EVENTS_NUM; ++i) {
				if (_ESP_Driver->pfds[i].revents == POLLIN) {
					switch (i) {
						// main
						case ESP_EVENT_USER: {
							if (_ESP_EndUserEvent() != ESP_EVENT_DSIZE) {
								goto esp_main_error;
							}
							_ESP_UserCmdWork();
						} // no break
						case ESP_EVENT_UART: {
							_ESP_PortRecvWork();
						} break;
						// other
						case ESP_EVENT_KILL: {
							_ESP_EndKillEvent();
							pthread_mutex_unlock(&_ESP_Driver->killmu);
							goto esp_main_end;
						} break;
						case ESP_EVENT_RESET: {
							_ESP_EndResetEvent();
							_ESP_Driver->state = ESP_State_Created;
							_ESP_SetProcess(ESP_Cmd_Init, &_ESP_Driver->init, sizeof(ESP_Init_t));
						} break;
					}
				}
			}
		} else {
			goto esp_main_error;
		}
	}
esp_main_error:
	ESP_LOG(TM_ERROR, "error in main poll... stop drv");
esp_main_end:
	pthread_exit(0);
}



/*
 * ****************************************************************************************************************************************************************
 * ****************************************************************************************************************************************************************
 * ****************************************************************************************************************************************************************
 * ****************************************************************************************************************************************************************
 * */



/*! ---------------------------------------------------------------------------
 * @fn: ESP_IGMPJoin
 *
 * @brief: Присоединиться к мультикаст группе
 *
 * input parameters
 * @param ip - IP мультикаст группы
 *
 * output parameters
 *
 * return результат выполнения @ref ESP_Result_e
*/
ESP_Result_e ESP_IGMPJoin(uint32_t ip)
{
	ESP_COMMON_CHECK();
	if (
			_ESP_Driver->state < ESP_State_Connected
	) {
		_ESP_PrintProcessWarn("%s %d", "ESP_IGMPJoin prereq fail", 
			_ESP_Driver->state);
		return ESP_Fail;
	}
	return _ESP_SetProcess(ESP_Cmd_IGMPJoin, &ip, sizeof(uint32_t));

}



/*! ---------------------------------------------------------------------------
 * @fn: ESP_IGMPLeave
 *
 * @brief: Выйти из мультикаст группы
 *
 * input parameters
 * @param ip - IP мультикаст группы
 *
 * output parameters
 *
 * return результат выполнения @ref ESP_Result_e
*/
ESP_Result_e ESP_IGMPLeave(uint32_t ip)
{
	ESP_COMMON_CHECK();
	return _ESP_SetProcess(ESP_Cmd_IGMPLeave, &ip, sizeof(uint32_t));
}



/*! ---------------------------------------------------------------------------
 * @fn: ESP_Send
 *
 * @brief: Отправка данных
 *
 * input parameters
 * @param channel - канал
 *
 * output parameters
 *
 * return результат выполнения @ref ESP_Result_e
*/
ESP_Result_e ESP_Send(const ESP_Channel_t *channel)
{
	ESP_COMMON_CHECK();
	if (
			!channel ||
			!channel->data.buf ||
			_ESP_Driver->state < ESP_State_Connected ||
			!_ESP_Driver->channels[channel->id].isOpened ||
			channel->data.remoteIP == 0 ||
			channel->data.remotePort == 0 ||
			channel->data.size == 0 ||
			channel->data.size >= ESP_CHANNEL_DATA_MAX_SIZE

	) {
		if (!channel) {
			_ESP_PrintProcessWarn("%s", "ESP_Send prereq fail: channel");
			return ESP_Fail;
		}
		_ESP_PrintProcessWarn("%s %u %d %d %u %u %d", "ESP_Send prereq",
			(uint32_t)channel->data.buf, _ESP_Driver->state, (int)_ESP_Driver->channels[channel->id].isOpened,
			channel->data.remoteIP, channel->data.remotePort, channel->data.size);
		return ESP_Fail;
	}
	memcpy(_ESP_Driver->sendBuffer.data, channel->data.buf, channel->data.size);
	_ESP_Driver->sendBuffer.curPos = channel->data.size;
	return _ESP_SetProcess(ESP_Cmd_Send, channel, sizeof(ESP_Channel_t));
}



/*! ---------------------------------------------------------------------------
 * @fn: ESP_Close
 *
 * @brief: Закрытие сетевого канала
 *
 * input parameters
 * @param id - id открываемого канала
 *
 * output parameters
 *
 * return результат выполнения @ref ESP_Result_e
*/
ESP_Result_e ESP_Close(int id)
{
	ESP_COMMON_CHECK();
	if (
			_ESP_Driver->state < ESP_State_Connected ||
			!_ESP_Driver->channels[id].isOpened
	) {
		_ESP_PrintProcessWarn("%s %d %d", "ESP_Close prereq",
			_ESP_Driver->state, (int)_ESP_Driver->channels[id].isOpened);
		return ESP_Fail;
	}
	return _ESP_SetProcess(ESP_Cmd_Close, &id, sizeof(int));
}



/*! ---------------------------------------------------------------------------
 * @fn: ESP_Open
 *
 * @brief: Открытие сетевого канала
 *
 * input parameters
 * @param params - параметры открываемого канала
 *
 * output parameters
 *
 * return результат выполнения @ref ESP_Result_e
*/
ESP_Result_e ESP_Open(const ESP_ChannelParams_t *params)
{
	ESP_COMMON_CHECK();
	if (
			!params ||
			_ESP_Driver->state < ESP_State_Connected ||
			params->type != ESP_ChT_UDP
	) {
		_ESP_PrintProcessWarn("%s %u %d", "ESP_Open prereq fail",
			(uint32_t)params, _ESP_Driver->state);
		return ESP_Fail;
	}
	return _ESP_SetProcess(ESP_Cmd_Open, params, sizeof(ESP_ChannelParams_t));
}



/*! ---------------------------------------------------------------------------
 * @fn: ESP_GetConnectParams
 *
 * @brief: Запрос параметров соединения с AP
 *
 * input parameters
 *
 * output parameters
 *
 * return результат выполнения @ref ESP_Result_e
*/
ESP_Result_e ESP_GetConnectParams(void)
{
	ESP_COMMON_CHECK();
	if (
			_ESP_Driver->state < ESP_State_Connected
	) {
		_ESP_PrintProcessWarn("%s %d", "ESP_GetConnectParams prereq fail",
			_ESP_Driver->state);
		return ESP_Fail;
	}
	return _ESP_SetProcess(ESP_Cmd_ConParams, 0, 0);
}



/*! ---------------------------------------------------------------------------
 * @fn: ESP_Connect
 *
 * @brief: Подключение к точке доступа
 *
 * NOTE: при повторном вызове старое подключение или процесс подключения
 * 		прекращаются
 *
 * input parameters
 * @param name - имя точки доступа
 * @param passwd - пароль
 *
 * output parameters
 *
 * return результат выполнения @ref ESP_Result_e
*/
ESP_Result_e ESP_Connect(const char *name, const char *passwd)
{
	ESP_COMMON_CHECK();
	if (
			!name || !passwd ||
			_ESP_Driver->state < ESP_State_Init
	) {
		_ESP_PrintProcessWarn("%s %u %u %d", "ESP_Connect prereq fail",
			(uint32_t)name, (uint32_t)passwd, _ESP_Driver->state);
		return ESP_Fail;
	}
	ESP_AccessPoint_t acessPoint = {0};
	strcpy(acessPoint.name, name);
	strcpy(acessPoint.password, passwd);
	return _ESP_SetProcess(ESP_Cmd_Connect, &acessPoint, sizeof(ESP_AccessPoint_t));
}



/*! ---------------------------------------------------------------------------
 * @fn: ESP_Find
 *
 * @brief: Получение списка точек доступа
 *
 * input parameters
 * sortType - тип сортировки списка
 *
 * output parameters
 *
 * return результат выполнения @ref ESP_Result_e
*/
ESP_Result_e ESP_Find(ESP_APList_Sort_e sortType)
{
	ESP_COMMON_CHECK();
	if (
			_ESP_Driver->state < ESP_State_Init
	) {
		_ESP_PrintProcessWarn("%s %d", "ESP_Find prereq fail",
			_ESP_Driver->state);
		return ESP_Fail;
	}
	return _ESP_SetProcess(ESP_Cmd_Find, &sortType, sizeof(ESP_APList_Sort_e));
}



/*! ---------------------------------------------------------------------------
 * @fn: ESP_DeInit
 *
 * @brief: Завершение работы с драйвером
 *
 * input parameters
 *
 * output parameters
 *
 * no return
*/
void ESP_DeInit(void)
{
	if (_ESP_Driver && _ESP_Driver->state >= ESP_State_Init) {
		pthread_mutex_lock(&_ESP_Driver->killmu);
		_ESP_RaiseKillEvent();
		pthread_mutex_lock(&_ESP_Driver->killmu);
		close(_ESP_Driver->pfds[ESP_EVENT_USER].fd);
		close(_ESP_Driver->pfds[ESP_EVENT_KILL].fd);
		close(_ESP_Driver->pfds[ESP_EVENT_RESET].fd);
		pthread_mutex_destroy(&_ESP_Driver->procMutex);
		pthread_mutex_destroy(&_ESP_Driver->killmu);
		UART_Close();
		_ESP_Driver->state = ESP_State_Created;
		_ESP_ChipReset();
	}
	return;
}



/*! ---------------------------------------------------------------------------
 * @fn: ESP_Init
 *
 * @brief: Инициализация драйвера
 *
 * input parameters
 * @param initStruct - указатель на структуру инициализации
 *
 * output parameters
 *
 * return результат выполнения @ref ESP_Result_e
*/
ESP_Result_e ESP_Init(const ESP_Init_t *initStruct)
{
	/* проверка параметров */
	if (_ESP_Driver == 0)
		ESP_PARAM_CHECK_FAIL("Driver doesn't created");
	if (initStruct == 0)
		ESP_PARAM_CHECK_FAIL("Init struct is null ptr");
	if (initStruct->callback == 0)
		ESP_PARAM_CHECK_FAIL("Init struct has null ptr to cb func");
	if (
			initStruct->nmode == ESP_NM_Static &&
			(initStruct->ip == 0 || initStruct->mask == 0)
	) {
		char ip[ESP_NET_ADDR_SIZE], gw[ESP_NET_ADDR_SIZE], mask[ESP_NET_ADDR_SIZE];
		strcpy(ip, ip_to_str(initStruct->ip));
		strcpy(gw, ip_to_str(initStruct->gateway));
		strcpy(mask, ip_to_str(initStruct->mask));
		ESP_PARAM_CHECK_FAIL("mode = static, ip = %s, mask = %s, gw = %s",
				ip, mask, gw
		);
	}

	// сохранить init структуру для возможности само-переинициализации
	memcpy(&_ESP_Driver->init, initStruct, sizeof(ESP_Init_t));

	_ESP_Driver->callback = initStruct->callback;

	/* создание драйвера порта */
	if (UART_Create() != UART_Ok)
		ESP_FAIL("Couldn't create UART driver");

	/* параметры порта */
	UART_Params_t uart_params = {
			.baudRate = 115200,
			.timeOut = {
					.Ms = 0,
					.nChars = 1
			}
	};

	/* открытие порта */
	if (UART_Open(ESP_PORT_NAME, &uart_params) != UART_Ok)
		ESP_FAIL("Couldn't open port");

	/* создание дескриптора событий на команды пользователя */
	_ESP_Driver->pfds[ESP_EVENT_USER].events = POLLIN;
	_ESP_Driver->pfds[ESP_EVENT_USER].fd = eventfd(0, 0);
	/* создание дескриптора на завершение работы */
	_ESP_Driver->pfds[ESP_EVENT_KILL].events = POLLIN;
	_ESP_Driver->pfds[ESP_EVENT_KILL].fd = eventfd(0, 0);
	/* создание дескриптора на сброс модуля по ошибке обращения */
	_ESP_Driver->pfds[ESP_EVENT_RESET].events = POLLIN;
	_ESP_Driver->pfds[ESP_EVENT_RESET].fd = eventfd(0, 0);
	/* установка дескриптора UART */
	_ESP_Driver->pfds[ESP_EVENT_UART].events = POLLIN;
	_ESP_Driver->pfds[ESP_EVENT_UART].fd = UART_GetFD();

	/* запуск основного потока */
	if (pthread_mutex_init(&_ESP_Driver->procMutex, 0) < 0)
		ESP_FAIL("Couldn't init process mutex");
	if (pthread_mutex_init(&_ESP_Driver->killmu, 0) < 0)
		ESP_FAIL("Couldn't init killmu");
	if (pthread_create(&_ESP_Driver->mthread, 0, ESP_Main, 0) < 0)
		ESP_FAIL("Couldn't create main thread");
	if (pthread_detach(_ESP_Driver->mthread) < 0)
		ESP_FAIL("Couldn't detach main thread");

	/* передача потоку команды на инициализацию esp */
	return _ESP_SetProcess(ESP_Cmd_Init, initStruct, sizeof(ESP_Init_t));
}



/*! ---------------------------------------------------------------------------
 * @fn: ESP_Create
 *
 * @brief: Создание экземпляра драйвера
 *
 * input parameters
 *
 * output parameters
 *
 * return результат выполнения @ref ESP_Result_e
*/
ESP_Result_e ESP_Create(void)
{
	_ESP_Driver = (ESP_Driver_t *)calloc(1, sizeof(ESP_Driver_t));
	return (_ESP_Driver)? ESP_Ok : ESP_Fail;
}



/*! ---------------------------------------------------------------------------
 * @fn: ESP_GetFreeChannelID
 *
 * @brief: Поиск свободного канала
 *
 * input parameters
 *
 * output parameters
 *
 * return id свободного канала или -1
*/
int ESP_GetFreeChannelID(void)
{
	int i;
	for (i = 0; i < ESP_CHANNELS; ++i) {
		if ( !_ESP_Driver->channels[i].isOpened ) {
			return _ESP_Driver->channels[i].prm.id;
		}
	}
	return -1;
}



/*! ---------------------------------------------------------------------------
 * @fn: ip_to_str
 *
 * @brief: Конвертация IP в hex в строку
 *
 * input parameters
 *
 * output parameters
 *
 * return указатель на строку с IP-адресом
*/
char *ip_to_str(uint32_t ip)
{
	static char _esp_ip_[ESP_NET_ADDR_SIZE];
	ui32_u uip = { .d = ip };
	if (
			sprintf(
					_esp_ip_, "%hhu.%hhu.%hhu.%hhu",
					uip.b[0], uip.b[1], uip.b[2], uip.b[3]
			) > 0
	) {
		return _esp_ip_;
	} else {
		return 0;
	}
}



/*! ---------------------------------------------------------------------------
 * @fn: str_to_ip
 *
 * @brief: Конвертация строки с IP в hex
 *
 * input parameters
 *
 * output parameters
 *
 * return hex значение IP
*/
uint32_t str_to_ip(char *ip)
{
	ui32_u uip;
	if (
			sscanf(
					ip, "%hhu.%hhu.%hhu.%hhu", &uip.b[0], &uip.b[1], &uip.b[2], &uip.b[3]
			) != 4
	) {
		uip.d = 0;
	}
	return uip.d;
}



/*! ---------------------------------------------------------------------------
 * @fn: DumpHex
 *
 * @brief: 
 *
 * input parameters
 * @param data
 * @param size
 *
 * output parameters
 *
 * no return
*/
static void DumpHex(const void* data, size_t size)
{
    #define DUMPHEX_WIDTH 16
	char dump[DUMPHEX_WIDTH*16];
	size_t it = 0;

	char ascii[DUMPHEX_WIDTH+1];
	size_t i, j;
	ascii[DUMPHEX_WIDTH] = 0;

	for (i = 0; i < size; ++i) {
		sprintf(dump + it, "%02X ", ((uint8_t*)data)[i]);
		it += strlen(dump + it);
		if (((uint8_t*)data)[i] >= ' ' && ((uint8_t*)data)[i] <= '~') {
			ascii[i % DUMPHEX_WIDTH] = ((uint8_t*)data)[i];
		} else {
			ascii[i % DUMPHEX_WIDTH] = '.';
		}
		if ((i+1) % 8 == 0 || i+1 == size) {
			sprintf(dump + it, " ");
			it += strlen(dump + it);
			if ((i+1) % DUMPHEX_WIDTH == 0) {
				sprintf(dump + it, "|  %s", ascii);
				it += strlen(dump + it);
				dump[it] = 0;
				ESP_LOG(TM_ERROR, "%s", dump);
				it = 0;
			} else if (i+1 == size) {
				ascii[(i+1) % DUMPHEX_WIDTH] = '\0';
				if ((i+1) % DUMPHEX_WIDTH <= 8) {
					sprintf(dump + it, " ");
					it += strlen(dump + it);
				}
				for (j = (i+1) % DUMPHEX_WIDTH; j < DUMPHEX_WIDTH; ++j) {
					sprintf(dump + it, "   ");
					it += strlen(dump + it);
				}
				sprintf(dump + it, "|  %s", ascii);
				it += strlen(dump + it);
				dump[it] = 0;
				ESP_LOG(TM_ERROR, "%s", dump);
				it = 0;
			}
		}
	}
}
