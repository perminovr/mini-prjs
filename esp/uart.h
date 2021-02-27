/*******************************************************************************
  * @file    uart.h
  * @author  Перминов Р.И.
  * @version v0.0.0.1
  * @date    19.04.2019
  *****************************************************************************/

#ifndef UART_ELE_H_
#define UART_ELE_H_

#include <stdint.h>
#include <stddef.h>



/*! ---------------------------------------------------------------------------
 * @def: UART_STATE
 *
 * @brief: Состояние порта
*/
typedef enum {
	UART_Closed = 0,
	UART_Opened
} UART_State_t;



/*! ---------------------------------------------------------------------------
 * @def: UART_Result_e
 *
 * @brief: Результат выполнения функции
*/
typedef enum {
	UART_Ok,
	UART_Fail,
} UART_Result_e;



/*! ---------------------------------------------------------------------------
 * @def: UART_TimeOut_t
 *
 * @brief: Структура таймаутов порта
*/
typedef struct {
	uint32_t Ms;
	uint32_t nChars;
} UART_TimeOut_t;



/*! ---------------------------------------------------------------------------
 * @def: UART_Params_t
 *
 * @brief:
*/
typedef struct {
	uint32_t baudRate;
	UART_TimeOut_t timeOut;
} UART_Params_t;



/*! ---------------------------------------------------------------------------
 * @fn: UART_Read
 *
 * @brief: Прием данных
 *
 * input parameters
 * buffer_size - размер буфера на прием
 *
 * output parameters
 * buffer - буфер с принятыми данными
 *
 * return число принятых байт или ошибка (-1)
*/
extern int UART_Read(void *buffer, size_t buffer_size);



/*! ---------------------------------------------------------------------------
 * @fn: UART_ReadNonBlocking
 *
 * @brief: Прием данных без блокировки
 *
 * input parameters
 * buffer_size - размер буфера на прием
 *
 * output parameters
 * buffer - буфер с принятыми данными
 *
 * return число принятых байт или ошибка (-1)
*/
extern int UART_ReadNonBlocking(void *buffer, size_t buffer_size);



/*! ---------------------------------------------------------------------------
 * @fn: UART_Write
 *
 * @brief: Передача данных
 *
 * input parameters
 * buffer - данные
 * buffer_size - размер в байтах
 *
 * output parameters
 *
 * return число переданных байт или ошибка (-1)
*/
extern int UART_Write(const void *buffer, size_t buffer_size);



/*! ---------------------------------------------------------------------------
 * @fn: UART_WriteStr
 *
 * @brief: Передача строки
 *
 * input parameters
 * str - строка
 *
 * output parameters
 *
 * return число переданных байт или ошибка (-1)
*/
extern int UART_WriteStr(const char *str);



/*! ---------------------------------------------------------------------------
 * @fn: UART_WaitSpace
 *
 * @brief: Ожидание тишины на линии
 *
 * input parameters
 * @param duration - длительность ожидания (в символах)
 *
 * output parameters
 *
 * no return
*/
extern void UART_WaitSpace(uint8_t duration);



/*! ---------------------------------------------------------------------------
 * @fn: UART_Flush
 *
 * @brief: Очистка буфера приема до наступления тишины на линии  в 10 символов
 *
 * input parameters
 *
 * output parameters
 *
 * no return
*/
extern void UART_Flush(void);



/*! ---------------------------------------------------------------------------
 * @fn: UART_FastFlush
 *
 * @brief: Очистка буфера приема
 *
 * input parameters
 *
 * output parameters
 *
 * no return
*/
extern void UART_FastFlush(void);



/*! ---------------------------------------------------------------------------
 * @fn: UART_GetAvailableBytes
 *
 * @brief: Возвращает число доступных байт буфера приема
 *
 * input parameters
 *
 * output parameters
 *
 * return число доступных байт буфера приема
*/
extern size_t UART_GetAvailableBytes(void);



/*! ---------------------------------------------------------------------------
 * @fn: UART_SetParams
 *
 * @brief: Установка параметров работы порта
 *
 * input parameters
 * params - параметры работы
 *
 * output parameters
 *
 * return результат выполнения @ref UART_Result_e
*/
extern UART_Result_e UART_SetParams(const UART_Params_t *params);



/*! ---------------------------------------------------------------------------
 * @fn: UART_Open
 *
 * @brief: Открытие порта
 *
 * input parameters
 * name - полное имя порта
 * params - параметры работы
 *
 * output parameters
 *
 * return результат выполнения @ref UART_Result_e
*/
extern UART_Result_e UART_Open(const char *name, const UART_Params_t *params);



/*! ---------------------------------------------------------------------------
 * @fn: UART_Close
 *
 * @brief: Закрытие порта
 *
 * input parameters
 *
 * output parameters
 *
 * no return
*/
extern void UART_Close(void);



/*! ---------------------------------------------------------------------------
 * @fn: UART_GetFD
 *
 * @brief: Получить файловый дескриптор порта
 *
 * input parameters
 *
 * output parameters
 *
 * return файловый дескриптор порта
*/
extern int UART_GetFD(void);



/*! ---------------------------------------------------------------------------
 * @fn: UART_Create
 *
 * @brief: Создание экземпляра драйвера
 *
 * input parameters
 *
 * output parameters
 *
 * return результат выполнения @ref UART_Result_e
*/
extern UART_Result_e UART_Create(void);

#endif /* UART_ELE_H_ */
