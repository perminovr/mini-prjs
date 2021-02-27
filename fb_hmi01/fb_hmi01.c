/*******************************************************************************
  * @file	fb_hmi01.c
  * @author  Перминов Р.И.
  * @version v 3.5.11.1
  * @date	20.11.2018
  * @description - методы для работы с сетью для библиотеки ElsyHMILib
  *
  *****************************************************************************/

#include "prjconf.h"

#include "utils.h"
#include "common.h"
#include "logserv.h"
#include "servto.h"
#include "ssdp.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdint.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <time.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/ip.h>
#include <linux/if_packet.h>
#include <ifaddrs.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>



/* Причины передач */
typedef enum {
	rCycle,				/* 0 - Циклические данные */
	rData,				/* 1 - Данные с гарантией доставки */
	rAck,				/* 2 - Подтверждение данных с гарантией */
	rEnd				/* Нет причины */
} reason_e;

/* Список кадров */
typedef enum {
	fbcNone,			/* 0 - Не использовать */
	fbcBase,			/* 1 - Основной кадр обмена */
	fbcEcho,			/* 2 - Кадр эхо */
	fbcPlcParam = 0x80, /* 3 - Кадр параметров ПЛК */
} frame_e;

/* Коды подтверждения */
typedef enum {
	aOk,				/* 0 - Кадр успешно принят */
	aDuble,				/* 1 - Принят кадр-дубль */
	aBusy,				/* 2 - Процесс приема занят */
	aErr,				/* 3 - Ошибка при обработке данных */
	aEnd				/* Нет причины */
} ack_e;

/* Тип переаваемых данных  */
typedef enum {
	tdNone,				/* 0 - Не определен */
	tdOwner,			/* 1 - Свои данные */
} type_data_e;



#pragma pack(push,1)

/* Заголовок кадра */
/* Не переставлять, порядок важен */
typedef struct {
	uint8_t number;		/* Нумерация кадра */
	uint8_t reason;		/* Причина передачи @ref reason_e */
	uint8_t frame;		/* Тип кадра @ref frame_e */
	uint8_t ack;		/* Код подтверждения @ref ack_e */
} workframeheader_t;

/* Кадр данных */
typedef struct {
	workframeheader_t header;	/* Заголовок кадра */
	char data[1];				/* Данные */
} workframe_t;

typedef struct {
	workframe_t *frame;
	int size;
} frame_t;

#pragma pack(pop)


#define UDP_MAX		 1
#define EVENTS_MAX	  1
#define SSDP_REQFD_MAX  10
// максимальный размер массива управляющих дескрипторов
#define PFDS_SIZE_MAX	(UDP_MAX+EVENTS_MAX+SSDP_REQFD_MAX)

#define WORK_SOCKET_IDX		0	// индекс основного рабочего дескриптора 
#define EVENT_KILL_IDX		1 	// индекс декскриптора остановки потока



#define EVENT_DSIZE			8



#define PORT_WORK			10010	// рабочий порт
#define PORT_FIND			1901	// порт ssdp
#define IP_FIND				"239.255.255.251"	// мультикаст группа sdp


#define TIMEOUTS_MULT		1000	// множитель мкс -> мс
#define ECHO_REPEAT_TO	  500		// таймаут на эхо запрос
#define SSDP_REPEAT_TO		500		// таймаут на ssdp запрос
#define MINIMAL_TO_DEF		500		// минмальный таймаут по умолчанию (выбирается из установленных)
#define DATAREPEAT_TO_DEF	500		// таймаут переповтора по умолчанию (задается пользователем)



// максимальный размер буфера для работы по eth
const int RECVBUF_SIZE_MAX = (HMICLIENTCONST_XRECVBUF_SIZE+sizeof(workframeheader_t));
const int SENDBUF_SIZE_MAX = (HMICLIENTCONST_XSENDBUF_SIZE+sizeof(workframeheader_t));



/*
Таймауты на обработку
*/
typedef struct {
	int dataRepeatTO;
	int connectionTO;   // считается как 3 * dataRepeatTO
	int ssdpRepeatTO;
	int echoRepeatTO;
	int minimalTO;		// минимальный таймаут, в случае отсутствия других	
} timeouts_t;



/*
Идентификаторы таймаутов
*/
enum {
	toDataRepeatID,
	toConnectionID,
	toSSDPRepeatID,
	toEchoRepeatID,
};



/*
Элемент списка ответивших на ssdp запрос
*/
typedef struct {
	char ip[16];
	char ver[32];
	char type[32];
	char name[32];
} findnode_t;



enum WorkErrors {
	errNo,		  /* Нет ошибок */
	errInit,		/* Инициализация не выполнена */
	errFind,		/* Найдено более одного сервера */
	errBusy,		/* Занят обработкой гарантии доставки */
	errOverload,	/* Размер передаваемых данных превышает размер буфера */
	errServerIP,	/* попытка отправки данных: IP сервера не задан */
	errEnd,		 /*  */
};



