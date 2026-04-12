all:
	g++ recorder.cpp -o recorder -lportaudio -lsndfile -lpthread -std=c++17
