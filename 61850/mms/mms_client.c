/*******************************************************************************
  * @file    mms_client.c
  * @author  Перминов Р.И.
  * @version v 1
  * @date    12.02.2020
  *****************************************************************************/

#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <stdlib.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <errno.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <stdarg.h>
#include <semaphore.h>

#include <libiec61850/mms_client_connection.h>
#include <libiec61850/iec61850_client.h>

#include "ber_integer.h"
#include "mms_client.h"

#include "mms_utils.h"


/*
Политика использования потоков:
    Все клиенты используют один поток. Любое воздействие пользователя методами Setup
    приводит к блокировке работы всех клиентов
*/
#define MMSCLIENT_ONE_THREAD 0

/*
Политика использования потоков:
    Каждый клиент использует свой поток. Любое воздействие пользователя методами Setup
    приводит к блокировке работы только этого клиента
*/
#define MMSCLIENT_EACH_CLIENT_THREAD 0

/*
Политика использования потоков:
    Несколько клиентов используют один поток. Любое воздействие пользователя методами Setup
    приводит к блокировке работы только закрепленных за этим потоком клиентов
*/
#define MMSCLIENT_SHARED_THREADS 1
// #define MMSCLIENT_SHARED_PER_THREAD 7
#define MMSCLIENT_SHARED_THREADS_MAX 4

#if (!MMSCLIENT_ONE_THREAD && !MMSCLIENT_EACH_CLIENT_THREAD && !MMSCLIENT_SHARED_THREADS)
#error "Thread politic must be defined"
#endif

#define RCBINDECES_MAX 32

#define ASYNCSETUP_MAX  (OUTSTANDING_CALLS/2 - 1)


#ifdef TEST
typedef unsigned char    u8;
typedef unsigned short     u16;
typedef unsigned int     u32;
#define TM_NOTE            1
#define TM_WARN            2
#define TM_ERROR        4
#define TM_DUMPRX        8
#define TM_DUMPTX        16
#define TM_TRACE        32
#define TM_USER            64
#define TM_SERV            128
# define mcl_debug(m,...) printf("mcl: " m "\n",##__VA_ARGS__)
# define mcl_log(t,m,...) printf("mcl: " m "\n",##__VA_ARGS__)
#else
# include "logserv.h"
# define mcl_debug(m,...) printf("mcl: " m "\n",##__VA_ARGS__)
# define mcl_log(t,m,...) log_write(t,"mcl: " m,##__VA_ARGS__)
#endif



typedef struct Report_s Report_t;



/*
 * @def: AsyncOp_t
 * @brief: Тип асинхронной операции
 * */
typedef enum {
    asopNDef,

    readDataSetValues,
    writeObject,
    readObject,

    rcbParsing,
    rcbGetting,
    rcbSetup,

    getSpecCtl,
    reqCtl,
    selectCtl,
    selectValCtl,
    operateCtl,
    cancelCtl,
} AsyncOp_t;



/*
 * @def: Report_s
 * @brief: Отчет MMS
 * */
struct Report_s {
    // must be first
    Report_t *next;

    uint64_t entryId;
    MmsValue *data; // report data
    int repSz; // report size
    uint8_t incIdx[MMSCONST_MMSREPORT_ELEMCOUNT_MAX]; // indeces of included elements of dataset report
    int incSz; // incIdx size
    int lastIdx; // index for reading by inclusion flags
};



/*
 * @def: LoopPrm_t
 * @brief: Вспомогательная структура для потоков
 * */
typedef struct {
    pthread_t pth;
    pthread_mutex_t mu;
    bool isRunning;
} LoopPrm_t;



/*
 * @def: SharedLoopPrm_t
 * @brief: Структура для работы разделяемых потоков
 * */
typedef struct SharedLoopPrm_s SharedLoopPrm_t;
struct SharedLoopPrm_s {
    LoopPrm_t prm;              // thread params
    MMSClient_t *clients;       // clients for this thread
    int ccnt;                   // total clients for this thread
    SharedLoopPrm_t *next;      // next thread
};



/*
 * @def: MMSClient_s
 * @brief: Клиент MMS
 * */
struct MMSClient_s {
    // must be first
    MMSClient_t *next;

    // user params
    int id;         // object id
    const char *myIp; // client ip
    const char *ip; // server ip / hostname
    int port;       // server port
    AcseAuthenticationMechanism authtype; // auth type
    const char *authdata; // cert / passw
    int license;
    //
    int reportBufSizeMax;
    // connection params
    int connectTimeout; // handshake timeout
    int tcpKA_idle;     // keepalive idle
    int tcpKA_cnt;      // keepalive retry
    int tcpKA_interval; // keepalive interval
    mms_client_callback cb; // callback for change state
    void *userData;         // user data

    // sys
    IedConnection con;
    AcseAuthenticationParameter auth;
    MMSClientData_t *dataList;
    uint32_t invokeReqID;   // mms sys (unused)
    int dataCnt;        // size of dataList
    int link;           // link state
    // async
    int asyncCnt;       // num of async threads
    queue_t queue;      // queue for async
    bool isConnected;
    int cntOp;          // number of operation without delay
    int sleepTime;      // delay after operations

    #if MMSCLIENT_SHARED_THREADS
        SharedLoopPrm_t *handler;   // working thread params
    #elif MMSCLIENT_EACH_CLIENT_THREAD
        LoopPrm_t loop;             // self-thread params
    #endif
    bool working;     // connect/disconnect from user
};



/*
 * @def: MMSClientData_s
 * @brief: Данные клиента MMS (dataset/repot/signal)
 * */
struct MMSClientData_s {
    // must be first
    MMSClientData_t *next;

    // input user params
    MMSClient_Type_t type;  // type
    const char *logicPath;  // logical ref from LN
    int reportBufCnt;
    int reportBufSize;
    mms_client_data_callback cb; // callback for setup funcs
    void *userData;   // user data

    // sys
    qitem_t qitem;
    MMSClient_t *client;
    uint32_t invokeReqID; // mms sys (unused)
    int isBusy; // флаг выполнения команды
    AsyncOp_t asyncOp; // текущая операция
    // sys data
    union {
        // dataset
        struct {
            ClientDataSet self;
        } dataset;
        // report
        struct {
            char logicPath[128];
            ClientReportControlBlock rcb;
            MMSRcbParam_t params;  // copy of user prms
            MMSRcbParam_t prevParams;  // previous prms
            struct {
                uint8_t size;
                uint8_t self[RCBINDECES_MAX]; // rcb indeces: 101, 102, etc
                uint8_t curIdx;
            } rcbIndeces;
            Report_t *list; // buffered reports list
            int bufferedCnt;  // size of list
            Report_t *current; // current selected report
            int loseCnt; // lose report counter
            uint8_t tryingSetupRcb; // flag: roll over rcbIndeces
            MmsValue *entryId;
            uint8_t reportHandlerInstalled;
        } rpt;
        // control
        struct {
            MmsValue *model; // ctlModel
            MmsVariableSpecification *varSpec; // ctlVarSpecification
            ControlObjectClient obj; // ctlObj
            uint64_t operTime; // operate time to implement
            int initDone; // init state
            bool useConstantT;
            bool interlockCheck;
            bool synchroCheck;
        } ctl;
    };
    // common
    pthread_mutex_t lmu; // mutex list head
    struct {
        MCRes_t result;
        IedClientError sysErr;
    } lastOperation; // results of the last func call
    union {
        // dataset & report
        struct {
            MmsValue *currDataSet;
            MmsValue *lastVal;
        };
        // read & write & control signal
        MmsValue *actualValue;
    };
};



/*
 * @def: libMmsValue
 * @brief: Копия структуры MmsValue из библиотеки libiec61850
 *      для прямого доступа к полям
 * */
typedef struct ATTRIBUTE_PACKED {
    MmsType type;
    uint8_t deleteValue;
    union uMmsValue {
        MmsDataAccessError dataAccessError;
        struct {
            int size;
            MmsValue** components;
        } structure;
        bool boolean;
        Asn1PrimitiveValue* integer;
        struct {
            uint8_t exponentWidth;
            uint8_t formatWidth; /* number of bits - either 32 or 64)  */
            uint8_t* buf;
        } floatingPoint;
        struct {
            uint16_t size;
            uint16_t maxSize;
            uint8_t* buf;
        } octetString;
        struct {
            int size;     /* Number of bits */
            uint8_t* buf;
        } bitString;
        struct {
            char* buf;
            int16_t size; /* size of the string, equals the amount of allocated memory - 1 */
        } visibleString;
        uint8_t utcTime[8];
        struct {
            uint8_t size;
            uint8_t buf[6];
        } binaryTime;
    } value;
    MmsValue *parent;
    int idx;
} libMmsValue;



typedef struct {
    /* заголовок списка клиентов, устанавливается пользователем
     *      @ref MMSClient_Start; обход для поиска клиента
     *      @ref MMSClient_FindById */
    MMSClient_t *list;
    #if MMSCLIENT_ONE_THREAD
        LoopPrm_t loop;
    #elif MMSCLIENT_SHARED_THREADS
        SharedLoopPrm_t *loop;
    #endif
} MMSClientPriv_t;



static const uint8_t BitReverseTable256[];
static char *SplitObjRefWithFC(const char *objRef, char *buf, FunctionalConstraint *fc);
static void FreeReport(Report_t *report);
static MCRes_t SetupAsync(MMSClientData_t *data, IedClientError *err);
static MCRes_t ReplaceAsync(MMSClientData_t *data, IedClientError *err);
static MCRes_t NextAsync(MMSClient_t *client, IedClientError *err);
static MMSVal_Type_t ConvertType(MmsType type);



/*
 * @def: priv
 * @brief: Данные для работы потока клиентов
 * */
static MMSClientPriv_t *priv;



/*
 * @def: SetResult
 * @brief: установка результата работы функции над данными клиента
 * */
#define SetResult(d,r,e) ({                 \
    d->lastOperation.result = (MCRes_t)r;   \
    d->lastOperation.sysErr = (IedClientError)e;   \
    d->lastOperation.result;                \
})



// MAIN LOOP **********************************************************************************
// ********************************************************************************************


#if MMSCLIENT_ONE_THREAD

static inline void MainLoopLock(void __attribute__((unused)) *arg)
{
    pthread_mutex_lock(&priv->loop.mu);
}
static inline void MainLoopUnlock(void __attribute__((unused)) *arg)
{
    pthread_mutex_unlock(&priv->loop.mu);
}



/*
 * @fn: MainLoop
 * @brief: основной поток: обработка соединения клиентов
 * no return
 * */
static void *MainLoop(void *p)
{
    MmsConnection mmsConnection;
    bool nowait; // выполнение обработки тика без ожидания
    int cnt; // число обработок без ожидания

    prctl(PR_SET_NAME, "mms_task");

    mcl_log(TM_NOTE, "Main loop started");

    while (priv->loop.isRunning) {
        for (MMSClient_t *c = priv->list; c; c = c->next) {
            if (c->working) {
                mmsConnection = IedConnection_getMmsConnection(c->con);
                if (mmsConnection) {
                    nowait = true;
                    cnt = 0;
                    MainLoopLock(0);
                    if (c->working) {
                        while (nowait) {
                            nowait = !MmsConnection_tick(mmsConnection);
                            cnt++;
                            if (cnt >= c->cntOp)
                                break;
                        }
                    }
                    MainLoopUnlock(0);
                }
            }
        }
        usleep(MMSCONST_THREAD_SLEEP_MS*1000);
    }

    mcl_log(TM_NOTE, "Main loop done");

    pthread_exit(0);
}



/*
 * @fn: MainLoopCreate
 * @brief: создание основного потока
 * no return
 * */
static void MainLoopCreate()
{
    pthread_attr_t attr;

    pthread_mutex_init(&priv->loop.mu, 0);

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, MMSCONST_STACK_SIZE);
    pthread_create(&priv->loop.pth, &attr, MainLoop, 0);
}



/*
 * @fn: MainLoopDestroy
 * @brief: завершение основного потока
 * no return
 * */
static void MainLoopDestroy()
{
    if (priv) {
        priv->loop.isRunning = false;
        pthread_join(priv->loop.pth, 0);
        pthread_mutex_destroy(&priv->loop.mu);
    }
}

#elif MMSCLIENT_SHARED_THREADS

static inline void MainLoopLock(MMSClient_t *client)
{
    SharedLoopPrm_t *shared = client->handler;
    if (shared)
        pthread_mutex_lock(&shared->prm.mu);
}
static inline void MainLoopUnlock(MMSClient_t *client)
{
    SharedLoopPrm_t *shared = client->handler;
    if (shared)
        pthread_mutex_unlock(&shared->prm.mu);
}



static inline int GetSleepTime(MMSClient_t *head, int cnt)
{
    int ret = 0;
    int i = 0;
    for (MMSClient_t *c = head; c; c = c->next, ++i) {
        if (cnt > 0 && i >= cnt)
            break;
        ret += (c->sleepTime > 0)? c->sleepTime : 0;
    }
    return (i)? ret / i : 0;
}



