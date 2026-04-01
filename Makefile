CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra $(shell pkg-config --cflags gtkmm-3.0 jack alsa)
LDFLAGS = $(shell pkg-config --libs gtkmm-3.0 jack alsa)

SOURCES = src/main.cpp src/JackGraph.cpp src/JackClient.cpp \
          src/AlsaClient.cpp src/GraphCanvas.cpp src/Node.cpp \
          src/Connection.cpp src/Config.cpp
OBJECTS = $(SOURCES:.cpp=.o)
TARGET = jack-graph

$(TARGET): $(OBJECTS)
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	install -d $(DESTDIR)/usr/bin
	install $(TARGET) $(DESTDIR)/usr/bin
	install -d $(DESTDIR)/usr/share/applications
	install resources/jack-graph.desktop $(DESTDIR)/usr/share/applications/

.PHONY: clean install
