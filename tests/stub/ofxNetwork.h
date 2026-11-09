#pragma once

// Stand-in for ofxNetwork, for the syntax check only (make wrapper): the
// calls example-osc makes, with openFrameworks 0.12's signatures.

class ofxUDPManager {
public:
    bool Create() { return true; }
    bool Connect(const char*, unsigned short) { return true; }
    bool Bind(unsigned short) { return true; }
    bool SetNonBlocking(bool) { return true; }
    int  Send(const char*, const int size) { return size; }
    int  Receive(char*, const int) { return 0; }
};
