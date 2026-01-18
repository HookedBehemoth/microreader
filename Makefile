CC  := clang -m32 -g #-fsanitize=undefined
CXX := clang++ -m32 -g #-fsanitize=undefined

DEFINES := \
	-DBASEDIR="\"./\"" \
	-DMINIZ_NO_MALLOC -DMINIZ_NO_DEFLATE_APIS -DMINIZ_NO_STDIO -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ZLIB_APIS \
  -DXML_GE=1 -DXML_CONTEXT_BYTES=1024 \ # Saves roughly 500 bytes of RAM
	# -DMEMCANARY
LIBSOURCES := \
	core/content/epub/EpubLoader.cpp core/content/css/EpubCssParser.cpp \
	core/content/zip/ZipParser.cpp core/fs/Fs.cpp \
	core/content/xml/xml_util.cpp core/content/xml/Stream.cpp \
	core/content/epub/Container.cpp core/content/epub/Content.cpp core/content/epub/TocNcx.cpp \
	core/content/epub/FileResolver.cpp \
	lib/miniz/miniz.cpp \
	lib/expat/xmlparse.c lib/expat/xmlrole.c lib/expat/xmltok_impl.c lib/expat/xmltok_ns.c lib/expat/xmltok.c
SOURCES := main.cpp $(LIBSOURCES)
OBJECTS := $(patsubst %.c,build/%.o,$(patsubst %.cpp,build/%.o,$(SOURCES)))
DEPENDS := $(patsubst %.c,build/%.d,$(patsubst %.cpp,build/%.d,$(SOURCES)))
LDFLAGS := 
INCLUDE := -Iextern -Icore -Ilib/expat -Ilib/miniz
FLAGS := $(DEFINES) $(INCLUDE) \
	-ffunction-sections -fdata-sections \
	-Os -Wall -Wextra \
	-fno-exceptions -Wno-unused-function -MMD

CCFLAGS := $(FLAGS) -std=c23
CXXFLAGS := $(FLAGS) -std=c++26 -fno-rtti

parser: $(OBJECTS)
	@$(CXX) $(LDFLAGS) -Wl,--gc-sections -Os -o parser $(OBJECTS) $(LDFLAGS)
	@echo linking $@
build/%.o: %.c
	@mkdir -p $(dir $@)
	@$(CC) $(CCFLAGS) -c $< -o $@
	@echo building $<
build/%.o: %.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo building $<
clean:
	rm -rf build
	rm -f parser xmltest

.PHONY: all clean

-include $(DEPENDS)
