
SOURCES = main.cpp XmlParser.cpp EpubLoader.cpp ZipParser.cpp Fs.cpp miniz.cpp
OBJECTS = $(SOURCES:.cpp=.o)
FLAGS = -Os -Wall -Wextra -std=c++26 -fno-exceptions -Wno-unused-function
LDFLAGS = -lglfw

parser: $(OBJECTS)
	g++ -g -o parser $(OBJECTS) $(LDFLAGS)

%.o: %.cpp
	g++ -g $(FLAGS) -c $< -o $@

test: XmlTest.cpp XmlParser.cpp
	g++ $(FLAGS) -o xmltest XmlTest.cpp XmlParser.cpp
	./xmltest

clean:
	rm parser xmltest $(OBJECTS)
