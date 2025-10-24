#pragma once

#include <optional>
#include <string>

class script_server
{
public:
    script_server();
    ~script_server();

    bool initialize(int port);
    void close();
    [[nodiscard]] std::optional<std::string> receive_script();
    [[nodiscard]] std::string last_error() const;

private:
    struct impl;
    impl* pimpl;
};