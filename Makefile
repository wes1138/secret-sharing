SOURCES := $(wildcard *.c src/**/*.c *.cpp src/**/*.cpp)
OBJECTS := $(SOURCES:.c=.o)
OBJECTS := $(OBJECTS:.cpp=.o)
HEADERS := $(wildcard *.h include/*.h)

# COMMON   := -O2 -Wall -Wformat=2 -march=native
COMMON   := -O2 -Wall -march=native
CFLAGS   := $(CFLAGS) $(COMMON)
CXXFLAGS := $(CXXFLAGS) $(COMMON)
CC       := gcc
CXX      := g++
LD       := $(CXX)
LDFLAGS  := $(LDFLAGS)  # -L/path/to/libs/
LDADD    := -lntl
INCLUDE  :=  # -I../path/to/headers/
DEFS     :=  # -DLINUX

TARGET   := sshare

.PHONY : all
all : $(TARGET)

# {{{ for debugging
DBGFLAGS := -g
debug : CFLAGS += $(DBGFLAGS)
debug : CXXFLAGS += $(DBGFLAGS)
debug : all
.PHONY : debug
# }}}

$(TARGET) : $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $^ $(LDADD)

%.o : %.cpp $(HEADERS)
	$(CXX) $(DEFS) $(INCLUDE) $(CXXFLAGS) -c $< -o $@

%.o : %.c $(HEADERS)
	$(CC) $(DEFS) $(INCLUDE) $(CFLAGS) -c $< -o $@

.PHONY : clean
clean :
	rm -f $(TARGET) $(OBJECTS)

# vim:ft=make:foldmethod=marker:foldmarker={{{,}}}

