# Targets
#   make dll   : Windows DLL + rebuild trigger   (via build_dll.bat)
#   make cli   : CLI exe linking against the DLL (via build_cli.bat, run after dll)
#   make wasm  : WASM + JS                       (emcc must be in PATH)
#   make all   : dll + cli + wasm
#   make clean : remove build/

SHELL       := cmd
.SHELLFLAGS := /c

OUT     := build
SRC     := fernbuffer.cpp
EMCC    := emcc
EMFLAGS := -O3 -std=c++17 -I. -DFERNBUFFER_BUILD_DLL \
           -sEXPORTED_FUNCTIONS=_fernbuffer_encode,_fernbuffer_decode,_fernbuffer_free,_malloc,_free \
           -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU32,UTF8ToString,stringToUTF8,lengthBytesUTF8 \
           -sALLOW_MEMORY_GROWTH=1 -sNO_EXIT_RUNTIME=1 -sMODULARIZE=1 -sEXPORT_NAME=FernBuffer \
           --post-js src/wrapper.js

.PHONY: all dll cli wasm clean

all: dll cli wasm

$(OUT):
	if not exist $(OUT) mkdir $(OUT)

dll:
	build_dll.bat

cli:
	build_cli.bat

wasm: | $(OUT)
	$(EMCC) $(EMFLAGS) $(SRC) -o $(OUT)/fernbuffer.js

clean:
	if exist $(OUT) rmdir /s /q $(OUT)