/*
 * @fn: MainLoop
 * @brief: основной поток: обработка соединения клиентов
 * no return
 * */
static void *MainLoop(void *p)
{
    SharedLoopPrm_t *shared = (SharedLoopPrm_t *)p;
    LoopPrm_t *prm = &shared->prm;

    prctl(PR_SET_NAME, "mms_task");

    MmsConnection mmsConnection;
    bool nowait; // выполнение обработки тика без ожидания
    int cnt; // число обработок без ожидания
    const int sleeptime = GetSleepTime(shared->clients, shared->ccnt)*1000;

    while (!prm->isRunning)
        usleep(10000);

    mcl_debug("MainLoop running (%d) [%d]", shared->ccnt, shared->clients->id);

    while (prm->isRunning) {
        int  i = 0;
        for (MMSClient_t *c = shared->clients; i < shared->ccnt; c = c->next, ++i) {
            if (c->working) {
                mmsConnection = IedConnection_getMmsConnection(c->con);
                if (mmsConnection) {
                    nowait = true;
                    cnt = 0;
                    MainLoopLock(c);
                    if (c->working) {
                        while (nowait) {
                            nowait = !MmsConnection_tick(mmsConnection);
                            cnt++;
                            if (cnt >= c->cntOp)
                                break;
                        }
                    }
                    MainLoopUnlock(c);
                    if (cnt > 4)
                        usleep(c->sleepTime*1000);
                }
            }
        }
        usleep(sleeptime);
    }

    pthread_exit(0);
}



/*
 * @fn: MainLoopCreate
 * @brief: создание основного потока
 * no return
 * */
static SharedLoopPrm_t *MainLoopCreate(MMSClient_t *head)
{
    if (!head)
        return 0;

    pthread_attr_t attr;

    SharedLoopPrm_t *shared = (SharedLoopPrm_t *)calloc(1, sizeof(SharedLoopPrm_t));
    pthread_mutex_init(&shared->prm.mu, 0);

    shared->clients = head;

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, MMSCONST_STACK_SIZE/8);
    pthread_create(&shared->prm.pth, &attr, MainLoop, shared);
    return shared;
}



/*
 * @fn: MainLoopDestroy
 * @brief: завершение основного потока
 * no return
 * */
static void MainLoopDestroy(SharedLoopPrm_t *shared)
{
    if (shared) {
        shared->prm.isRunning = false;
        pthread_join(shared->prm.pth, 0);
        pthread_mutex_destroy(&shared->prm.mu);
        free(shared);
    }
}



#elif MMSCLIENT_EACH_CLIENT_THREAD

static inline void MainLoopLock(MMSClient_t *client)
{
    pthread_mutex_lock(&client->loop.mu);
}
static inline void MainLoopUnlock(MMSClient_t *client)
{
    pthread_mutex_unlock(&client->loop.mu);
}



/*
 * @fn: MainLoop
 * @brief: основной поток: обработка соединения клиентов
 * no return
 * */
static void *MainLoop(void *p)
{
    MMSClient_t *client = (MMSClient_t *)p;
    MmsConnection mmsConnection;
    bool nowait; // выполнение обработки тика без ожидания
    int cnt; // число обработок без ожидания
    const int sleeptime = client->sleepTime*5*1000;

    prctl(PR_SET_NAME, "mms_task");

    while (client->loop.isRunning) {
        if (client->working) {
            mmsConnection = IedConnection_getMmsConnection(client->con);
            if (mmsConnection) {
                nowait = true;
                cnt = 0;
                MainLoopLock(client);
                if (client->working) {
                    while (nowait) {
                        nowait = !MmsConnection_tick(mmsConnection);
                        cnt++;
                        if (cnt >= c->cntOp)
                            break;
                    }
                }
                MainLoopUnlock(client);
            }
        }
        usleep(sleeptime);
    }
    pthread_exit(0);
}



/*
 * @fn: MainLoopCreate
 * @brief: создание основного потока
 * no return
 * */
static void MainLoopCreate(MMSClient_t *client)
{
    if (client) {
        pthread_attr_t attr;
        pthread_mutex_init(&client->loop.mu, 0);

        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, MMSCONST_STACK_SIZE/8); // reduced stack
        pthread_create(&client->loop.pth, &attr, MainLoop, client);
    }
}



/*
 * @fn: MainLoopDestroy
 * @brief: завершение основного потока
 * no return
 * */
static void MainLoopDestroy(MMSClient_t *client)
{
    if (client) {
        client->loop.isRunning = false;
        pthread_join(client->loop.pth, 0);
        pthread_mutex_destroy(&client->loop.mu);
    }
}
#endif // MMSCLIENT_ONE_THREAD



// USER SUPPORT *******************************************************************************
// ********************************************************************************************



#define MMSClient_GetNextMMSValue(v) ((MmsValue *)GetNextNode(v))



/*
 * @fn: MMSValue_to_MMSVal
 * @brief: преобразование библиотечного mms значения к пользовательскому виду
 * NOTE:
 *      - MMS_INTEGER / MMS_UNSIGNED размер 4 или 8;
 *      - MMS_BIT_STRING размер в битах, порядок определяется @ref MMSCONST_BITSTRING_NET_ORDER
 *          прямой: с младшего бита к старшему
 *          сетевой: от старшего к младшему + padding (0) до целого байта
 *      - MMS_ARRAY / MMS_STRUCTURE устанавливается тип и записывается sizeof(size_t)
 *          байт адреса в dest->value.raw
 * @param src
 * @param dest
 * return
 *      src, если успешно
 *      0, если ошибка
 * */
static MmsValue *MMSValue_to_MMSVal(MmsValue *src, MMSVal_t *dest)
{
    MmsType type = MmsValue_getType(src);
    libMmsValue *lVal = (libMmsValue *)(src);
    dest->type = mmsvtnDef;

    memset(dest, 0, sizeof(MMSVal_t));

    switch (type) {
        case MMS_BOOLEAN: {
            dest->type = mmsvtBool;
            dest->size = 1;
            memcpy(dest->value.raw, &lVal->value.boolean, dest->size);
        } break;
        case MMS_INTEGER: {
            dest->type = mmsvtInt;
            dest->size = lVal->value.integer->size;
            if (dest->size <= 4) {
                BerInteger_toInt32(lVal->value.integer, (void*)dest->value.raw);
            } else {
                BerInteger_toInt64(lVal->value.integer, (void*)dest->value.raw);
            }
        } break;
        case MMS_UNSIGNED: {
            dest->type = mmsvtUInt;
            dest->size = lVal->value.integer->size;
            if (dest->size <= 4) {
                BerInteger_toUint32(lVal->value.integer, (void*)dest->value.raw);
            } else {
                BerInteger_toUint64(lVal->value.integer, (void*)dest->value.raw);
            }
        } break;
        case MMS_FLOAT: {
            dest->size = lVal->value.floatingPoint.formatWidth/8;
            memcpy(dest->value.raw, lVal->value.floatingPoint.buf, dest->size);
            if (dest->size == 4) {
                dest->type = mmsvtReal;
            } else {
                dest->type = mmsvtLReal;
            }
        } break;
        case MMS_STRING: {
            if (lVal->value.visibleString.size < MMSCONST_MMSVAL_SIZE_MAX) {
                dest->type = mmsvtString;
                dest->size = (int)lVal->value.visibleString.size;
                memcpy(dest->value.raw, lVal->value.visibleString.buf, dest->size);
            } else {
                return 0;
            }
        } break;
        case MMS_VISIBLE_STRING: {
            if (lVal->value.visibleString.size < MMSCONST_MMSVAL_SIZE_MAX) {
                dest->type = mmsvtVString;
                dest->size = (int)lVal->value.visibleString.size;
                memcpy(dest->value.raw, lVal->value.visibleString.buf, dest->size);
            } else {
                return 0;
            }
        } break;
        case MMS_BIT_STRING: {
            const uint8_t *bt = BitReverseTable256;
            int sz = lVal->value.bitString.size/8;
            uint8_t rem = (uint8_t)(lVal->value.bitString.size-8*sz);
            int sizebyte = sz + ((rem > 1)? 1:0);

            if (sizebyte < MMSCONST_MMSVAL_SIZE_MAX) {
                dest->size = lVal->value.bitString.size;
                dest->type = mmsvtBitString;
                if (MMSCONST_BITSTRING_NET_ORDER) {
                    for (int i = 0; i < sizebyte; ++i) {
                        dest->value.raw[i] = lVal->value.bitString.buf[i];
                    }
                } else {
                    for (int i = 0; i < sizebyte; ++i) {
                        dest->value.raw[i] = bt[ lVal->value.bitString.buf[i] ];
                    }
                }
            } else {
                return 0;
            }
        } break;
        case MMS_OCTET_STRING: {
            if (lVal->value.octetString.size < MMSCONST_MMSVAL_SIZE_MAX) {
                dest->type = mmsvtOctString;
                dest->size = (int)lVal->value.octetString.size;
                memcpy(dest->value.raw, lVal->value.octetString.buf, dest->size);
            } else {
                return 0;
            }
        } break;
        case MMS_UTC_TIME: {
            dest->type = mmsvtUTC;
            dest->size = 8;
            uint64_t time = MmsValue_getUtcTimeInMs(src);
            memcpy(dest->value.raw, &time, dest->size);
        } break;
        case MMS_BINARY_TIME: {
            dest->type = mmsvtBinTime;
            dest->size = 8;
            uint64_t time = MmsValue_getBinaryTimeAsUtcMs(src);
            memcpy(dest->value.raw, &time, dest->size);
        } break;
        case MMS_DATA_ACCESS_ERROR: {
            dest->type = mmsvtAccessErr;
            dest->size = 1;
            memcpy(dest->value.raw, &lVal->value.dataAccessError, dest->size);
        } break;
        case MMS_ARRAY:
        case MMS_STRUCTURE: {
            dest->type = (type == MMS_ARRAY)?
                    mmsvtArray : mmsvtStruct;
            dest->size = lVal->value.structure.size;
            // save struct/array mem address
            size_t addr = (size_t)src;
            memcpy(dest->value.raw, &addr, sizeof(size_t));
        } break;
        case MMS_GENERALIZED_TIME:
        case MMS_BCD:
        case MMS_OBJ_ID:
        default: {
            return 0;
        } break;
    }

    return src;
}



/*
 * @fn: MMSValue_isSuitableSize
 * @brief:
 * @param val
 * @param size
 * return
 * */
static bool MMSValue_isSuitableSize(MmsValue *val, int size)
{
    MmsType type = MmsValue_getType(val);
    libMmsValue *lVal = (libMmsValue *)(val);

    switch (type) {
        case MMS_DATA_ACCESS_ERROR:
        case MMS_UTC_TIME:
        case MMS_BINARY_TIME:
        case MMS_BOOLEAN: {
            return true;
        } break;
        case MMS_INTEGER:
        case MMS_UNSIGNED: {
            if (size <= lVal->value.integer->maxSize)
                return true;
        } break;
        case MMS_FLOAT: {
            if (size <= (lVal->value.floatingPoint.formatWidth/8))
                return true;
        } break;
        case MMS_STRING:
        case MMS_VISIBLE_STRING: {
            if (size <= lVal->value.visibleString.size)
                return true;
        } break;
        case MMS_BIT_STRING: {
            if (size <= lVal->value.bitString.size)
                return true;
        } break;
        case MMS_OCTET_STRING: {
            if (size <= lVal->value.octetString.size)
                return true;
        } break;
        case MMS_ARRAY:
        case MMS_STRUCTURE: {
            if (size <= lVal->value.structure.size)
                return true;
        } break;
        case MMS_GENERALIZED_TIME:
        case MMS_BCD:
        case MMS_OBJ_ID:
        default: { /* NOP */ } break;
    }
    return false;
}



/*
 * @fn: MMSVal_to_MMSValue
 * @brief: обратная функция к @ref MMSValue_to_MMSVal
 * NOTE:
 *      - MMS_ARRAY / MMS_STRUCTURE указатель на структуру должен быть уложен в
 *          sizeof(size_t) байт адреса в src->value.raw
 * @param src
 * @param oldVal
 * @param createNew - создавать новое значение, если размер превышает выделенную память
 * return
 *      указатель на новое MmsValue, если oldVal == 0 или было созданное новое значение в соответствии с флагом createNew
 *      указатель на oldVal
 * */