/*
Рабочая структура драйвера
*/
typedef struct {
	// on init ------------------------------------------------------

	pthread_t mthread;		// основной поток
	pthread_mutex_t mu;		// мьютекс основного потока 
	pthread_mutex_t killmu;	// мьютекс завершения потока

	/* управляющие дескрипторы */
	struct pollfd pfds[PFDS_SIZE_MAX];
	int pfds_size;			// размер pfds
	int ssdp_start_idx;		// индекс начала ssdp дескрипторов в pfds
	int ssdp_size;			// число ssdp дескрипторов в pfds
	int ssdp_cur_idx;		// текущий индекс отправки ssdp запроса

	uint8_t initDone;		// флаг окончания инициализации
	
	// while working ------------------------------------------------

	to_que_t to_que;		// очередь таймаутов
	to_item_t dataRepeatTO;	// таймаут на переповтор отправки кадра data
	to_item_t connectionTO;	// таймаут на соединение с сервером
	to_item_t ssdpRepeatTO;	// таймаут на переповтор отправки кадра SSDP
	to_item_t echoRepeatTO;	// таймаут на переповтор отправки кадра echo
	timeouts_t timeouts;	// таймауты

	findnode_t curFindPrm;	// заданные параметры поиска сервера
	uint8_t aLotOfServers;  // флаг обнаружения нескольких серверов

	frame_t rxframe;		// буфер приема
	frame_t txframe;		// буфер отправки

	char server_ip[16];		// ip адрес сервера (hmi01)

	/* переменные нумерации кадров */
	uint8_t rdnum;
	uint8_t wrnum;
	uint8_t rdone;

	uint32_t cntRepeat;		// счетчик повторов отправки
	int isBusy;				// флаг работы с кдрами, требующими ответа
	int link;				// наличие связи с сервером
} client_t;



/*
Сигнал события
*/
static inline int RaiseEvent(int fd)
{
	uint8_t buf = 1;
	return write(fd, &buf, EVENT_DSIZE);
}
#define RaiseKillEvent() RaiseEvent(priv->pfds[EVENT_KILL_IDX].fd)



/*
Подтверждение события
*/
static inline int EndEvent(int fd)
{
	uint8_t buf[EVENT_DSIZE];
	return read(fd, &buf, EVENT_DSIZE);
}
#define EndKillEvent() EndEvent(priv->pfds[EVENT_KILL_IDX].fd)




/// Проверка нумерации кадра на приеме
//\ param num - нумерация кадра
//\ return - признак дубля пакета 1-дубль,0-уникален
static ack_e rdnum(client_t *priv, uint8_t num, int busy)
{
	ack_e dub;
	if (num) {
		dub = (priv->rdnum == num)? 
			aDuble : aOk;
		priv->rdone = 0;
	} else {
		dub = (priv->rdone == 0)?
			aOk : aDuble;
		priv->rdone = 1;
	}
	if (!busy)
		priv->rdnum = num;
	else
		dub = aBusy;
	
	return dub;
}



/// Установка нумерации кадра на передачу
//\ return - номер передающего пакета
static uint8_t wrnum(client_t *priv)
{
	uint8_t num;
	num=priv->wrnum;
	if (!num) {
		priv->wrnum=2;
	} else {
		priv->wrnum++;
		if (!priv->wrnum) priv->wrnum=1;
	}
	return num;
}



/// Замена символа в строке
//\ param find - искомый
//\ param replace - требуемый
static int replace_char(char* str, char find, char replace)
{
	int cnt = 0;
	char *current_pos = strchr(str,find);
	while (current_pos) {
		cnt++;
		*current_pos = replace;
		current_pos = strchr(current_pos+1, find);
	}
	return cnt;
}



/// Отправка кадра
static int SendRaw(int sock, const char* ip, uint16_t port, const void *buf, int size)
{
	static struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = inet_addr(ip);

	int res = sendto(sock, buf, size, 0, (const struct sockaddr *) &addr, sizeof(struct sockaddr_in));
	
	if (res < 0) {
		log_write(TM_TRACE, "hmiclient ERR SENDTO { sock %d, ip: %s, port: %d, size %d }", sock, ip, port, size);
	}

	return res;
}



/// Отправка рабочего кадра
static int SendWorkFrame(client_t *priv)
{
	if (strlen(priv->server_ip) == 0)
		return 0;
	return SendRaw(priv->pfds[WORK_SOCKET_IDX].fd, priv->server_ip, PORT_WORK, priv->txframe.frame, priv->txframe.size);
}



