#!/bin/bash

# 1 - sources dir name 

# SEARCHING **********************************************************************

PREREQS=`find * -name "$1prere*"`
PREREQS="$PREREQS `find $1/* -name "prere*"`"

# RENAME *************************************************************************

for pr in $PREREQS; do
	sed -i "s/\(^.*\)_[a-z0-9A-Z]\+\(\s*:=\s*.*$\)/\1_$1\2/" $pr
done

# END ****************************************************************************

exit 0