static MmsValue *MMSVal_to_MMSValue(MMSVal_t *src, MmsValue *oldVal, bool createNew)
{
    void *val = (void *)src->value.raw;

    if ( createNew && oldVal && !MMSValue_isSuitableSize(oldVal, src->size) )
        oldVal = 0;

    switch (src->type) {
        case mmsvtBool: {
            if (!oldVal)
                oldVal = MmsValue_newBoolean(0);
            if (oldVal)
                MmsValue_setBoolean(oldVal, *(bool*)val);
        } break;
        case mmsvtInt: {
            if (src->size <= 4) {
                if (!oldVal)
                    oldVal = MmsValue_newIntegerFromInt32(0);
                if (oldVal)
                    MmsValue_setInt32(oldVal, *(int32_t*)val);
            } else {
                if (!oldVal)
                    oldVal = MmsValue_newIntegerFromInt64(0);
                if (oldVal)
                    MmsValue_setInt64(oldVal, *(int64_t*)val);
            }
        } break;
        case mmsvtUInt: {
            if (src->size <= 4) {
                if (!oldVal)
                    oldVal = MmsValue_newUnsignedFromUint32(0);
                if (oldVal)
                    MmsValue_setUint32(oldVal, *(uint32_t*)val);
            } else {
                if (!oldVal)
                    oldVal = MmsValue_newUnsignedFromUint64(0);
                if (oldVal)
                    MmsValue_setUint64(oldVal, *(uint64_t*)val);
            }
        } break;
        case mmsvtReal: {
            if (!oldVal)
                oldVal = MmsValue_newFloat(0);
            if (oldVal)
                MmsValue_setFloat(oldVal, *(float*)val);
        } break;
        case mmsvtLReal: {
            if (!oldVal)
                oldVal = MmsValue_newDouble(0);
            if (oldVal)
                MmsValue_setDouble(oldVal, *(double*)val);
        } break;
        case mmsvtString: {
            if (!oldVal)
                oldVal = MmsValue_newMmsStringWithSize(MMSCONST_MMSVAL_SIZE_MAX);
            if (oldVal)
                MmsValue_setMmsString(oldVal, (char*)val);
        } break;
        case mmsvtVString: {
            if (!oldVal)
                oldVal = MmsValue_newVisibleStringWithSize(MMSCONST_MMSVAL_SIZE_MAX);
            if (oldVal)
                MmsValue_setVisibleString(oldVal, (char*)val);
        } break;
        case mmsvtBitString: {
            const uint8_t *bt = BitReverseTable256;
            uint8_t buf[MMSCONST_MMSVAL_SIZE_MAX];
            int sz = src->size/8;
            uint8_t rem = (uint8_t)(src->size-8*sz);
            int sizebyte = sz + ((rem > 1)? 1:0);
            if (MMSCONST_BITSTRING_NET_ORDER) {
                for (int i = 0; i < sizebyte; ++i) {
                    buf[i] = ((uint8_t*)val)[i];
                }
            } else {
                for (int i = 0; i < sizebyte; ++i) {
                    buf[i] = bt[ ((uint8_t*)val)[i] ];
                }
            }
            if (!oldVal)
                oldVal = MmsValue_newBitString(MMSCONST_MMSVAL_SIZE_MAX*8);
            if (oldVal) {
                libMmsValue *lVal = (libMmsValue *)(oldVal);
                lVal->value.bitString.size = src->size;
                memcpy(lVal->value.bitString.buf, buf, sizebyte);
            }
        } break;
        case mmsvtOctString: {
            if (!oldVal)
                oldVal = MmsValue_newOctetString(src->size, MMSCONST_MMSVAL_SIZE_MAX);
            if (oldVal)
                MmsValue_setOctetString(oldVal, (uint8_t*)val, src->size);
        } break;
        case mmsvtUTC: {
            if (!oldVal)
                oldVal = MmsValue_newUtcTimeByMsTime(0);
            if (oldVal)
                MmsValue_setUtcTimeMs(oldVal, *(uint64_t*)val);
        } break;
        case mmsvtBinTime: {
            if (!oldVal)
                oldVal = MmsValue_newBinaryTime(false);
            if (oldVal)
                MmsValue_setBinaryTime(oldVal, *(uint64_t*)val);
        } break;
        case mmsvtAccessErr: {
            if (!oldVal)
                oldVal = MmsValue_newDataAccessError(0);
            if (oldVal) {
                libMmsValue *lVal = (libMmsValue *)(oldVal);
                lVal->value.dataAccessError = *(MmsDataAccessError*)val;
            }
        } break;
        case mmsvtStruct:
        case mmsvtArray: {
            size_t addr;
            memcpy(&addr, src->value.raw, sizeof(size_t));
            if (addr) {
                if (!oldVal)
                    oldVal = MmsValue_clone((MmsValue *)addr);
                if (oldVal)
                    MmsValue_update(oldVal, (MmsValue *)addr);
            }
        } break;
        default: {
            /* NOP */
        } break;
    }
    return oldVal;
}



MCRes_t MMSClient_CreateStruct(int size, MMSVal_t *result)
{
    if (size && result) {
        MmsValue *res = MmsValue_createEmptyStructure(size);
        if (res) {
            MmsValue *tmp = MMSValue_to_MMSVal(res, result);
            return (tmp == res)?
                    mcsrOk : mcsrDataErr;
        }
    }
    return mcsrInvalidArg;
}



MCRes_t MMSClient_RemoveStruct(MMSVal_t *data)
{
    if (data) {
        if (data->type == mmsvtArray || data->type == mmsvtStruct) {
            size_t addr;
            memcpy(&addr, data->value.raw, sizeof(size_t));
            if (addr) {
                MmsValue_delete((MmsValue *)addr);
                return mcsrOk;
            }
        }
    }
    return mcsrInvalidArg;
}



MCRes_t MMSClient_AddStructElem(MMSVal_t *structure, int index, MMSVal_t *val)
{
    if (structure && val) {
        if (structure->type == mmsvtArray || structure->type == mmsvtStruct) {
            size_t addr;
            memcpy(&addr, structure->value.raw, sizeof(size_t));
            if (addr) {
                int strSize = MmsValue_getArraySize((MmsValue *)addr);
                if ((index >= 0) && (index < strSize)) {
                    MmsValue *old;
                    old = MmsValue_getElement((MmsValue *)addr, index);
                    if (!old) {
                        if (val->type == mmsvtArray || val->type == mmsvtStruct) {
                            size_t addr2;
                            memcpy(&addr2, val->value.raw, sizeof(size_t));
                            old = (MmsValue *)addr2;
                        }
                        // если базовое значение (old == 0), то createNew игнорируется => создается новое MmsValue
                        // если структура (old != 0), то только обновление (не клонируется)
                        MmsValue *tmp = MMSVal_to_MMSValue(val, old, false);
                        MmsValue_setElement((MmsValue *)addr, index, tmp);
                        return mcsrOk;
                    }
                }
            }
        }
    }
    return mcsrInvalidArg;
}



MCRes_t MMSClient_GetStructElem(MMSVal_t *data, int index, MMSVal_t *result)
{
    if (data && result) {
        if (data->type == mmsvtArray || data->type == mmsvtStruct) {
            size_t addr;
            memcpy(&addr, data->value.raw, sizeof(size_t));
            if (addr) {
                MmsValue *res = MmsValue_getElement((MmsValue *)addr, index);
                if (res) {
                    MmsValue *tmp = MMSValue_to_MMSVal(res, result);
                    return (tmp == res)?
                            mcsrOk : mcsrDataErr;
                }
            }
        }
    }
    return mcsrInvalidArg;
}



MCRes_t MMSClient_SetStructElem(MMSVal_t *data, int index, MMSVal_t *val)
{
    if (data && val) {
        if (data->type == mmsvtArray || data->type == mmsvtStruct) {
            if (val->type != mmsvtArray && val->type != mmsvtStruct) {
                size_t addr;
                memcpy(&addr, data->value.raw, sizeof(size_t));
                if (addr) {
                    MmsValue *structure = (MmsValue *)addr;
                    MmsValue *res = MmsValue_getElement(structure, index);
                    if (res) {
                        MMSVal_t storage;
                        if (MMSValue_to_MMSVal(res, &storage) != res) {
                            return mcsrDataErr;
                        }
                        if (storage.type == val->type) { // res->type == val->type
                            MmsValue *ret = MMSVal_to_MMSValue(val, res, true); // res != 0
                            if (ret != res) {
                                MmsValue_delete(res);
                                MmsValue_setElement(structure, index, ret);
                            }
                            return mcsrOk;
                        }
                    }
                }
            }
        }
    }
    return mcsrInvalidArg;
}



MCRes_t MMSClient_GetMMSValPtr(MMSVal_t *data, int index, MMSValPtr_t *ptr)
{
    if (data && ptr) {
        if (data->type == mmsvtArray || data->type == mmsvtStruct) {
            size_t addr;
            memcpy(&addr, data->value.raw, sizeof(size_t));
            if (addr) {
                MmsValue *res = MmsValue_getElement((MmsValue *)addr, index);
                if (res) {
                    MMSVal_t storage;
                    if (MMSValue_to_MMSVal(res, &storage) == res) {
                        memcpy(&ptr->data, &storage, sizeof(MMSVal_t));
                        ptr->ptr = res;
                        return mcsrOk;
                    }
                    return mcsrDataErr;
                }
            }
        }
    }
    return mcsrInvalidArg;
}



MCRes_t MMSClient_GetMMSValByPtr(MMSValPtr_t *ptr)
{
    if (ptr && ptr->ptr) {
        MMSVal_t *dest = &ptr->data;
        MmsValue *val = (MmsValue *)ptr->ptr;
        return (MMSValue_to_MMSVal(val, dest) == val)?
                mcsrOk : mcsrDataErr;
    }
    return mcsrInvalidArg;
}



MCRes_t MMSClient_SetMMSValByPtr(MMSValPtr_t *ptr)
{
    if (ptr && ptr->ptr) {
        MMSVal_t *val = &ptr->data;
        if (val->type != mmsvtArray && val->type != mmsvtStruct) {
            MMSVal_t storage;
            MMSValue_to_MMSVal((MmsValue *)ptr->ptr, &storage);
            if (storage.type == val->type) {
                MMSVal_to_MMSValue(val, (MmsValue *)ptr->ptr, false); // ptr->ptr != 0
                return mcsrOk;
            }
        }
    }
    return mcsrInvalidArg;
}



MCRes_t MMSClient_ReadLastObject(MMSClientData_t *data, MMSVal_t *result)
{
    if (data) {
        if (result) {
            if (data->actualValue) {
                MmsValue *tmp = MMSValue_to_MMSVal(data->actualValue, result);
                return SetResult(data, (tmp == data->actualValue)?
                        mcsrOk : mcsrDataErr, 0);
            }
            return SetResult(data, mcsrDataErr, 0);
        }
        return SetResult(data, mcsrInvalidArg, 0);
    }
    return mcsrInvalidArg;
}



MCRes_t MMSClient_SelectNextDataset(MMSClientData_t *data)
{
    if (data) {
        data->lastOperation.result = mcsrInvalidArg;
        data->lastVal = 0;
        switch (data->type) {
            case mmsctReport: {
                Report_t *rep;
                if (data->rpt.current) { // 0 on first and last calls
                    rep = data->rpt.current->next;
                    // remove current report
                    data->rpt.bufferedCnt--;
                    if (data->rpt.current == data->rpt.list) { // move head
                        pthread_mutex_lock(&data->lmu);
                        data->rpt.list = rep;
                        if (data->rpt.bufferedCnt == 0) {
                            data->rpt.list = 0;
                        }
                        pthread_mutex_unlock(&data->lmu);
                    }
                    FreeReport(data->rpt.current);
                } else {
                    rep = data->rpt.list;
                }
                if (!rep || data->rpt.bufferedCnt == 0) {
                    data->rpt.current = 0;
                    return SetResult(data, mcsrEndReached, 0);
                }
                data->rpt.current = rep;
                data->currDataSet = rep->data;
                mcl_log(TM_TRACE, "[%d]{%s} next report (cnt=%d)",
                        data->client->id, data->logicPath, data->rpt.bufferedCnt);
                return SetResult(data, mcsrOk, 0);
            } break;
            case mmsctDataSet: {
                if (data->currDataSet) { // 0 on first call
                    return SetResult(data, mcsrEndReached, 0);
                }
                if (data->dataset.self) {
                    data->currDataSet = ClientDataSet_getValues(data->dataset.self);
                    return SetResult(data, mcsrOk, 0);
                }
            } break;
            default: { /* NOP */ } break;
        }
        return SetResult(data, mcsrInvalidArg, 0);
    }
    return mcsrInvalidArg;
}



MCRes_t MMSClient_GetSelectedDataset(MMSClientData_t *data, MMSVal_t *result)
{
    if (data) {
        if (result) {
            if (!data->currDataSet) {
                return SetResult(data, mcsrNotSelected, 0);
            }
            MmsValue *tmp = MMSValue_to_MMSVal(data->currDataSet, result);
            return SetResult(data, (tmp == data->currDataSet)?
                    mcsrOk : mcsrDataErr, 0);
        }
        return SetResult(data, mcsrInvalidArg, 0);
    }
    return mcsrInvalidArg;
}



