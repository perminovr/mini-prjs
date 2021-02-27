/*
 *  ber_integer.h
 *
 *  Copyright 2013-2018 Michael Zillgith
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

#ifndef BER_INTEGER_H_
#define BER_INTEGER_H_

#include <stdint.h>
#include <libiec61850/libiec61850_common_api.h>

#ifndef ATTRIBUTE_PACKED
#warning "ATTRIBUTE_PACKED does not set!"
#endif

typedef struct ATTRIBUTE_PACKED {
	uint8_t size;
	uint8_t maxSize;
	uint8_t* octets;
} Asn1PrimitiveValue;

extern int /* 1 - if conversion is possible, 0 - out of range */
BerInteger_toInt32(Asn1PrimitiveValue* self, int32_t* nativeValue);

extern int /* 1 - if conversion is possible, 0 - out of range */
BerInteger_toUint32(Asn1PrimitiveValue* self, uint32_t* nativeValue);

extern int /* 1 - if conversion is possible, 0 - out of range */
BerInteger_toInt64(Asn1PrimitiveValue* self, int64_t* nativeValue);

extern int /* 1 - if conversion is possible, 0 - out of range */
BerInteger_toUint64(Asn1PrimitiveValue* self, uint64_t* nativeValue);

#endif /* BER_INTEGER_H_ */