/// SSDP запрос
// return	0 - ok
//		  -1 - error
static int SendSSDPRequest(client_t *priv)
{
	int i;
	int res, ret = -1;
	char request[256];
	char *pr = request;	

	priv->isBusy = 1;
	
	strcpy(pr, "REQUEST;CLASS=3;TYPE=0;IDENT=;");
	pr += strlen(pr);

	to_append(&priv->to_que, &priv->ssdpRepeatTO, priv->timeouts.ssdpRepeatTO * TIMEOUTS_MULT);
	priv->ssdp_cur_idx++;
	if (priv->ssdp_cur_idx >= priv->ssdp_size + priv->ssdp_start_idx) {
		priv->ssdp_cur_idx = priv->ssdp_start_idx;
	}
	i = priv->ssdp_cur_idx;
	// for (i = priv->ssdp_start_idx; i < priv->ssdp_size + priv->ssdp_start_idx; ++i) {
		res = SendRaw(priv->pfds[i].fd, IP_FIND, PORT_FIND, request, strlen(request));
		if (res > 0) { // хотя бы один -> успех
			ret = 0;
		}
	// }
	return ret;
}



/// Echo запрос
// return	0 - ok
//		  -1 - error
static int SendEcho(client_t *priv)
{
	workframeheader_t *header = &priv->txframe.frame->header;

	priv->txframe.size = sizeof(workframeheader_t);
	header->number = wrnum(priv);
	header->frame = fbcEcho;
	header->reason = rCycle;
	header->ack = aOk;
	priv->isBusy = 1;

	to_append(&priv->to_que, &priv->echoRepeatTO, priv->timeouts.echoRepeatTO * TIMEOUTS_MULT);
	return ( SendWorkFrame(priv) > 0 )?
		0 : -1;
}



/// Отправка кадров параметров ПЛК
static int SendPlcParam(client_t *priv, char *data, size_t dsize)
{
	workframeheader_t *header = &priv->txframe.frame->header;

	header->number = wrnum(priv);
	header->frame = fbcPlcParam;
	header->reason = rData;
	header->ack = aOk;
	memcpy(priv->txframe.frame->data, data, dsize);
	priv->txframe.size = (int)(sizeof(workframeheader_t) + dsize);

	priv->isBusy = 1;

	to_append(&priv->to_que, &priv->dataRepeatTO, priv->timeouts.dataRepeatTO * TIMEOUTS_MULT);
	return ( SendWorkFrame(priv) > 0 )?
		0 : -1;
}



static inline void SendPlcParamInit(client_t *priv)
{
	char data[SENDBUF_SIZE_MAX - sizeof(workframeheader_t)];
	size_t dataSize = SENDBUF_SIZE_MAX - sizeof(workframeheader_t);

	size_t dsize = cfgpar_init(data, dataSize);

	printf("SendPlcParamInit %d\n", dsize);

	if (dsize != 0) {
		SendPlcParam(priv, data, dsize);
	}
}



static inline void HandlePlcParamFrame(client_t *priv, char *rxBuf, int rxSize)
{
	char data[SENDBUF_SIZE_MAX - sizeof(workframeheader_t)];
	size_t dataSize = SENDBUF_SIZE_MAX - sizeof(workframeheader_t);

	size_t dsize = cfgpar_read(rxBuf, rxSize, data, dataSize);

	printf("HandlePlcParamFrame %d\n", dsize);

	if (dsize != 0) {
		SendPlcParam(priv, data, dsize);
	}
}



/// Вычисление таймаута
static int poll_get_to(client_t *priv)
{
	int to = to_poll(&priv->to_que);
	if (to > 0) {
		to /= 1000;
		to++;
	}
	if (to == -1 || to > priv->timeouts.minimalTO) {
		to = priv->timeouts.minimalTO;
	}
	return to;
}



static void SetLink(client_t *priv, int link)
{
	if (!priv->link) {
		SendPlcParamInit(priv);
	}
	priv->link = link;
}



/// Установка параметров на разрыв соединения
static void Disconnect(client_t *priv) 
{
	SetLink(priv, 0);
	priv->aLotOfServers = 0;
	to_delete(&priv->to_que, &priv->dataRepeatTO);
	to_delete(&priv->to_que, &priv->echoRepeatTO);
	to_delete(&priv->to_que, &priv->connectionTO);
	priv->server_ip[0] = 0;
}