MCRes_t MMSClient_GetNextBaseTypeMMSVal(MMSClientData_t *data, MMSVal_t *result)
{
    if (!data)
        return mcsrInvalidArg;

    if (!result)
        return SetResult(data, mcsrInvalidArg, 0);

    if (!data->currDataSet)
        return SetResult(data, mcsrNotSelected, 0);

    MmsValue *mv = data->lastVal;
    if (!mv) {
        switch (data->type) {
            // report usually doesn't include some real existing data
            case mmsctReport: {
                Report_t *rep = data->rpt.current;
                if (rep) {
                    int nextIdx = rep->lastIdx+1; // start from -1
                    rep->lastIdx = -1;
                    if (nextIdx < rep->incSz) {
                        rep->lastIdx = rep->incIdx[nextIdx];
                    }
                    if (rep->lastIdx == -1) {
                        // nothing to read
                        return SetResult(data, mcsrEndReached, 0);
                    } else {
                        mv = MmsValue_getElement(rep->data, rep->lastIdx);
                        // remove parent dependencies for MMSClient_GetNextMMSValue
                        libMmsValue *lVal = (libMmsValue *)mv;
                        lVal->parent = 0;
                        lVal->idx = 0;
                    }
                } else {
                    return SetResult(data, mcsrNotSelected, 0);
                }
            } break;
            default: {
                mv = data->currDataSet;
            } break;
        }
    }

    // extract data
    mv = MMSClient_GetNextMMSValue(mv);
    if (data->lastVal == mv) {
        return SetResult(data, mcsrEndReached, 0);
    }
    data->lastVal = mv;
    if (mv) {
        MmsValue *res = MMSValue_to_MMSVal(mv, result);
        if (res == mv) {
            return SetResult(data, mcsrOk, 0);
        }
    } else {
        switch (data->type) {
            // recall for next report elem
            case mmsctReport: {
                return MMSClient_GetNextBaseTypeMMSVal(data, result);
            } break;
            // all done
            default: {
                return SetResult(data, mcsrEndReached, 0);
            } break;
        }
    }
    return SetResult(data, mcsrDataErr, 0);
}



MCRes_t MMSClient_GetSimpleStruct(MMSVal_t *data, uint8_t *buf, int *bufSz)
{
    if (data && buf) {
        if (data->type == mmsvtArray || data->type == mmsvtStruct) {
            size_t addr;
            memcpy(&addr, data->value.raw, sizeof(size_t));
            if (addr) {
                // remove parent for MMSClient_GetNextMMSValue work
                libMmsValue *lVal = (libMmsValue *)addr;
                MmsValue *parent = lVal->parent;
                int idx = lVal->idx;
                lVal->parent = 0;
                lVal->idx = 0;
                //
                MCRes_t ret = mcsrOk;
                MMSVal_t storage;
                MmsValue *mv = (MmsValue *)addr;
                MmsValue *last = 0;
                int curIdx = 0;
                int maxSize = *bufSz;
                while (1) {
                    mv = MMSClient_GetNextMMSValue(mv);
                    if (mv == last)
                        break; // end reached -> end
                    last = mv;
                    if (mv) {
                        MmsValue *res = MMSValue_to_MMSVal(mv, &storage);
                        if (mv == res) {
                            int tSz = 0;
                            switch (storage.type) {
                                // 1 byte
                                case mmsvtBool:
                                case mmsvtAccessErr: {
                                    tSz = 1;
                                } break;
                                // 4 byte
                                case mmsvtBitString: {
                                    storage.size = storage.size/8 + ((storage.size%8)? 1:0);
                                } // no break;
                                case mmsvtLReal:
                                case mmsvtReal:
                                case mmsvtUInt:
                                case mmsvtInt: {
                                    tSz = 4;
                                } break;
                                // 8 byte
                                case mmsvtUTC:
                                case mmsvtBinTime: {
                                    tSz = 8;
                                } break;
                                // 64 byte
                                case mmsvtString:
                                case mmsvtVString:
                                case mmsvtOctString: {
                                    tSz = 64;
                                } break;
                                default: {
                                    /* NOP */
                                } break;
                            }
                            if (curIdx+tSz > maxSize) {
                                ret = mcsrDataErr;
                                break;
                            }
                            if (storage.size <= tSz) {
                                memcpy(buf+curIdx, storage.value.raw, tSz);
                            } else {
                                memset(buf+curIdx, 0xff, tSz);
                            }
                            curIdx += tSz;
                        } else {
                            ret = mcsrDataErr;
                            break;
                        }
                    } else {
                        break; // end reached -> end
                    }
                }
                // restore parent
                lVal->parent = parent;
                lVal->idx = idx;
                // return result
                *bufSz = curIdx;
                return ret;
            }
        }
    }
    return mcsrInvalidArg;
}



MCRes_t MMSClient_GetReportIncludesIdx(MMSClientData_t *data, uint8_t *buf, int *bufSz)
{
    if (!data)
        return mcsrInvalidArg;

    if (!buf || !bufSz)
        return SetResult(data, mcsrInvalidArg, 0);

    if (data->type != mmsctReport)
        return SetResult(data, mcsrInvalidArg, 0);

    if (!data->rpt.current)
        return SetResult(data, mcsrNotSelected, 0);

    Report_t *rep = data->rpt.current;
    int cpySz = (rep->incSz < *bufSz)?
        rep->incSz : *bufSz;
    memcpy(buf, rep->incIdx, cpySz);
    *bufSz = cpySz;

    return SetResult(data, mcsrOk, 0);
}



int MMSClient_GetReportsCount(MMSClientData_t *data)
{
    return (data)? data->rpt.bufferedCnt : 0;
}



MCRes_t MMSClient_GetReportEntryId(MMSClientData_t *data, uint64_t *result)
{
    if (!data)
        return mcsrInvalidArg;

    if (!result)
        return SetResult(data, mcsrInvalidArg, 0);

    if (data->type != mmsctReport)
        return SetResult(data, mcsrInvalidArg, 0);

    if (!data->rpt.current)
        return SetResult(data, mcsrNotSelected, 0);

    *result = data->rpt.current->entryId;

    return SetResult(data, mcsrOk, 0);
}



MCRes_t MMSClient_GetLastOperationResult(MMSClientData_t *data, IedClientError *sysErr)
{
    if (data && sysErr) {
        *sysErr = data->lastOperation.sysErr;
        return data->lastOperation.result;
    } else {
        return mcsrInvalidArg;
    }
}



MMSClient_Type_t MMSClient_GetType(MMSClientData_t *data)
{
    return (data)? data->type : -1;
}



MCRes_t MMSClient_GetControlType(MMSClientData_t *data, MMSVal_Type_t *result)
{
    if (!data)
        return mcsrInvalidArg;

    if (data->type != mmsctControl)
        return SetResult(data, mcsrInvalidArg, 0);

    if (!data->ctl.initDone)
        return SetResult(data, mcsrInvalidArg, 0);

    if (!data->ctl.obj)
        return SetResult(data, mcsrInvalidArg, 0);

    MmsType type = ControlObjectClient_getCtlValType(data->ctl.obj);
    *result = ConvertType(type);

    return SetResult(data, mcsrOk, 0);
}



MCRes_t MMSClient_SetOriginControl(MMSClientData_t *data, const char* orIdent, int orCat)
{
    if (!data)
        return mcsrInvalidArg;

    if (data->type != mmsctControl)
        return SetResult(data, mcsrInvalidArg, 0);

    if (!data->ctl.initDone)
        return SetResult(data, mcsrInvalidArg, 0);

    if (!data->ctl.obj)
        return SetResult(data, mcsrInvalidArg, 0);

    ControlObjectClient_setOrigin(data->ctl.obj, orIdent, orCat);

    return SetResult(data, mcsrOk, 0);
}



// USER SETTINGS SUPPORT **********************************************************************
// ********************************************************************************************



void MMSClientData_SetupParams(MMSClientData_t *data, MMSClient_Type_t type, char *logicPath)
{
    if (data) {
        data->type = type;
        data->logicPath = logicPath;
    }
}



void MMSClientData_SetupCallback(MMSClientData_t *data, mms_client_data_callback cb, void *userData)
{
    if (data) {
        data->cb = cb;
        data->userData = userData;
    }
}



void MMSClient_SetupParams(MMSClient_t *client, int id, char *myIp, char *servIp, int servPort,
        AcseAuthenticationMechanism authtype, char *authdata)
{
    if (client) {
        client->id = id;
        client->myIp = myIp;
        client->ip = servIp;
        client->port = servPort;
        client->authdata = authdata;
        client->authtype = authtype;
        client->reportBufSizeMax = MMSCONST_MMSREPORT_BUFSIZE_MAX;
    }
}



void MMSClient_SetupConnectionParams(MMSClient_t *client, int connectTimeout,
        int KAidle, int KAcnt, int KAinterval)
{
    if (client) {
        client->connectTimeout = connectTimeout;
        client->tcpKA_idle = KAidle;
        client->tcpKA_cnt = KAcnt;
        client->tcpKA_interval = KAinterval;
    }
}


void MMSClient_SetupThreadParams(MMSClient_t *client, int cntOp, int sleepTime)
{
    if (client) {
        client->cntOp = cntOp;
        client->sleepTime = sleepTime;
    }
}


void MMSClient_SetupCallback(MMSClient_t *client, mms_client_callback cb, void *userData)
{
    if (client) {
        client->cb = cb;
        client->userData = userData;
    }
}



void MMSClient_Start(MMSClient_t *head)
{
    if (!priv)
        priv = (MMSClientPriv_t *)calloc(1, sizeof(MMSClientPriv_t));
    if (!priv) {
        mcl_log(TM_ERROR, "Couldn't create client main loop");
        return;
    }
    if (head) {
        // extra params
        int clients = 0;
        for (MMSClient_t *c = head; c; c = c->next) {
            if (c->cntOp <= 0) c->cntOp = 16;
            if (c->sleepTime <= 0) c->sleepTime = MMSCONST_THREAD_SLEEP_MS;
            clients++;
        }
        #if MMSCLIENT_ONE_THREAD
            if (!priv->loop.isRunning) {
                priv->list = head;
                priv->loop.isRunning = true;
                MainLoopCreate();
            }
        #elif MMSCLIENT_SHARED_THREADS
            priv->list = head;
            int cnt = 0;
            int sharedPerThread = clients/MMSCLIENT_SHARED_THREADS_MAX + 1;
            SharedLoopPrm_t *current = 0;
            for (MMSClient_t *c = head; c; c = c->next) {
                if (cnt == 0) { // new thread
                    SharedLoopPrm_t *newshared = MainLoopCreate(c); // allocate: clients starts from this client
                    if (!newshared) { // fatal
                        mcl_log(TM_ERROR, "Couldn't create(2) client main loop");
                        return;
                    }
                    if (!current) { // head
                        priv->loop = newshared;
                    } else {
                        current->next = newshared;
                    }
                    current = newshared;
                }
                current->ccnt++; // total clients for this thread
                c->handler = current;
                cnt++;
                if (cnt >= sharedPerThread) { // maximum clients per thread reached // MMSCLIENT_SHARED_PER_THREAD
                    cnt = 0; // reset for new thread
                    current->prm.isRunning = true;
                }
            }
            current->prm.isRunning = true;
        #elif MMSCLIENT_EACH_CLIENT_THREAD
            priv->list = head;
            for (MMSClient_t *c = head; c; c = c->next) {
                if (!c->loop.isRunning) {
                    c->loop.isRunning = true;
                    MainLoopCreate(c);
                }
            }
        #endif
    }
}



void MMSClient_SetLicense(MMSClient_t *client, int license)
{
    if (client) {
        client->license = license;
    }
}



MMSClient_t *MMSClient_FindById(int id)
{
    if (priv->list) {
        for (MMSClient_t *c = priv->list; c; c = c->next) {
            if (c->id == id) {
                return c;
            }
        }
    }
    return 0;
}



// CALLBACKS **********************************************************************************
// ********************************************************************************************



static void CallBack_EndEvent(MMSClientData_t *client_data, MCCbRes_t res, IedClientError err)
{
    if (client_data->isBusy) {
        client_data->isBusy = 0;
        client_data->lastOperation.result = (err == IED_ERROR_OK)?
                mcsrOk : mcsrSysErr;
        client_data->lastOperation.sysErr = err;
        if (client_data->cb) {
            client_data->cb(res, client_data, client_data->userData);
        }
        NextAsync(client_data->client, &err);
    }
}



static void CallBack_ContinueEvent(MMSClientData_t *client_data, AsyncOp_t op, MCCbRes_t resIfErr)
{
    IedClientError err = IED_ERROR_OK;
    client_data->asyncOp = op;
    ReplaceAsync(client_data, &err);
    if (err != IED_ERROR_OK) {
        CallBack_EndEvent(client_data, resIfErr, err);
    }
}



