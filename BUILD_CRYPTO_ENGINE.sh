set -e

rm -rf crypto_engine
mkdir -p crypto_engine/include/binance crypto_engine/src/binance

cat <<'CMAKE' > crypto_engine/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(crypto_engine LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
add_library(crypto_engine
  src/CryptoEngine.cpp
  src/binance/OrderBook.cpp
  src/binance/DeltaGate.cpp
  src/binance/VenueHealth.cpp
  src/binance/BinaryLogWriter.cpp
  src/binance/BinanceRestClient.cpp
  src/binance/BinanceDepthFeed.cpp
)
target_include_directories(crypto_engine PUBLIC include)
CMAKE

cat <<'HPP' > crypto_engine/include/binance/OrderBook.hpp
#pragma once
#include <vector>
namespace binance {
struct PriceLevel { double price; double qty; };
class OrderBook {
public:
  void load_snapshot(const std::vector<PriceLevel>& b,const std::vector<PriceLevel>& a);
  bool empty() const;
  double best_bid() const;
  double best_ask() const;
private:
  std::vector<PriceLevel> bids, asks;
};
}
HPP

cat <<'CPP' > crypto_engine/src/binance/OrderBook.cpp
#include "binance/OrderBook.hpp"
namespace binance {
void OrderBook::load_snapshot(const std::vector<PriceLevel>& b,const std::vector<PriceLevel>& a){bids=b;asks=a;}
bool OrderBook::empty() const {return bids.empty()||asks.empty();}
double OrderBook::best_bid() const {return bids.front().price;}
double OrderBook::best_ask() const {return asks.front().price;}
}
CPP

cat <<'HPP' > crypto_engine/include/binance/DeltaGate.hpp
#pragma once
#include <cstdint>
namespace binance {
class DeltaGate {
public:
  void reset(uint64_t u);
  bool accept(uint64_t u);
private:
  uint64_t last=0;
  bool init=false;
};
}
HPP

cat <<'CPP' > crypto_engine/src/binance/DeltaGate.cpp
#include "binance/DeltaGate.hpp"
namespace binance {
void DeltaGate::reset(uint64_t u){last=u;init=true;}
bool DeltaGate::accept(uint64_t u){if(!init||u<=last)return false;last=u;return true;}
}
CPP

cat <<'HPP' > crypto_engine/include/binance/VenueHealth.hpp
#pragma once
#include <atomic>
namespace binance {
enum class Health{GREEN,RED};
class VenueHealth {
public:
  void set(Health h){state.store(h);}
  Health get() const {return state.load();}
private:
  std::atomic<Health> state{Health::RED};
};
}
HPP

cat <<'CPP' > crypto_engine/src/binance/VenueHealth.cpp
#include "binance/VenueHealth.hpp"
CPP

cat <<'HPP' > crypto_engine/include/binance/BinaryLogWriter.hpp
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
HPP

cat <<'CPP' > crypto_engine/src/binance/BinaryLogWriter.cpp
#include "binance/BinaryLogWriter.hpp"
#include <stdexcept>
namespace binance {
BinaryLogWriter::BinaryLogWriter(const std::string& p):out(p,std::ios::binary){
  if(!out.is_open()) throw std::runtime_error("blog open failed");
}
BinaryLogWriter::~BinaryLogWriter(){if(out.is_open())out.close();}
void BinaryLogWriter::write(const void* d,size_t n){out.write((const char*)d,n);}
}
CPP

cat <<'HPP' > crypto_engine/include/binance/BinanceRestClient.hpp
#pragma once
#include "OrderBook.hpp"
namespace binance {
class BinanceRestClient {
public:
  void snapshot(std::vector<PriceLevel>& b,std::vector<PriceLevel>& a,uint64_t& u);
};
}
HPP

cat <<'CPP' > crypto_engine/src/binance/BinanceRestClient.cpp
#include "binance/BinanceRestClient.hpp"
namespace binance {
void BinanceRestClient::snapshot(std::vector<PriceLevel>& b,std::vector<PriceLevel>& a,uint64_t& u){
  b={{100,1}}; a={{101,1}}; u=1;
}
}
CPP

cat <<'HPP' > crypto_engine/include/binance/BinanceDepthFeed.hpp
#pragma once
#include "OrderBook.hpp"
#include "DeltaGate.hpp"
#include "VenueHealth.hpp"
#include "BinaryLogWriter.hpp"
#include "BinanceRestClient.hpp"
namespace binance {
class BinanceDepthFeed {
public:
  BinanceDepthFeed(BinanceRestClient&,OrderBook&,DeltaGate&,VenueHealth&,BinaryLogWriter&);
  void start();
private:
  BinanceRestClient& r; OrderBook& b; DeltaGate& g; VenueHealth& h; BinaryLogWriter& log;
};
}
HPP

cat <<'CPP' > crypto_engine/src/binance/BinanceDepthFeed.cpp
#include "binance/BinanceDepthFeed.hpp"
namespace binance {
BinanceDepthFeed::BinanceDepthFeed(BinanceRestClient& r_,OrderBook& b_,DeltaGate& g_,VenueHealth& h_,BinaryLogWriter& l_)
:r(r_),b(b_),g(g_),h(h_),log(l_){}
void BinanceDepthFeed::start(){
  std::vector<PriceLevel> bids,asks; uint64_t u=0;
  r.snapshot(bids,asks,u); b.load_snapshot(bids,asks); g.reset(u); h.set(Health::GREEN);
}
}
CPP

cat <<'CPP' > crypto_engine/src/CryptoEngine.cpp
#include "binance/BinanceDepthFeed.hpp"
#include "binance/BinaryLogWriter.hpp"
#include <iostream>
using namespace binance;
void run_crypto_engine(){
  BinanceRestClient r; OrderBook b; DeltaGate g; VenueHealth h; BinaryLogWriter log("test.blog");
  BinanceDepthFeed feed(r,b,g,h,log); feed.start();
  std::cout<<"ENGINE OK\n";
}
CPP

echo "OK"
