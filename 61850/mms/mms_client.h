/*******************************************************************************
  * @file    mms_client.h
  * @author  Перминов Р.И.
  * @version v 1
  * @date    12.02.2020
  *****************************************************************************/

#ifndef MMS_CLIENT_H_
#define MMS_CLIENT_H_

#include "fb_iec850mmsm.h"



/*
 * @fn: MMSClient_FindById
 * @brief: Поиск клиента по идентификатору
 * @param list_head - указатель на первый элемент списка клиентов
 * @param id - идентификатор клиента
 * return указатель на клиента или 0
 * */
extern MMSClient_t *MMSClient_FindById(int id);



/*
 * @fn: MMSClient_SetupCallback
 * @brief: Установка callback функции на изменение состояния
 * @param client - клиент
 * @param cb - callback функция
 * @param userData - пользовательские данные
 * no return
 * */
extern void MMSClient_SetupCallback(MMSClient_t *client, mms_client_callback cb, 
        void *userData);        
/*
 * @fn: MMSClientData_SetupCallback
 * @brief: Установка callback функции по приему/завершению записи данных
 * @param data - данные клиента
 * @param cb - callback функция
 * @param userData - пользовательские данные
 * no return
 * */
extern void MMSClientData_SetupCallback(MMSClientData_t *data, mms_client_data_callback cb, 
        void *userData);



/*
 * @fn: MMSClient_SetupConnect
 * @brief: Запуск клиента на установку соединения с сервером
 * @param client - клиент
 * @param err - системная ошибка
 * return @ref MCRes_t
 * */
extern MCRes_t MMSClient_SetupConnect(MMSClient_t *client, IedClientError *err);
/*
 * @fn: MMSClient_SetupConnectByIP
 * @brief: Запуск клиента на установку соединения с сервером по заданному IP
 * @param client - клиент
 * @param ip - IP адрес сервера
 * @param err - системная ошибка
 * return @ref MCRes_t
 * */
extern MCRes_t MMSClient_SetupConnectByIP(MMSClient_t *client, const char *ip, IedClientError *err);
/*
 * @fn: MMSClient_Disconnect
 * @brief: Запуск клиента на разрыв соединения с сервером
 * @param client - клиент
 * @param err - системная ошибка
 * return @ref MCRes_t
 * */
extern MCRes_t MMSClient_Disconnect(MMSClient_t *client, IedClientError *err);



/*
 * @fn: MMSClient_Setup
 * @brief: Запуск на выполнение команды
 * NOTE:
 *      Выполняемая команда зависит от типа данных @ref MMSClient_Type_t.
 *      - mmsctDataSet - установка на чтение dataset
 *      - mmsctReport - установка на чтение rcb отчета
 *      - mmsctWrSignal - установка на запись сигнала
 *      - mmsctRdSignal - установка на чтение сигнала
 * @param data - данные клиента
 * дополнительные параметры:
 * @param MMSVal_t *val - значение на запись
 * @param uint64_t operTime - время применения CO
 * return @ref MCRes_t
 * */
extern MCRes_t MMSClient_Setup(MMSClientData_t *data, ...);
/*
 * @fn: MMSClient_isBusy
 * @brief: Запрос состояния данных клиента
 * @param data - данные клиента
 * return 
 *      1 - выполнение команды setup
 *      0 - не занят
 * */
extern int MMSClient_isBusy(MMSClientData_t *data);
/*
 * @fn: MMSClient_GetLastOperationResult
 * @brief: Получение результата последней операции
 * @param data - данные клиента
 * @param sysErr - код системной ошибки @ref mcsrSysErr
 * return @ref MCRes_t
 * */
extern MCRes_t MMSClient_GetLastOperationResult(MMSClientData_t *data, IedClientError *sysErr);



/*
 * @fn: MMSClient_SelectNextDataset
 * @brief: Установка текущего обрабатываемого dataset на следующий принятый
 * NOTE:
 *      - вызов очищает место в буфере и делает недоступным предыдущий dataset
 *          и все его данные
 * @param data - данные клиента
 * return @ref MCRes_t:
 *      mcsrEndReached - все dataset были вычитаны
 *      mcsrOk - успешная установка dataset
 *      или код ошибки
 * */
extern MCRes_t MMSClient_SelectNextDataset(MMSClientData_t *data);
/*
 * @fn: MMSClient_GetSelectedDataset
 * @brief: Получение текущего установленного dataset
 * @param data - данные клиента
 * @param result - указатель для получения результата
 * return @ref MCRes_t:
 *      mcsrNotSelected - dataset не установлен @ref MMSClient_SelectNextDataset
 *      mcsrOk - успешное получение dataset
 *      или код ошибки
 * */
