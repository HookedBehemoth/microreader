
SOURCES = main.cpp XmlParser.cpp EpubLoader.cpp ZipParser.cpp Fs.cpp miniz.cpp
OBJECTS = $(SOURCES:.cpp=.o)
FLAGS = -Os -Wall -Wextra -std=c++26 -fno-exceptions -Wno-unused-function

parser: $(OBJECTS)
	g++ -m32 -g -o parser $(OBJECTS) $(LDFLAGS)

%.o: %.cpp
	g++ -m32 -g $(FLAGS) -c $< -o $@

test: XmlTest.cpp XmlParser.cpp
	g++ -m32 -g $(FLAGS) -o xmltest XmlTest.cpp XmlParser.cpp
	./xmltest

clean:
	rm parser xmltest $(OBJECTS)
