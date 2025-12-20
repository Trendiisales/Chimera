#include "binance/BinaryLogWriter.hpp"
#include <stdexcept>
namespace binance {
BinaryLogWriter::BinaryLogWriter(const std::string& p):out(p,std::ios::binary){
  if(!out.is_open()) throw std::runtime_error("blog open failed");
}
BinaryLogWriter::~BinaryLogWriter(){if(out.is_open())out.close();}
void BinaryLogWriter::write(const void* d,size_t n){out.write((const char*)d,n);}
}
