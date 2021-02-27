
#include "GooseFrame.hpp"
#include <cstring>
#include <cfloat>
#include <vector>



using std::vector;



typedef vector <uint8_t> IECArray;



#define log(l,m,...) {this->logger->Print(DrvLogger::LogLevel::l, "GooseFrame: " m "", ##__VA_ARGS__);}
#define dump(d,s) {this->logger->DumpData(d,s);}


/*!
 * @union UniVal
 * 
 * @brief: Унифицированный тип целочисленных
*/
union UniVal {
    bool        b;
    char        c;
    int         i;
    uint        ui;
    uint8_t     u8;
    uint16_t    u16;
    uint32_t    u32;
    uint64_t    u64;
    int8_t      i8;
    int16_t     i16;
    int32_t     i32;
    int64_t     i64;
    uint8_t     _b[8];
};



/*!
 * @property BitReverseTable256
 * 
 * @brief: Таблица перекодировки бит
*/
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



/*!
 * @fn GetNextOffset
 * 
 * @brief: получение смещения на тэг следующего данного
*/
static uint16_t GetNextOffset(const uint8_t *tag)
{
    const uint8_t *p = tag + 1;
    if ( *(p) <= 0x80 ) {
        return 2 + *(p); // (tag + size) + data len
    } else {
        uint16_t size = 0;
        uint16_t ssize = (*(p) & 0x7F);
        switch (ssize) {
            case 1:
                size = (*(p+1));
                break;
            case 2:
                size = ((*(p+1)) << 8) + (*(p+2));
                break;
            default: // size > 65535 ?
                return 0;    
        }
        return 2 + ssize + size; // (tag + ssize) + size + data len
    }
}



/*!
 * @fn GetUniVal
 * 
 * @brief: получение целочисленного данного
 * 
 * @param tag - указатель на тэг данного
 * @param offset - смещение на тэг следующего данного
 * @param size - полезный размер данных 
 * 
 * NOTE: Supported types:
 *      86 83 85
 *      8c (bin-time)
 *      91 (utc time)
*/
static UniVal GetUniVal(const uint8_t *tag, uint16_t &offset, uint16_t &size)
{
    UniVal ret = {0};
    const uint8_t *p = tag;
   
    p += 1; // skip tag
    size = (*(p));
    // check for sign for int
    bool sign = false;
    if ((*tag == 0x85) && *(p+1) == 0xff) {
        sign = true;
        p += 1; // skip sign-tag
        size--;
    }
    if ( size <= 8 ) {
        offset = 0;
        for (uint16_t i = size; i != 0; --i, ++offset) {
            ret._b[offset] = (*(p+i));
        }
        // int correction
        if (sign) {
            memset(ret._b+offset, 0xff, 8-size);
            offset += 1; // sign-tag
        }
        offset += 2; // tag & size
    } else {
        offset = 2 + size;
    }
    return ret;
}



/*!
 * @fn GetBitString
 * 
 * @brief: получение битовой строки
 * 
 * @param tag - указатель на тэг данного
 * @param offset - смещение на тэг следующего данного
 * @param size - полезный размер данных 
 * 
 * NOTE: Supported types:
 *      84
*/
static uint32_t GetBitString(const uint8_t *tag, uint16_t &offset, uint16_t &size)
{
    union {
        uint32_t d;
        uint8_t b[4];
    } ret = {0};
    const uint8_t *p = tag;
    const uint8_t *bt = BitReverseTable256;
   
    size = (*(p+1)); // skip tag
    p += 2; // skip tag + size
    if ( size <= 5 ) {
        offset = 0;
        // skip first byte (padding)
        for (uint16_t i = 1; i < size; ++i, ++offset) {
            ret.b[offset] = bt[(*(p+i))];
        }
        offset += 3; // tag + size + padding
    } else {
        offset = 2 + size;
    }
    return ret.d;
}



/*!
 * @fn GetDouble
 * 
 * @brief: получение числа с плав.точкой
 * 
 * @param tag - указатель на тэг данного
 * @param offset - смещение на тэг следующего данного
 * @param size - полезный размер данных 
 * 
 * NOTE: Supported types:
 *      87
*/
static float64 GetDouble(const uint8_t *tag, uint16_t &offset, uint16_t &size)
{
    union {
        float64 d;
        float32 f;
        uint8_t b[8];
    } tmp = {0};
    const uint8_t *p = tag;
   
    p += 1; // skip tag
    size = (*(p));
    if ( size <= 9 ) {
        offset = 0;
        // skip first byte (08/0b)
        for (uint16_t i = size; i != 1; --i, ++offset) {
            tmp.b[offset] = (*(p+i));
        }
        offset += 3; // tag & size & float-tag
    } else {
        offset = 2 + size;
    }
    size--;
    return (size == 8)?
        tmp.d : (float64)tmp.f;
}



