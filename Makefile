CXX = clang++ -m32 -g #-fsanitize=undefined

# DEFINES := -DMEMCANARY
SOURCES := main.cpp XmlParser.cpp EpubLoader.cpp EpubCssParser.cpp ZipParser.cpp Fs.cpp miniz.cpp
OBJECTS := $(patsubst %.cpp,build/%.o,$(SOURCES))
DEPENDS := $(patsubst %.cpp,build/%.d,$(SOURCES))

FLAGS := $(DEFINES) -Os -Wall -Wextra -std=c++26 -fno-exceptions -Wno-unused-function -MMD

parser: $(OBJECTS)
	$(CXX) -o parser $(OBJECTS) $(LDFLAGS)
build/%.o: %.cpp | build
	$(CXX) $(FLAGS) -c $< -o $@

build:
	mkdir -p build

test: XmlTest.cpp XmlParser.cpp
	$(CXX) $(FLAGS) -o xmltest XmlTest.cpp XmlParser.cpp
	./xmltest

clean:
	rm -rf build
	rm -f parser xmltest

.PHONY: all clean

-include $(DEPENDS)
