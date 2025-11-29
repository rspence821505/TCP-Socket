CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

all: binary_client binary_client_zerocopy binary_mock_server

binary_client: binary_client.cpp binary_protocol.hpp
	$(CXX) $(CXXFLAGS) binary_client.cpp -o binary_client

binary_client_zerocopy: binary_client_zerocopy.cpp binary_protocol.hpp ring_buffer.hpp
	$(CXX) $(CXXFLAGS) binary_client_zerocopy.cpp -o binary_client_zerocopy

binary_mock_server: binary_mock_server.cpp binary_protocol.hpp
	$(CXX) $(CXXFLAGS) binary_mock_server.cpp -o binary_mock_server

clean:
	rm -f binary_client binary_client_zerocopy binary_mock_server

.PHONY: all clean