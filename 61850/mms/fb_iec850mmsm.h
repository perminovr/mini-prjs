/*******************************************************************************
  * @file    fb_iec850mmsm.h
  * @author  Пискунов С.Н.
  * @version v 3.5.11.1
  * @date    12.02.2020
  * @description - методы для работы с протоколом IEC61850 MMS Client через ФБ Codesys
  *
  *****************************************************************************/

#ifndef FB_IEC850MMSM_H
#define FB_IEC850MMSM_H

/*
	****************************************************************
	MMS CLIENT *****************************************************
	****************************************************************
*/



#include <libiec61850/iec61850_client.h>

/*
 * @def: MMSCONST_THREAD_SLEEP_MS
 * @brief: Пауза перед проходом всех MMS клиентов основного потока
 * */
#ifndef MMSCONST_THREAD_SLEEP_MS
# define MMSCONST_THREAD_SLEEP_MS 10
#endif

/*
 * @def: MMSCONST_STACK_SIZE
 * @brief: Размер стэка для потока MMS
 * */
#ifndef MMSCONST_STACK_SIZE
# define MMSCONST_STACK_SIZE 0x100000
#endif

/*
 * @def: MMSCONST_MMSVAL_SIZE_MAX
 * @brief: Максимальный размер буфера под значение MMS
 * */
#ifndef MMSCONST_MMSVAL_SIZE_MAX
# define MMSCONST_MMSVAL_SIZE_MAX 128
#endif

/*
 * @def: MMSCONST_MMSREPORT_ELEMCOUNT_MAX
 * @brief: Максимальное число элементов в одном отчете
 * */
#ifndef MMSCONST_MMSREPORT_ELEMCOUNT_MAX
# define MMSCONST_MMSREPORT_ELEMCOUNT_MAX 255
#endif

/*
 * @def: MMSCONST_MMSREPORT_BUFSIZE_MAX
 * @brief: Максимальное число буферизируемых отчетов
 * */
#ifndef MMSCONST_MMSREPORT_BUFSIZE_MAX
# define MMSCONST_MMSREPORT_BUFSIZE_MAX 32
#endif

/*
 * @def: MMSCONST_BITSTRING_NET_ORDER
 * @brief: Порядок следования бит в BitString
 *      сетевой: от старшего бита к младшему + padding (0) до целого байта
 *      прямой: от младшего бита к старшему
 * */
#ifndef MMSCONST_BITSTRING_NET_ORDER
# define MMSCONST_BITSTRING_NET_ORDER 0
#endif



/*
 * @def: MMSClient_Type_t
 * @brief: Тип данных клиента
 * */
typedef enum {
	mmsctDataSet,
	mmsctReport,
	mmsctWrSignal,
	mmsctRdSignal,
	mmsctControl,
} MMSClient_Type_t;



/*
 * @def: MCRes_t
 * @brief: Результат работы функций Setup*
 * */
typedef enum {
    // normal
    mcsrOk = 0,
    mcsrEndReached,
    // err
    mcsrSysErr, // ошибка @ref IedClientError
    mcsrBusy,
    mcsrConnectionBusy,
    mcsrNotConnected,
    mcsrDataErr,
    mcsrInvalidArg,
    mcsrNoLicense,
    mcsrNotSelected,
    mcsrAllRcbBusy,
} MCRes_t;





/*
 * @def: MCCbRes_t
 * @brief: Результат работы функции связанной с callback
 * */
typedef enum {
    // normal
	mccrReadOk = 500,
    mccrWriteOk,
    mccrChgState,
    mccrEventLose,
    // err
	mccrReadFail,
	mccrWriteFail,
    mccrNoMem,
} MCCbRes_t;



typedef struct MMSClient_s MMSClient_t;
typedef struct MMSClientData_s MMSClientData_t;



typedef void (*mms_client_data_callback)(MCCbRes_t result, MMSClientData_t *data, void *user);
typedef void (*mms_client_callback)(MCCbRes_t result, MMSClient_t *client, void *user);



/*
 * @def: MMSVal_Type_t
 * @brief: Тип MMS данных
 * */
typedef enum {
    mmsvtnDef,
    mmsvtBool,
    mmsvtInt,
    mmsvtUInt,
    mmsvtReal,
    mmsvtLReal,
    mmsvtString,
    mmsvtVString,
    mmsvtBitString,
    mmsvtOctString,
    mmsvtUTC,
    mmsvtBinTime,
    mmsvtAccessErr,
    mmsvtArray,
    mmsvtStruct,
} MMSVal_Type_t;



/*
 * @def: MMSVal_t
 * @brief: MMS значение
 * */
typedef struct {
    MMSVal_Type_t type;
    int size; // size in bytes
    union {
        bool Boolean;
        int32_t Int32;
        int64_t Int64;
        uint32_t UInt32;
        uint64_t UInt64;
        float Real;
        double LReal;
        unsigned char raw[MMSCONST_MMSVAL_SIZE_MAX];
    } value;
} MMSVal_t;



/*
 * @def: MMSValPtr_t
 * @brief: Указатель на MMS значение
 * */
typedef struct {
    void *ptr;      // указатель на системное MMS значение
    void *userData; // пользовательский указатель
    MMSVal_t data;   // данные MMS
} MMSValPtr_t;