extern MCRes_t MMSClient_GetSelectedDataset(MMSClientData_t *data, MMSVal_t *result);
/*
 * @fn: MMSClient_GetNextBaseTypeMMSVal
 * @brief: Получение следующего элемента базового типа из текущего 
 *      dataset данных клиента
 * NOTE:
 *      - после возврата mcsrEndReached следующий вызов функции вернет 
 *          первый элемент
 * @param data - данные клиента
 * @param result - указатель для получения результата
 * return @ref MCRes_t:
 *      mcsrNotSelected - dataset не установлен @ref MMSClient_SelectNextDataset
 *      mcsrEndReached - все элементы были вычитаны
 *      mcsrOk - успешное получение элемента
 *      или код ошибки
 * */
extern MCRes_t MMSClient_GetNextBaseTypeMMSVal(MMSClientData_t *data, MMSVal_t *result);

/*
 * @fn: MMSClient_GetReportIncludesIdx
 * @brief: Получение массива включения данных в установленный dataset 
 * @param data - данные клиента
 * @param buf - буфер для размещения индексов
 * @param bufSz - размер buf в байтах (на входе - макс. размер, 
 *      на выходе - число установленных индексов)
 * return @ref MCRes_t:
 *      mcsrNotSelected - dataset не установлен @ref MMSClient_SelectNextDataset
 *      mcsrOk - успешное получение элемента
 *      или код ошибки
 * */
extern MCRes_t MMSClient_GetReportIncludesIdx(MMSClientData_t *data, uint8_t *buf, int *bufSz);
/*
 * @fn: MMSClient_GetReportsCount
 * @brief: Получение числа забуферизированных репортов
 * @param data - данные клиента
 * return число репортов
 * */
extern int MMSClient_GetReportsCount(MMSClientData_t *data);
/*
 * @fn: MMSClient_SetupRcbParams
 * @brief: Установка параметров rcb указанного отчета
 * @param data - данные клиента
 * @param prm - параметры rcb
 * return @ref MCRes_t:
 *      mcsrOk
 *      или код ошибки
 * */
extern MCRes_t MMSClient_SetupRcbParams(MMSClientData_t *data, MMSRcbParam_t *prm);
/*
 * @fn: MMSClient_GetReportEntryId
 * @brief: Получение entryId указанного отчета
 * @param data - данные клиента
 * @param result - указатель для получения результата
 * return @ref MCRes_t:
 *      mcsrNotSelected - dataset не установлен @ref MMSClient_SelectNextDataset
 *      mcsrOk - успешное получение элемента
 *      или код ошибки
 * */
extern MCRes_t MMSClient_GetReportEntryId(MMSClientData_t *data, uint64_t *result);



/*
 * @fn: MMSClient_SetupReadDataset
 * @brief: Установка на чтение dataset
 * @param data - данные клиента
 * return @ref MCRes_t
 * */
extern MCRes_t MMSClient_SetupReadDataset(MMSClientData_t *data);
/*
 * @fn: MMSClient_SetupGetRcb
 * @brief: Установка на получение rcb отчета
 * @param data - данные клиента
 * return @ref MCRes_t
 * */
extern MCRes_t MMSClient_SetupGetRcb(MMSClientData_t *data);
/*
 * @fn: MMSClient_SetupReadObject
 * @brief: Установка на чтение сигнала
 * @param data - данные клиента
 * return @ref MCRes_t
 * */
extern MCRes_t MMSClient_SetupReadObject(MMSClientData_t *data);
/*
 * @fn: MMSClient_SetupWriteObject
 * @brief: Установка на запись сигнала
 * @param data - данные клиента
 * @param val - значение сигнала
 * return @ref MCRes_t
 * */
extern MCRes_t MMSClient_SetupWriteObject(MMSClientData_t *data, MMSVal_t *val);
/*
 * @fn: MSClient_SetupControlObject
 * @brief: Установка на запись контроллируемого объекта
 * @param data - данные клиента
 * @param val - значение сигнала
 * @param operTime - время установки сигнала
 * return @ref MCRes_t
 * */
extern MCRes_t MSClient_SetupControlObject(MMSClientData_t *data, MMSVal_t *val, uint64_t operTime);

/*
 * @fn: MMSClient_ReadLastObject
 * @brief: Чтение последнего полученного объекта
 * @param data - данные клиента
 * @param result - указатель для получения результата
 * return @ref MCRes_t
 * */
extern MCRes_t MMSClient_ReadLastObject(MMSClientData_t *data, MMSVal_t *result);



/*
 * @fn: MMSClient_SetupInitControl
 * @brief: Запуск инициализации объекта контроля
 * @param data - данные клиента
 * @param useConstantT - использование констатной метки времени при выполнении операций
 * @param interlockCheck - флаг проверки interlock при выполнении операций
 * @param synchroCheck - флаг проверки synchro при выполнении операций
 * return @ref MCRes_t
 * */
extern MCRes_t MMSClient_SetupInitControl(MMSClientData_t *data, bool useConstantT, bool interlockCheck, bool synchroCheck);
/*
 * @fn: MMSClient_GetControlType
 * @brief: Получение типа контролируемого объекта
 * @param data - данные клиента
 * @param result - указатель для получения результата
 * return @ref MCRes_t
 * */
