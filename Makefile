
SOURCES = main.cpp XmlParser.cpp
FLAGS = -Os -Wall -Wextra -std=c++26 -fno-exceptions

parser: $(SOURCES)
	g++ $(FLAGS) -o parser $(SOURCES)
