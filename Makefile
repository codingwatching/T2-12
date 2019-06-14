SHELL = /bin/bash
# 'packages' must install first, then pkgconfig
SUBDIRS:=packages pkgconfig lkms pru apps files services 
HOSTNAME:=$(shell uname -n)
HOSTMACH:=$(shell uname -m)
ON_TILE:=
#ifeq ($(HOSTNAME),T2-12)
ifeq ($(HOSTNAME),beaglebone)
ifeq ($(HOSTMACH),armv7l)
ON_TILE:=true
endif
endif

all:	$(SUBDIRS) 

touch:	$(SUBDIRS) 

clean:	$(SUBDIRS)

realclean:	clean $(SUBDIRS)

ifeq ($(ON_TILE),)
install:	
	$(error Must be on tile to install))

test:	FORCE
	$(error Must be on tile to test))
else
install:	$(SUBDIRS)

test:	FORCE
	$(MAKE) -C tests
endif

TAR_SWITCHES+=--exclude=tools --exclude=*~ --exclude=.git --exclude=doc/internal --exclude=spikes --exclude-backups
TAR_SWITCHES+=--exclude=extra

TAR_SWITCHES+=--mtime="1969-01-01 00:00:00"
TAR_SWITCHES+=--owner=0 --group=0 --numeric-owner 

cdmd:	FORCE
	MPWD=`pwd`;BASE=`basename $$MPWD`;echo $$MPWD for $$BASE;pushd ..;tar cvzf $$BASE-built.tgz $(TAR_SWITCHES) $$BASE;cp -f $$BASE-built.tgz /home/debian/CDM-TGZS/;/home/t2/GITHUB/MFM/bin/mfzmake make - cdmd-$$BASE.mfz $$BASE-built.tgz;popd

$(SUBDIRS):	FORCE
	$(MAKE) -C $@ $(MAKECMDGOALS)


.PHONY:	all clean realclean install test FORCE HOSTCHECK