extern MCRes_t MMSClient_GetControlType(MMSClientData_t *data, MMSVal_Type_t *result);
/*
 * @fn: MMSClient_SetupOriginControl
 * @brief: Установка идентификации клиента для контроля
 * @param data - данные клиента
 * @param orIdent - идентификация
 * @param orCat - категория
 * return @ref MCRes_t
 * */
extern MCRes_t MMSClient_SetOriginControl(MMSClientData_t *data, const char* orIdent, int orCat);
/*
 * @fn: MMSClient_CancelControl
 * @brief: Выдача отмены объекту контроля
 * @param data - данные клиента
 * return @ref MCRes_t
 * */
extern MCRes_t MMSClient_SetupCancelControl(MMSClientData_t *data);



/*
 * @fn: MMSClient_CreateStruct
 * @brief: Создание структуры
 * @param size - размер структуры
 * @param result - указатель для получения результата
 * return @ref MCRes_t
 * */
extern MCRes_t MMSClient_CreateStruct(int size, MMSVal_t *result);
/*
 * @fn: MMSClient_RemoveStruct
 * @brief: Удаление структуры и всех ее элементов
 * @param data - указатель на структуру
 * return @ref MCRes_t
 * */
extern MCRes_t MMSClient_RemoveStruct(MMSVal_t *data);
/*
 * @fn: MMSClient_AddStructElem
 * @brief: Добавление элемента в структуру
 * NOTE: Только добавление нового элемента в пустую позицию, 
 *      все изменения должны выполняться через
 *      @ref MMSClient_GetStructElem @ref MMSClient_SetStructElem
 * @param data - структура/dataset
 * @param index - номер элемента
 * @param val - значение элемента
 * return @ref MCRes_t
 * */
extern MCRes_t MMSClient_AddStructElem(MMSVal_t *structure, int index, MMSVal_t *val);
/*
 * @fn: MMSClient_GetStructElem
 * @brief: Получение элемента структуры/dataset по индексу
 * @param data - структура/dataset
 * @param index - номер элемента
 * @param result - указатель для получения результата
 * return @ref MCRes_t
 * */
extern MCRes_t MMSClient_GetStructElem(MMSVal_t *data, int index, MMSVal_t *result);
/*
 * @fn: MMSClient_SetStructElem
 * @brief: Установка элемента структуры/dataset по индексу
 * @param data - структура/dataset
 * @param index - номер элемента
 * @param val - значение элемента
 * return @ref MCRes_t
 * */
extern MCRes_t MMSClient_SetStructElem(MMSVal_t *data, int index, MMSVal_t *val);



/*
 * @fn: MMSClient_GetSimpleStruct
 * @brief: Получение данных в виде упрощенной структуры:      
 *       mmsvtBool:
 *       mmsvtAccessErr:
 *               1 байт        
 *       mmsvtBitString:
 *       mmsvtLReal:
 *       mmsvtReal:
 *       mmsvtUInt:
 *       mmsvtInt:
 *               4 байта
 *       mmsvtUTC:
 *       mmsvtBinTime:
 *               8 байт
 *       case mmsvtString:
 *       case mmsvtVString:
 *       case mmsvtOctString:
 *               64 байта
 * NOTE:
 *      в случае переполнения все биты будут установлены в 1
 * @param data - структура/dataset
 * @param buf - буфер для размещения данных
 * @param bufSz - размер buf в байтах (на входе - макс. размер, 
 *      на выходе - число размещенных в buf байт)
 * return @ref MCRes_t
 * */
extern MCRes_t MMSClient_GetSimpleStruct(MMSVal_t *data, uint8_t *buf, int *bufSz);



/*
 * @fn: MMSClient_GetMMSValPtr
 * @brief: Получение указателя на MMS значение
 * NOTE: 
 *      при успешном получении указателя записывает данные в result->val
 * @param data - структура/dataset
 * @param index - номер элемента
 * @param result - указатель для получения результата
 * return @ref MCRes_t
 * */
extern MCRes_t MMSClient_GetMMSValPtr(MMSVal_t *data, int index, MMSValPtr_t *result);
/*
 * @fn: MMSClient_GetMMSValByPtr
 * @brief: Получение MMS значения по указателю
 * @param ptr - указатель на MMS значение
 * return @ref MCRes_t
 * */
extern MCRes_t MMSClient_GetMMSValByPtr(MMSValPtr_t *ptr);
/*
 * @fn: MMSClient_SetMMSValByPtr
 * @brief: Установка MMS значения по указателю
 * @param ptr - указатель на MMS значение
 * return @ref MCRes_t
 * */
extern MCRes_t MMSClient_SetMMSValByPtr(MMSValPtr_t *ptr);



#endif /* MMS_CLIENT_H_ */