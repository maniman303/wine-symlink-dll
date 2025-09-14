# Makefile for building Winelib DLL with wineg++

DLLNAME = wine-symlink
SRC     = main.cpp
SPEC    = main.spec
OUT     = $(DLLNAME).dll

WINEGCC = wineg++ -m64
WINECMD = /opt/wine-stable/bin/wine64 cmd

all: $(OUT)

$(OUT): $(SRC) $(SPEC)
	$(WINEGCC) -shared -std=c++17 -o $(OUT) $(SRC) $(SPEC)

test: $(OUT)
	$(WINECMD) /c "rundll32 $(OUT).so,Test"

clean:
	rm -f $(OUT) *.o *.so *.dll.so