static void MMSClient_ReportCB(void *data, ClientReport report)
{
    MCCbRes_t res = mccrReadFail;
    MMSClientData_t *client_data = (MMSClientData_t *)data;
    MMSClient_t __attribute__((unused)) *client = client_data->client;
    {
        res = mccrNoMem;
        if (client_data->rpt.bufferedCnt != client->reportBufSizeMax) {
            Report_t *rep = calloc(1, sizeof(Report_t));
            if (rep) {
                res = mccrReadOk;
                rep->lastIdx = -1; // must be -1 before first call @ref MMSClient_GetNextBaseTypeMMSVal
                rep->data = MmsValue_clone(ClientReport_getDataSetValues(report));
                { // entryId
                    libMmsValue *val = (libMmsValue *)ClientReport_getEntryId(report);
                    if (val) {
                        char *dst = (char*)(&rep->entryId);
                        for (int i = 0, j = 8; i < 8; ++i)
                            dst[--j] = val->value.octetString.buf[i];
                    }
                }
                rep->repSz = (int)MmsValue_getArraySize(rep->data); // sizeof report dataset
                rep->incSz = 0; // real size
                // bckp inclusions
                for (int i = 0; i < rep->repSz; ++i) {
                    if (ClientReport_getReasonForInclusion(report, i))
                        rep->incIdx[rep->incSz++] = (uint8_t)i;
                    if (rep->incSz == MMSCONST_MMSREPORT_ELEMCOUNT_MAX)
                        break;
                }
                pthread_mutex_lock(&client_data->lmu);
                list_add((list_t **)&client_data->rpt.list, (list_t *)rep);
                client_data->rpt.bufferedCnt++;
                pthread_mutex_unlock(&client_data->lmu);
                mcl_log(TM_TRACE, "[%d]{%s} new report (cnt=%d)",
                        client_data->client->id, client_data->logicPath, client_data->rpt.bufferedCnt);
            }
        }
    }
    if (res != mccrReadOk) {
        client_data->rpt.loseCnt++;
    }
    if (client_data->cb) {
        client_data->cb(res, client_data, client_data->userData);
    }
}



static void MMSClient_DataSetCB(uint32_t invokeId, void *data, IedClientError err,
        ClientDataSet dataSet)
{
    MCCbRes_t res = mccrReadFail;
    MMSClientData_t *client_data = (MMSClientData_t *)data;
    if (err == IED_ERROR_OK) {
        client_data->dataset.self = dataSet;
        client_data->currDataSet = 0;
        res = mccrReadOk;
    }
    CallBack_EndEvent(client_data, res, err);
}



static void MMSClient_RcbGettingCB(uint32_t invokeId, void *data, IedClientError err,
        ClientReportControlBlock rcb)
{
    MMSClientData_t *client_data = (MMSClientData_t *)data;
    if (err == IED_ERROR_OK) {
        client_data->rpt.rcb = rcb;
        { // update rcb reference
            char logicPath[256];
            int idx = client_data->rpt.rcbIndeces.curIdx;
            sprintf(logicPath, "%s%d", client_data->rpt.logicPath, client_data->rpt.rcbIndeces.self[idx]);
            ClientReportControlBlock_updateObjectReference(rcb, logicPath);
        }
        if (client_data->rpt.tryingSetupRcb) {
            // try to setup rcb params
            CallBack_ContinueEvent(client_data, rcbSetup, mccrWriteFail);
        } else {
            // without subscribing (only getting rcb)
            CallBack_EndEvent(client_data, mccrReadOk, err);
        }
    } else {
        // reset flags
        client_data->rpt.params._force = 0;
        client_data->rpt.tryingSetupRcb = 0;
        CallBack_EndEvent(client_data, mccrReadFail, err);
    }
}



static void MMSClient_RcbSetupCB(uint32_t invokeId, void *data, IedClientError err)
{
    MMSClientData_t *client_data = (MMSClientData_t *)data;
    if (err == IED_ERROR_OK) {
        client_data->rpt.params._force = 0;
        client_data->rpt.tryingSetupRcb = 0;
        if (!client_data->rpt.reportHandlerInstalled) {
            char logicPath[256];
            int idx = client_data->rpt.rcbIndeces.curIdx;
            sprintf(logicPath, "%s%d", client_data->rpt.logicPath, client_data->rpt.rcbIndeces.self[idx]);
            IedConnection_installReportHandler(client_data->client->con, logicPath,
                        ClientReportControlBlock_getRptId(client_data->rpt.rcb), MMSClient_ReportCB, client_data);
            client_data->rpt.reportHandlerInstalled = 1;
        }
        CallBack_EndEvent(client_data, mccrWriteOk, err);
    } else {
        client_data->rpt.rcbIndeces.curIdx++;
        if (client_data->rpt.rcbIndeces.curIdx < client_data->rpt.rcbIndeces.size) {
            CallBack_ContinueEvent(client_data, rcbGetting, mccrWriteFail);
        } else {
            client_data->rpt.params._force = 0;
            client_data->rpt.tryingSetupRcb = 0;
            CallBack_EndEvent(client_data, mcsrAllRcbBusy, err);
        }
    }
}



static void MMSClient_WriteObjectCB(uint32_t invokeId, void *data, IedClientError err)
{
    MCCbRes_t res = mccrWriteFail;
    MMSClientData_t *client_data = (MMSClientData_t *)data;
    if (err == IED_ERROR_OK) {
        res = mccrWriteOk;
    }
    CallBack_EndEvent(client_data, res, err);
}



static void MMSClient_ReadObjectCB(uint32_t invokeId, void *data, IedClientError err, MmsValue *value)
{
    MCCbRes_t res = mccrReadFail;
    MMSClientData_t *client_data = (MMSClientData_t *)data;
    if (err == IED_ERROR_OK) {
        client_data->actualValue = value;
        res = mccrReadOk;
    }
    CallBack_EndEvent(client_data, res, err);
}



static void MMSClient_CmdTerminateCtlCB(void *data, ControlObjectClient obj)
{
    MMSClientData_t *client_data = (MMSClientData_t *)data;
    CallBack_EndEvent(client_data, mccrWriteFail, IED_ERROR_UNEXPECTED_VALUE_RECEIVED);
}



static void MMSClient_GetSpecCtlCB(uint32_t invokeId, void *data, IedClientError err, MmsVariableSpecification *spec)
{
    MMSClientData_t *client_data = (MMSClientData_t *)data;
    if (err == IED_ERROR_OK) {
        client_data->ctl.initDone = 1;
        FunctionalConstraint fc = IEC61850_FC_NONE;
        char buf[255];
        char *ref = SplitObjRefWithFC(client_data->logicPath, buf, &fc);
        client_data->ctl.varSpec = spec;
        client_data->ctl.obj = ControlObjectClient_createEx(ref, client_data->client->con,
                CONTROL_MODEL_DIRECT_NORMAL, spec);
        if (client_data->ctl.obj) {
            ControlObjectClient_setCommandTerminationHandler(client_data->ctl.obj,
                    MMSClient_CmdTerminateCtlCB, client_data);
            ControlObjectClient_setInterlockCheck(client_data->ctl.obj, client_data->ctl.interlockCheck);
            ControlObjectClient_setSynchroCheck(client_data->ctl.obj, client_data->ctl.synchroCheck);
            ControlObjectClient_useConstantT(client_data->ctl.obj, client_data->ctl.useConstantT);
        }
    }
    CallBack_EndEvent(client_data, mccrChgState, err);
}



static void MMSClient_ReqCtlCB(uint32_t invokeId, void *data, IedClientError err, MmsValue *value)
{
    MMSClientData_t *client_data = (MMSClientData_t *)data;
    if (err == IED_ERROR_OK) {
        AsyncOp_t op;
        if (client_data->ctl.model)
            MmsValue_delete(client_data->ctl.model);
        client_data->ctl.model = value;
        uint32_t ctlModel = MmsValue_toUint32(client_data->ctl.model);
        ControlObjectClient_setControlModel(client_data->ctl.obj, (ControlModel)ctlModel);
        switch (ctlModel) {
            case CONTROL_MODEL_DIRECT_NORMAL:
            case CONTROL_MODEL_DIRECT_ENHANCED:
                op = operateCtl;
                break;
            case CONTROL_MODEL_SBO_NORMAL:
                op = selectCtl;
                break;
            case CONTROL_MODEL_SBO_ENHANCED:
                op = selectValCtl;
                break;
            default:
                CallBack_EndEvent(client_data, mcsrSysErr, DATA_ACCESS_ERROR_OBJECT_ACCESS_UNSUPPORTED);
                return;
                break;
        }
        CallBack_ContinueEvent(client_data, op, mccrWriteFail);
    } else {
        CallBack_EndEvent(client_data, mccrWriteFail, err);
    }
}



static void MMSClient_OperateCtlCB(uint32_t invokeId, void *data, IedClientError err, ControlActionType type, bool success)
{
    MCCbRes_t res = mccrWriteFail;
    MMSClientData_t *client_data = (MMSClientData_t *)data;
    if (err == IED_ERROR_OK && success) {
        res = mccrWriteOk;
    }
    CallBack_EndEvent(client_data, res, err);
}



static void MMSClient_SelectCtlCB(uint32_t invokeId, void *data, IedClientError err, ControlActionType type, bool success)
{
    MCCbRes_t res = mccrWriteFail;
    MMSClientData_t *client_data = (MMSClientData_t *)data;
    if (err == IED_ERROR_OK && success) {
        CallBack_ContinueEvent(client_data, operateCtl, mccrWriteFail);
        return;
    }
    CallBack_EndEvent(client_data, res, err);
}



static void MMSClient_SelectWithValCtlCB(uint32_t invokeId, void *data, IedClientError err, ControlActionType type, bool success)
{
    MCCbRes_t res = mccrWriteFail;
    MMSClientData_t *client_data = (MMSClientData_t *)data;
    if (err == IED_ERROR_OK && success) {
        res = mccrWriteOk;
    }
    CallBack_EndEvent(client_data, res, err);
}



static void MMSClient_CancelCtlCB(uint32_t invokeId, void *data, IedClientError err, ControlActionType type, bool success)
{
    MCCbRes_t res = mccrWriteFail;
    MMSClientData_t *client_data = (MMSClientData_t *)data;
    if (err == IED_ERROR_OK && success) {
        res = mccrWriteOk;
    }
    CallBack_EndEvent(client_data, res, err);
}



static void MMSClient_ReadWriteCB(void *data, uint8_t *buf, int bufsz, bool received)
{
    // MMSClient_t *client = (MMSClient_t*)data;
    // if (received) {
    //     mcl_log(TM_DUMPRX, "recv: %d bytes to %p", bufsz, client);
    //     DumpHex(buf, bufsz);
    // }
}



static void MMSClient_ConnectedCB(void *data, IedConnection con)
{
    MMSClient_t *client = (MMSClient_t *)data;
    if (client->link == 0) {
        client->link = 1;
        client->isConnected = true;
        mcl_log(TM_NOTE, "[%d] Link is up", client->id);
        if (client->cb)
            client->cb(mccrChgState, client, client->userData);
    }
}



static void MMSClient_ReconnectCB(void *data, IedConnection con)
{
    MMSClient_t *client = (MMSClient_t *)data;
    if (client->link == 1) {
        client->link = 0;
        mcl_log(TM_NOTE, "[%d] Link is down", client->id);
        if (client->cb)
            client->cb(mccrChgState, client, client->userData);
    }
}



// READ/WRITE DATA SETUPS *********************************************************************
// ********************************************************************************************



MCRes_t MMSClient_Setup(MMSClientData_t *data, ...)
{
    MCRes_t res = mcsrInvalidArg;
    if (data) {
        switch (data->type) {
            case mmsctDataSet: {
                res = MMSClient_SetupReadDataset(data);
            } break;
            case mmsctReport: {
                res = MMSClient_SetupGetRcb(data);
            } break;
            case mmsctWrSignal: {
                va_list args;
                va_start(args, data);
                MMSVal_t *val = va_arg(args, MMSVal_t *);
                res = MMSClient_SetupWriteObject(data, val);
                va_end(args);
            } break;
            case mmsctRdSignal: {
                res = MMSClient_SetupReadObject(data);
            } break;
            case mmsctControl: {
                va_list args;
                va_start(args, data);
                MMSVal_t *val = va_arg(args, MMSVal_t *);
                uint64_t operTime = va_arg(args, uint64_t);
                res = MSClient_SetupControlObject(data, val, operTime);
                va_end(args);
            } break;
        }
    }
    return SetResult(data, res, 0);
}



