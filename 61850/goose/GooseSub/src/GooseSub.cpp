
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <net/if.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <algorithm> 
#include <stdexcept>
#include <thread>
#include <mutex>
#include <list>
#include <map>
#include <iostream>
#include "GooseSub.hpp"



#define GOOSE_ETHER_TYPE        (0x88B8)



using std::cout;
using std::cin;
using std::endl;
using std::exception;
using std::runtime_error;
using std::thread;
using std::mutex;
using std::map;



#define log(l,m,...) {priv->logger->Print(DrvLogger::LogLevel::l, "GooseSub: " m "", ##__VA_ARGS__);}
#define dump(d,s) {priv->logger->DumpData(d,s);}
#define MSG(x) "GooseSub: " x "\n"
#define EXCEPTION(x) {log(Error, x); throw runtime_error(MSG(x));}
#define puto16(pu) *((uint16_t*)pu)
#define puto32(pu) *((uint32_t*)pu)



#define GOOSESUB_EVENTS_NUM     (2)
#define GOOSESUB_EVENT_RECV     (0)
#define GOOSESUB_EVENT_KILL     (1)
#define GOOSESUB_EVENT_DSIZE	(8)



#pragma pack(push, 1)
struct EtherFrame {
    ether_header header;
    uint8_t data[ETH_DATA_LEN];
    int size;
};
union EtherFrame_u {
    EtherFrame field;
    uint8_t buf[ETH_FRAME_LEN];
};
#pragma pack(pop)



/*!
 * @struct GooseID
 * 
 * @brief: Идентификатор goose-сообщения
*/
struct GooseID {
    uint16_t appID;
    std::string goCBRef;
    std::string datSet;
    std::string goID;
};
/*!
 * @typedef GooseIDtoNum
 * 
 * @brief: Вектор соответствия идентификатора сообщения к числу изменений данных
*/
typedef std::vector < std::pair<GooseID, uint32_t> > GooseIDtoNum;



/*!
 * @struct GSFilters_t
 * 
 * @brief: Структура фильтров
*/
struct GSFilters_t {
    std::list <GooseSub::Filter> list;
    mutable GooseIDtoNum currGooses;
    mutex mu;
};



/*!
 * @struct GSPriv_t
 * 
 * @brief: Внутренние данные драйвера
*/
struct GSPriv_t {
    int idx;    // индекс системного сетевого интерфейса
    thread *mainThread;         // рабочий поток драйвера
    mutex killmu;               // мьютекс на остановку работы потока (по деструктору)
    uint8_t myMAC[ETH_ALEN];    // MAC адрес устройства
    GSFilters_t filters;        // структура фильтров
    struct pollfd pfds[GOOSESUB_EVENTS_NUM];    // управляющие файловые дескрипторы
    const DrvLogger *logger;  // логгер
};



bool operator==(const GooseID &v1, const GooseID &v2)
{
    if ( v1.appID != v2.appID )
        return false;
    if ( v1.goCBRef.compare(v2.goCBRef) != 0 )
        return false;
    if ( v1.datSet.compare(v2.datSet) != 0 )
        return false;
    if ( v1.goID.compare(v2.goID) != 0 )
        return false;
    return true;
}



bool operator==(const std::pair<GooseID, uint32_t> &v1, const GooseID &v2)
{
    return (v1.first == v2);
}



bool operator==(const GooseSub::Filter &v1, const GooseSub::Filter &v2)
{
    if ( memcmp(v1.destMAC, v2.destMAC, ETH_ALEN) != 0 )
        return false;
    if ( memcmp(v1.sourceMAC, v2.sourceMAC, ETH_ALEN) != 0 )
        return false;
    if ( v1.vlanID != v2.vlanID )
        return false;
    if ( v1.appID != v2.appID )
        return false;
    if ( v1._fbits != v2._fbits )
        return false;
    if ( v1.goCBRef.compare(v2.goCBRef) != 0 )
        return false;
    if ( v1.datSet.compare(v2.datSet) != 0 )
        return false;
    if ( v1.goID.compare(v2.goID) != 0 )
        return false;
    return true;
}



static void HandleFrame(const GSPriv_t *priv, const EtherFrame &frame);
static bool isGooseFrame(const EtherFrame_u &frame);



