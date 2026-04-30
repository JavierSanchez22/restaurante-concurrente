CXX = g++
CXXFLAGS = -Wall -std=c++20 -pthread
LDFLAGS = -lrt

SRC_DIR = src
BUILD_DIR = build

# Archivo de salida
TARGET = $(BUILD_DIR)/restaurante

# Encuentra todos los archivos .cpp en src/
SOURCES = $(wildcard $(SRC_DIR)/*.cpp)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)/*

run: $(TARGET)
	./$(TARGET)