/// Основной рабочий поток
static void *MainLoop(void *data)
{
	client_t *priv = (client_t*)data;
	const size_t rbuf_size = RECVBUF_SIZE_MAX;
	char rbuf[rbuf_size];

	int res, i;
	for (;;) {
		res = poll(priv->pfds, priv->pfds_size, poll_get_to(priv));
		// events
		if (res > 0) {
			for (i = 0; i < priv->pfds_size; ++i) {
				if (priv->pfds[i].revents & POLLIN) {
					switch (i) {
						// sys events
						case EVENT_KILL_IDX: {
							EndKillEvent();
							pthread_mutex_unlock(&priv->killmu);
							goto MainLoop_exit;
						} break;
						// network events
						default: {
							// get frame
							struct sockaddr_in addr;
							socklen_t addr_len = sizeof(struct sockaddr_in);
							res = recvfrom(priv->pfds[i].fd, rbuf, rbuf_size, 0, (struct sockaddr *)&addr, &addr_len);
							if (res <= 0) {
								log_write(TM_TRACE, "hmiclient MainLoop recvfrom: %d, err: %s", res, strerror(errno));
								continue;
							}
							char *ip = inet_ntoa(*(struct in_addr*)&addr.sin_addr.s_addr);

							// handle frame
							pthread_mutex_lock(&priv->mu);
							switch (i) {
								case WORK_SOCKET_IDX: {
									int isServer = (strcmp(priv->server_ip, ip) == 0)? 
										1 : 0;
									if (!isServer) {
										goto work_out;
									}

									workframe_t *rxframe = (workframe_t *)&rbuf;
									workframeheader_t *header = &rxframe->header;
									uint8_t framenum = header->number; // for ans & ack

									// состояние связи с сервером
									SetLink(priv, 1);
									to_reappend(&priv->to_que, &priv->connectionTO, priv->timeouts.connectionTO * TIMEOUTS_MULT);

									switch (header->frame) {
										case fbcEcho: {
											if (priv->txframe.frame->header.frame == fbcEcho) {
												priv->isBusy = 0;
												to_delete(&priv->to_que, &priv->echoRepeatTO);
											}
											goto work_out;
										} break;
										case fbcPlcParam:
										case fbcBase: /* go next */ {
											// NOP
										} break;
										default: /* error */ {
											goto work_out;
										} break;
									}

									switch (header->reason) {
										case rData:
										case rCycle: {
											int busy = (priv->rxframe.size != 0);
											ack_e ack = rdnum(priv, header->number, busy); // compute rdnum and set ack for ans
											switch (header->reason) {
												case rData: /* need to ans */ {
													workframeheader_t ans;
													ans.number = framenum;
													ans.reason = rAck;
													ans.frame = header->frame;
													ans.ack = ack;
													SendRaw(priv->pfds[i].fd, ip, PORT_WORK, &ans, sizeof(workframeheader_t));
												} // no break
												case rCycle: /* without ans */ {
													if (ack == aOk) {
														switch (header->frame) {
															case fbcBase: { // данные для управления дисплеем
																priv->rxframe.size = res - sizeof(workframeheader_t);
																memcpy(priv->rxframe.frame->data, rxframe->data, priv->rxframe.size);
															} break;
															case fbcPlcParam: { // данные для ПЛК-клиента
																HandlePlcParamFrame(priv, rxframe->data, (res-sizeof(workframeheader_t)));
															} break;
														}
													}							
												} break;
											}
										} break;
										case rAck: /* timeup */ {
											switch (header->ack) {
												case aDuble:
												case aOk: {
													workframeheader_t *header = &priv->txframe.frame->header;
													if (header->number == framenum) {
														switch (header->frame) {
															case fbcBase:
															case fbcPlcParam:
																priv->isBusy = 0;
																to_delete(&priv->to_que, &priv->dataRepeatTO);
																break;
														}
													}
												} break;
												case aErr:
												case aBusy: {
													// NOP
												} break;
											}
										} break;
										default:
											break;
									}

									work_out:
									;
								} break;
								default: /* ssdp */ {
									// REQUEST_ANS;STATUS=1;NAME=hmi01;VERSION=0.1.0.14631;IDENT=98:5d:ad:db:ef:52;TYPE=3,100,0,0.0,0;EXT=un=none,ut=none;
									// 3 - класс устройств отображения
									if ( !strstr(rbuf, "REQUEST_ANS;") || !strstr(rbuf, "TYPE=3") )
										break;
									
									char *status_tag = "STATUS=";
									char *ver_tag = "VERSION=";
									char *ext_tag = "EXT=";
									char *un_tag = "un=";
									char *ut_tag = "ut=";
									//
									char *status = strstr(rbuf, status_tag);
									if (!status) break;
									status += strlen(status_tag);
									//
									char *ver = strstr(rbuf, ver_tag);
									if (!ver) break;
									ver += strlen(ver_tag);
									//
									char *ext = strstr(rbuf, ext_tag);
									if (!ext) break;
									//
									char *name = strstr(ext, un_tag);
									if (!name) break;
									name += strlen(un_tag);
									//
									char *type = strstr(ext, ut_tag);
									if (!type) break;
									type += strlen(ut_tag);
									//
									replace_char(rbuf, ';', 0);
									replace_char(ext, ',', 0);
									
									if (isdigit(status[0])) {
										int stat = atoi(status);
										// занят или заблокирован
										if (stat & 0x6)
											break;
									}

									int fver = (strlen(priv->curFindPrm.ver) > 0)? 0 : 1; // not set => true
									if (!fver) {
										fver = (strcmp(priv->curFindPrm.ver, ver) == 0)? 1 : 0; // equal => true
									}
									int fname = (strlen(priv->curFindPrm.name) > 0)? 0 : 1; // not set => true
									if (!fname) {
										fname = (strcmp(priv->curFindPrm.name, name) == 0)? 1 : 0; // equal => true
									}
									int ftype = (strlen(priv->curFindPrm.type) > 0)? 0 : 1; // not set => true
									if (!ftype) {
										ftype = (strcmp(priv->curFindPrm.type, type) == 0)? 1 : 0; // equal => true
									}
								
									// log_write(TM_WARN, "hmiclient-- %d %d %d \n %s %s %s",
									//		fver, fname, ftype, ver, name, type);
											
									// filter pass
									if (fver && fname && ftype) {
										// done
										priv->isBusy = 0;
										to_delete(&priv->to_que, &priv->ssdpRepeatTO);
										// already connected => num of servers > 1 => set flag => diconnect
										if (priv->aLotOfServers || (strlen(priv->server_ip) != 0 && strcmp(priv->server_ip, ip) != 0)) {
											priv->aLotOfServers = 1;
											priv->server_ip[0] = 0;
											SetLink(priv, 0);
										} 
										// first response => connect
										else {
											strcpy(priv->server_ip, ip);
											SetLink(priv, 1);
											to_reappend(&priv->to_que, &priv->connectionTO, priv->timeouts.connectionTO * TIMEOUTS_MULT);
										}
									}
								} break;
							}
							pthread_mutex_unlock(&priv->mu);
						} break; // network fds
					} // switch i
				} // if pollin
			} // for pfds
		} // res != 0
		// timeouts
		else if (res == 0) {
			for (;;) {
				to_item_t *item = to_check(&priv->to_que);
				if (!item) break;
				pthread_mutex_lock(&priv->mu);
				to_delete(&priv->to_que, item);
				switch (item->id) {
					case toDataRepeatID: {
						priv->cntRepeat++;
						SendWorkFrame(priv);
						to_append(&priv->to_que, &priv->dataRepeatTO, priv->timeouts.dataRepeatTO * TIMEOUTS_MULT);
					} break;
					case toSSDPRepeatID: {
						SendSSDPRequest(priv);
					} break;
					case toEchoRepeatID: {
						SendEcho(priv);
					} break;
					case toConnectionID: {
						SetLink(priv, 0);
					} break;
				}
				pthread_mutex_unlock(&priv->mu);
			}
		}
	}

MainLoop_exit:
	pthread_exit(0);
}



