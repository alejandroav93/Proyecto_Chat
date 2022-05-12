all: protocol server client

protocol: register.proto
	protoc -I=. --cpp_out=. register.proto
server: server.cpp register.pb.cc
	g++ -std=c++11 -o server server.cpp register.pb.cc -lpthread -lprotobuf
client: client.cpp register.pb.cc
	g++ -std=c++11 -o client client.cpp register.pb.cc -lpthread -lprotobuf
