COMMON_H = \
    $(top_srcdir)/include/sys/crypto/api.h \
    $(top_srcdir)/include/sys/crypto/common.h

KERNEL_H =

USER_H =

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_KERNEL
kerneldir = /usr/src/spl-$(VERSION)/include/sys/crypto
kernel_HEADERS = $(KERNEL_H)
endif