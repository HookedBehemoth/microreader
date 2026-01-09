
SOURCES = main.cpp XmlParser.cpp
FLAGS = -Os -Wall -Wextra -std=c++26 -fno-exceptions

parser: $(SOURCES)
	g++ $(FLAGS) -o parser $(SOURCES)

test: XmlTest.cpp XmlParser.cpp
	g++ $(FLAGS) -o xmltest XmlTest.cpp XmlParser.cpp
	./xmltest
