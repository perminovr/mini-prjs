#!/usr/bin/python3.6

import sys
import getopt
import json

wsroot='${workspaceRoot}/'

def Usage():
	print ("""\
-j filename
	property file
-s filename
	file with sources to add
-i filename
	file with includes to add
-c
	clear the file before
-e
	exit after clear the file (use: '-e 1')\
""")
	exit(0)

def writeConfig():
	with open(PROP_FILE, 'w') as f:
		json.dump(prop, f, indent=4)

if len(sys.argv) < 5:
	Usage()

PROP_FILE = incFile = srcFile = ""
clrflg = exitfl = 0
opts, args = getopt.getopt(sys.argv[1:], 'hce:j:s:i:')
for opt, arg in opts:
	if opt == '-j':
		PROP_FILE = arg
	elif opt == '-s':
		srcFile = arg
	elif opt == '-i':
		incFile = arg
	elif opt == '-c':
		clrflg=1
	elif opt == '-e':
		exitfl=1
	else:
		Usage()		

prop = json.load( open(PROP_FILE) )

includes = prop["configurations"][0]["includePath"]
sources = prop["configurations"][0]["browse"]["path"]

if clrflg == 1:
	includes.clear()
	sources.clear()
	if exitfl == 1:
		writeConfig()
		exit(0)

new_sources = [ line.rstrip('\n') for line in open(srcFile,'r') ]
new_includes = [ line.rstrip('\n') for line in open(incFile,'r') ]

for ni in new_includes:
	ni = wsroot + '*' if ni == '.' else wsroot + ni + '/*'
	if ni not in includes:
		includes.append(ni)

for ns in new_sources:
	ns = wsroot + '*' if ns == '.' else wsroot + ns + '/*'
	if ns not in sources:
		sources.append(ns)

writeConfig()

