CVSROOT=:pserver:anoncvs:@bleurgh.com:/var/lib/cvs

KERNELMODULE=minix/kernel
KERNELSAVEAS=kernel

HEADERMODULE=minix/include
HEADERSAVEAS=include

TTYMODULE=minix/drivers/tty
TTYSAVEAS=drivers/tty

MEMMODULE=minix/drivers/memory
MEMSAVEAS=drivers/memory

LDRVMODULE=minix/drivers/libdriver
LDRVSAVEAS=drivers/libdriver

LOGMODULE=minix/drivers/log
LOGSAVEAS=drivers/log

SERVERSMODULE=minix/servers
SERVERSSAVEAS=servers

VBDMODULE=minix/drivers/xenvbd
VBDSAVEAS=drivers/xenvbd

MINIXIP=192.168.0.2
MINIXUSER=root
FYPHOST=192.168.1.13
SRCDIR=/usr/src
BUILDDIR=/usr/src/tools
KERNELIMAGE=$(BUILDDIR)/image
TMPIMAGE=/tmp/minix
FYPKERNELIMAGE=/etc/xen/minix

all: deploy

update: 
	@ echo; echo "*** Updating sources from CVS ***"; echo
	rsh -l $(MINIXUSER) $(MINIXIP) "cd $(SRCDIR) && cvs -d $(CVSROOT) login && cvs -d $(CVSROOT) co -d $(KERNELSAVEAS) $(KERNELMODULE);"


build: update
	@ echo; echo "*** Building kernel image ***"; echo
	rsh -l $(MINIXUSER) $(MINIXIP) "cd $(BUILDDIR) && make image";

updateheaders: 
	@ echo; echo "*** Updating headers from CVS ***"; echo
	rsh -l $(MINIXUSER) $(MINIXIP) "cd $(SRCDIR) && cvs -d $(CVSROOT) login && cvs -d $(CVSROOT) co -d $(HEADERSAVEAS) $(HEADERMODULE);"

installheaders: updateheaders
	@ echo; echo "*** Installing headers ***"; echo
	rsh -l $(MINIXUSER) $(MINIXIP) "cd $(SRCDIR)/$(HEADERSAVEAS) && make install";


updatetty: 
	@ echo; echo "*** Updating tty driver from CVS ***"; echo
	rsh -l $(MINIXUSER) $(MINIXIP) "cd $(SRCDIR) && cvs -d $(CVSROOT) login && cvs -d $(CVSROOT) co -d $(TTYSAVEAS) $(TTYMODULE);"

updatemem: 
	@ echo; echo "*** Updating mem driver from CVS ***"; echo
	rsh -l $(MINIXUSER) $(MINIXIP) "cd $(SRCDIR) && cvs -d $(CVSROOT) login && cvs -d $(CVSROOT) co -d $(MEMSAVEAS) $(MEMMODULE);"

updateldrv: 
	@ echo; echo "*** Updating mem driver from CVS ***"; echo
	rsh -l $(MINIXUSER) $(MINIXIP) "cd $(SRCDIR) && cvs -d $(CVSROOT) login && cvs -d $(CVSROOT) co -d $(LDRVSAVEAS) $(LDRVMODULE);"

updatelog: 
	@ echo; echo "*** Updating mem driver from CVS ***"; echo
	rsh -l $(MINIXUSER) $(MINIXIP) "cd $(SRCDIR) && cvs -d $(CVSROOT) login && cvs -d $(CVSROOT) co -d $(LOGSAVEAS) $(LOGMODULE);"

updateservers: 
	@ echo; echo "*** Updating servers driver from CVS ***"; echo
	rsh -l $(MINIXUSER) $(MINIXIP) "cd $(SRCDIR) && cvs -d $(CVSROOT) login && cvs -d $(CVSROOT) co -d $(SERVERSSAVEAS) $(SERVERSMODULE);"

buildis: updateservers
	@ echo; echo "*** Building is server ***"; echo
	rsh -l $(MINIXUSER) $(MINIXIP) "cd $(SRCDIR)/servers/is && make";

updatevbd: 
	@ echo; echo "*** Updating vbd driver from CVS ***"; echo
	rsh -l $(MINIXUSER) $(MINIXIP) "cd $(SRCDIR) && cvs -d $(CVSROOT) login && cvs -d $(CVSROOT) co -d $(VBDSAVEAS) $(VBDMODULE);"

buildvbd: updatevbd
	@ echo; echo "*** Building VBD driver ***"; echo
	rsh -l $(MINIXUSER) $(MINIXIP) "cd $(SRCDIR)/drivers/xenvbd && make";


buildtty: updatetty
	@ echo; echo "*** Building tty driver ***"; echo
	rsh -l $(MINIXUSER) $(MINIXIP) "cd $(SRCDIR)/$(TTYSAVEAS) && make";

cleantty: 
	@ echo; echo "*** Cleaning tty driver ***"; echo
	rsh -l $(MINIXUSER) $(MINIXIP) "cd $(SRCDIR)/$(TTYSAVEAS) && make clean";

kernelclean: 
	@ echo; echo "*** Cleaning kernel sources ***"; echo
	rsh -l $(MINIXUSER) $(MINIXIP) "cd $(SRCDIR)/$(KERNELSAVEAS) && make clean";

tags:
	find . -name '*.[sch]' | xargs etags --members --typedefs --defines --globals

deploy: build
	@echo "Copying image from minix host"
	rcp root@$(MINIXIP):$(KERNELIMAGE) $(TMPIMAGE);
	@echo "Copying image to fyp host"
	scp $(TMPIMAGE) root@$(FYPHOST):$(FYPKERNELIMAGE);

indentivan:
	find . -name '*.[c]' | xargs indent -kr -l100 -i8

indentast:
	find . -name '*.[c]' | xargs indent -kr -l100 -i2