/*
Поздняя инициализация

 инициализация вынесена т.к. codesys устанавливает IP адреса конфигурации пользователя
на интерфейсах после запуска fb_init => ssdp не может захватить все реальные интерфейсы
*/
static int LateInit(client_t *priv)
{
	if (priv->initDone) {
		log_write(TM_ERROR, "hmiclient late init: init already done");
		goto LateInit_error;
	}
	
	int fd;

	// open working socket
	{
		struct sockaddr_in localSock = {0};
		int val = 1;

		fd = socket(PF_INET, SOCK_DGRAM, 0);
		priv->pfds[WORK_SOCKET_IDX].fd = fd;
		priv->pfds[WORK_SOCKET_IDX].events = POLLIN;
		priv->pfds_size++;

		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*)&val, sizeof(val)) < 0) {
			log_write(TM_ERROR, "hmiclient late init: error SO_REUSEADDR work");
			goto LateInit_error;
		}

		localSock.sin_family = AF_INET;
		localSock.sin_port = 0; // htons(PORT_WORK);
		localSock.sin_addr.s_addr = INADDR_ANY;
		if (bind(fd, (struct sockaddr*)&localSock, sizeof(struct sockaddr_in)) < 0) {
			log_write(TM_ERROR, "hmiclient late init: error bind port %d", PORT_WORK);
			goto LateInit_error;
		}
	}

	// other fds
	{
		// kill thread fd
		fd = eventfd(0, 0);
		priv->pfds[EVENT_KILL_IDX].fd = fd;
		priv->pfds[EVENT_KILL_IDX].events = POLLIN;
		priv->pfds_size++;
	}

	// open ssdp sockets
	{
   		struct ifaddrs *ifap, *i;
		int val = 1;
		char loopch = 0;
		struct ip_mreqn group = {{0}};
		struct sockaddr_in addr = {0};

		if ( getifaddrs(&ifap) != 0 ) {
			log_write(TM_ERROR, "hmiclient late init: error getifaddrs");
			goto LateInit_error;
		}
		priv->ssdp_start_idx = priv->pfds_size;
		for (i = ifap; i != 0; i = i->ifa_next) {
			if (i->ifa_addr == 0 || i->ifa_addr->sa_family != AF_INET)
				continue;
			if (strcmp(i->ifa_name, "lo") == 0 || strcmp(i->ifa_name, "usb0") == 0) 
				continue;

			struct in_addr ip = ((struct sockaddr_in*)(i->ifa_addr))->sin_addr;
			unsigned int idx = if_nametoindex(i->ifa_name);

			fd = socket(PF_INET, SOCK_DGRAM, 0);
			priv->pfds[priv->pfds_size].fd = fd;
			priv->pfds[priv->pfds_size].events |= POLLIN;
			priv->pfds_size++;
			priv->ssdp_size++;

			log_write(TM_NOTE, "hmiclient late init: SSDP add to -- %s (%u), ip: %s", i->ifa_name, idx, inet_ntoa(ip));

			if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char*)&val, sizeof(val)) < 0) {
				log_write(TM_ERROR, "hmiclient late init: error SO_REUSEADDR %s", i->ifa_name);
				goto LateInit_error;
			}
			if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, (char*)&loopch, sizeof(loopch)) < 0) {
				log_write(TM_ERROR, "hmiclient late init: error IP_MULTICAST_LOOP %s", i->ifa_name);
				goto LateInit_error;
			}
			group.imr_multiaddr.s_addr = inet_addr(IP_FIND);
			group.imr_ifindex = idx;
			if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&group, sizeof(struct ip_mreqn)) < 0) {
				log_write(TM_ERROR, "hmiclient late init: error IP_ADD_MEMBERSHIP %s", i->ifa_name);
				goto LateInit_error;
			}
			if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, (char *)&group, sizeof(struct ip_mreqn)) < 0) {
				log_write(TM_ERROR, "hmiclient late init: error IP_MULTICAST_IF %s", i->ifa_name);
				goto LateInit_error;
			}

			addr.sin_family = AF_INET;
			addr.sin_port = 0; //htons(PORT_FIND);
			addr.sin_addr.s_addr = ip.s_addr;
			if (bind(fd, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)) < 0) {
				log_write(TM_ERROR, "hmiclient late init: error bind port %d dev %s", PORT_FIND, i->ifa_name);
				goto LateInit_error;
			}

			if (priv->ssdp_size == SSDP_REQFD_MAX) {
				break;
			}
		}
		freeifaddrs(ifap);
		priv->ssdp_cur_idx = priv->ssdp_start_idx - 1; // -1 т.к. в SendSSDPRequest начинается с инкремента
		log_write(TM_NOTE, "hmiclient late init: SSDP size %d", priv->ssdp_size);
	}

	// инициализация таймаутов
	{
		int i, min;
		int *t = (int*)(&priv->timeouts);
		const int sz = sizeof(timeouts_t)/sizeof(int);
		to_initque(&priv->to_que);
		to_inititem(&priv->dataRepeatTO, 0, toDataRepeatID);
		to_inititem(&priv->connectionTO, 0, toConnectionID);
		to_inititem(&priv->ssdpRepeatTO, 0, toSSDPRepeatID);
		to_inititem(&priv->echoRepeatTO, 0, toEchoRepeatID);
		// priv->timeouts.dataRepeatTO = DATAREPEAT_TO_DEF;
		// priv->timeouts.connectionTO = 3 * priv->timeouts.dataRepeatTO;
		priv->timeouts.ssdpRepeatTO = SSDP_REPEAT_TO;
		priv->timeouts.echoRepeatTO = ECHO_REPEAT_TO;
		priv->timeouts.minimalTO = MINIMAL_TO_DEF;
		for (i = 0; i < sz; ++i) {
			min = t[i];
			if (priv->timeouts.minimalTO > min) {
				priv->timeouts.minimalTO = min;
			}
		}
	}	

	// start thread
	cmattr_t cmattr;
	cmattr.stacksize = 0;
	common_setattr("fb_hmi01", &cmattr);
	if (pthread_create(&priv->mthread, &cmattr.attr, MainLoop, (void*)priv) < 0) {
		common_clearattr("fb_hmi01", &cmattr);
		log_write(TM_ERROR, "hmiclient init: error thread create");
		goto LateInit_error;
	}
	common_clearattr("fb_hmi01", &cmattr);
	if (pthread_detach(priv->mthread) < 0) {
		log_write(TM_ERROR, "hmiclient init: error thread detach");
		goto LateInit_error;
	}

	// фиксация конца инициализации
	priv->initDone = 1;
	return 0;

