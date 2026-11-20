#pragma once

struct _XDisplay;

namespace layoutfix {

class XConnection {
public:
    XConnection();
    ~XConnection();
    XConnection(const XConnection&) = delete;
    XConnection& operator=(const XConnection&) = delete;

    bool open();
    _XDisplay* ctrl() const { return ctrl_; }
    _XDisplay* data() const { return data_; }
    int ctrl_fd() const;
    int data_fd() const;
    void flush_ctrl() const;

private:
    _XDisplay* ctrl_ = nullptr;
    _XDisplay* data_ = nullptr;
};

}  // namespace layoutfix
