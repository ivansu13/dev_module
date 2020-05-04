# $FreeBSD$

.PATH:  ${.CURDIR}/../../../dev/devmsg

KMOD    = devmsg
SRCS    = devmsg.c
SRCS += device_if.h bus_if.h pci_if.h devmsg.h

DEBUG_FLAGS=-g
.include <bsd.kmod.mk>

