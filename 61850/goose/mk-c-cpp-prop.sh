#!/bin/bash

incTMP=".incTMP"
srcTMP=".srcTMP"
INSERT_SCRIPT="`pwd`/Build/insert.py"
PROP="`pwd`/.vscode/c_cpp_properties.json"

while true; do
	case $1 in
	clear)
		python3 ${INSERT_SCRIPT} -j "${PROP}" -c -e 1
		shift 1
		;;
	exit)
		exit
		;;
	* )
		break
	;;
	esac
done

# SEARCHING **********************************************************************

inc_ext="hpp h"
src_ext="cpp c"

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

echo
echo "Detected headers directories:"
echo "$INCLUDES"
echo "$INCLUDES" > ${incTMP}
echo
echo "Detected sources directories:"
echo "$SOURCES"
echo "$SOURCES" > ${srcTMP}
echo

python3 ${INSERT_SCRIPT} -j "${PROP}" -i "${incTMP}" -s "${srcTMP}"

rm -rf ${incTMP} ${srcTMP}

exit 0















# INSERTING **********************************************************************
# TODO need to add ${wsroot} + /* to INCLUDES & SOURCES

# ************** ******** **************
__INCLUDE_START="\\\"includePath\\\""						# \"includePath\"
__INCLUDE_END="]\\(,\\|\\s\\|$\\)"							# ]\(,\|\s\|$\)
__INCLUDE_AREA="/${__INCLUDE_START}/,/${__INCLUDE_END}/"	# /\"includePath\"/,/]\(,\|\s\|$\)/
__SRC_START1="\\\"browse\\\""								# \"browse\"
__SRC_END1="}\\(,\\|\\s\\|\$\\)"							# }\(,\|\s\|$\)
__SRC_AREA1="/${__SRC_START1}/,/${__SRC_END1}/"				# /\"browse\"/,/}\(,\|\s\|$\)/
__SRC_START2="\\\"path\\\""									# \"path\"
__SRC_END2="]\\(,\\|\\s\\|\$\\)"							# ]\(,\|\s\|$\)
__SRC_AREA2="/${__SRC_START2}/,/${__SRC_END2}/"				# /\"path\"/,/]\(,\|\s\|$\)/
__SRC_AREA="${__SRC_AREA1} { ${__SRC_AREA2}"				# /\"browse\"/,/}\(,\|\s\|$\)/ { /\"path\"/,/]\(,\|\s\|$\)/

__LAST_RECORD="^\\s*\\\"\\\"\\(\\s\\|$\\).*"				# ^\s*\"\"\(\s\|$\).*
__NOT_COMMA_RECORDS="\\\".+\\\"\\s*\\([^,:]\\|$\\)"			# \".+\"\s*\([^,:]\|$\)

__ESCAPE_SPACES="s/\\(\\s\\)/\\\\\\1/g"						# s/\(\s\)/\\\1/g
__ESCAPE_ASTERIKS="s/\\*/\\\\\\*/g"							# s/\*/\\\*/g
__ESCAPE_DOLL="s/\\$/\\\\\\$/g"								# s/\$/\\\$/g
__ESCAPE_BSLASH="s/\\//\\\\\\//g"							# s/\//\\\//gp

# ************** INCLUDES **************
# get spaces
TABS="$(sed -n -e "s/\(^\s*\)${__INCLUDE_START}.*/\1/p" $PROP)    "
# escape all spaces
TABS="$(echo "$TABS" | sed "${__ESCAPE_SPACES}")"
#
for ii in $INCLUDES; do
	# escape * $ / chars
	jj="$(echo $ii | sed -n -e "${__ESCAPE_ASTERIKS}" -e "${__ESCAPE_DOLL}" -e "${__ESCAPE_BSLASH}" -e "p")"
	# find exists
	res="$(sed -n "${__INCLUDE_AREA} s/$jj/&/p" $PROP | head -1)"
	if [ "$res" == "" ]; then
		# path -> "path",
		jj="\"$jj\","
		# add as first record with properly num of spaces
		sed -i "/${__INCLUDE_START}/ a\
		${TABS}$jj" $PROP
	fi
done
# insert , after records
sed -i "${__INCLUDE_AREA} s/${__NOT_COMMA_RECORDS}/&,/p " $PROP
# search last record ""
res="$(sed -n "${__INCLUDE_AREA} s/${__LAST_RECORD}/&/p" $PROP)"
if [ "$res" == "" ]; then
	# add last record
	sed -i "${__INCLUDE_AREA} {
	/${__INCLUDE_END}/ i\
	${TABS}\"\"
	}" $PROP
fi
# ************** SOURCES **************
# get spaces
TABS="$(sed -n -e "${__SRC_AREA1} { s/\(^\s*\)${__SRC_START2}.*/\1/p }" $PROP)    "
# escape all spaces
TABS="$(echo "$TABS" | sed "${__ESCAPE_SPACES}")"
#
for ss in $SOURCES; do
	# escape * $ / chars
	jj="$(echo $ss | sed -n -e "${__ESCAPE_ASTERIKS}" -e "${__ESCAPE_DOLL}" -e "${__ESCAPE_BSLASH}" -e "p")"
	# find exists
	res="$(sed -n "${__SRC_AREA} s/$jj/&/p }" $PROP | head -1)"
	if [ "$res" == "" ]; then
		# path -> "path",
		jj="\"$jj\","
		# add as first record with properly num of spaces
		sed -i "${__SRC_AREA1} { /${__SRC_START2}/ a\
		${TABS}$jj
		}" $PROP
	fi
done
# insert , after records
sed -i "${__SRC_AREA} s/${__NOT_COMMA_RECORDS}/&,/p } " $PROP
# search last record ""
res="$(sed -n "${__SRC_AREA} s/${__LAST_RECORD}/&/p }" $PROP)"
if [ "$res" == "" ]; then
	# add last record
	sed -i "${__SRC_AREA} {
		/${__SRC_END2}/ i\
		${TABS}\"\"
	}}" $PROP
fi