/*!
 * @fn GooseSub_RaiseEvent
 * 
 * @brief: Выдача сигнала о событии
*/
static inline int GooseSub_RaiseEvent(int fd)
{
	uint8_t buf = 1;
	return write(fd, &buf, GOOSESUB_EVENT_DSIZE);
}
#define GooseSub_RaiseKillEvent() GooseSub_RaiseEvent(priv->pfds[GOOSESUB_EVENT_KILL].fd)



/*!
 * @fn GooseSub_RaiseEvent
 * 
 * @brief: Подтверждение события
*/
static inline int GooseSub_EndEvent(int fd)
{
	uint8_t buf[GOOSESUB_EVENT_DSIZE];
	return read(fd, &buf, GOOSESUB_EVENT_DSIZE);
}
#define GooseSub_EndKillEvent() GooseSub_EndEvent(priv->pfds[GOOSESUB_EVENT_KILL].fd)



/*!
 * @fn MainLoop
 * 
 * @brief: Рабочий цикл драйвера
*/
void GooseSub::MainLoop(void)
{
    GSPriv_t *priv = (GSPriv_t *)this->priv;
    EtherFrame_u uframe;
    int res, i;

    log(Trace, "MainLoop started");

    for (;;) {
        res = poll(priv->pfds, GOOSESUB_EVENTS_NUM, -1);
        if (res > 0) {
            for (i = 0; i < GOOSESUB_EVENTS_NUM; ++i) {
                if (priv->pfds[i].revents == POLLIN) {
                    switch (i) {
                        case GOOSESUB_EVENT_RECV: 
                            break;
                        case GOOSESUB_EVENT_KILL: 
                            log(Trace, "event kill");
                            goto mainloop_exit;
                            break;
                    }
                }
            }

            // vlan
            // struct tpacket_auxdata aux;
            // struct iovec iov = { uframe.buf, ETH_FRAME_LEN };
            // struct msghdr hdr = {
            //     0, 0, &iov, 1, &aux, sizeof(struct tpacket_auxdata), 0,
            // };
            // uframe.field.size = recvmsg(priv->pfds[GOOSESUB_EVENT_RECV].fd, &hdr, 0);

            uframe.field.size = recv(priv->pfds[GOOSESUB_EVENT_RECV].fd, uframe.buf, ETH_FRAME_LEN, 0);

            if (!isGooseFrame(uframe))
                continue;

            log(Dump, "received goose frame (%d) -- dump:", uframe.field.size);
            dump(uframe.buf, uframe.field.size);

            if (priv->filters.list.size() == 0)
                continue;

            priv->filters.mu.lock();
            HandleFrame(priv, uframe.field);
            priv->filters.mu.unlock();

        } else if (res == 0) {
            log(Error, "poll timeout");
        } else {
            log(Error, "poll error");
        }
    }

mainloop_exit:
    priv->killmu.unlock();
    return;
}



void GooseSub::Subcribe(const GooseSub::Filter &filter)
{
    GSPriv_t *priv = (GSPriv_t *)this->priv;

    priv->filters.mu.lock();
    priv->filters.list.push_back(filter);
    priv->filters.mu.unlock();

    log(Trace, "Subcribe done");
}



void GooseSub::Unsubscribe(const GooseSub::Filter &filter)
{
    GSPriv_t *priv = (GSPriv_t *)this->priv;

    priv->filters.mu.lock();
    priv->filters.list.remove(filter);
    priv->filters.mu.unlock();

    log(Trace, "Unsubscribe done");
}



