all: sync compile

realcompile:
	cc -o minixbuild minixbuild.c -I include -lxenctrl -lxenstore -ggdb

compile:
	ssh socrates "cd /usr/src/minixbuilder; make realcompile;" 

sync:
	rsync -avP . socrates:/usr/src/minixbuilder

