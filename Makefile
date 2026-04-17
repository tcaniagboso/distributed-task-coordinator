CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -Iinclude -pthread

# Common source files
COMMON_SRCS = \
    src/net/net_utils.cpp \
    src/rpc/client.cpp \
    src/rpc/server_connection.cpp \
    src/coordinator/coordinator.cpp \
    src/router/router.cpp \
    src/client/client.cpp \
    src/worker/worker.cpp

# Top sources
TOP_SRCS = \
    src/top/top.cpp

# Targets
all: worker coordinator router client top

worker: src/worker/main.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) $^ -o worker

coordinator: src/coordinator/main.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) $^ -o coordinator

router: src/router/main.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) $^ -o router

client: src/client/main.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) $^ -o client

# 🔥 TOP TARGET
top: src/top/main.cpp $(COMMON_SRCS) $(TOP_SRCS)
	$(CXX) $(CXXFLAGS) $^ -o top -lncurses

clean:
	rm -f worker coordinator router client top

.PHONY: all clean