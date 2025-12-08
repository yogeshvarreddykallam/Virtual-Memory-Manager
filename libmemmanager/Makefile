.PHONY: default clean

CFLAGS = -D_XOPEN_SOURCE=700

default: libmemmanager.a

libmemmanager.a: init.o vmm.o logger.o
	$(AR) rcs $@ $^

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ -c $< $(LDFLAGS) $(LDLIBS)

clean:
	rm -f libmemmanager.a *.o