MCRes_t MSClient_SetupControlObject(MMSClientData_t *data, MMSVal_t *val, uint64_t operTime)
{
    if (data) {
        if (val) {
            MMSClient_t *client = data->client;

            if (data->isBusy)
                return SetResult(data, mcsrBusy, 0);
            if (!client->link)
                return SetResult(data, mcsrNotConnected, 0);
            if (!client->license)
                return SetResult(data, mcsrNoLicense, 0);

            IedClientError err;
            switch (data->type) {
                case mmsctControl: {
                    if (!data->ctl.initDone)
                        return SetResult(data, mcsrInvalidArg, 0);
                    if (!data->ctl.obj)
                        return SetResult(data, mcsrInvalidArg, 0);
                    MmsType type = ControlObjectClient_getCtlValType(data->ctl.obj);
                    if (val->type != ConvertType(type))
                        return SetResult(data, mcsrInvalidArg, 0);

                    // should be removed
                    if (data->actualValue)
                        MmsValue_delete(data->actualValue);
                    data->ctl.operTime = operTime;
                    data->actualValue = MMSVal_to_MMSValue(val, 0, false);
                    data->asyncOp = reqCtl;
                    MCRes_t res = SetupAsync(data, &err);
                    return SetResult(data, res, err);
                } break;
                default: {
                    return SetResult(data, mcsrInvalidArg, 0);
                } break;
            }
        }
        return SetResult(data, mcsrInvalidArg, 0);
    }
    return mcsrInvalidArg;
}



MCRes_t MMSClient_SetupWriteObject(MMSClientData_t *data, MMSVal_t *val)
{
    if (data) {
        if (val) {
            MMSClient_t *client = data->client;

            if (data->isBusy)
                return SetResult(data, mcsrBusy, 0);
            if (!client->link)
                return SetResult(data, mcsrNotConnected, 0);
            if (!client->license)
                return SetResult(data, mcsrNoLicense, 0);

            IedClientError err;
            switch (data->type) {
                case mmsctWrSignal: {
                    // should be removed
                    if (data->actualValue)
                        MmsValue_delete(data->actualValue);
                    data->actualValue = MMSVal_to_MMSValue(val, 0, false);
                    data->asyncOp = writeObject;
                    MCRes_t res = SetupAsync(data, &err);
                    return SetResult(data, res, err);
                } break;
                default: {
                    return SetResult(data, mcsrInvalidArg, 0);
                } break;
            }
        }
        return SetResult(data, mcsrInvalidArg, 0);
    }
    return mcsrInvalidArg;
}



MCRes_t MMSClient_SetupReadDataset(MMSClientData_t *data)
{
    if (data) {
        MMSClient_t *client = data->client;

        if (data->isBusy)
            return SetResult(data, mcsrBusy, 0);
        if (!client->link)
            return SetResult(data, mcsrNotConnected, 0);
        if (!client->license)
            return SetResult(data, mcsrNoLicense, 0);
        if (data->type != mmsctDataSet)
            return SetResult(data, mcsrInvalidArg, 0);

        IedClientError err;
        data->asyncOp = readDataSetValues;
        MCRes_t res = SetupAsync(data, &err);
        return SetResult(data, res, err);
    }
    return mcsrInvalidArg;
}



MCRes_t MMSClient_SetupGetRcb(MMSClientData_t *data)
{
    // removed operation -> return ok
    return SetResult(data, mcsrOk, IED_ERROR_OK);
    //
    if (data) {
        MMSClient_t *client = data->client;

        if (data->isBusy)
            return SetResult(data, mcsrBusy, 0);
        if (!client->link)
            return SetResult(data, mcsrNotConnected, 0);
        if (!client->license)
            return SetResult(data, mcsrNoLicense, 0);
        if (data->type != mmsctReport)
            return SetResult(data, mcsrInvalidArg, 0);

        IedClientError err;
        data->asyncOp = rcbGetting;
        MCRes_t res = SetupAsync(data, &err);
        return SetResult(data, res, err);
    }
    return mcsrInvalidArg;
}



MCRes_t MMSClient_SetupRcbParams(MMSClientData_t *data, MMSRcbParam_t *prm)
{
    if (data) {
        MMSClient_t *client = data->client;

        if (data->isBusy)
            return SetResult(data, mcsrBusy, 0);
        if (!client->link)
            return SetResult(data, mcsrNotConnected, 0);
        if (!client->license)
            return SetResult(data, mcsrNoLicense, 0);
        if (data->type != mmsctReport)
            return SetResult(data, mcsrInvalidArg, 0);
        // if (!data->rpt.rcb)
        //     return SetResult(data, mcsrInvalidArg, 0);

        int opf = data->rpt.params.optFileds;
        memcpy(&data->rpt.params, prm, sizeof(MMSRcbParam_t));
        data->rpt.params.optFileds = opf;
        data->rpt.params.trgops.padding = 0;

        IedClientError err;
        data->asyncOp = (data->rpt.rcb == 0)? rcbParsing : rcbSetup;
        MCRes_t res = SetupAsync(data, &err);
        return SetResult(data, res, err);
    }
    return mcsrInvalidArg;
}



MCRes_t MMSClient_SetupReadObject(MMSClientData_t *data)
{
    if (data) {
        MMSClient_t *client = data->client;

        if (data->isBusy)
            return SetResult(data, mcsrBusy, 0);
        if (!client->link)
            return SetResult(data, mcsrNotConnected, 0);
        if (!client->license)
            return SetResult(data, mcsrNoLicense, 0);

        IedClientError err;
        switch (data->type) {
            case mmsctRdSignal: {
                if (data->actualValue) {
                    MmsValue_delete(data->actualValue);
                    data->actualValue = 0;
                }
                data->asyncOp = readObject;
                MCRes_t res = SetupAsync(data, &err);
                return SetResult(data, res, err);
            } break;
            default: {
                return SetResult(data, mcsrInvalidArg, 0);
            } break;
        }
    }
    return mcsrInvalidArg;
}



MCRes_t MMSClient_SetupInitControl(MMSClientData_t *data, bool useConstantT, bool interlockCheck, bool synchroCheck)
{
    if (data) {
        MMSClient_t *client = data->client;

        if (data->isBusy)
            return SetResult(data, mcsrBusy, 0);
        if (!client->link)
            return SetResult(data, mcsrNotConnected, 0);
        if (!client->license)
            return SetResult(data, mcsrNoLicense, 0);

        IedClientError err;
        switch (data->type) {
            case mmsctControl: {
                data->ctl.useConstantT = useConstantT;
                data->ctl.interlockCheck = interlockCheck;
                data->ctl.synchroCheck = synchroCheck;
                data->asyncOp = getSpecCtl;
                MCRes_t res = SetupAsync(data, &err);
                return SetResult(data, res, err);
            } break;
            default: {
                return SetResult(data, mcsrInvalidArg, 0);
            } break;
        }
    }
    return mcsrInvalidArg;
}



MCRes_t MMSClient_SetupCancelControl(MMSClientData_t *data)
{
    if (data) {
        MMSClient_t *client = data->client;

        if (data->isBusy)
            return SetResult(data, mcsrBusy, 0);
        if (!client->link)
            return SetResult(data, mcsrNotConnected, 0);
        if (!client->license)
            return SetResult(data, mcsrNoLicense, 0);

        IedClientError err;
        switch (data->type) {
            case mmsctControl: {
                data->asyncOp = cancelCtl;
                MCRes_t res = SetupAsync(data, &err);
                return SetResult(data, res, err);
            } break;
            default: {
                return SetResult(data, mcsrInvalidArg, 0);
            } break;
        }
    }
    return mcsrInvalidArg;
}



// CONNECTION SUPPORT *************************************************************************
// ********************************************************************************************



static inline int MMSClient_SetupLogin(MMSClient_t *client)
{
    if (client && client->con && client->authdata) {
        switch (client->authtype) {
            case ACSE_AUTH_NONE: {
                return 0;
            } break;
            case ACSE_AUTH_CERTIFICATE:
            case ACSE_AUTH_PASSWORD: {
                if (!client->auth) {
                    client->auth = AcseAuthenticationParameter_create();
                }
                AcseAuthenticationParameter_setAuthMechanism(client->auth, client->authtype);
                AcseAuthenticationParameter_setPassword(client->auth, client->authdata);
            } break;
            default: {
                return -2;
            } break;
        }
        MmsConnection mmsConnection = IedConnection_getMmsConnection(client->con);
        IsoConnectionParameters parameters = MmsConnection_getIsoConnectionParameters(mmsConnection);
        IsoConnectionParameters_setAcseAuthenticationParameter(parameters, client->auth);
        return 0;
    }
    return -1;
}



static MCRes_t SetupConnect(MMSClient_t *client, const char *ip, IedClientError *err)
{
    if (client && !client->isConnected && !client->working && ip) {
        IedClientError errlocal;
        IedClientError *pe = (err)? err : &errlocal;
        if (!client->con) { // if resources were destroyed
            client->con = IedConnection_createEx(0, false);
            if (!client->con) {
                return mcsrSysErr;
            }
            MmsConnection mmsCon = IedConnection_getMmsConnection(client->con);
            MmsConnection_setRawMessageHandler(mmsCon, MMSClient_ReadWriteCB, client);
            MMSClient_SetupLogin(client);
            IedConnection_installConnectionClosedHandler(client->con, MMSClient_ReconnectCB, client);
            IedConnection_installConnectionReconnectHandler(client->con, MMSClient_ReconnectCB, client);
            IedConnection_installConnectionConnectedHandler(client->con, MMSClient_ConnectedCB, client);
            IedConnection_setConnectTimeout(client->con, client->connectTimeout);
            IedConnection_setKeepAliveParams(client->con, client->tcpKA_idle, client->tcpKA_cnt, client->tcpKA_interval);
        }
        IedConnection_connectAsync(client->con, pe, client->myIp, 0, ip, client->port);
        if (*pe == IED_ERROR_OK) {
            client->working = true;
            //mcl_debug("[%d] client started", client->id);
            return mcsrOk;
        } else {
            if (errno != 0) {
                *pe = -(errno);
            }
            return mcsrSysErr;
        }
    }
    return mcsrInvalidArg;
}



MCRes_t MMSClient_SetupConnect(MMSClient_t *client, IedClientError *err)
{
    if (client) {
        return SetupConnect(client, client->ip, err);
    }
    return mcsrInvalidArg;
}



MCRes_t MMSClient_SetupConnectByIP(MMSClient_t *client, const char *ip, IedClientError *err)
{
    if (client) {
        return SetupConnect(client, ip, err);
    }
    return mcsrInvalidArg;
}



MCRes_t MMSClient_Disconnect(MMSClient_t *client, IedClientError *err)
{
    if (client && client->con) {
        if (client->working) { // clear all resources
            client->working = false;
            client->isConnected = false;
            MmsConnection mmsCon = IedConnection_getMmsConnection(client->con);
            if (mmsCon) {
                MainLoopLock(client);
                MmsConnection_close(mmsCon);
                IedConnection_destroy(client->con);
                client->con = 0;
                MainLoopUnlock(client);
                for (MMSClientData_t *cd = client->dataList; cd; cd = cd->next) {
                    // reset rcbs
                    if (cd->type == mmsctReport && cd->rpt.rcb) {
                        ClientReportControlBlock_destroy(cd->rpt.rcb);
                        cd->rpt.rcb = 0;
                    }
                    cd->isBusy = 0;
                }
                //mcl_debug("[%d] client stopped", client->id);
                return mcsrOk;
            }
        } else { // if not working => resources saved
            return mcsrOk;
        }
    }
    return mcsrInvalidArg;
}



int MMSClient_GetConnectState(MMSClient_t *client)
{
    return (client)? client->link : 0;
}



int MMSClient_isBusy(MMSClientData_t *data)
{
    return (data)? data->isBusy : 0;
}



// OBJECTS SUPPORT ****************************************************************************
// ********************************************************************************************



static void FreeReport(Report_t *report)
{
    if (report) {
        if (report->data)
            MmsValue_delete(report->data);
        free(report);
    }
}



static MMSClientData_t *MMSClientData_Create()
{
    MMSClientData_t *data = (MMSClientData_t *)calloc(1, sizeof(MMSClientData_t));
    if (data) {
        pthread_mutex_init(&data->lmu, 0);
    }
    return data;
}



static MMSClient_t *MMSClient_Create()
{
    MMSClient_t *client = (MMSClient_t *)calloc(1, sizeof(MMSClient_t));
    // if (client) {
        // client->con = IedConnection_createEx(0, false);
    // }
    return client;
}



MMSClientData_t *MMSClientData_Add(MMSClient_t *client, MMSClientData_t **head)
{
    if (!client || !head)
        return 0;
    MMSClientData_t *data = (MMSClientData_t *)list_add_alloc (
            (list_t **)head, (list_create_node)MMSClientData_Create
    );
    if (data) {
        data->client = client;
        client->dataList = *head;
        client->dataCnt++;
    }
    return data;
}