GooseSub::GooseSub(const std::string &devName)
{
    // private part
    GSPriv_t *priv = new GSPriv_t();
    if (!priv) {
        EXCEPTION("Cannot allocate memory for private!");
    }
    this->priv = priv;

    // interface idx
    priv->idx = if_nametoindex(devName.c_str());
    if (priv->idx <= 0) {
        EXCEPTION("Cannot open device!");
    }

    // socket
    int sockopt = 1;
    struct ifreq _iface = {0};
    int fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (fd < 0) {
        EXCEPTION("Cannot open socket!");    
    }    

    // source MAC
    strncpy(_iface.ifr_name, devName.c_str(), IFNAMSIZ-1);
    if ( ioctl(fd, SIOCGIFHWADDR, &_iface) < 0 ) {    
        EXCEPTION("Cannot get source MAC!");    
    }  
    memcpy((void*)priv->myMAC, (void*)(_iface.ifr_hwaddr.sa_data), ETH_ALEN);

    // promisc mode
    struct packet_mreq mreq = {0};
    mreq.mr_ifindex = priv->idx;
    mreq.mr_type = PACKET_MR_PROMISC;

    if ( setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof(sockopt)) < 0 ) {
        EXCEPTION("Cannot set socket as reusable!"); 
    }
    if ( setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, devName.c_str(), IFNAMSIZ-1) < 0 ) {
        EXCEPTION("Cannot bind to device!"); 
    }  
    if ( setsockopt(fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0 ) {
        EXCEPTION("Cannot move to promisc mode!"); 
    }

    // events
	priv->pfds[GOOSESUB_EVENT_RECV].events = POLLIN;
	priv->pfds[GOOSESUB_EVENT_RECV].fd = fd;
	priv->pfds[GOOSESUB_EVENT_KILL].events = POLLIN;
	priv->pfds[GOOSESUB_EVENT_KILL].fd = eventfd(0, 0);

    if (priv->pfds[GOOSESUB_EVENT_KILL].fd < 0) {
        EXCEPTION("Cannot get kill thread eventfd!"); 
    }

    // start thread
    priv->mainThread = new thread(&GooseSub::MainLoop, this);
    priv->mainThread->detach();
}



GooseSub::~GooseSub()
{
    GSPriv_t *priv = (GSPriv_t *)this->priv;
    priv->killmu.lock();
    GooseSub_RaiseKillEvent();
    priv->killmu.lock();
    for (int i = 0; i < GOOSESUB_EVENTS_NUM; ++i) {
        close (priv->pfds[i].fd);
    }
    delete priv->mainThread;
    delete priv;
}



void GooseSub::SetupLogger(const DrvLogger *logger)
{
    GSPriv_t *priv = (GSPriv_t *)this->priv;
    priv->logger = logger;
    log(Trace, "logger started");
}



/*!
 * @fn GetString
 * 
 * @brief: Получение строкого параметра из goose-кадра
 * 
 * @param tag - указатель на тэг параметра
 * @param offset - смещение на тэг следующего данного goose-кадра
*/
static std::string GetString(const uint8_t *tag, uint16_t &offset)
{
    const uint8_t *p = tag;
    const uint8_t *str = 0;
    std::string ret;
    uint32_t size;

    p += 1; // skip tag
    if ( *(p) <= 0x80 ) {
        size = *(p);
        str = p + 1; // skip size
        offset = 2 + size;
    } else {
        uint16_t it = 0;
        uint16_t ssize = (*(p) & 0x7F);
        offset = 2 + ssize;
        str = p + 1 + ssize;
        while (ssize--) {
            size = *(p+1+it) << (8*ssize);
            it++;
        }
        offset += size;
    }
    ret.reserve(size+1);
    ret.resize(size);
    memcpy(&(ret[0]), str, size);
    ret.push_back('\0');
    return ret;
}



/*!
 * @fn GetPDUSize
 * 
 * @brief: Получение размера PDU
 * 
 * @param tag - указатель на тэг параметра
 * @param offset - смещение на тэг следующего данного goose-кадра
*/
static uint32_t GetPDUSize(const uint8_t *tag, uint16_t &offset)
{
    union {
        uint8_t b[4];
        uint32_t d;
    } ret = {0};
    const uint8_t *p = tag;

    p += 1; // skip tag
    if ( *(p) <= 0x80 ) {
        ret.d = *(p);
        offset = 2;
    } else {
        uint16_t size = (*(p) & 0x7F);
        p += 1; // skip len
        switch (size) {
            case 1:
                ret.b[0] = *(p+0);
                break;
            case 2:
                ret.b[0] = *(p+1);
                ret.b[1] = *(p+0);
                break;
            case 3:
                ret.b[0] = *(p+2);
                ret.b[1] = *(p+1);
                ret.b[2] = *(p+0);
                break;
            case 4:
                ret.b[0] = *(p+3);
                ret.b[1] = *(p+2);
                ret.b[2] = *(p+1);
                ret.b[3] = *(p+0);
                break;
        }
        // (tag + sizeof len) + len
        offset = 2 + size;
    }
    return ret.d;
}



