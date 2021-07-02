all: warmup

warmup:
	g++ -I/opt/halon/include/ -I/usr/local/include/ -fPIC -shared warmup.cpp -o warmup.so
