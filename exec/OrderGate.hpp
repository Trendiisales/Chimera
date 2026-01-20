#pragma once
#include <string>

class OrderGate {
public:
    bool allow(const std::string& = "") const { return true; }
    void onSubmit() {}
    void onComplete() {}
};
