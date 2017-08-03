M5_UTIL:=${CURDIR}/../../../util/m5
M5_INCLUDE:=${CURDIR}/../../../include/

# Export CFLAGS so that subMAKEs don't define any flags we don't want.
CFLAGS=-I ${M5_INCLUDE} -O2
export CFLAGS

bin/X86/linux/m5-exit: src/m5-exit.c ${M5_UTIL}/m5op_x86.o
	gcc -DM5 $< ${M5_UTIL}/m5op_x86.o ${CFLAGS} -o $@

${M5_UTIL}/m5op_x86.o:
	$(MAKE) -C ${M5_UTIL} -f Makefile.x86 m5op_x86.o

clean:
	rm -f m5-exit m5op.h
	$(MAKE) -C ${M5_UTIL} -f Makefile.x86 clean
