#include "core/CryptoEngine.hpp"
#include "binance/BinanceHFTFeed.hpp"
namespace core { void CryptoEngine::run(){ binance::BinanceHFTFeed f; f.start(); } }
