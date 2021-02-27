
#include "DrvLogger.hpp"
#include <unistd.h>
#include <cstdarg>
#include <cstring>
#include <cstdio>
#include <cstdint>



DrvLogger::DrvLogger(int fd, DrvLogger::LogLevel lvl)
{
    this->fd = fd;
    this->lvl = lvl;
}


DrvLogger::DrvLogger(int fd)
{
    this->fd = fd;
    this->lvl = DrvLogger::LogLevel::No;
}



void DrvLogger::SetupDebugLvl(DrvLogger::LogLevel lvl)
{
    this->lvl = lvl;
}



void DrvLogger::Print(DrvLogger::LogLevel lvl, const char *format, ...) const
{
    if (this->lvl == DrvLogger::LogLevel::No || lvl > this->lvl) {
        return;
	}

	std::va_list args;
    va_start(args, format);
    vdprintf(this->fd, format, args);
    va_end(args);
}



void DrvLogger::DumpData(const void* data, size_t size) const
{
	if (this->lvl != DrvLogger::LogLevel::Dump) {
		return;
	}

    #define DUMPHEX_WIDTH 16
	char ascii[DUMPHEX_WIDTH+1];
	size_t i, j;
	ascii[DUMPHEX_WIDTH] = '\0';
	for (i = 0; i < size; ++i) {
		dprintf(this->fd, "%02X ", ((uint8_t*)data)[i]);
		if (((uint8_t*)data)[i] >= ' ' && ((uint8_t*)data)[i] <= '~') {
			ascii[i % DUMPHEX_WIDTH] = ((uint8_t*)data)[i];
		} else {
			ascii[i % DUMPHEX_WIDTH] = '.';
		}
		if ((i+1) % 8 == 0 || i+1 == size) {
			dprintf(this->fd, " ");
			if ((i+1) % DUMPHEX_WIDTH == 0) {
				dprintf(this->fd, "|  %s \n", ascii);
			} else if (i+1 == size) {
				ascii[(i+1) % DUMPHEX_WIDTH] = '\0';
				if ((i+1) % DUMPHEX_WIDTH <= 8) {
					dprintf(this->fd, " ");
				}
				for (j = (i+1) % DUMPHEX_WIDTH; j < DUMPHEX_WIDTH; ++j) {
					dprintf(this->fd, "   ");
				}
				dprintf(this->fd, "|  %s \n", ascii);
			}
		}
	}
}