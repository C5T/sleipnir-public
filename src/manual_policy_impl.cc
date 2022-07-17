// g++ -O3 -DNDEBUG -pthread -std=c++17 manual_policy_impl.cc -o manual_policy_impl

#include <map>
#include <vector>
#include <string>
#include <utility>
#include <thread>

#include "current/blocks/http/api.h"
#include "current/typesystem/serialization/json.h"

std::map<std::string, std::vector<std::string>> user_roles = {
  {"alice", {"eng", "web"}},
  {"bob", {"hr"}}
};

std::map<std::string, std::vector<std::pair<std::string, std::string>>> role_permissions = {
  {"eng", {{"read", "server123"}}},
  {"web", {{"read", "server123"}, {"write", "server123"}}},
  {"hr", {{"read", "database456"}}}
};

CURRENT_STRUCT(OPARequest) {
  CURRENT_FIELD(user, std::string);
  CURRENT_FIELD(action, std::string);
  CURRENT_FIELD(object, std::string);
};

CURRENT_STRUCT(OPAInput) {
  CURRENT_FIELD(input, OPARequest);
};

CURRENT_STRUCT(OPAResult) {
  CURRENT_FIELD(result, bool, false);
};

int main() {
  auto& http = HTTP(current::net::BarePort(8181));
  auto const http_scope = http.Register("/", URLPathArgs::CountMask::Any, [](Request r) {
    auto const in = ParseJSON<OPAInput>(r.body);
    OPAResult out;
    auto const cit = user_roles.find(in.input.user);
    if (cit != user_roles.end()) {
      for (auto const& role : cit->second) {
        for (auto const& action_object : role_permissions[role]) {
          if (action_object.first == in.input.action && action_object.second == in.input.object) {
            out.result = true;
            break;
          }
        }
        if (out.result) {
          break;
        }
      }
    }
    r(out);
  });
  http.Join();
}
