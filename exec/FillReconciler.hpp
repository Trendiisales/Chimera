#pragma once
#include <string>

class FillReconciler {
public:
    void onSubmit(const std::string&, double, bool, const std::string&) {}
    void onExecutionReport(const std::string&, double, double, bool) {}
};