static void MMSCleintData_FreeData(MMSClientData_t *data)
{
    if (data) {
        //mcl_debug("[%d]{%s} data *_destroy begin", data->client->id, data->logicPath);
        pthread_mutex_destroy(&data->lmu);
        switch (data->type) {
            case mmsctReport:
                if (data->rpt.entryId)
                    MmsValue_delete(data->rpt.entryId);
                if (data->rpt.rcb)
                    ClientReportControlBlock_destroy(data->rpt.rcb);
                if (data->rpt.list)
                    list_free((list_t **)&data->rpt.list, (list_free_node)FreeReport);
                break;
            case mmsctDataSet:
                if (data->dataset.self)
                    ClientDataSet_destroy(data->dataset.self);
                break;
            case mmsctControl:
                if (data->ctl.model)
                    MmsValue_delete(data->ctl.model);
                if (data->ctl.varSpec)
                    MmsVariableSpecification_destroy(data->ctl.varSpec);
                if (data->ctl.obj)
                    ControlObjectClient_destroy(data->ctl.obj);
                // no break;
            case mmsctWrSignal:
            case mmsctRdSignal:
                if (data->actualValue)
                    MmsValue_delete(data->actualValue);
                break;
            default:
                break;
        }
        free(data);
    }
}



static void MMSClientData_Free(MMSClientData_t **head)
{
    if (!head)
        return;
    list_free((list_t **)head, (list_free_node)MMSCleintData_FreeData);
}



MMSClient_t *MMSClient_Add(MMSClient_t **head)
{
    if (!head)
        return 0;
    return (MMSClient_t *)list_add_alloc (
            (list_t **)head, (list_create_node)MMSClient_Create
    );
}



static void MMSClient_FreeDataList(MMSClient_t *head)
{
    if (!head)
        return;
    if (head->dataList) {
        MMSClientData_Free(&head->dataList);
    }
}



static void MMSClient_FreeClient(MMSClient_t *client)
{
    if (client) {
        int __attribute__((unused)) id = client->id;
        #if MMSCLIENT_EACH_CLIENT_THREAD
            MainLoopDestroy(client);
        #endif
        if (client->con) {
            MmsConnection mmsCon = IedConnection_getMmsConnection(client->con);
            if (mmsCon) {
                MmsConnection_close(mmsCon);
            }
        }
        // mcl_debug("[%d] IedConnection_destroy", id);
        MMSClient_FreeDataList(client);
        // mcl_debug("[%d] MMSClient_FreeDataList", id);
        if (client->auth) {
            switch (client->authtype) {
                case ACSE_AUTH_CERTIFICATE:
                case ACSE_AUTH_PASSWORD: {
                    AcseAuthenticationParameter_destroy(client->auth);
                } break;
                default: {
                } break;
            }
        }
        if (client->con) {
            IedConnection_destroy(client->con);
        }
        // mcl_debug("[%d] AcseAuthenticationParameter_destroy", id);
        free(client);
        // mcl_debug("[%d] completly destroyed", id);
    }
}



void MMSClient_Free(MMSClient_t **head)
{
    if (!head)
        return;
    #if MMSCLIENT_ONE_THREAD
        MainLoopDestroy();
    #elif MMSCLIENT_SHARED_THREADS
        for (SharedLoopPrm_t *next, *sh = priv->loop; sh;) {
            next = sh->next;
            MainLoopDestroy(sh);
            sh = next;
        }
    #endif
    list_free((list_t **)head, (list_free_node)MMSClient_FreeClient);
    free(priv);
    priv = 0;
}




MMSClient_t *MMSClient_GetClient(MMSClientData_t *data)
{
    return (data)? data->client : 0;
}



MMSClientData_t *MMSClient_GetData(MMSClient_t *client)
{
    return (client)? client->dataList : 0;
}



MMSClient_t *MMSClient_Next(MMSClient_t *client)
{
    return (client)? client->next : 0;
}



MMSClientData_t *MMSClient_NextData(MMSClientData_t *data)
{
    return (data)? data->next : 0;
}



// MISC ***************************************************************************************
// ********************************************************************************************



static inline MCRes_t GetDoAsyncResult(MMSClientData_t *data, IedClientError err)
{
    MMSClient_t *client = data->client;
    if (err == IED_ERROR_OK) {
        client->asyncCnt++;
        return mcsrOk;
    } else {
        data->isBusy = 0;
        return mcsrSysErr;
    }
}



/*
 * @fn: DoAsync
 * @brief: Установка на выполнение асинхронных функций по типу data->asyncOp
 * @param data
 * @param err
 * return @ref MCRes_t
 * */
static MCRes_t DoAsync(MMSClientData_t *data, IedClientError *err)
{
    if (data) {
        if (data->isBusy) { // must be noted
            IedClientError errlocal;
            IedClientError* pe = (err)? err : &errlocal;
            MMSClient_t *client = data->client;
            *pe = IED_ERROR_OK;
            char buf[255];
            switch (data->asyncOp) {
                //
                case readDataSetValues: {
                    if (data->logicPath) {
                        data->invokeReqID = IedConnection_readDataSetValuesAsync(client->con, pe,
                                data->logicPath, data->dataset.self, MMSClient_DataSetCB, data);
                        return GetDoAsyncResult(data, *pe);
                    }
                } break;
                //
                case writeObject: {
                    if (data->logicPath) {
                        if (data->actualValue) {
                            FunctionalConstraint fc = IEC61850_FC_NONE;
                            char *ref = SplitObjRefWithFC(data->logicPath, buf, &fc);
                            if (ref) {
                                data->invokeReqID = IedConnection_writeObjectAsync(client->con, pe,
                                        ref, fc, data->actualValue, MMSClient_WriteObjectCB, data);
                                return GetDoAsyncResult(data, *pe);
                            }
                        }
                    }
                } break;
                //
                case readObject: {
                    if (data->logicPath) {
                        FunctionalConstraint fc = IEC61850_FC_NONE;
                        char *ref = SplitObjRefWithFC(data->logicPath, buf, &fc);
                        if (ref) {
                            data->invokeReqID = IedConnection_readObjectAsync(client->con, pe,
                                    ref, fc, MMSClient_ReadObjectCB, data);
                            return GetDoAsyncResult(data, *pe);
                        }
                    }
                } break;
                //
                case rcbParsing: {
                    if (data->logicPath) {
                        char *brace = 0;
                        int pathLen = strlen(data->logicPath);
                        int symbolicPathLen = 0;
                        if (!brace) brace = strchr(data->logicPath, '[');
                        if (!brace) brace = strchr(data->logicPath, '(');
                        if (!brace) brace = strchr(data->logicPath, '{');
                        if (brace) {
                            data->rpt.rcbIndeces.curIdx = 0;
                            data->rpt.rcbIndeces.size = (uint8_t)extractNumbers(brace, data->rpt.rcbIndeces.self, RCBINDECES_MAX);
                            symbolicPathLen = brace - data->logicPath;
                        } else {
                            int idxLen = 0;
                            for (int i = pathLen-1; i != 0; --i) {
                                if (isdigit(data->logicPath[i])) {
                                    idxLen++;
                                } else {
                                    break; // end for
                                }
                            }
                            if (idxLen) {
                                data->rpt.rcbIndeces.curIdx = 0;
                                data->rpt.rcbIndeces.size = 1;
                                symbolicPathLen = pathLen - idxLen;
                                data->rpt.rcbIndeces.self[0] = (uint8_t)atoi(data->logicPath+symbolicPathLen);
                            } else {
                                break; // error
                            }
                        }
                        if (symbolicPathLen && symbolicPathLen < sizeof(data->rpt.logicPath)-1) {
                            strncpy(data->rpt.logicPath, data->logicPath, symbolicPathLen);
                        } else {
                            break; // error
                        }
                        { // try to setup rcb params
                            data->rpt.params._force = 1;
                            data->rpt.tryingSetupRcb = 1;
                        }
                    }
                } // no break
                case rcbGetting: {
                    if (data->logicPath) {
                        char logicPath[256];
                        int idx = data->rpt.rcbIndeces.curIdx;
                        sprintf(logicPath, "%s%d", data->rpt.logicPath, data->rpt.rcbIndeces.self[idx]);
                        data->rpt.reportHandlerInstalled = 0; // reset handler
                        IedConnection_uninstallReportHandler(data->client->con, logicPath);
                        data->invokeReqID = IedConnection_getRCBValuesAsync(client->con, pe,
                                logicPath, data->rpt.rcb, MMSClient_RcbGettingCB, data);
                        return GetDoAsyncResult(data, *pe);
                    }
                } break;
                //
                case rcbSetup: {
                    ClientReportControlBlock rcb = data->rpt.rcb;
                    if (rcb) {
                        MMSRcbParam_t *param = &data->rpt.params;
                        MMSRcbParam_t *prevp = &data->rpt.prevParams;
                        uint32_t parametersMask = 0;
                        if (param->_force || param->ena != prevp->ena) {
                            parametersMask |= RCB_ELEMENT_RPT_ENA;
                            ClientReportControlBlock_setRptEna(rcb, param->ena);
                        }
                        if (param->_force || param->trgops.all != prevp->trgops.all) {
                            parametersMask |= RCB_ELEMENT_TRG_OPS;
                            ClientReportControlBlock_setTrgOps(rcb, param->trgops.all);
                        }
                        if (param->_force || param->bufTm != prevp->bufTm) {
                            parametersMask |= RCB_ELEMENT_BUF_TM;
                            ClientReportControlBlock_setBufTm(rcb, param->bufTm);
                        }
                        if (param->_force || param->intgrTm != prevp->intgrTm) {
                            parametersMask |= RCB_ELEMENT_INTG_PD;
                            ClientReportControlBlock_setIntgPd(rcb, param->intgrTm);
                        }
                        if (param->_force || param->entryId != prevp->entryId) {
                            if (!data->rpt.entryId)
                                data->rpt.entryId = MmsValue_newOctetString(8, 8);
                            if (data->rpt.entryId) {
                                char *src = (char*)(&param->entryId);
                                libMmsValue *val = (libMmsValue *)data->rpt.entryId;
                                for (int i = 0, j = 8; i < 8; ++i)
                                    val->value.octetString.buf[--j] = src[i];
                                parametersMask |= RCB_ELEMENT_ENTRY_ID;
                                ClientReportControlBlock_setEntryId(data->rpt.rcb, data->rpt.entryId);
                            }
                        }
                        if (param->_force || param->optFileds != prevp->optFileds) {
                            param->optFileds = RPT_OPT_REASON_FOR_INCLUSION | RPT_OPT_DATA_REFERENCE | RPT_OPT_ENTRY_ID;
                            parametersMask |= RCB_ELEMENT_OPT_FLDS;
                            ClientReportControlBlock_setOptFlds(data->rpt.rcb, param->optFileds);
                        }
                        if (param->purgeBuf) {
                            parametersMask |= RCB_ELEMENT_PURGE_BUF;
                            ClientReportControlBlock_setPurgeBuf(rcb, true);
                        }
                        if (param->gi) {
                            parametersMask |= RCB_ELEMENT_GI;
                            ClientReportControlBlock_setGI(rcb, true);
                            parametersMask |= RCB_ELEMENT_RPT_ENA;
                            ClientReportControlBlock_setRptEna(rcb, true);
                        }
                        memcpy(prevp, param, sizeof(MMSRcbParam_t));

                        data->invokeReqID = IedConnection_setRCBValuesAsync(client->con, pe,
                                rcb, parametersMask, true, MMSClient_RcbSetupCB, data);
                        return GetDoAsyncResult(data, *pe);
                    }
                } break;
                //
                case getSpecCtl: {
                    if (data->logicPath) {
                        FunctionalConstraint fc = IEC61850_FC_NONE;
                        char *ref = SplitObjRefWithFC(data->logicPath, buf, &fc);
                        if (fc == IEC61850_FC_CO && ref) {
                            data->invokeReqID = IedConnection_getVariableSpecificationAsync(client->con, pe,
                                    ref, IEC61850_FC_CO, MMSClient_GetSpecCtlCB, data);
                            return GetDoAsyncResult(data, *pe);
                        }
                    }
                } break;
                //
                case reqCtl: {
                    if (data->logicPath) {
                        char reference[129];
                        if (strlen(data->logicPath) < 120) {
                            sprintf(reference, "%s.ctlModel", data->logicPath);
                            FunctionalConstraint fc = IEC61850_FC_NONE;
                            char *ref = SplitObjRefWithFC(reference, buf, &fc);
                            if (fc == IEC61850_FC_CO && ref) {
                                data->invokeReqID = IedConnection_readObjectAsync(client->con, pe,
                                        ref, IEC61850_FC_CF, MMSClient_ReqCtlCB, data);
                                return GetDoAsyncResult(data, *pe);
                            }
                        }
                    }
                } break;
                //
                case selectCtl: {
                    if (data->ctl.initDone && data->ctl.obj) {
                        data->invokeReqID = ControlObjectClient_selectAsync(data->ctl.obj, pe,
                                MMSClient_SelectCtlCB, data);
                        return GetDoAsyncResult(data, *pe);
                    }
                } break;
                //
                case selectValCtl: {
                    if (data->ctl.initDone && data->actualValue && data->ctl.obj) {
                        data->invokeReqID = ControlObjectClient_selectWithValueAsync(data->ctl.obj, pe,
                                data->actualValue, MMSClient_SelectWithValCtlCB, data);
                        return GetDoAsyncResult(data, *pe);
                    }
                } break;
                //
                case operateCtl: {
                    if (data->ctl.initDone && data->actualValue && data->ctl.obj) {
                        data->invokeReqID = ControlObjectClient_operateAsync(data->ctl.obj, pe,
                                data->actualValue, data->ctl.operTime, MMSClient_OperateCtlCB, data);
                        return GetDoAsyncResult(data, *pe);
                    }
                } break;
                //
                case cancelCtl: {
                    if (data->ctl.initDone && data->ctl.obj) {
                        data->invokeReqID = ControlObjectClient_cancelAsync(data->ctl.obj, pe,
                                MMSClient_CancelCtlCB, data);
                        return GetDoAsyncResult(data, *pe);
                    }
                } break;
                //
                default: { /* NOP */ } break;
            }
        }
        data->isBusy = 0;
    }
    return mcsrInvalidArg;
}