/*!
 * @fn GetUInt
 * 
 * @brief: Получение числового параметра goose-кадра
 * 
 * @param tag - указатель на тэг параметра
 * @param offset - смещение на тэг следующего данного goose-кадра
*/
static uint64_t GetUInt(const uint8_t *tag, uint16_t &offset)
{
    union {
        uint8_t b[8];
        uint32_t u32;
        uint64_t u64;
    } ret = {0};
    const uint8_t *p = tag;
   
    p += 1; // skip tag
    uint8_t size = (*(p));
    if ( size <= 8 ) {
        offset = 0;
        for (uint8_t i = size; i != 0; --i, ++offset) {
            ret.b[offset] = (*(p+i));
        }
        offset += 2; // tag & size
    }
    return ret.u64;
}



/*!
 * @fn RawToGooseHeader
 * 
 * @brief: Формирование заголовка goose кадра из ethernet кадра
 * 
 * @param frame - буфер принятого ethernet кадра
 * @param offset - смещение на тэг данных goose-кадра
*/
static GooseFrameHeader RawToGooseHeader(const EtherFrame &frame, uint16_t &dataOffset)
{
    uint16_t offset;
    const uint8_t *pu8 = (const uint8_t *)frame.header.ether_dhost;
    const uint8_t *start = pu8;
    GooseFrameHeader gooseHeader = {0};

    // dest mac ---------------------------------------------------
    memcpy(gooseHeader.destMAC, pu8, ETH_ALEN);
    pu8 += ETH_ALEN;
    // src mac ----------------------------------------------------
    memcpy(gooseHeader.sourceMAC, pu8, ETH_ALEN);
    pu8 += ETH_ALEN;
    // vlan id ----------------------------------------------------
    if (puto16(pu8) == htons(ETH_P_8021Q)) {
        gooseHeader.etherType1 = ETH_P_8021Q;
        pu8 += 2;
        gooseHeader.vlanID = ntohs(puto16(pu8));
        pu8 += 2;
    }
    // ethertype --------------------------------------------------
    gooseHeader.etherType2 = ntohs(puto16(pu8));
    pu8 += 2;
    // appid ------------------------------------------------------
    gooseHeader.appID = ntohs(puto16(pu8));
    pu8 += 2;
    // length -----------------------------------------------------
    gooseHeader.length = ntohs(puto16(pu8));
    pu8 += 2;
    // R ----------------------------------------------------------
    pu8 += 4; // reserve
    // pdu size ---------------------------------------------------
    if (*pu8 == 0x61) {
        gooseHeader.PDUSize = GetPDUSize(pu8, offset);
        pu8 += offset;
    } else {
        return {0};
    }
    // goCBRef ----------------------------------------------------
    if (*pu8 == 0x80) {
        gooseHeader.goCBRef = GetString(pu8, offset);
        pu8 += offset;
    } else {
        return {0};
    }
    // timeAllowedtoLive ------------------------------------------
    if (*pu8 == 0x81) {
        gooseHeader.timeAllowedtoLive = (uint32_t)GetUInt(pu8, offset);
        pu8 += offset;
    } else {
        return {0};
    }
    // datSet -----------------------------------------------------
    if (*pu8 == 0x82) {
        gooseHeader.datSet = GetString(pu8, offset);
        pu8 += offset;
    } else {
        return {0};
    }
    // goID -------------------------------------------------------
    if (*pu8 == 0x83) {
        gooseHeader.goID = GetString(pu8, offset);
        pu8 += offset;
    } else {
        return {0};
    }
    // t ----------------------------------------------------------
    if (*pu8 == 0x84) {
        gooseHeader.t = GetUInt(pu8, offset);
        pu8 += offset;
    } else {
        return {0};
    }
    // stNum ------------------------------------------------------
    if (*pu8 == 0x85) {
        gooseHeader.stNum = (uint32_t)GetUInt(pu8, offset);
        pu8 += offset;
    } else {
        return {0};
    }
    // sqNum ------------------------------------------------------
    if (*pu8 == 0x86) {
        gooseHeader.sqNum = (uint32_t)GetUInt(pu8, offset);
        pu8 += offset;
    } else {
        return {0};
    }
    // test -------------------------------------------------------
    if (*pu8 == 0x87) {
        gooseHeader.test = (bool)GetUInt(pu8, offset);
        pu8 += offset;
    } else {
        return {0};
    }
    // confRev ----------------------------------------------------
    if (*pu8 == 0x88) {
        gooseHeader.confRev = (uint32_t)GetUInt(pu8, offset);
        pu8 += offset;
    } else {
        return {0};
    }
    // ndsCom -----------------------------------------------------
    if (*pu8 == 0x89) {
        gooseHeader.ndsCom = (bool)GetUInt(pu8, offset);
        pu8 += offset;
    } else {
        return {0};
    }
    // numDatSetEtries --------------------------------------------
    if (*pu8 == 0x8A) {
        gooseHeader.numDatSetEtries = (uint32_t)GetUInt(pu8, offset);
        pu8 += offset;
    } else {
        return {0};
    }
    // data set ---------------------------------------------------
    dataOffset = (uint16_t)(pu8 - start);

    return gooseHeader;
}