/*!
 * @fn GetArraySize
 * 
 * @brief: получение размера массива goose-данных
 * 
 * @param tag - указатель на тэг данного
 * @param offset - смещение на данные массива
 * 
 * NOTE: Supported types:
 *      @ref GetArray
*/
static uint16_t GetArraySize(const uint8_t *tag, uint16_t &offset)
{
    const uint8_t *p = tag;

    uint16_t size = 0;
    offset = 0;

    p += 1; // skip tag
    if ( *(p) <= 0x80 ) {
        size = (*(p));
        offset = 2; // tag + size
    } else {
        uint16_t ssize = (*(p) & 0x7F);   
        switch (ssize) {
            case 1:
                size = (*(p+1));
                offset = 3;  // tag + ssize + size
                break;
            case 2:
                size = ((*(p+1)) << 8) + (*(p+2));
                offset = 4;  // tag + ssize + size(2)
                break;
            default: // size > 65535 ?
                return 0;
        }
    }

    return size;
}



/*!
 * @fn GetArray
 * 
 * @brief: получение массива данных
 * 
 * @param tag - указатель на тэг данного
 * @param offset - смещение на тэг следующего данного
 * @param size - полезный размер данных 
 * 
 * NOTE: Supported types:
 *      81 array
 *      89 octet
 *      8a string
 *      90 mms string (utf)
 *      ab allData
 *      a2 struct
*/
static IECArray GetArray(const uint8_t *tag, uint16_t &offset, uint16_t &size)
{
    IECArray ret = {0};
    const uint8_t *p = tag;

    size = GetArraySize(p, offset);
    p += offset;
    ret.assign(p, p + size);
    offset += size;

    return ret;
}



/*!
 * @fn GetFirstEntryOffset
 * 
 * @brief: получение смещения на тэг первого данного структуры
 * 
 * @param tag - указатель на тэг структуры
*/
static uint16_t GetFirstEntryOffset(const uint8_t *tag)
{
    if ( ((*(tag)) == 0xAB) || ((*(tag)) == 0xA2) ) {
        const uint8_t *p = tag + 1;
        if ( *(p) <= 0x80 ) {
            return 2;   // tag + size
        } else {
            return 2 + (*(tag+1) & 0x7F); // (tag + ssize) + size
        }
    }
    return 0;
}



// /*!
//  * @fn GetEntryOffsetByIdx
//  * 
//  * @brief: получение смещения на тэг первого структуры по индексу в структуре
//  * 
//  * @param tag - указатель на тэг структуры
//  * @param idx - порядковый номер элемента в структуре (с 1)
//  * 
//  * return смещение относительно тэга структуры
// */
// static uint16_t GetEntryOffsetByIdx(const uint8_t *tag, uint16_t idx)
// {
//     if ( ((*(tag)) == 0xAB) || ((*(tag)) == 0xA2) ) {
//         uint16_t offset, size, tmp;
//         offset = GetFirstEntryOffset(tag);
//         IECArray data = GetArray(tag, tmp, size);
//         const uint8_t *p = &(data[0]); 
//         if (size > 0) {
//             uint16_t i = 1;
//             uint16_t sum = 0;
//             for (; i < idx; ++i) {
//                 sum += GetNextOffset(p + sum);
//             }
//             return (sum >= size)?
//                 0 : sum + offset;
//         }
//     }
//     return 0;
// }



/*!
 * @fn _GetNextIECValue
 * 
 * @brief: получение данного
 * 
 * @param tag - указатель на тэг данного
 * @param offset - смещение на тэг следующего данного
 * 
 * NOTE: если данное = структура, то смещение offset на тэг первого элемента структуры,
 *      при этом поле data IECValue = 0
*/
static IECValue _GetNextIECValue(const uint8_t *tag, uint16_t &offset)
{
    IECValue ret;
    ret.data = {0};
    ret.type = IECTypes::NotSupported;

    uint16_t sz = 0;
    switch ( *(tag) ) {
        // structs
        case 0xA2: 
        {
            offset = GetFirstEntryOffset(tag);
            ret.type = IECTypes::Struct;
        } break;

        // arr,oct,str,mmsstr
        case 0x81:
        case 0x89:
        case 0x8a:
        case 0x90:
        {
            IECArray data = GetArray(tag, offset, sz);
            data.resize(IECTYPE_MAXSZ, 0);
            data[IECTYPE_MAXSZ-1] = 0;
            memcpy(&(ret.data._raw[0]), &(data[0]), IECTYPE_MAXSZ);
            switch ( *(tag) ) {
                case 0x81: ret.type = IECTypes::Array; break;
                case 0x89: ret.type = IECTypes::OctetString; break;
                case 0x8a: ret.type = IECTypes::VisString; break;
                case 0x90: ret.type = IECTypes::Unicode; break;
            }
        } break;

        // float
        case 0x87:
        {
            union {
                float32 f;
                float64 d;
                uint8_t b[8];
            } res = {0};
            float64 data = GetDouble(tag, offset, sz);
            if (sz == 4) {
                res.f = (float32)data;
                ret.type = IECTypes::Float32;
            } else {
                res.d = data;
                ret.type = IECTypes::Float64;
            }
            memcpy(&(ret.data._raw[0]), &(res.b[0]), sz);
        } break;

        // bitstr
        case 0x84:
        {
            union {
                uint32_t d;
                uint8_t b[4];
            } res = {0};
            res.d = GetBitString(tag, offset, sz);
            memcpy(&(ret.data._raw[0]), &(res.b[0]), sizeof(res));
            ret.type = IECTypes::BitString;
        } break;

        // int
        case 0x86:
        case 0x85:
        case 0x83:
        case 0x8c:
        case 0x91:
        {
            UniVal data = GetUniVal(tag, offset, sz);
            memcpy(&(ret.data._raw[0]), &(data._b[0]), sizeof(UniVal));
            switch ( *(tag) ) {
                case 0x86:
                    if (sz <= 1) {
                        ret.type = IECTypes::Int8U;
                    } else if (sz == 2) {
                        ret.type = IECTypes::Int16U;
                    } else if (sz <= 4) {
                        ret.type = IECTypes::Int32U;
                    } else if (sz <= 8) {
                        ret.type = IECTypes::Int64U;
                    }
                    break;
                case 0x85:
                    if (sz <= 1) {
                        ret.type = IECTypes::Int8;
                    } else if (sz == 2) {
                        ret.type = IECTypes::Int16;
                    } else if (sz <= 4) {
                        ret.type = IECTypes::Int32;
                    } else if (sz <= 8) {
                        ret.type = IECTypes::Int64;
                    }
                    break;
                case 0x83: ret.type = IECTypes::Boolean; break;
                case 0x8c: ret.type = IECTypes::EntryTime; break;
                case 0x91: ret.type = IECTypes::Timestamp; break;
            }
        } break;

        // not supported
        default:
        {
            offset = GetNextOffset(tag);
        } break;
    }

    return ret;
}



