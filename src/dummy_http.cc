// g++ -O3 -DNDEBUG -pthread -std=c++17 dummy_http.cc -o dummy_http

#include "current/blocks/http/api.h"

int main() {
  auto& http = HTTP(current::net::BarePort(8181));
  auto const http_scope = http.Register("/", URLPathArgs::CountMask::Any, [](Request r) {
    r("{\"result\":false}");
  });
  http.Join();
}
