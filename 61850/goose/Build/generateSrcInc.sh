#!/bin/bash

# 1 - sources dir name 
# 2 - sources type (c / cpp)
# 3 - path for results

incTMP="$3/.incTMP"
srcTMP="$3/.srcTMP"

# SEARCHING **********************************************************************

cd $1

inc_ext="hpp h"
case $2 in
c)
	src_ext="c"
	;;
cpp)
	src_ext="cpp"
	;;
*)
	;;
esac

for ii in $inc_ext; do
	INC="$(dirname `find * -name "*.${ii}"` 2>/dev/null)"
	[ "${INC}" != "" -a "${INCLUDES}" != "" ] && INCLUDES=${INCLUDES}$'\n'
	INCLUDES="${INCLUDES}${INC}"
done
for ss in $src_ext; do
	SRC="$(dirname `find * -name "*.${ss}"` 2>/dev/null)"
	[ "${SRC}" != "" -a "${SOURCES}" != "" ] && SOURCES=${SOURCES}$'\n'
	SOURCES="${SOURCES}${SRC}"
done
echo "$INCLUDES" > ${incTMP}
echo "$SOURCES" > ${srcTMP}

sort ${incTMP} -u -o ${incTMP}
sort ${srcTMP} -u -o ${srcTMP}

# INSERTING **********************************************************************

INCLUDES=`cat ${incTMP}`
SOURCES=`cat ${srcTMP}`

MKF="$3/localmake-sources"

echo > $MKF
echo "INC_$1 := \\" >> $MKF
for ii in $INCLUDES; do
	echo $'\t'"-I`pwd`/$ii \\" >> $MKF
done
echo >> $MKF
echo >> $MKF
echo >> $MKF
echo "SRC_$1 := \\" >> $MKF
for ss in $SOURCES; do
	src_dir=`pwd`/$ss
	for ts in $src_ext; do
		for fss in `find $src_dir -name "*.${ts}"`; do
			echo $'\t'"$fss \\" >> $MKF
		done
	done
done
echo >> $MKF

# END ****************************************************************************

exit 0


