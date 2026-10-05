MELIS_BASE   ?= /home/dwi/projects/privat/F133/D1s-Melis
TARGET_BOARD ?= d1s-nezha
export MELIS_BASE TARGET_BOARD

ifeq ($(KBUILD_EXTMOD),)

.PHONY: all modules prepare test clean

all modules: prepare
	$(MAKE) -C $(MELIS_BASE) M=$(CURDIR)/build modules

prepare:
	@mkdir -p build/include build/package build/temporary
	@[ "$$(readlink build/Makefile)" = ../Makefile ] || ln -sfn ../Makefile build/Makefile
	@[ "$$(readlink build/lds)" = ../lds ] || ln -sfn ../lds build/lds
	@for source in src/*.c; do target="build/$${source##*/}"; [ "$$(readlink "$$target")" = "../$$source" ] || ln -sfn "../$$source" "$$target"; done
	@if [ ! -f build/include/full_body_argb.h ] || [ assets/full_body.png -nt build/include/full_body_argb.h ] || [ tools/img2h.sh -nt build/include/full_body_argb.h ]; then \
		bash tools/img2h.sh -o build/include/.full_body_argb.h.tmp -n full_body assets/full_body.png && \
		if cmp -s build/include/.full_body_argb.h.tmp build/include/full_body_argb.h; then rm build/include/.full_body_argb.h.tmp; else mv build/include/.full_body_argb.h.tmp build/include/full_body_argb.h; fi; \
	fi

test:
	@mkdir -p build/tests
	$(CC) -std=c99 -Wall -Wextra -Werror -Iinclude src/canbus_slcan.c src/canbus_pdc.c tests/test_slcan.c -o build/tests/test_slcan
	@build/tests/test_slcan
	$(CC) -std=c99 -Wall -Wextra -Werror -Iinclude src/canbus_media.c tests/test_media.c -o build/tests/test_media
	@build/tests/test_media

clean:
	rm -rf build

else

subdir-ccflags-y :=	-I$(src)/../include \
			-I$(src)/include \
			-I$(srctree)/include/melis/ekernel \
			-I$(srctree)/include/melis/kernel/drivers \
			-I$(srctree)/include/melis/arch/chip \
			-I$(srctree)/include/melis/module \
			-I$(srctree)/include/melis/kernel \
			-I$(srctree)/include/melis/board \
			-I$(srctree)/include/melis/emodules \
			-I$(srctree)/include/melis/libs/libc

usrlibs-y +=		--start-group \
			-lsyscall -lminic \
			--end-group

MOD_NAME  := canbus
SUF_NAME  := mod
PRJ_PATH  := $(obj)/package/
TEMP_PATH := $(obj)/temporary/

$(MOD_NAME)-objs += canbus_mod.o
$(MOD_NAME)-objs += canbus_pdc.o
$(MOD_NAME)-objs += canbus_slcan.o
$(MOD_NAME)-objs += canbus_media.o
$(MOD_NAME)-objs += canbus_render.o
$(MOD_NAME)-objs += magic.o

include $(MELIS_BASE)/scripts/Makefile.mods

endif
