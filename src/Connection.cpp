#include "Connection.hpp"

Connection::Connection(std::shared_ptr<Node> src, std::shared_ptr<Node> dst, PortType type, bool is_alsa)
    : source(std::move(src)), destination(std::move(dst)), type(type), is_alsa(is_alsa) {
}
