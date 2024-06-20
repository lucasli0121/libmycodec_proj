
FFMPEG=/develop_tools/ffmpeg
DEBUG=-g
PARAM=-c $(DEBUG) -o
CFLAG=-I$(FFMPEG)/include -I/usr/include -I$(MYLIB_HOME)/inc -DMY_LINUX  #-DMY_DEBUG
LDFLAG=-L$(FFMPEG)/lib -L/usr/lib -lavcodec -lavutil -lswscale -lavformat -lxml2 -L$(MYLIB_HOME)/lib -lMylibd


SRC=$(wildcard ./*.cpp)
OBJ=$(foreach n,$(SRC),./$(notdir $(subst .cpp,.o,$(n))))

all:$(OBJ)

$(OBJ):$(SRC)
	$(CC) $(PARAM) $@ ./$(notdir $*).cpp $(CFLAG) $(LDFLAG)

clean:
	-$(RM) -vf $(OBJ)
