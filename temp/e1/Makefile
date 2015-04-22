.PHONY: default _prepare_dir  
  
CXXFLAGS = -std=c++98 -Wall -Wextra -g -O0  
GCC = g++  
LINK = $(GCC)  
  
FFMPEG_LIB = ~/work/tunehog-desktop-libs/bin/
INC = $(FFMPEG_LIB)/include  
LIB = $(FFMPEG_LIB)/lib  

echo $INC
  
OBJS = objs  
  
default: _prepare_dir Makefile hello.o  
    $(LINK) -o $(OBJS)/tool $(OBJS)/hello.o $(LIB)/libavformat.a $(LIB)/libavcodec.a $(LIB)/libavutil.a $(LIB)/librtmp.a $(LIB)/libx264.a $(LIB)/libmp3lame.a $(LIB)/libfdk-aac.a -lz -lm -lpthread  
  
_prepare_dir:  
    @mkdir -p $(OBJS)  
  
hello.o: hello.cpp  
    $(GCC) -c $(CXXFLAGS) -I$(INC) -o $(OBJS)/hello.o hello.cpp  
