/*******************************************************************************
  * @file    esp8266.h
  * @author  Перминов Р.И.
  * @version v0.0.0.1
  * @date    19.04.2019
  *****************************************************************************/

#ifndef ESP8266_H_
#define ESP8266_H_

#include "esp8266_ap_list.h"
#include "esp8266_channel.h"


#define ESP_LOGGER
//#define ESP_PRINTF
#ifndef ESP_PORT_NAME
# define ESP_PORT_NAME				"/dev/ttyS1"
#endif
#define ESP_HARD_CONTROL
#ifdef ESP_HARD_CONTROL
# ifndef ESP_PIN_RST
#  define ESP_PIN_RST				"/sys/class/leds/ESP_RST/brightness"
# endif /* ESP_PIN_RST */
# ifndef ESP_PIN_IO0
#  define ESP_PIN_IO0				"/sys/class/leds/ESP_IO0/brightness"
# endif /* ESP_PIN_IO0 */
#endif /* ESP_HARD_CONTROL */

#define ESP_CHANNELS			5
#define ESP_ACCESS_POINTS		20



/*! ---------------------------------------------------------------------------
 * @def: ESP_Result_e
 *
 * @brief: Результат выполнения функции
*/
typedef enum {
	ESP_Ok,
	ESP_Fail,
} ESP_Result_e;




/*! ---------------------------------------------------------------------------
 * @def: ESP_Cmd_e
 *
 * @brief:
*/
typedef enum {
	ESP_Cmd_No,
	ESP_Cmd_Init,		/* data = no */
	ESP_Cmd_Find,		/* data = ESP_AccessPoint_List_t */
	ESP_Cmd_Connect,	/* data = ESP_ConnectParams_t */
	ESP_Cmd_ConParams,	/* data = ESP_ConnectParams_t */
	ESP_Cmd_Open,		/* data = ESP_ChannelParams_t */
	ESP_Cmd_Close,		/* data = int (id) */
	ESP_Cmd_Send,		/* data = int (id) */
	ESP_Cmd_Receive,	/* data = ESP_Channel_t */
	ESP_Cmd_IGMPJoin,	/* data = no */
	ESP_Cmd_IGMPLeave,	/* data = no */
} ESP_Cmd_e;



/*! ---------------------------------------------------------------------------
 * @def: ESP_CallBack_Data_t
 *
 * @brief: Данные передаваемые в callback-функцию
*/
typedef struct {
	ESP_Cmd_e type;		/* тип возврата */
	ESP_Result_e res;	/* результат выполнения запроса */
	void *data;			/* типы данных указаны в @ref ESP_Cmd_e */
} ESP_CallBack_Data_t;



/*! ---------------------------------------------------------------------------
 * @def: ESP_CallBack_t
 *
 * @brief: Указатель на callback-функцию
*/
typedef void (*ESP_CallBack_t)(ESP_CallBack_Data_t *data);



/*! ---------------------------------------------------------------------------
 * @def: ESP_NetMode_e
 *
 * @brief: Режим работы в сети
*/
typedef enum {
	ESP_NM_DHCP,
	ESP_NM_Static
} ESP_NetMode_e;



/*! ---------------------------------------------------------------------------
 * @def: ESP_ConnectParams_t
 *
 * @brief: Параметры подключения к ТД
*/
typedef struct {
	ESP_AccessPoint_t *ap;
	ESP_NetMode_e nmode;
	uint32_t ip;
	uint32_t mask;
	uint32_t gateway;
} ESP_ConnectParams_t;



/*! ---------------------------------------------------------------------------
 * @def: ESP_Init_t
 *
 * @brief:
*/
typedef struct {
	ESP_NetMode_e nmode;
	uint32_t ip;
	uint32_t mask;
	uint32_t gateway;
	ESP_CallBack_t callback;
} ESP_Init_t;



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
extern ESP_Result_e ESP_IGMPJoin(uint32_t ip);



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
extern ESP_Result_e ESP_IGMPLeave(uint32_t ip);



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
extern ESP_Result_e ESP_Find(ESP_APList_Sort_e sortType);



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
extern ESP_Result_e ESP_Connect(const char *name, const char *passwd);



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
extern ESP_Result_e ESP_GetConnectParams(void);



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
extern ESP_Result_e ESP_Close(int id);



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
extern ESP_Result_e ESP_Open(const ESP_ChannelParams_t *params);



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
extern ESP_Result_e ESP_Send(const ESP_Channel_t *channel);



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
extern ESP_Result_e ESP_Init(const ESP_Init_t *initStruct);



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
extern void ESP_DeInit(void);



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
extern ESP_Result_e ESP_Create(void);

#endif /* ESP8266_H_ */