LateInit_error:
	return -1;
}



/*
Инициализация ФБ:
		- выделение памяти
		- открытие сокетов
		- инициализация таймаутов
		- инциализация буферов
		- запуск потока
*/
void CDECL CDECL_EXT hmiclient__fb_init(hmiclient_fb_init_struct *p)
{
	client_t *priv = (client_t*)calloc(1, sizeof(client_t));
	p->pInstance->xPriv = 0;
	if (!priv) {
		log_write(TM_ERROR, "hmiclient init: error calloc priv");
		goto hmiclient__fb_init_error;
	}
	p->pInstance->xPriv = (RTS_IEC_BYTE *)priv;

	// init rx/tx buf
	priv->rxframe.frame = (workframe_t *)calloc(1, RECVBUF_SIZE_MAX);
	if (!priv->rxframe.frame) {
		log_write(TM_ERROR, "hmiclient init: error calloc rxframe");
		goto hmiclient__fb_init_error;
	}
	priv->txframe.frame = (workframe_t *)calloc(1, SENDBUF_SIZE_MAX);
	if (!priv->txframe.frame) {
		log_write(TM_ERROR, "hmiclient init: error calloc txframe");
		goto hmiclient__fb_init_error;
	}

	// create working mutexes
	if (pthread_mutex_init(&priv->killmu, 0) < 0) {
		log_write(TM_ERROR, "hmiclient init: error kill mutex");
		goto hmiclient__fb_init_error;
	}
	if (pthread_mutex_init(&priv->mu, 0) < 0) {
		log_write(TM_ERROR, "hmiclient init: error mutex");
		goto hmiclient__fb_init_error;
	}

	log_write(TM_TRACE, "hmiclient init done");
	return;

hmiclient__fb_init_error:
	return;
}