/*
 * @fn: NextAsync
 * @brief: Выбор следующей асинхронной функции на исполнение,
 *      запускается по цепочке callback из libiec61850
 * @param client
 * @param err
 * return @ref MCRes_t
 * */
static MCRes_t NextAsync(MMSClient_t *client, IedClientError *err)
{
    if (client) {
        IedClientError errlocal;
        IedClientError* pe = (err)? err : &errlocal;
        MCRes_t res = mcsrSysErr;
        client->asyncCnt--;
        if (client->asyncCnt < ASYNCSETUP_MAX) {
            qitem_t *qh = que_get_head(&client->queue);
            if (qh) {
                MMSClientData_t *data = container_of(qh, MMSClientData_t, qitem);
                data->isBusy = 1;
                res = DoAsync(data, pe);
                que_del_item(&data->qitem);
                if (res != mcsrOk) {
                    SetResult(data, res, *pe);
                }
            }
        } else {
            mcl_log(TM_WARN, "[%d] NextAsync, but asyncCnt=%d", client->id, client->asyncCnt);
        }
        return res;
    }
    return mcsrInvalidArg;
}



/*
 * @fn: ReplaceAsync
 * @brief: Установка следующей асинхронной функции на исполнение,
 *      запускается по цепочке callback из libiec61850, не освобождая ресурса
 * @param data
 * @param err
 * return @ref MCRes_t
 * */
static MCRes_t ReplaceAsync(MMSClientData_t *data, IedClientError *err)
{
    if (data) {
        IedClientError errlocal;
        IedClientError* pe = (err)? err : &errlocal;
        MMSClient_t *client = data->client;
        client->asyncCnt--;
        DoAsync(data, pe);
        return mcsrOk;
    }
    return mcsrInvalidArg;
}



/*
 * @fn: SetupAsync
 * @brief: Установка следующей асинхронной функции в очередь на исполнение
 * @param data
 * @param err
 * return @ref MCRes_t
 * */
static MCRes_t SetupAsync(MMSClientData_t *data, IedClientError *err)
{
    if (data) {
        IedClientError errlocal;
        IedClientError* pe = (err)? err : &errlocal;
        MMSClient_t *client = data->client;
        MCRes_t ret = mcsrOk;
        data->isBusy = 1;
        MainLoopLock(client);
        if (client->asyncCnt < ASYNCSETUP_MAX) {
            ret = DoAsync(data, pe);
        } else {
            que_add_tail(&client->queue, &data->qitem);
        }
        MainLoopUnlock(client);
        return ret;
    }
    return mcsrInvalidArg;
}



/*
 * @fn: SplitObjRefWithFC
 * @brief: Извлечение fc и dataRef из objRef
 * @param objRef
 * @param fc
 * return @ref MCRes_t
 * */
static char *SplitObjRefWithFC(const char *objRef, char *buf, FunctionalConstraint *fc)
{
    const char *slesh = strstr(objRef, "/");
    if (slesh) {
        const char *fpoint = strstr(slesh, ".");
        if (fpoint) {
            int res = sscanf(fpoint+1, "%[^.]", buf);
            if (res == 1 && strlen(buf) == 2) {
                size_t sz = fpoint-objRef;
                *fc = FunctionalConstraint_fromString(buf);
                strncpy(buf, objRef, sz);
                buf[sz] = 0;
                strcpy(buf+strlen(buf), fpoint+3);
                return buf;
            }
        }
    }
    return 0;
}



static MMSVal_Type_t ConvertType(MmsType type)
{
    switch (type) {
        case MMS_BOOLEAN: {
            return mmsvtBool;
        } break;
        case MMS_INTEGER: {
            return mmsvtInt;
        } break;
        case MMS_UNSIGNED: {
            return mmsvtUInt;
        } break;
        case MMS_FLOAT: {
            return mmsvtLReal;
        } break;
        case MMS_STRING:
        case MMS_VISIBLE_STRING: {
            return mmsvtVString;
        } break;
        case MMS_BIT_STRING: {
            return mmsvtBitString;
        } break;
        case MMS_OCTET_STRING: {
            return mmsvtOctString;
        } break;
        case MMS_UTC_TIME: {
            return mmsvtUTC;
        } break;
        case MMS_BINARY_TIME: {
            return mmsvtBinTime;
        } break;
        case MMS_DATA_ACCESS_ERROR: {
            return mmsvtAccessErr;
        } break;
        case MMS_ARRAY:
        case MMS_STRUCTURE: {
            return (type == MMS_ARRAY)?
                    mmsvtArray : mmsvtStruct;
        } break;
        case MMS_GENERALIZED_TIME:
        case MMS_BCD:
        case MMS_OBJ_ID:
        default: {
            return mmsvtnDef;
        } break;
    }
}



/* GetNextNode funcs */
static int GetNextNode_valueIsArray(void *val)
{
    return  (((libMmsValue *)val)->type == MMS_ARRAY ||
            ((libMmsValue *)val)->type == MMS_STRUCTURE);
}
static void *GetNextNode_getArrayElem(void *val, int idx)
{
    return ((libMmsValue *)val)->value.structure.components[idx];
}
static int GetNextNode_getArraySize(void *val)
{
    return ((libMmsValue *)val)->value.structure.size;
}
static int GetNextNode_getIndex(void *val)
{
    return ((libMmsValue *)val)->idx;
}
static void *GetNextNode_getParent(void *val)
{
    return ((libMmsValue *)val)->parent;
}



/* for bit strings */
static const uint8_t BitReverseTable256[] =
{
    0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0, 0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0,
    0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8, 0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8,
    0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4, 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4,
    0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC, 0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC,
    0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2, 0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2,
    0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA, 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
    0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6, 0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6,
    0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE, 0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE,
    0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1, 0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
    0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9, 0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9,
    0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5, 0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5,
    0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED, 0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
    0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3, 0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3,
    0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB, 0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,
    0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7, 0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7,
    0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF, 0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF
};



const char *SysErr_to_String(IedClientError err)
{
    const char *ret;
    int error = err;
    if (error >= 0) {
        switch (err) {
            /** No error occurred - service request has been successful */
            case IED_ERROR_OK:
                ret = "Ok";
                break;

            /** The service request can not be executed because the client is not yet connected */
            case IED_ERROR_NOT_CONNECTED:
                ret = "Client is not connected";
                break;

            /** Connect service not execute because the client is already connected */
            case IED_ERROR_ALREADY_CONNECTED:
                ret = "Client is already connected";
                break;

            /** The service request can not be executed caused by a loss of connection */
            case IED_ERROR_CONNECTION_LOST:
                ret = "Connection lost";
                break;

            /** The service or some given parameters are not supported by the client stack or by the server */
            case IED_ERROR_SERVICE_NOT_SUPPORTED:
                ret = "Service is not supported";
                break;

            /** Connection rejected by server */
            case IED_ERROR_CONNECTION_REJECTED:
                ret = "Connection rejected by server";
                break;

            /** Cannot send request because outstanding call limit is reached */
            case IED_ERROR_OUTSTANDING_CALL_LIMIT_REACHED:
                ret = "Outstanding call limit is reached";
                break;

            /* client side errors */

            /** API function has been called with an invalid argument */
            case IED_ERROR_USER_PROVIDED_INVALID_ARGUMENT:
                ret = "Invalid API argument";
                break;

            case IED_ERROR_ENABLE_REPORT_FAILED_DATASET_MISMATCH:
                ret = "Report failed: dataset mismatch";
                break;

            /** The object provided object reference is invalid (there is a syntactical error). */
            case IED_ERROR_OBJECT_REFERENCE_INVALID:
                ret = "Object reference is invalid";
                break;

            /** Received object is of unexpected type */
            case IED_ERROR_UNEXPECTED_VALUE_RECEIVED:
                ret = "Object with unexpected type received";
                break;

            /* service error - error reported by server */

            /** The communication to the server failed with a timeout */
            case IED_ERROR_TIMEOUT:
                ret = "Connect to server failed: timeout";
                break;

            /** The server rejected the access to the requested object/service due to access control */
            case IED_ERROR_ACCESS_DENIED:
                ret = "Access denied";
                break;

            /** The server reported that the requested object does not exist (returned by server) */
            case IED_ERROR_OBJECT_DOES_NOT_EXIST:
                ret = "Object doesn't exist";
                break;

            /** The server reported that the requested object already exists */
            case IED_ERROR_OBJECT_EXISTS:
                ret = "Object already exists";
                break;

            /** The server does not support the requested access method (returned by server) */
            case IED_ERROR_OBJECT_ACCESS_UNSUPPORTED:
                ret = "Server doesn't support requested access method";
                break;

            /** The server expected an object of another type (returned by server) */
            case IED_ERROR_TYPE_INCONSISTENT:
                ret = "Server expected an object of another type";
                break;

            /** The object or service is temporarily unavailable (returned by server) */
            case IED_ERROR_TEMPORARILY_UNAVAILABLE:
                ret = "Object or service is temporarily unavailable";
                break;

            /** The specified object is not defined in the server (returned by server) */
            case IED_ERROR_OBJECT_UNDEFINED:
                ret = "Object is not defined in the server";
                break;

            /** The specified address is invalid (returned by server) */
            case IED_ERROR_INVALID_ADDRESS:
                ret = "Specified address is invalid";
                break;

            /** Service failed due to a hardware fault (returned by server) */
            case IED_ERROR_HARDWARE_FAULT:
                ret = "Server hardware fault";
                break;

            /** The requested data type is not supported by the server (returned by server) */
            case IED_ERROR_TYPE_UNSUPPORTED:
                ret = "Requested data type is not supported by the server";
                break;

            /** The provided attributes are inconsistent (returned by server) */
            case IED_ERROR_OBJECT_ATTRIBUTE_INCONSISTENT:
                ret = "Requested data type is not supported by the server";
                break;

            /** The provided object value is invalid (returned by server) */
            case IED_ERROR_OBJECT_VALUE_INVALID:
                ret = "Object value is invalid";
                break;

            /** The object is invalidated (returned by server) */
            case IED_ERROR_OBJECT_INVALIDATED:
                ret = "Object is invalidated";
                break;

            /** Received an invalid response message from the server */
            case IED_ERROR_MALFORMED_MESSAGE:
                ret = "Invalid response message";
                break;

            /** Service not implemented */
            case IED_ERROR_SERVICE_NOT_IMPLEMENTED:
                ret = "Service not implemented";
                break;

            default:
                ret = "Unknown error";
                break;
        }
    } else {
        ret = strerror(-error);
    }
    return ret;
}



const char *MCRes_to_String(MCRes_t res)
{
    const char *ret;
    switch (res) {
        case mcsrOk:
            ret = "Ok";
            break;

        case mcsrEndReached:
            ret = "Data end reached";
            break;

        case mcsrBusy:
            ret = "No available resources: busy";
            break;

        case mcsrNotConnected:
            ret = "Not connected";
            break;

        case mcsrSysErr:
            ret = "System error";
            break;

        case mcsrDataErr:
            ret = "Data error";
            break;

        case mcsrInvalidArg:
            ret = "Invalid argument";
            break;

        case mcsrNoLicense:
            ret = "No license available";
            break;

        default:
            ret = "Unknown error";
            break;
    }
    return ret;
}



const char *MCCbRes_to_String(MCCbRes_t res)
{
    const char *ret;
    switch (res) {
        case mccrReadOk:
            ret = "Read ok";
            break;
        case mccrWriteOk:
            ret = "Write ok";
            break;
        case mccrChgState:
            ret = "State changed";
            break;
        case mccrEventLose:
            ret = "Event lose";
            break;
        case mccrReadFail:
            ret = "Read fail";
            break;
        case mccrWriteFail:
            ret = "Write fail";
            break;
        case mccrNoMem:
            ret = "No mem";
            break;
        default:
            ret = "Unknown error";
            break;
    }
    return ret;
}


