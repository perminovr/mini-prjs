/*
 *  ber_integer.c
 *
 *  Copyright 2013-2019 Michael Zillgith
 *
 *  This file is part of libIEC61850.
 *
 *  libIEC61850 is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  libIEC61850 is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with libIEC61850.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  See COPYING file for the complete license text.
 */

#include "ber_integer.h"

int /* 1 - if conversion is possible, 0 - out of range */
BerInteger_toInt32(Asn1PrimitiveValue* self, int32_t* nativeValue)
{
    if (self->size < 5) {
        uint8_t* buf = self->octets;
        int i;

        if (buf[0] & 0x80) /* sign extension */
            *nativeValue = 0xffffffff;
        else
            *nativeValue = 0;

        for (i = 0; i < self->size; i++)
            *nativeValue = (*nativeValue << 8) | buf[i];

        return 1;
    }
    else
        return 0;
}

int /* 1 - if conversion is possible, 0 - out of range */
BerInteger_toUint32(Asn1PrimitiveValue* self, uint32_t* nativeValue)
{
    if (self->size < 6) {
        uint8_t* buf = self->octets;
        int i;

        *nativeValue = 0;

        for (i = 0; i < self->size; i++)
            *nativeValue = (*nativeValue << 8) | buf[i];

        return 1;
    }
    else
        return 0;
}

int /* 1 - if conversion is possible, 0 - out of range */
BerInteger_toInt64(Asn1PrimitiveValue* self, int64_t* nativeValue)
{
    if (self->size <= 9) {
        uint8_t* buf = self->octets;
        int i;

        if (buf[0] & 0x80) /* sign extension */
            *nativeValue = 0xffffffffffffffff;
        else
            *nativeValue = 0;

        for (i = 0; i < self->size; i++)
            *nativeValue = (*nativeValue << 8) | buf[i];

        return 1;
    }
    else
        return 0;
}

int /* 1 - if conversion is possible, 0 - out of range */
BerInteger_toUint64(Asn1PrimitiveValue* self, uint64_t* nativeValue)
{
    if (self->size <= 9) {
        uint8_t* buf = self->octets;
        int i;

        *nativeValue = 0;

        for (i = 0; i < self->size; i++)
            *nativeValue = (*nativeValue << 8) | buf[i];

        return 1;
    }
    else
        return 0;
}