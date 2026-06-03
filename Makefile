TARGET = SVMDriver
SDK   = macosx26.5
KDK   = /Library/Developer/KDKs/KDK_26.5_25F71.kdk

CXX   = xcrun -sdk $(SDK) cc
SRC   = SVMDriver.cpp
OBJ   = $(SRC:.cpp=.o)

KERNEL_FRAMEWORK = $(shell xcrun -sdk macosx26.5 --show-sdk-path)/System/Library/Frameworks/Kernel.framework

CXXFLAGS = \
    -arch x86_64 \
    -mkernel \
    -fapple-kext \
    -fno-builtin \
    -fno-exceptions \
    -fno-rtti \
    -fno-stack-protector \
    -nostdlib \
    -fno-common \
    -fno-vectorize \
    -fno-slp-vectorize \
    -DKERNEL \
    -D__APPLE__ \
    -D__KERNEL__ \
    -I$(KERNEL_FRAMEWORK)/Headers \
    -I$(KERNEL_FRAMEWORK)/Headers/IOKit \
    -I$(KERNEL_FRAMEWORK)/Headers/libkern \
    -I$(KERNEL_FRAMEWORK)/PrivateHeaders \
    -Os \
    -Wall \
    -Werror

LDFLAGS = \
    -arch x86_64 \
    -Xlinker -kext \
    -Xlinker -final_output -Xlinker $(TARGET) \
    -nostdlib \
    -fapple-kext \
    -lkmod \
    -lkmodc++

all: $(TARGET).kext/Contents/MacOS/$(TARGET)

$(TARGET).kext/Contents/MacOS/$(TARGET): $(OBJ) Info.plist
	mkdir -p $(TARGET).kext/Contents/MacOS
	$(CXX) $(LDFLAGS) -o $@ $(OBJ)
	cp Info.plist $(TARGET).kext/Contents/Info.plist
	@echo "=== Built: $(TARGET).kext ==="

%.o: %.cpp AMDSVM.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf *.o $(TARGET).kext

install: $(TARGET).kext/Contents/MacOS/$(TARGET)
	sudo cp -R $(TARGET).kext /Library/Extensions/
	sudo chown -R root:wheel /Library/Extensions/$(TARGET).kext
	sudo kextutil -v /Library/Extensions/$(TARGET).kext

.PHONY: all clean install