/*
Инициализация параметров
*/
void CDECL CDECL_EXT hmiclient__lateinit(hmiclient_lateinit_struct *p)
{
	client_t *priv = (client_t*)p->pInstance->xPriv;

	log_write(TM_TRACE, "hmiclient lateinit start");

	if (!priv) {
		p->pInstance->iError = -errInit;
		return;
	}

	priv->timeouts.dataRepeatTO = p->iRepeatTO;
	priv->timeouts.connectionTO = 3 * priv->timeouts.dataRepeatTO;

	int ret = LateInit(priv);
	if (ret != 0) {
		p->pInstance->iError = -errInit;
		p->LateInit = -1;
		return;
	}
	p->LateInit = 0;

	log_write(TM_TRACE, "hmiclient lateinit done");
}



/*
Деинициализация ФБ:
		- освобождение памяти
*/
void CDECL CDECL_EXT hmiclient__fb_exit(hmiclient_fb_exit_struct *p)
{
	int i;
	client_t *priv = (client_t*)p->pInstance->xPriv;

	if (priv->initDone) {
		pthread_mutex_lock(&priv->killmu);
		RaiseKillEvent();
		pthread_mutex_lock(&priv->killmu);
		for (i = 0; i < priv->pfds_size; ++i) {
			close(priv->pfds[i].fd);
		}
	}
	pthread_mutex_destroy(&priv->killmu);
	pthread_mutex_destroy(&priv->mu);
	free(priv->rxframe.frame);
	free(priv->txframe.frame);
	free(priv);
	p->pInstance->xPriv = 0;

	log_write(TM_TRACE, "hmiclient exit done");
}



/*
Вызов функционального блока
		- перекладка кадра и диагностики
		- установка ошибки
*/
void CDECL CDECL_EXT hmiclient__main(hmiclient_main_struct *p)
{
	client_t *priv = (client_t *)p->pInstance->xPriv;
	hmiclient_struct *pub = p->pInstance;
	p->pInstance->iError = errNo;

	if (!priv || !priv->initDone) {
		p->pInstance->iError = -errInit;
		return;
	}

	pthread_mutex_lock(&priv->mu);
	pub->xBusy = priv->isBusy;
	pub->xLinkState = priv->link;
	pub->udRepeatCnt = priv->cntRepeat;
	strcpy(pub->sIPAddress, priv->server_ip);

	// copy received to user
	int size = 0;
	if (priv->rxframe.size != 0) {
		size = (HMICLIENTCONST_XRECVBUF_SIZE > priv->rxframe.size)?
			priv->rxframe.size : HMICLIENTCONST_XRECVBUF_SIZE;
		memcpy(pub->xRecvBuf, priv->rxframe.frame->data, size);
		priv->rxframe.size = 0;
	}
	pub->iReceived = size;

	// error proceed
	if (priv->aLotOfServers) {
		p->pInstance->iError = -errFind;
	}
	pthread_mutex_unlock(&priv->mu);
	
	//log_write(TM_TRACE, "hmiclient main done");
}



