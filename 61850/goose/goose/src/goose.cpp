
#include <iostream>
#include <unistd.h>

#include "GooseSub.hpp"
#include "goose.hpp"

using std::cout;
using std::cin;
using std::endl;



#define DEF_INTERFACE "enp2s0f0"



#pragma pack(push, 4)

struct TypesTest {
    unsigned int iVal;
    float32 fVal;
    struct {
        uint32_t u8;
        uint32_t u16;
        uint32_t u32;
        uint32_t uu32;
        uint64_t u64;
        uint64_t uu64;
        uint64_t uuu64;
        uint64_t uuuu64;
        uint32_t Bool;
        int i8;
        int ii8;
        int i16;
        int ii16;
        int iii16;
        int i32;
        int ii32;
        int iii32;
        int iiii32;
        int64_t i64;
        int64_t ii64;
        int64_t iii64;
        int64_t iiii64;
        int64_t iiiii64;
        int64_t iiiiii64;
        int64_t iiiiiii64;
        int64_t iiiiiiii64;
        float32 f32;
        float32 ff32;
        float32 fff32;
        float64 f64;
        float64 ff64;
    } s1;
    struct {
        uint32_t Enum;
        char s32[IECTYPE_MAXSZ];
        char s64[IECTYPE_MAXSZ];
        char s65[IECTYPE_MAXSZ];
        char s129[IECTYPE_MAXSZ];
        char s255[IECTYPE_MAXSZ];
        uint32_t Dbpos;
        uint32_t Tcmd;
        uint8_t o64[IECTYPE_MAXSZ];
        uint8_t Unicode[IECTYPE_MAXSZ];
        uint64_t EntryTime;
        uint32_t Check;
        uint32_t Quality;
        uint64_t t;
    } s2;
    struct {
        uint8_t o1[IECTYPE_MAXSZ];
        uint8_t o2[IECTYPE_MAXSZ];
        uint32_t Check;
        uint32_t Quality;
        uint64_t t; 
        char s255[IECTYPE_MAXSZ];
    } s3;
};

#pragma pack(pop)



static uint64_t cnt = 0;

void ReceiveCallback(const GooseFrame &frame, uint32_t id, void *priv)
{

    printf("ReceiveCallback OK %lu\n", cnt);
    cnt++;

    IECAllData data = frame.data.ToIECAllData();
    TypesTest *tt = (TypesTest *)&(data[0]);

    printf("%s\n", tt->s3.s255);

}



int main(int argc, char **argv)
{
    GooseSub sub(DEF_INTERFACE);
    GooseSub::Filter filter = {0};

    DrvLogger logger (1, DrvLogger::LogLevel::Dump);
    sub.SetupLogger(&logger);

    //filter.callOnce = 1;
    filter.ReceiveCallback = ReceiveCallback;
    sub.Subcribe(filter);

    for (;;)
        usleep(100000);

    return 0;
}
