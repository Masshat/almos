include config.mk

all:
	( for f in $(DIRS); do ( cd $$f ; make all TGT_LIB=$(TGT_LIB) TGT_INCLUDE=$(TGT_INCLUDE) CFLAGS="$(CFLAGS)" CC=$(CC) AR=$(AR) ) || exit 1 ; done )

clean:
	rm -f *~ $(TGT_LIB)/libTinyGL.a $(TGT_INCLUDE)/GL/*~ TAGS
	( for f in $(DIRS); do ( cd $$f ; make clean ; ) done )