typedef struct {
    // триггер отчета
    union {
        struct {
            uint8_t dataChg : 1;       // по изменению данных
            uint8_t qualityChg : 1;    // по изменению качества
            uint8_t dataUpd : 1;       // по обновлению данных
            uint8_t intgr : 1;         // периодически
            uint8_t gi : 1;            // по запросу
            uint8_t padding : 2;       // всегда = 0!
            uint8_t trans : 1;         //
        };
        uint8_t all;
    } trgops;
    // общие параметры
    bool ena;                           // подписка на отчеты
    uint32_t bufTm;                     // время до отправки отчета (мс)
    uint32_t intgrTm;                   // период выдачи отчетов (мс)
    bool gi;                            // запрос по gi
    bool purgeBuf;                      // флаг прочистки отчетов
    uint64_t entryId;                   // идентификация записи
    int optFileds;                      // опциональные поля отчетов

    bool _force;                        // передать и без изменений
} MMSRcbParam_t;



/*
 * @fn: MMSClient_Add
 * @brief: Добавление клиента в список
 * NOTE:
 *      - если *head = 0, то заголовоком списка станет новый клиент
 * @param head - заголовок списка
 * return указатель на нового клиента
 * */
extern MMSClient_t *MMSClient_Add(MMSClient_t **head);
/*
 * @fn: MMSClientData_Add
 * @brief: Добавление данных клиента в список клиента
 * NOTE:
 *      - если *head = 0, то заголовоком списка станет новый клиент
 *      - заголовок будет помещен в dataList клиента
 *      - клиент будет помещен в client данных
 * @param client - mms клиент
 * @param head - заголовок списка данных клиента
 * return указатель на новые данные клиента
 * */
extern MMSClientData_t *MMSClientData_Add(MMSClient_t *client, MMSClientData_t **head);

/*
 * @fn: MMSClient_Next
 * @brief: Получение следующего клиента в списке
 * @param client - элемент списка
 * return указатель на следующего клиента
 * */
extern MMSClient_t *MMSClient_Next(MMSClient_t *client);
/*
 * @fn: MMSClient_GetData
 * @brief: Получение данных клиента
 * @param data - клиент
 * return client клиента MMS
 * */
extern MMSClientData_t *MMSClient_GetData(MMSClient_t *client);
/*
 * @fn: MMSClient_NextData
 * @brief: Получение следующих данных клиента в списке
 * @param data - элемент списка
 * return указатель на следующие данные клиента
 * */
extern MMSClientData_t *MMSClient_NextData(MMSClientData_t *data);
/*
 * @fn: MMSClient_GetClient
 * @brief: Получение клиента
 * @param data - данные клиента
 * return клиент MMS
 * */
extern MMSClient_t *MMSClient_GetClient(MMSClientData_t *data);
/*
 * @fn: MMSClient_GetType
 * @brief: Получение типа клиента
 * @param data - данные клиента
 * return тип клиента
 * */
extern MMSClient_Type_t MMSClient_GetType(MMSClientData_t *data);

/*
 * @fn: MMSClientData_SetupParams
 * @brief: Установка параметров данных
 * @param data - данные клиента
 * @param type - тип данных
 * @param logicPath - полная ссылка на объект
 *      ("domainID/itemID", через точку, с указанием FC)
 * no return
 * */
extern void MMSClientData_SetupParams(MMSClientData_t *data, MMSClient_Type_t type,
        char *logicPath);
/*
 * @fn: MMSClient_SetupParams
 * @brief: Установка параметров клиента
 * @param client - клиент
 * @param id - идентификатор клиента
 * @param myIp - адрес клиента
 * @param servIp - адрес сервера
 * @param servPort - порт сервера
 * @param authtype - тип аутентификации
 * @param authdata - данные аутентификации (пароль/сертификат)
 * no return
 * */
extern void MMSClient_SetupParams(MMSClient_t *client, int id, char *myIp, char *servIp, int servPort,
        AcseAuthenticationMechanism authtype, char *authdata);
/*
 * @fn: MMSClient_SetupConnectionParams
 * @brief: Установка сетевых параметров клиента
 * @param client - клиент
 * @param connectTimeout - таймаут тройного рукопожатия
 * @param KAidle - таймаут до отправки keepalive
 * @param KAcnt - число кадров keepalive
 * @param KAinterval - интервал отправки keepalive
 * no return
 * */
extern void MMSClient_SetupConnectionParams(MMSClient_t *client, int connectTimeout,
        int KAidle, int KAcnt, int KAinterval);
/*
 * @fn: MMSClient_SetupThreadParams
 * @brief: Установка параметров потоков
 * @param client - клиент
 * @param cntOp - число операция без задержки
 * @param sleepTime - время задержки после отработки операций клиентом в мс
 * no return
 * */
extern void MMSClient_SetupThreadParams(MMSClient_t *client, int cntOp, int sleepTime);

/*
 * @fn: MMSClient_Start
 * @brief: Запуск работы MMS клиентов
 * @param client - первый элемент списка всех клиентов
 * no return
 * */
extern void MMSClient_Start(MMSClient_t *list_head);

/*
 * @fn: MMSClient_SetLicense
 * @brief: Установка ицензии на использование клиента
 * @param client - первый элемент списка клиентов
 * @param license - наличие лицензии (0 - нет, 1 - есть)
 * no return
 * */
extern void MMSClient_SetLicense(MMSClient_t *client, int license);
/*
 * @fn: MMSClient_GetConnectState
 * @brief: Получение статуса соединения
 * @param client - клиент
 * return   1 - соединение с сервером установлено
 *          0 - соединение с сервером отсутствует
 * */
extern int MMSClient_GetConnectState(MMSClient_t *client);

/*
 * @fn: MMSClient_Free
 * @brief: Завершение работы с mms
 * @param list_head - указатель на первый элемент списка клиентов
 * */
extern void MMSClient_Free(MMSClient_t **list_head);



extern const char *MCRes_to_String(MCRes_t res);
extern const char *SysErr_to_String(IedClientError err);
extern const char *MCCbRes_to_String(MCCbRes_t res);



#endif /* FB_IEC850MMSM_H */