/*!
 * @fn Filter_MAC_multicast
 * 
 * @brief: Фильтр по мультикастовой группе MAC адресов
*/
static bool Filter_MAC_multicast(const uint8_t *fMAC_start, const uint8_t *fMAC_end, const uint8_t *MAC)
{
    for (int i = 0; i < ETH_ALEN; ++i) {
        if (fMAC_start[i] != fMAC_end[i]) {
            return (MAC[i] >= fMAC_start[i] && MAC[i] <= fMAC_end[i]);
        }
        if (MAC[i] != fMAC_start[i]) {
            return false;
        }
    }
    return true;
}



/*!
 * @fn Filter_srcMAC
 * 
 * @brief: Фильтр по MAC источника
*/
static bool Filter_srcMAC(const uint8_t *fMAC, const uint8_t *MAC)
{
    uint8_t tmpbuf1[ETH_ALEN] = {0};
    uint8_t tmpbuf2[ETH_ALEN] = {0xff,0xff,0xff,0xff,0xff,0xff};
    // without filter -> all
    if (memcmp(tmpbuf1, fMAC, ETH_ALEN) == 0)
        return true;
    // unicast -> only from this node
    if (memcmp(fMAC, MAC, ETH_ALEN) == 0)
        return true;
    // broadcast -> all
    if (memcmp(tmpbuf2, fMAC, ETH_ALEN) == 0)
        return true;
    // multicast -> only from this group
    if (Filter_MAC_multicast(fMAC, (fMAC+6), MAC))
        return true;
    return false;
}



/*!
 * @fn Filter_srcMAC
 * 
 * @brief: Фильтр по MAC назначения
*/
static bool Filter_destMAC(const uint8_t *fMAC, const uint8_t *MAC)
{
    uint8_t tmpbuf1[ETH_ALEN] = {0};
    uint8_t tmpbuf2[ETH_ALEN] = {0xff,0xff,0xff,0xff,0xff,0xff};
    // without filter -> all
    if (memcmp(tmpbuf1, fMAC, ETH_ALEN) == 0)
        return true;
    // unicast -> only for this node
    if (memcmp(fMAC, MAC, ETH_ALEN) == 0)
        return true;
    // broadcast -> only broadcast
    if (memcmp(tmpbuf2, fMAC, ETH_ALEN) == 0 &&
        memcmp(tmpbuf2, MAC, ETH_ALEN) == 0)
        return true;
    // multicast -> for node from this group or for this group
    if (Filter_MAC_multicast(fMAC, (fMAC+6), MAC))
        return true;
    return false;
}



static inline bool Filter_VLAN(const uint16_t fvlanID, const uint16_t vlanID, uint16_t type)
{
    return (fvlanID == 0) || ((type == htons(ETH_P_8021Q)) && ((fvlanID & 0x0FFF) == (vlanID & 0x0FFF)));
}



static inline bool Filter_APPID(const uint16_t fappID, const uint16_t appID)
{
    return (fappID == 0) || (fappID == appID);
}



/*!
 * @fn Filter_StrPrmCmp
 * 
 * @brief: Фильтр по строковому параметру
 * 
 * @param s1 - фильтр
 * @param s2 - строка кадра
 * 
 * NOTE: фильтр может содержать не полную строку 
 *          (s1="12", s2="123" => фильтр пройден)
*/
static inline bool Filter_StrPrmCmp(const std::string &s1, const std::string &s2)
{
    return (s1.size() == 0) || (s1.compare(0, s1.size(), s2, 0, s1.size()) == 0);
}



