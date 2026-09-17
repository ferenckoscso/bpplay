CC := clang
# -mmacosx-version-min was missing entirely, so clang defaulted the
# deployment target to the build machine's own SDK version -- the
# resulting binary required whatever macOS the build happened to run
# on (e.g. minos 27.0), and modern ld64 also emits LC_DYLD_CHAINED_FIXUPS
# for such a high target, a load command old dyld versions don't
# understand ("dyld: cannot load 'bpplay' (load command 0x80000034 is
# unknown)" on macOS 10.12.6). bpplay.c already has an
# AvailabilityMacros-gated fallback for the pre-12.0
# kAudioObjectPropertyElementMaster/Main rename, so 10.12 should build
# and run correctly once the target is actually set this low.
CFLAGS := -O2 -mmacosx-version-min=10.12
FRAMEWORKS := -framework CoreAudio -framework CoreFoundation -framework IOKit
TARGET := bpplay
SRC := bpplay.c
HEADERS := dsd.h source.h lang.h dr_flac.h alac.h

.PHONY: all universal clean

all: $(TARGET)

$(TARGET): $(SRC) $(HEADERS)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(FRAMEWORKS)

universal: $(SRC) $(HEADERS)
	$(CC) $(CFLAGS) -arch x86_64 -arch arm64 $(SRC) -o $(TARGET) $(FRAMEWORKS)

clean:
	rm -f $(TARGET)
