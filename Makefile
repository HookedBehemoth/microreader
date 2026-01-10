
SOURCES = main.cpp XmlParser.cpp EpubLoader.cpp miniz.cpp
FLAGS = -Os -Wall -Wextra -std=c++26 -fno-exceptions -Wno-unused-function
LDFLAGS = -lglfw

parser: $(SOURCES)
	g++ -g $(FLAGS) -o parser $(SOURCES)

test: XmlTest.cpp XmlParser.cpp
	g++ $(FLAGS) -o xmltest XmlTest.cpp XmlParser.cpp
	./xmltest

clean:
	rm parser xmltest
