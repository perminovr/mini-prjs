
#ifndef GOOSEFRAME_H_
#define GOOSEFRAME_H_

#include <netinet/ether.h>
#include <string>
#include <vector>

#include "DrvLogger.hpp"



/*!
 * @def IECTYPE_MAXSZ
 * 
 * @brief: максимальный рамер буфера памяти для хранения данных IEC типа
*/
#ifndef IECTYPE_MAXSZ
#define IECTYPE_MAXSZ   128
#endif



/*!
 * @def float 32-64
*/
#ifndef FLOAT32_TYPE
#define FLOAT32_TYPE
typedef float float32;
#endif
#ifndef FLOAT64_TYPE
#define FLOAT64_TYPE
typedef double float64;
#endif



/*!
 * @enum IECTypes
 * 
 * @brief: перечисление типов данных IEC
*/
enum IECTypes {
    Boolean,            /* gc = 0x83 */
    Enum,               /* gc = 0x85 */
    Int8 = Enum,        /* ... */
    Int16,              /* ... */
    Int32,              /* ... */
    Int64,              /* ... */
    Int8U,              /* gc = 0x86 */
    Int16U,             /* ... */
    Int32U,             /* ... */
    Int64U,             /* ... */
    Float32,            /* gc = 0x87 */
    Float64,            /* ... */
    VisString,          /* gc = 0x8a */
    OctetString,        /* gc = 0x89 */
    Unicode,            /* gc = 0x90 */
    EntryTime,          /* gc = 0x8c */
    Timestamp,          /* gc = 0x91 */
    BitString,          /* gc = 0x84 */
    Dbpos = BitString,  /* ... */
    Tcmd = BitString,   /* ... */
    Check = BitString,  /* ... */
    Quality = BitString,/* ... */
    Array,              /* gc = 0x81 */
    Struct,             /* gc = 0xa2 */
    NotSupported,       /* ... */
    tEnd
};



/*!
 * @union IECType
 * 
 * @brief: Объединение типов данных IEC
 * 
 * NOTE: максимальный размер буфера равен @ref IECTYPE_MAXSZ.
 *      Типы соответствуют @ref IECTypes
*/
union IECType {
    bool Boolean;
    uint8_t Enum;
    int8_t Int8;
    int16_t Int16;
    int32_t Int32;
    int64_t Int64;
    uint8_t Int8U;
    uint16_t Int16U;
    uint32_t Int32U;
    uint64_t Int64U;
    float32 Float32;
    float64 Float64;
    char VisString[IECTYPE_MAXSZ];
    uint8_t OctetString[IECTYPE_MAXSZ];
    uint8_t Unicode[IECTYPE_MAXSZ];
    uint64_t EntryTime;
    uint64_t Timestamp;
    uint32_t BitString;
    uint32_t Dbpos;
    uint32_t Tcmd;
    uint32_t Check;
    uint32_t Quality;
    uint8_t Array[IECTYPE_MAXSZ];
    uint8_t _raw[IECTYPE_MAXSZ];
};



/*!
 * @def IECAllData
 * 
 * @brief: массив набора данных
 * 
 * NOTE: Представляет собой заданный содержанием goose-кадра порядок данных 
 *      базовых типов, размещенных в памяти в соответствии со следующими
 *      соглашениями:
 *      .-------------------------------.-----------------.----------------------------.
 *      |  Тип                          |  Размер в кадре |  Размер в памяти IECAllData|
 *      |-------------------------------|-----------------|----------------------------|
 *      |  Целочисленные (Int)          |  <= 4 байт      |  4 байт                    |
 *      |  Целочисленные (Int)          |  >  4 байт      |  8 байт                    |
 *      |  Логические (Boolean)         |  =  1 байт      |  4 байт                    |
 *      |  Перечисления (Enum)          |  =  1 байт      |  4 байт                    |
 *      |  Прочие (EntryTime,           |  =  1 байт      |  4 байт                    |
 *      |      Timestamp)               |                 |                            |
 *      |-------------------------------|-----------------|----------------------------|
 *      |  С плавающей точкой (Float32) |  <= 4 байт      |  4 байт                    |
 *      |  С плавающей точкой (Float64) |  >  4 байт      |  8 байт                    |
 *      |-------------------------------|-----------------|----------------------------|
 *      |  Бит-строки (BitString,       |  <= 4 байт      |  4 байт                    |
 *      |      Dbpos, Tcmd, Check,      |                 |                            |
 *      |      Quality)                 |                 |                            |
 *      |-------------------------------|-----------------|----------------------------|
 *      |  Прочие (VisString,           |  >= 0 байт      |  @ref IECTYPE_MAXSZ байт;  |
 *      |      OctetString, Unicode,    |                 |  последний байт всегда = 0 |
 *      |      Array)                   |                 |                            |
 *      *-------------------------------*-----------------*----------------------------*
 * Использование: установить указатель упакованной структуры (по 4 байта)
 *      на начало массива (struct X*)&(iecAllData[0])
*/
typedef std::vector <uint8_t> IECAllData;



