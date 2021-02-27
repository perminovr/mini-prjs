/*******************************************************************************
  * @file    esp8266_channel.h
  * @author  Перминов Р.И.
  * @version v0.0.0.1
  * @date    19.04.2019
  *****************************************************************************/

#ifndef ESP8266_CH_H_
#define ESP8266_CH_H_

#include <stdint.h>



#define ESP_CHANNEL_DATA_MAX_SIZE		1471



/*! ---------------------------------------------------------------------------
 * @def: ESP_Ch_Type_e
 *
 * @brief: Тип канала ESP
*/
typedef enum {
	ESP_ChT_No,
	ESP_ChT_UDP,
	ESP_ChT_TCP,			/* не поддерживается */
	ESP_ChT_SSH,			/* не поддерживается */
} ESP_Ch_Type_e;



/*! ---------------------------------------------------------------------------
 * @def: ESP_Ch_TCP_Params_t
 *
 * @brief: Параметры канала для TCP соединения
*/
typedef struct {
	int keepalive;			/* Время до разрыва соединения */
	uint32_t remoteIP;		/* IP удаленного узла */
	uint16_t remotePort;	/* PORT удаленного узла */
} ESP_Ch_TCP_Params_t;



/*! ---------------------------------------------------------------------------
 * @def: ESP_Ch_SSH_Params_t
 *
 * @brief: Параметры канала для SSH соединения
*/
typedef struct {
	int keepalive;			/* Время до разрыва соединения */
	uint32_t remoteIP;		/* IP удаленного узла */
	uint16_t remotePort;	/* PORT удаленного узла */
} ESP_Ch_SSH_Params_t;



/*! ---------------------------------------------------------------------------
 * @def: ESP_Ch_UDPMode_e
 *
 * @brief: Режим канала UDP
 *
 * NOTE: не поддерживается; можно не указывать
*/
typedef enum {
	ESP_ChUM_Fix,			/* Не изменять адрес назначения по получению кадра */
	ESP_ChUM_Once,			/* Изменить адрес назначения по получению кадра один раз */
	ESP_ChUM_Var,			/* Менять адрес назначения по получению каждого кадра */
} ESP_Ch_UDPMode_e;



/*! ---------------------------------------------------------------------------
 * @def: ESP_Ch_UDP_Params_t
 *
 * @brief: Параметры канала для UDP
*/
typedef struct {
	uint16_t ownPort;		/* PORT esp */
	ESP_Ch_UDPMode_e mode;	/* режим работы */
} ESP_Ch_UDP_Params_t;



/*! ---------------------------------------------------------------------------
 * @def: ESP_Ch_UDP_Params_t
 *
 * @brief: Параметры по типу канала
*/
typedef union {
	ESP_Ch_UDP_Params_t udp;
	ESP_Ch_SSH_Params_t ssh;
	ESP_Ch_TCP_Params_t tcp;
} ESP_Ch_TypeParams_u;



/*! ---------------------------------------------------------------------------
 * @def: ESP_Channel_Params_t
 *
 * @brief: Параметры канала
*/
typedef struct {
	int id;						/* идентификатор соединения [0..4] */
	ESP_Ch_Type_e type;			/* тип */
	ESP_Ch_TypeParams_u tprms;	/* параметры по типу канала */
} ESP_ChannelParams_t;



/*! ---------------------------------------------------------------------------
 * @def: ESP_Channel_Data_t
 *
 * @brief: Данные канала
*/
typedef struct {
	void *buf;				/* данные */
	int size;				/* число байт данных */
	uint32_t remoteIP;		/* IP удаленного узла */
	uint16_t remotePort;	/* PORT удаленного узла */
} ESP_ChannelData_t;



/*! ---------------------------------------------------------------------------
 * @def: ESP_Channel_t
 *
 * @brief: Канал ESP
*/
typedef struct {
	int id;
	ESP_ChannelData_t data;	/* данные */
} ESP_Channel_t;



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
extern int ESP_GetFreeChannelID(void);



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
extern char *ip_to_str(uint32_t ip);



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
extern uint32_t str_to_ip(char *ip);


#endif /* ESP8266_CH_H_ */
