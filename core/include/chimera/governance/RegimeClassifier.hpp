#pragma once

namespace chimera {

class RegimeClassifier {
public:
    RegimeClassifier();
    
    void setQuality(int q);
    int quality() const;

private:
    int quality_;
};

} // namespace chimera