/*
Отправка кадра данных:
		- проверка
		- установка заголовка
		- установка таймаута на повтор (если требуется)
*/
void CDECL CDECL_EXT hmiclient__sendraw(hmiclient_sendraw_struct *p)
{
	client_t *priv = (client_t *)p->pInstance->xPriv;

	if (!priv || !priv->initDone) {
		p->pInstance->iError = -errInit;
		return;
	}

	//log_write(TM_TRACE, "hmiclient send raw: start");

	pthread_mutex_lock(&priv->mu);
	if (priv->isBusy) {
		p->SendRaw = -1;
		p->pInstance->iError = -errBusy;
		log_write(TM_TRACE, "hmiclient send raw: errBusy");
		goto hmiclient__sendraw_exit;
	}
	if (p->Len > HMICLIENTCONST_XSENDBUF_SIZE) {
		p->SendRaw = -1;
		p->pInstance->iError = -errOverload;
		log_write(TM_WARN, "hmiclient send raw: errOverload");
		goto hmiclient__sendraw_exit;
	}
	if (strlen(priv->server_ip) == 0) {
		p->SendRaw = -1;
		p->pInstance->iError = -errServerIP;
		log_write(TM_WARN, "hmiclient send raw: errServerIP");
		goto hmiclient__sendraw_exit;
	}
	workframeheader_t *header = &priv->txframe.frame->header;
	memcpy(priv->txframe.frame->data, p->Data, p->Len);
	priv->txframe.size = (int)p->Len + sizeof(workframeheader_t);
	header->number = wrnum(priv);
	header->frame = fbcBase;
	header->ack = aOk;
	if (p->Ack) {
		to_append(&priv->to_que, &priv->dataRepeatTO, priv->timeouts.dataRepeatTO * TIMEOUTS_MULT);
		header->reason = rData;
		priv->isBusy = 1;
	} else {
		header->reason = rCycle;
		priv->isBusy = 0;
	}

	p->pInstance->xBusy = priv->isBusy;
	p->SendRaw = ((SendWorkFrame(priv) - 4) == p->Len)?
		0 : -1;

	//log_write(TM_TRACE, "hmiclient send raw: done");

hmiclient__sendraw_exit:
	pthread_mutex_unlock(&priv->mu);
	return;
}



/*
Соединение по идентификатору:
		- Установка параметров поиска
		- Отправка ssdp запроса
*/
void CDECL CDECL_EXT hmiclient__connectbyid(hmiclient_connectbyid_struct *p)
{
	client_t *priv = (client_t *)p->pInstance->xPriv;

	if (!priv || !priv->initDone) {
		p->ConnectByID = -1;
		p->pInstance->iError = -errInit;
		return;
	}

	log_write(TM_TRACE, "hmiclient send ssdp req: start");

	pthread_mutex_lock(&priv->mu);
	Disconnect(priv);
	strcpy(priv->curFindPrm.name, p->sName);
	strcpy(priv->curFindPrm.type, p->sType);
	strcpy(priv->curFindPrm.ver, p->sVersion);
	p->ConnectByID = SendSSDPRequest(priv);
	pthread_mutex_unlock(&priv->mu);

	log_write(TM_TRACE, "hmiclient send ssdp req: done");
}



/*
Установка текущего IP адреса сервера
*/
void CDECL CDECL_EXT hmiclient__connectbyip(hmiclient_connectbyip_struct *p)
{
	client_t *priv = (client_t *)p->pInstance->xPriv;

	if (!priv || !priv->initDone) {
		p->ConnectByIP = -1;
		p->pInstance->iError = -errInit;
		return;
	}

	log_write(TM_TRACE, "hmiclient set server ip: start");

	pthread_mutex_lock(&priv->mu);
	Disconnect(priv);
	strcpy(priv->server_ip, p->sIP); 
	p->ConnectByIP = SendEcho(priv);
	pthread_mutex_unlock(&priv->mu);

	log_write(TM_TRACE, "hmiclient set server ip: done");
}



/*
Разрыв связи с сервером:
		- Сброс текущего IP адреса сервера
*/
void CDECL CDECL_EXT hmiclient__disconnect(hmiclient_disconnect_struct *p)
{
	client_t *priv = (client_t *)p->pInstance->xPriv;

	if (!priv || !priv->initDone) {
		p->pInstance->iError = -errInit;
		p->Disconnect = -1;
		return;
	}

	log_write(TM_TRACE, "hmiclient disconnect: start");

	pthread_mutex_lock(&priv->mu);
	Disconnect(priv);
	p->Disconnect = 0;
	pthread_mutex_unlock(&priv->mu);

	log_write(TM_TRACE, "hmiclient disconnect: done");
}