/*!
 * @struct IECValue
 * 
 * @brief: IEC данные: тип и значение
*/
struct IECValue {
    IECTypes type;
    IECType data;
};



/*!
 * @struct GooseFrameHeader
 * 
 * @brief: заголовок кадра goose
*/
struct GooseFrameHeader {
    // MAC адрес назначения: сетевой порядок байт
    uint8_t destMAC[ETH_ALEN];
    // MAC адрес источника: сетевой порядок байт
    uint8_t sourceMAC[ETH_ALEN];
    // тип1 ethernet кадра 
    uint16_t etherType1;
    // идентификатор VLAN: PRIO (3) DEI (1) ID (12)
    // устанавливается, если etherType1 == 8100
    uint16_t vlanID;
    // тип2 ethernet кадра (при наличии 1-го)
    uint16_t etherType2;
    // идентификатор GOOSE-сообщения
    uint16_t appID;
    // длина фрагмента данных GOOSE-сообщени
    uint16_t length;
    // размер pdu
    uint32_t PDUSize;
    // ссылка в информационной модели устройства-отправителя
    std::string goCBRef;
    // время ожидания до прихода следующего события
    uint32_t timeAllowedtoLive;
    // ссылка в информационной модели устройства-отправителя
    std::string datSet;
    // идентификатор goose сообщения
    std::string goID;
    // метка времени момента изменения значения счетчика stNum
    uint64_t t;
    // инкремент по изменению данных
    uint32_t stNum;
    // сбрасывается по изменению данных
    uint32_t sqNum;
    // флаг теста
    bool test;
    // версия набора данных
    uint32_t confRev;
    // признак необходимости реконфигурации набора данных
    bool ndsCom;
    // число данных allData
    uint32_t numDatSetEtries;
};



/*!
 * @class GooseFrameData
 * 
 * @brief: данные кадра goose
*/
class GooseFrameData {
public:
    /*!
     * @fn GooseFrameData::GooseFrameData
     * 
     * @brief: конструтор
    */
    GooseFrameData();
    /*!
     * @fn GooseFrameData::~GooseFrameData
     * 
     * @brief: деструтор      
    */
    ~GooseFrameData();

    /*!
     * @fn GooseFrameData::Init
     * 
     * @brief: инициализация данных кадра goose
     * 
     * @param buf - указатель на начало данных кадра goose 
     *       (tag allData == 0xAB)
     * 
     * return    =0 - ok
     *           <0 - error
    */
    int Init(const uint8_t *buf);
    /*!
     * @fn GooseFrameData::Reset
     * 
     * @brief: сброс внутреннего итератора в начало
    */
    void Reset();

    /*!
     * @fn GooseFrameData::GetNextIECValue
     * 
     * @brief: возврат следующей порции данных IEC типа @ref IECValue
     *       из проинициализированного кадра goose
    */
    IECValue GetNextIECValue(void);   

    /*!
     * @fn GooseFrameData::ToIECAllData
     * 
     * @brief: возврат массива набора данных @ref ToIECAllData
     *       проинициализированного кадра goose
    */
    IECAllData ToIECAllData(void) const;

    /*!
     * @fn GooseSub::SetupLogger
     * 
     * @brief: установка логгера
    */
    void SetupLogger(const DrvLogger *logger);

private:
    /*!
     * @property GooseFrameData::start
     * 
     * @brief: указатель на tag allData данных кадра goose
    */
    uint8_t *start;
    /*!
     * @property GooseFrameData::start
     * 
     * @brief: итератор по данным кадра goose
    */
    uint8_t *current;
    /*!
     * @property GooseFrameData::logger
     * 
     * @brief: логгер
    */
    const DrvLogger *logger;
};



/*!
 * @struct GooseFrame
 * 
 * @brief: кадр goose: заголовок и данные
 *      
*/
struct GooseFrame {
    GooseFrameHeader header;
    GooseFrameData data;
};



#endif /* GOOSEFRAME_H_ */