/*!
 * @fn Filter_callOnce
 * 
 * @brief: Фильтр дублирования данных
*/
static bool Filter_callOnce(uint32_t fc, GooseIDtoNum &currGooses, const GooseID &gID, uint32_t stNum)
{
    bool ret = true;
    // вызывать не один раз => пройден
    if (!fc) {
        return ret;
    }

    auto it = std::find(currGooses.begin(), currGooses.end(), gID);
    if (it != currGooses.end()) {
        ret = (it->second != stNum);
        it->second = stNum;
    } else {
        currGooses.push_back( {gID, stNum} );
    }

    return ret;
}



/*!
 * @fn HandleFrame
 * 
 * @brief: Прохождение кадром фильтров
*/
static void HandleFrame(const GSPriv_t *priv, const EtherFrame &frame)
{
    uint16_t dataOffset;
    GooseFrame gooseFrame = {0};
    GooseFrameHeader &gooseHeader = gooseFrame.header;
    const GSFilters_t &filters = priv->filters;
    gooseHeader = RawToGooseHeader(frame, dataOffset);

    if (filters.list.size() == 0)
        return;

    for (const auto &f : filters.list) {
        if (!f.ReceiveCallback)
            continue;
        if ( !Filter_callOnce(f.callOnce, filters.currGooses, {gooseHeader.appID, 
                gooseHeader.goCBRef, gooseHeader.datSet, gooseHeader.goID}, 
                gooseHeader.stNum))
            continue;
        if ( !Filter_VLAN(f.vlanID, gooseHeader.vlanID, gooseHeader.etherType1) )
            continue;
        if ( !Filter_APPID(f.appID, gooseHeader.appID) )
            continue;
        if ( !Filter_StrPrmCmp(f.goCBRef, gooseHeader.goCBRef) )
            continue;
        if ( !Filter_StrPrmCmp(f.datSet, gooseHeader.datSet) )
            continue;
        if ( !Filter_StrPrmCmp(f.goID, gooseHeader.goID) )
            continue;
        if ( !Filter_destMAC(f.destMAC, gooseHeader.destMAC) )
            continue;
        if ( !Filter_srcMAC(f.sourceMAC, gooseHeader.sourceMAC) )
            continue;
        gooseFrame.data.SetupLogger(priv->logger);
        gooseFrame.data.Init(((const uint8_t *)frame.header.ether_dhost+dataOffset));
        f.ReceiveCallback(gooseFrame, f.id, f.priv);
        if (!f.notUnique)
            return;
    }
}



/*!
 * @fn isGooseFrame
 * 
 * @brief: Раняя проверка ethertype
*/
static bool isGooseFrame(const EtherFrame_u &frame)
{
    const uint8_t *p = frame.buf;
    const uint16_t *type;
    
    // mac (12)
    type = (const uint16_t *)(p+12);
    if (*type == htons(GOOSE_ETHER_TYPE)) {
        return true;
    } else {
        // mac (12) + 802.1q type (2) + vlanID (2)
        type = (const uint16_t *)(p+16);
        if (*type == htons(GOOSE_ETHER_TYPE)) {
            return true;
        }        
    }

    return false;
}



std::string MAC_to_string(const uint8_t *MAC, char splitter)
{
    char buf[128];
    std::string format;
    format.reserve(ETH_ALEN*3);
    sprintf(buf, "%%hhx%c", splitter);
    for (int i = 0; i < ETH_ALEN; ++i) {
        format += buf;
    }
    format.pop_back();
    sprintf(buf, format.c_str(), 
        MAC[0], MAC[1], MAC[2],
        MAC[3], MAC[4], MAC[5]
    );
    return std::string(buf); 
}



void string_to_MAC(const std::string &MAC, char splitter, uint8_t *data)
{
    char buf[128];
    std::string format;
    format.reserve(ETH_ALEN*3);
    sprintf(buf, "%%hhx%c", splitter);
    for (int i = 0; i < ETH_ALEN; ++i) {
        format += buf;
    }
    format.pop_back();
    sscanf(MAC.c_str(), format.c_str(), 
        &data[0], &data[1], &data[2],
        &data[3], &data[4], &data[5]
    );
}



