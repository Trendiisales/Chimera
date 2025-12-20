#pragma once
#include <fstream>
#include <string>
namespace binance {
class BinaryLogWriter {
public:
  explicit BinaryLogWriter(const std::string& p);
  ~BinaryLogWriter();
  void write(const void*,size_t);
private:
  std::ofstream out;
};
}
