all:
	g++ -o np_simple np_simple.cpp
	g++ -o np_single_proc np_single_proc.cpp
	g++ -o np_multi_proc np_multi_proc.cpp

clean:
	rm -f *.txt np_simple np_single_proc np_multi_proc *.exe