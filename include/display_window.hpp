#pragma once

class DisplayWindow {
public:
    DisplayWindow(unsigned logicalWidth, unsigned logicalHeight);
    int run();

private:
    unsigned width_;
    unsigned height_;
};
