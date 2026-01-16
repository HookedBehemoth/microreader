CXX := clang++ -m32 -g #-fsanitize=undefined

DEFINES := -DMINIZ_NO_MALLOC -DMINIZ_NO_DEFLATE_APIS -DMINIZ_NO_STDIO -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ZLIB_APIS # -DMEMCANARY
SOURCES := main.cpp XmlParser.cpp EpubLoader.cpp EpubCssParser.cpp ZipParser.cpp Fs.cpp xml/TocNcx.cpp EpubContentResolver.cpp miniz.cpp
OBJECTS := $(patsubst %.cpp,build/%.o,$(SOURCES))
DEPENDS := $(patsubst %.cpp,build/%.d,$(SOURCES))
LDFLAGS := -lexpat
FLAGS := $(DEFINES) -ffunction-sections -fdata-sections -Os -Wall -Wextra -std=c++26 -fno-exceptions -Wno-unused-function -MMD

parser: $(OBJECTS)
	$(CXX) $(LDFLAGS) -Wl,--gc-sections -Os -o parser $(OBJECTS) $(LDFLAGS)
build/%.o: %.cpp | build
	$(CXX) $(FLAGS) -c $< -o $@

build:
	mkdir -p build
	mkdir -p build/xml

test: XmlTest.cpp XmlParser.cpp
	$(CXX) $(FLAGS) -o xmltest XmlTest.cpp XmlParser.cpp
	./xmltest

clean:
	rm -rf build
	rm -f parser xmltest

.PHONY: all clean

-include $(DEPENDS)
