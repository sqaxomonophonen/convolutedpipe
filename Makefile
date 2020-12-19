#OPT=-O0 -g # debug
OPT=-O2
COMMONCFLAGS=$(OPT) -Wall

CFLAGS=$(COMMONCFLAGS)
CXXFLAGS=$(COMMONCFLAGS)

all: convolutedpipe

main.o: main.c
	$(CC) $(CFLAGS) -c $<

miniaudio.o: miniaudio.c miniaudio.h
	$(CC) $(CFLAGS) -c $<

opt.o: opt.c opt.h
	$(CC) $(CFLAGS) -c $<

conv.o: conv.cc conv.h
	$(CXX) $(CXXFLAGS) -c $<

AudioFFT.o: AudioFFT.cpp
	$(CXX) $(CXXFLAGS) -c $<

FFTConvolver.o: FFTConvolver.cpp
	$(CXX) $(CXXFLAGS) -c $<

TwoStageFFTConvolver.o: TwoStageFFTConvolver.cpp
	$(CXX) $(CXXFLAGS) -c $<

Utilities.o: Utilities.cpp
	$(CXX) $(CXXFLAGS) -c $<

convolutedpipe: main.o conv.o opt.o miniaudio.o Utilities.o TwoStageFFTConvolver.o FFTConvolver.o AudioFFT.o
	$(CXX) $(LDFLAGS) $^ -o $@ -lm -ldl -pthread

clean:
	rm -f convolutedpipe *.o
