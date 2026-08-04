CC := clang
CFLAGS := -O2
FRAMEWORKS := -framework CoreAudio -framework CoreFoundation -framework IOKit
TARGET := bpplay
SRC := bpplay.c
HEADERS := dsd.h source.h lang.h dr_flac.h

.PHONY: all universal clean

all: $(TARGET)

$(TARGET): $(SRC) $(HEADERS)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(FRAMEWORKS)

universal: $(SRC) $(HEADERS)
	$(CC) $(CFLAGS) -arch x86_64 -arch arm64 $(SRC) -o $(TARGET) $(FRAMEWORKS)

clean:
	rm -f $(TARGET)
