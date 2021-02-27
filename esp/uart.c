/*******************************************************************************
  * @file    uart.c
  * @author  Перминов Р.И.
  * @version v0.0.0.1
  * @date    19.04.2019
  *****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include "uart.h"



typedef struct {
	int fd;
	UART_Params_t params;
	UART_State_t state;
	uint32_t charsSpacing;
} UART_Driver_t;



UART_Driver_t *_UART_Driver;
#define this _UART_Driver



static int _UART_ChangeSysTimeouts(const UART_TimeOut_t *timeOut);



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
int UART_Read(void *buffer, size_t buffer_size)
{
	int res = -1;
	if (this->fd != -1) {
		UART_TimeOut_t TOStr;
		struct timeval tv;
		fd_set rfds;
		//
		tv.tv_sec = this->params.timeOut.Ms / 1000;
		tv.tv_usec = (this->params.timeOut.Ms % 1000) * 1000;
		TOStr.Ms = 0;
		TOStr.nChars = buffer_size;
		//
		_UART_ChangeSysTimeouts(&TOStr);
		//
		FD_ZERO(&rfds);
		FD_SET(this->fd, &rfds);
		res = select(this->fd + 1, &rfds, 0, 0, &tv);
		//
		if (res > 0)
			res = read(this->fd, buffer, buffer_size);
		else
			UART_FastFlush();
	}
	return res;
}



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
int UART_ReadNonBlocking(void *buffer, size_t buffer_size)
{
	UART_TimeOut_t TOStr = {0, 0};
	_UART_ChangeSysTimeouts(&TOStr);
	return read(this->fd, buffer, buffer_size);
}



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
int UART_Write(const void *buffer, size_t buffer_size)
{
	return write(this->fd, buffer, buffer_size);
}



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
int UART_WriteStr(const char *str)
{
	return write(this->fd, (const void*)str, strlen(str));
}



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
void UART_WaitSpace(uint8_t duration)
{
	if (this->fd != -1) {
		size_t available = UART_GetAvailableBytes(), tmp;
		while (1) {
			uint32_t sleept = this->charsSpacing * (uint32_t)duration;
			usleep(sleept);
			tmp = UART_GetAvailableBytes();
			if (available != tmp) {
				available = tmp;
			} else {
				break;
			}
		}
	}
}



/*! ---------------------------------------------------------------------------
 * @fn: UART_Flush
 *
 * @brief: Очистка буфера приема до наступления тишины на линии в 10 символов
 *
 * input parameters
 *
 * output parameters
 *
 * no return
*/
void UART_Flush(void)
{
	if (this->fd != -1) {
		while (1) {
			UART_FastFlush();
			usleep(this->charsSpacing * 10);
			if (UART_GetAvailableBytes() == 0)
				break;
		}
	}
}



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
void UART_FastFlush(void)
{
	tcflush(this->fd, TCIFLUSH);
}



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
UART_Result_e UART_SetParams(const UART_Params_t *params)
{
	if (this->fd != -1) {
		struct termios options;
		uint32_t baudRate;

		memcpy(&this->params, params, sizeof(UART_Params_t));

		tcgetattr(this->fd, &options);

		switch (this->params.baudRate) {
			case 300: baudRate = B300; break;
			case 600: baudRate = B600; break;
			case 1200: baudRate = B1200; break;
			case 2400: baudRate = B2400; break;
			case 4800: baudRate = B4800; break;
			case 9600: baudRate = B9600; break;
			case 19200: baudRate = B19200; break;
			case 38400: baudRate = B38400; break;
			case 57600: baudRate = B57600; break;
			case 115200: baudRate = B115200; break;
			case 230400: baudRate = B230400; break;
			case 460800: baudRate = B460800; break;
			case 921600: baudRate = B921600; break;
			case 1152000: baudRate = B1152000; break;
			case 2000000: baudRate = B2000000; break;
			case 2500000: baudRate = B2500000; break;
			case 3000000: baudRate = B3000000; break;
			case 4000000: baudRate = B4000000; break;
			default: {
				this->params.baudRate = 115200;
				baudRate = B115200;
			} break;
		}

		options.c_cflag = 0;
		cfsetispeed(&options, baudRate);
		cfsetospeed(&options, baudRate);
		options.c_cflag |= CS8 | CREAD | CLOCAL;
		//
		options.c_iflag = 0;
		//
		options.c_oflag = 0;
		//
		options.c_lflag = 0;
		//
		options.c_cc[VMIN] = 0;
		options.c_cc[VTIME] = 0;

		int res = tcsetattr(this->fd, TCSAFLUSH, &options);

		if (res != -1) {
			// in us
			this->charsSpacing = (uint32_t) (
				(8.0 / (float)(this->params.baudRate))*1000000.0
			) + 1;
			return UART_Ok;
		}
	}
	return UART_Fail;
}



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
UART_Result_e UART_Open(const char *name, const UART_Params_t *params)
{
	this->fd = open(name, O_RDWR | O_NOCTTY);

	if (UART_SetParams(params) == UART_Ok) {
		this->state = UART_Opened;
		return UART_Ok;
	}
	return UART_Fail;
}



/*! ---------------------------------------------------------------------------
 * @fn: UART_GetAvailableBytes
 *
 * @brief: Возвращает число доступных байт на прием
 *
 * input parameters
 *
 * output parameters
 *
 * return число доступных байт на прием
*/
size_t UART_GetAvailableBytes(void)
{
	size_t res = 0;
	ioctl(this->fd, FIONREAD, &res);
	return res;
}



/*! ---------------------------------------------------------------------------
 * @fn: _UART_ChangeSysTimeouts
 *
 * @brief: Изменение параметров ожидания по приему
 *
 * input parameters
 * timeOut - параметры ожидания байт
 *
 * output parameters
 *
 * return
 * 		0 - ошибка
 * 		1 - успех
*/
static int _UART_ChangeSysTimeouts(const UART_TimeOut_t *timeOut)
{
	struct termios options;
	tcgetattr(this->fd, &options);
	if (options.c_cc[VMIN] == timeOut->nChars && options.c_cc[VTIME] == timeOut->Ms / 100)
		return 1;
	options.c_cc[VMIN] = timeOut->nChars;
	options.c_cc[VTIME] = timeOut->Ms / 100;
	int res = tcsetattr(this->fd, TCSADRAIN, &options);
	return (res != -1)? 1 : 0;
}



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
int UART_GetFD(void)
{
	return this->fd;
}



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
void UART_Close(void)
{
	if (this->state == UART_Opened) {
		this->state = UART_Closed;
		close(this->fd);
	}
}



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
UART_Result_e UART_Create(void)
{
	if (!this)
		this = (UART_Driver_t *)calloc(1, sizeof(UART_Driver_t));
	if (this) {
		this->fd = -1;
		return UART_Ok;
	} else {
		return UART_Fail;
	}
}


