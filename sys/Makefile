
SUBDIR=	dietlibc libm libpthread libgomp \
	libz libcrypto libupc libphoenix TinyGL 


default: all

$(SUBDIR)::
	@$(MAKE) -C $@ $(MAKECMDGOALS)

all clean: $(SUBDIR)