IECAllData GooseFrameData::ToIECAllData(void) const
{
    log(Trace, "ToIECAllData -- start");
    if (this->start) {
        uint16_t offset, size;
        IECArray allData = GetArray(this->start, offset, size);
        if (size > 0) {
            IECAllData ret;
            IECValue val;
            const uint8_t *ps = &(allData[0]);
            const uint8_t *pc = ps;
            uint16_t of = 0;
            ret.reserve(256);
            offset = 0;
            for (;;) {
                val = _GetNextIECValue(pc, of);
                log(Trace, "ToIECAllData -- val with type %d inserted", val.type);
                switch (val.type) {
                    case IECTypes::Boolean:
                    case IECTypes::Int8:
                    case IECTypes::Int16:
                    case IECTypes::Int32:
                    case IECTypes::Int8U:
                    case IECTypes::Int16U:
                    case IECTypes::Int32U:
                    case IECTypes::Float32:
                    case IECTypes::BitString:
                        ret.insert(ret.end(), &(val.data._raw[0]), &(val.data._raw[3])+1);
                        break;
                    case IECTypes::Int64:
                    case IECTypes::Int64U:
                    case IECTypes::Float64:
                    case IECTypes::EntryTime:
                    case IECTypes::Timestamp:
                        ret.insert(ret.end(), &(val.data._raw[0]), &(val.data._raw[7])+1);
                        break;
                    case IECTypes::VisString:
                    case IECTypes::OctetString:
                    case IECTypes::Unicode:
                    case IECTypes::Array:
                        ret.insert(ret.end(), &(val.data._raw[0]), &(val.data._raw[IECTYPE_MAXSZ-1])+1);
                        break;
                    default:
                        break;
                }
                offset += of;
                pc = ps + offset;
                if (offset >= size) {
                    log(Trace, "ToIECAllData -- done");
                    return ret;
                }
            }
        } else {
            log(Warning, "ToIECAllData -- size of data <= 0");
        }
    } else {
        log(Warning, "ToIECAllData -- Init wasn't done");
    }
    return {0};
}



IECValue GooseFrameData::GetNextIECValue(void)
{
    uint16_t offset;
    IECValue val = _GetNextIECValue(this->current, offset);
    this->current += offset;
    log(Trace, "GetNextIECValue done -- val with type %d returned", val.type);
    return val;
}



void GooseFrameData::Reset()
{
    this->current = this->start;
    log(Trace, "buffer reset");
}



int GooseFrameData::Init(const uint8_t *buf)
{
    log(Trace, "init buffer start");
    if ((*(buf)) == 0xAB) {
        if (!this->start)
            delete this->start;
        uint16_t offset;
        uint16_t size = GetArraySize(buf, offset);
        size += offset;
        this->start = new uint8_t[size];
        memcpy(this->start, buf, size);
        this->current = this->start;
        log(Trace, "init buffer -- size = %u", size);
        log(Dump, "init buffer -- dump:");
        dump(this->start, size);
        return 0;
    } else {
        log(Warning, "init buffer -- tag != 0xAB");
        return -1;
    }
}



GooseFrameData::GooseFrameData()
{
    this->current = this->start = 0;
}



GooseFrameData::~GooseFrameData()
{
    delete this->start;
}



void GooseFrameData::SetupLogger(const DrvLogger *logger)
{
    this->logger = logger;
    log(Trace, "logger started");
}

