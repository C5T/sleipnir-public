// git clone https://github.com/c5t/current
// g++ -Wall -std=c++17 -O3 -DNDEBUG -pthread rego.cc -o rego

#include <cstdarg>
#include <iostream>
#include <map>

#include "current/blocks/http/api.h"
#include "current/blocks/json/json.h"
#include "current/blocks/xterm/progress.h"
#include "current/blocks/xterm/vt100.h"
#include "current/bricks/dflags/dflags.h"
#include "current/bricks/file/file.h"

using namespace current::json;
using namespace current::vt100;

// === INSERT CUSTOM TYPE INSTEAD OF `policy_input_t` IF NEEDED ===
CURRENT_STRUCT(OPARequest) {
  CURRENT_FIELD(user, std::string);
  CURRENT_FIELD(action, std::string);
  CURRENT_FIELD(object, std::string);
};

CURRENT_STRUCT(OPAInput) {
  CURRENT_FIELD(input, OPARequest);
};

using policy_input_t = OPAInput;
// === INSERT CUSTOM TYPE INSTEAD OF `policy_input_t` IF NEEDED ===

DEFINE_uint16(p, 0u, "Set `-p $PORT` to listen on `localhost:$PORT`.");
DEFINE_bool(d, false, "Set `-d` to daemonize the HTTP server on port `-p`.");
DEFINE_string(queries, "", "Set to run a local perftest, separating JSON parsing from policy evaluation.");
DEFINE_string(output, "", "Set to write the results of running against `--queries`.");

using OPAString = Optional<std::string>;
using OPANumber = Optional<double>;
using OPABoolean = Optional<bool>;

struct ArrayCreationCapacity final {
  size_t const capacity;
  ArrayCreationCapacity(size_t capacity) : capacity(capacity) {}
};

class OPAValue final {
  // TODO(dkorolev): Make it `private`, once the builtins are cleaned up.
 public:
  JSONValue opa_value;  // TODO: OPA's `undefined` is not the same as `null`, of course.

 public:
  OPAValue() : opa_value(JSONNull()) {}
  OPAValue(std::nullptr_t) : opa_value(JSONNull()) {}
  OPAValue(JSONValue opa_value) : opa_value(std::move(opa_value)) {}
  OPAValue(OPAString const& s) {
    if (Exists(s)) {
      opa_value = JSONString(Value(s));
    }
  }
  OPAValue(OPANumber const& v) {
    if (Exists(v)) {
      opa_value = JSONNumber(Value(v));
    }
  }
  OPAValue(OPABoolean const& b) {
    if (Exists(b)) {
      opa_value = JSONBoolean(Value(b));
    }
  }

  OPAValue(std::string s) : opa_value(JSONString(std::move(s))) {}
  OPAValue(char const* s) : opa_value(JSONString(s)) {}
  OPAValue(int i) : opa_value(JSONNumber(i)) {}
  OPAValue(double d) : opa_value(JSONNumber(d)) {}
  OPAValue(bool b) : opa_value(JSONBoolean(b)) {}

  OPAValue(ArrayCreationCapacity capacity) : opa_value(JSONArray()) {
    Value<JSONArray>(opa_value).elements.reserve(capacity.capacity);
  }

  OPAValue& operator=(ArrayCreationCapacity capacity) {
    opa_value = JSONArray();
    Value<JSONArray>(opa_value).elements.reserve(capacity.capacity);
    return *this;
  }

  OPAValue& operator=(size_t v) {
    opa_value = JSONNumber(static_cast<double>(v));
    return *this;
  }

  // NOTE: This is used for `Null()`, special case?
  OPAValue& operator=(JSONValue const& rhs){
    opa_value = rhs;
    return *this;
  }

  void DoResetToUndefined() { opa_value = JSONNull(); }
  bool DoIsUndefined() const { return Exists<JSONNull>(opa_value); }

  bool DoIsArray() const { return Exists<JSONArray>(opa_value); }
  bool DoIsObject() const { return Exists<JSONObject>(opa_value); }

  bool DoIsStringEqualTo(char const* desired) const {
    return Exists<JSONString>(opa_value) && Value<JSONString>(opa_value).string == desired;
  }

  bool DoIsBooleanEqualTo(bool desired) const {
    return Exists<JSONBoolean>(opa_value) && Value<JSONBoolean>(opa_value).boolean == desired;
  }

  void DoMakeObject() { opa_value = JSONObject(); }

  void DoMakeNull() {
    opa_value = JSONNull();  // TODO: See above re. `null` vs. `undefined`.
  }

  OPAValue DoGetValueByKey(char const* key) const {
    return Exists<JSONObject>(opa_value) ? Value<JSONObject>(opa_value)[key] : OPAValue();
  }

  OPAValue DoGetValueByKey(std::string const& key) const {
    return Exists<JSONObject>(opa_value) ? Value<JSONObject>(opa_value)[key] : OPAValue();
  }

  OPAValue DoGetValueByKey(size_t key) const {
    if (!Exists<JSONArray>(opa_value)) {
      return OPAValue();
    }
    JSONArray const& array = Value<JSONArray>(opa_value);
    if (key < array.size()) {
      return array[key];
    } else {
      return OPAValue();
    }
  }

  OPAValue DoGetValueByKey(OPANumber key) const {
    if (!Exists(key)) {
      return OPAValue();
    }
    double const v = Value(key);
    size_t const i = static_cast<size_t>(v);
    if (static_cast<double>(i) == v) {
      return DoGetValueByKey(i);
    } else {
      return OPAValue();
    }
  }

  void DoSetValueForKey(char const* key, OPAValue const& value) {
    if (Exists<JSONObject>(opa_value)) {
      Value<JSONObject>(opa_value).push_back(key, value.opa_value);
    }
  }
};

using OPAArray = OPAValue;  // This is ugly, but will do for now.

inline void ResetToUndefined(OPAValue& value) { value.DoResetToUndefined(); }
inline void ResetToUndefined(Optional<std::string>& value) { value = nullptr; }

inline void MakeNull(OPAValue& value) { value.DoMakeNull(); }
inline void MakeObject(OPAValue& value) { value.DoMakeObject(); }

inline bool IsArray(OPAValue const& value) { return value.DoIsArray(); }
inline bool IsObject(OPAValue const& value) { return value.DoIsObject(); }

inline bool IsUndefined(OPAValue const& value) { return value.DoIsUndefined(); }
inline bool IsUndefined(Optional<std::string> const& value) { return Exists(value); }

inline bool IsStringEqualTo(OPAValue const& value, char const* s) { return value.DoIsStringEqualTo(s); }
inline bool IsStringEqualTo(OPAString const& value, char const* s) { return Exists(value) && Value(value) == s; }
inline bool IsStringEqualTo(std::string const& value, char const* s) { return value == s; }
inline bool IsBooleanEqualTo(OPAValue const& value, bool b) { return value.DoIsBooleanEqualTo(b); }
inline bool IsBooleanEqualTo(bool value, bool b) { return value == b; }

inline size_t Len(OPAValue const& v0) {
  JSONValue const& v = v0.opa_value;
  if (Exists<JSONArray>(v)) {
    return Value<JSONArray>(v).size();
  } else if (Exists<JSONObject>(v)) {
    return Value<JSONObject>(v).size();
  } else {
    return 0u;
  }
}

inline bool AreLocalsEqual(OPANumber a, OPANumber b) {
  if (Exists(a) != Exists(b)) {
    return false;
  }
  if (Exists(a)) {
    return Value(a) == Value(b);
  } else {
    return true;
  }
}

inline bool AreLocalsEqual(OPANumber a, size_t b) { return Exists(a) && Value(a) == static_cast<double>(b); }
inline bool AreLocalsEqual(size_t a, OPANumber b) { return Exists(b) && static_cast<double>(a) == Value(b); }
inline bool AreLocalsEqual(size_t a, size_t b) { return a == b; }

inline bool AreLocalsEqual(OPAValue const& a, OPANumber b) {
  if (!Exists(b)) {
    return !Exists(a);
  } else {
    return Exists<JSONNumber>(a.opa_value) && Value<JSONNumber>(a.opa_value).number == Value(b);
  }
}
inline bool AreLocalsEqual(OPANumber a, OPAValue const& b) {
  return AreLocalsEqual(b, a);
}

inline bool AreLocalsEqual(OPAValue const& a, size_t b) {
  // NOTE: Would need tighter type checks.
  return Exists<JSONNumber>(a.opa_value) && Value<JSONNumber>(a.opa_value).number == static_cast<double>(b);
}
inline bool AreLocalsEqual(size_t a, OPAValue const& b) {
  return AreLocalsEqual(b, a);
}

inline bool AreJSONValuesEqual(JSONValue const& a, JSONValue const& b) {
  struct JSONValueComparator final {
    JSONValue const& b;
    bool result = false;
    JSONValueComparator(JSONValue const& b) : b(b) {}
    void operator()(JSONNull) { result = Exists<JSONNull>(b); }
    void operator()(JSONNumber const& a) { result = Exists<JSONNumber>(b) && a.number == Value<JSONNumber>(b).number; }
    void operator()(JSONString const& a) { result = Exists<JSONString>(b) && a.string == Value<JSONString>(b).string; }
    void operator()(JSONBoolean const& a) {
      result = Exists<JSONBoolean>(b) && a.boolean == Value<JSONBoolean>(b).boolean;
    }
    void operator()(JSONArray const& a) {
      if (Exists<JSONArray>(b)) {
        auto const& v = Value<JSONArray>(b).elements;
        if (a.size() == v.size()) {
          bool fail = false;
          for (size_t i = 0u; i < a.size(); ++i) {
            if (!AreJSONValuesEqual(a[i], v[i])) {
              fail = true;
              break;
            }
          }
          if (!fail) {
            result = true;
          }
        }
      }
    }
    void operator()(JSONObject const& a) {
      if (Exists<JSONObject>(b)) {
        auto const& v = Value<JSONObject>(b).fields;
        if (a.size() == v.size()) {
          bool fail = false;
          for (auto const& field : a.fields) {
            auto const cit = v.find(field.first);
            if (cit == v.end() || !AreJSONValuesEqual(field.second, cit->second)) {
              fail = true;
              break;
            }
          }
          if (!fail) {
            result = true;
          }
        }
      }
    }
  };
  JSONValueComparator comparator(b);
  a.Call(comparator);
  return comparator.result;
}

inline bool AreLocalsEqual(OPAValue const& a, OPAValue const& b) {
  return AreJSONValuesEqual(a.opa_value, b.opa_value);
}

// TODO(dkorolev): Should return a custom type that can be assigned to a string "variable" too!
inline OPAValue Undefined() {
  OPAValue undefined;
  return undefined;
}
inline OPAValue Object() {
  OPAValue object;
  object.DoMakeObject();
  return object;
}
inline JSONValue Null() {
  static JSONValue null = JSONNull();  // TODO(dkorolev): This `null` is the same as `undefined`.
  return null;
}

// TODO(dkorolev): Use a proper `template` for user-defined types here.
inline OPAValue GetValueByKey(OPAValue const& object, char const* key) { return object.DoGetValueByKey(key); }
inline OPAValue GetValueByKey(OPAValue const& object, size_t key) { return object.DoGetValueByKey(key); }
inline OPAValue GetValueByKey(OPAValue const& object, OPANumber key) { return object.DoGetValueByKey(key); }
inline OPAValue GetValueByKey(OPAValue const& object, OPAValue const& key) {
  if (Exists<JSONNumber>(key.opa_value)) {
    // NOTE: This `OPANumber` is extra work for the CPU. Can improve.
    return object.DoGetValueByKey(OPANumber(Value<JSONNumber>(key.opa_value).number));
  } else if (Exists<JSONString>(key.opa_value)) {
    return object.DoGetValueByKey(Value<JSONString>(key.opa_value).string);
  } else {
    return Undefined();
  }
}

template <typename T>
inline void SetValueForKey(OPAValue& target, char const* key, T&& value) {
  target.DoSetValueForKey(key, std::forward<T>(value));
}

void PushBack(OPAValue& array, OPAValue element) {
  if (Exists<JSONArray>(array.opa_value)) {
    Value<JSONArray>(array.opa_value).push_back(std::move(element.opa_value));
  }
}

void PushBack(OPAValue& array, char const* element) {
  if (Exists<JSONArray>(array.opa_value)) {
    Value<JSONArray>(array.opa_value).push_back(JSONString(element));
  }
}

template <typename K, typename V, class F>
inline void Scan(OPAValue const& source, K& key, V& value, F&& f) {
  if (Exists<JSONArray>(source.opa_value)) {
    JSONArray const& a = Value<JSONArray>(source.opa_value);
    for (size_t i = 0u; i < a.size(); ++i) {
      key = OPANumber(i);
      value = a[i];
      f();
    }
  } else {
    // TODO: `Scan` objects?
    // TODO: Error handling?
  }
}

inline OPAValue opa_plus(OPAValue const& a, OPAValue const& b) {
  if (Exists<JSONNumber>(a.opa_value) && Exists<JSONNumber>(b.opa_value)) {
    return Value<JSONNumber>(a.opa_value).number + Value<JSONNumber>(b.opa_value).number;
  } else {
    return nullptr;
  }
}

inline OPAValue opa_minus(OPAValue const& a, OPAValue const& b) {
  // TODO(dkorolev): Only work on numbers!
  if (Exists<JSONNumber>(a.opa_value) && Exists<JSONNumber>(b.opa_value)) {
    return Value<JSONNumber>(a.opa_value).number - Value<JSONNumber>(b.opa_value).number;
  } else {
    return nullptr;
  }
}

inline OPAValue opa_mul(OPAValue const& a, OPAValue const& b) {
  if (Exists<JSONNumber>(a.opa_value) && Exists<JSONNumber>(b.opa_value)) {
    return Value<JSONNumber>(a.opa_value).number * Value<JSONNumber>(b.opa_value).number;
  } else {
    return nullptr;
  }
}

inline OPAValue opa_range(OPAValue const& a, OPAValue const& b) {
  if (Exists<JSONNumber>(a.opa_value) && Exists<JSONNumber>(b.opa_value)) {
    JSONArray array;
    int const va = static_cast<int>(Value<JSONNumber>(a.opa_value).number);
    int const vb = static_cast<int>(Value<JSONNumber>(b.opa_value).number);
    for (int i = va; i <= vb; ++i) {
      array.push_back(JSONNumber(i));
    }
    return OPAValue(array);
  } else {
    return OPAValue();
  }
}

struct OPAResult final {
  std::vector<OPAValue> result_set;
  void AddToResultSet(OPAValue value) { result_set.push_back(std::move(value)); }
  JSONValue pack() const {
    if (result_set.empty()) {
      return JSONNull();
    } else if (result_set.size() == 1u) {
      JSONValue const& v = result_set.front().opa_value;
      if (Exists<JSONObject>(v)) {
        return Value<JSONObject>(v)["result"];
      } else {
        throw std::logic_error("The response from the policy should be an object for now.");
      }
    } else {
      JSONArray array;
      for (OPAValue const& value : result_set) {
        JSONValue const& v = value.opa_value;
        if (Exists<JSONObject>(v)) {
          array.push_back(Value<JSONObject>(v)["result"]);
        } else {
          throw std::logic_error("The response from the policy should be an object for now.");
        }
      }
      return array;
    }
  }
};
struct s0 final {
  constexpr static char const *s = "result";
  template <class T>
  static decltype(std::declval<T>().result) GetValueByKeyFrom(T &&x) {
    return std::forward<T>(x).result;
  }
  static OPAValue GetValueByKeyFrom(OPAValue const &object) {
    return object.DoGetValueByKey("result");
  }
};
struct s1 final {
  constexpr static char const *s = "user";
  template <class T>
  static decltype(std::declval<T>().user) GetValueByKeyFrom(T &&x) {
    return std::forward<T>(x).user;
  }
  static OPAValue GetValueByKeyFrom(OPAValue const &object) {
    return object.DoGetValueByKey("user");
  }
};
struct s2 final {
  constexpr static char const *s = "alice";
  template <class T>
  static decltype(std::declval<T>().alice) GetValueByKeyFrom(T &&x) {
    return std::forward<T>(x).alice;
  }
  static OPAValue GetValueByKeyFrom(OPAValue const &object) {
    return object.DoGetValueByKey("alice");
  }
};
struct s3 final {
  constexpr static char const *s = "eng";
  template <class T>
  static decltype(std::declval<T>().eng) GetValueByKeyFrom(T &&x) {
    return std::forward<T>(x).eng;
  }
  static OPAValue GetValueByKeyFrom(OPAValue const &object) {
    return object.DoGetValueByKey("eng");
  }
};
struct s4 final {
  constexpr static char const *s = "web";
  template <class T>
  static decltype(std::declval<T>().web) GetValueByKeyFrom(T &&x) {
    return std::forward<T>(x).web;
  }
  static OPAValue GetValueByKeyFrom(OPAValue const &object) {
    return object.DoGetValueByKey("web");
  }
};
struct s5 final {
  constexpr static char const *s = "bob";
  template <class T>
  static decltype(std::declval<T>().bob) GetValueByKeyFrom(T &&x) {
    return std::forward<T>(x).bob;
  }
  static OPAValue GetValueByKeyFrom(OPAValue const &object) {
    return object.DoGetValueByKey("bob");
  }
};
struct s6 final {
  constexpr static char const *s = "hr";
  template <class T>
  static decltype(std::declval<T>().hr) GetValueByKeyFrom(T &&x) {
    return std::forward<T>(x).hr;
  }
  static OPAValue GetValueByKeyFrom(OPAValue const &object) {
    return object.DoGetValueByKey("hr");
  }
};
struct s7 final {
  constexpr static char const *s = "action";
  template <class T>
  static decltype(std::declval<T>().action) GetValueByKeyFrom(T &&x) {
    return std::forward<T>(x).action;
  }
  static OPAValue GetValueByKeyFrom(OPAValue const &object) {
    return object.DoGetValueByKey("action");
  }
};
struct s8 final {
  constexpr static char const *s = "read";
  template <class T>
  static decltype(std::declval<T>().read) GetValueByKeyFrom(T &&x) {
    return std::forward<T>(x).read;
  }
  static OPAValue GetValueByKeyFrom(OPAValue const &object) {
    return object.DoGetValueByKey("read");
  }
};
struct s9 final {
  constexpr static char const *s = "object";
  template <class T>
  static decltype(std::declval<T>().object) GetValueByKeyFrom(T &&x) {
    return std::forward<T>(x).object;
  }
  static OPAValue GetValueByKeyFrom(OPAValue const &object) {
    return object.DoGetValueByKey("object");
  }
};
struct s10 final {
  constexpr static char const *s = "server123";
  template <class T>
  static decltype(std::declval<T>().server123) GetValueByKeyFrom(T &&x) {
    return std::forward<T>(x).server123;
  }
  static OPAValue GetValueByKeyFrom(OPAValue const &object) {
    return object.DoGetValueByKey("server123");
  }
};
struct s11 final {
  constexpr static char const *s = "database456";
  template <class T>
  static decltype(std::declval<T>().database456) GetValueByKeyFrom(T &&x) {
    return std::forward<T>(x).database456;
  }
  static OPAValue GetValueByKeyFrom(OPAValue const &object) {
    return object.DoGetValueByKey("database456");
  }
};
struct s12 final {
  constexpr static char const *s = "write";
  template <class T>
  static decltype(std::declval<T>().write) GetValueByKeyFrom(T &&x) {
    return std::forward<T>(x).write;
  }
  static OPAValue GetValueByKeyFrom(OPAValue const &object) {
    return object.DoGetValueByKey("write");
  }
};
template <typename T1, typename T2>
decltype(auto) function_body_0(T1 &&p1, T2 &&p2) {
  OPAValue retval;
  OPAValue x1;
  OPAValue x2;
  OPAArray x3;
  OPAArray x4;
  decltype(x1) x5;
  x1 = Undefined();
  x2 = Object();
  x3 = ArrayCreationCapacity(2);
  PushBack(x3, "eng");
  PushBack(x3, "web");
  SetValueForKey(x2, "alice", x3);
  x4 = ArrayCreationCapacity(1);
  PushBack(x4, "hr");
  SetValueForKey(x2, "bob", x4);
  if (IsUndefined(x1)) {
    x1 = x2;
  }
  if (!IsUndefined(x1)) {
    x5 = x1;
  }
  retval = x5;
  return retval;
}
template <typename T1, typename T2>
decltype(auto) function_0(T1 &&p1, T2 &&p2) {
  static auto singleton_result =
      function_body_0(std::forward<T1>(p1), std::forward<T2>(p2));
  return singleton_result;
}
template <typename T1, typename T2>
decltype(auto) function_body_1(T1 &&p1, T2 &&p2) {
  OPAValue retval;
  OPAValue x1;
  OPAValue x2;
  OPAArray x3;
  OPAValue x4;
  OPAArray x5;
  OPAValue x6;
  OPAArray x7;
  OPAValue x8;
  OPAValue x9;
  decltype(x1) x10;
  x1 = Undefined();
  x2 = Object();
  x3 = ArrayCreationCapacity(1);
  x4 = Object();
  SetValueForKey(x4, "action", "read");
  SetValueForKey(x4, "object", "server123");
  PushBack(x3, x4);
  SetValueForKey(x2, "eng", x3);
  x5 = ArrayCreationCapacity(1);
  x6 = Object();
  SetValueForKey(x6, "action", "read");
  SetValueForKey(x6, "object", "database456");
  PushBack(x5, x6);
  SetValueForKey(x2, "hr", x5);
  x7 = ArrayCreationCapacity(2);
  x8 = Object();
  SetValueForKey(x8, "action", "read");
  SetValueForKey(x8, "object", "server123");
  PushBack(x7, x8);
  x9 = Object();
  SetValueForKey(x9, "action", "write");
  SetValueForKey(x9, "object", "server123");
  PushBack(x7, x9);
  SetValueForKey(x2, "web", x7);
  if (IsUndefined(x1)) {
    x1 = x2;
  }
  if (!IsUndefined(x1)) {
    x10 = x1;
  }
  retval = x10;
  return retval;
}
template <typename T1, typename T2>
decltype(auto) function_1(T1 &&p1, T2 &&p2) {
  static auto singleton_result =
      function_body_1(std::forward<T1>(p1), std::forward<T2>(p2));
  return singleton_result;
}
template <typename T1, typename T2>
decltype(auto) function_body_2(T1 &&p1, T2 &&p2) {
  OPAValue retval;
  OPAValue x1;
  decltype(s1::GetValueByKeyFrom(std::forward<T1>(p1))) x2;
  decltype(x2) x3;
  decltype(function_0(std::declval<T1>(), std::declval<T2>())) x4;
  decltype(GetValueByKey(x4, x3)) x5;
  decltype(x5) x6;
  OPAValue x7;
  OPAValue x8;
  decltype(x7) x9;
  decltype(x8) x10;
  decltype(function_1(std::declval<T1>(), std::declval<T2>())) x11;
  decltype(GetValueByKey(x11, x10)) x12;
  decltype(x12) x13;
  OPAValue x14;
  OPAValue x15;
  decltype(x14) x16;
  decltype(x15) x17;
  decltype(s7::GetValueByKeyFrom(std::forward<T1>(p1))) x18;
  decltype(x18) x19;
  decltype(s9::GetValueByKeyFrom(std::forward<T1>(p1))) x20;
  decltype(x20) x21;
  size_t x22;
  size_t x23;
  decltype(s7::GetValueByKeyFrom(x17)) x24;
  decltype(x1) x25;
  x1 = Undefined();
  x2 = s1::GetValueByKeyFrom(std::forward<T1>(p1));
  x3 = x2;
  x4 = function_0(std::forward<T1>(p1), std::forward<T2>(p2));
  x5 = GetValueByKey(x4, x3);
  x6 = x5;
  Scan(x6, x7, x8, [&]() {
    x9 = x7;
    x10 = x8;
    x11 = function_1(std::forward<T1>(p1), std::forward<T2>(p2));
    x12 = GetValueByKey(x11, x10);
    x13 = x12;
    Scan(x13, x14, x15, [&]() {
      x16 = x14;
      x17 = x15;
      x18 = s7::GetValueByKeyFrom(std::forward<T1>(p1));
      x19 = x18;
      x20 = s9::GetValueByKeyFrom(std::forward<T1>(p1));
      x21 = x20;
      if (IsObject(x17)) {
        x22 = Len(x17);
        x23 = 2;
        if (AreLocalsEqual(x22, x23)) {
          x24 = s7::GetValueByKeyFrom(x17);
          if (AreLocalsEqual(x24, x19)) {
            x24 = s9::GetValueByKeyFrom(x17);
            if (AreLocalsEqual(x24, x21)) {
              if (IsUndefined(x1)) {
                x1 = OPABoolean(true);
              }
            }
          }
        }
      }
    });
  });
  if (!IsUndefined(x1)) {
    x25 = x1;
  }
  if (IsUndefined(x25)) {
    if (IsUndefined(x25)) {
      x25 = OPABoolean(false);
    }
  }
  retval = x25;
  return retval;
}
template <typename T1, typename T2>
decltype(auto) function_2(T1 &&p1, T2 &&p2) {
  return function_body_2(std::forward<T1>(p1), std::forward<T2>(p2));
}
template <typename T_INPUT, typename T_DATA>
OPAResult policy(T_INPUT &&input, T_DATA &&data) {
  OPAResult result;
  decltype(function_2(std::declval<T_INPUT>(), std::declval<T_DATA>())) x1;
  decltype(x1) x2;
  OPAValue x3;
  x1 = function_2(input, data);
  x2 = x1;
  x3 = Object();
  SetValueForKey(x3, "result", x2);
  result.AddToResultSet(x3);
  return result;
}template <class T>
struct PotentiallyCustomTypeImpl final {
  using extracted_t = decltype(std::declval<T>().input) const&;
  static policy_input_t DoParse(std::string const& input) { return ParseJSON<policy_input_t>(input); }
  static extracted_t DoExtract(T const& input) { return input.input; }
};

template <>
struct PotentiallyCustomTypeImpl<JSONValue> final {
  using extracted_t = JSONValue const&;
  static JSONValue DoParse(std::string const& input) { return ParseJSONUniversally(input); }
  static JSONValue const& DoExtract(JSONValue const& input) {
    static JSONValue null = JSONNull();
    if (Exists<JSONObject>(input)) {
      return Value<JSONObject>(input)["input"];
    } else {
      return null;
    }
  }
};

template <class T>
policy_input_t ParsePolicyInputFromString(std::string const& input) {
  return PotentiallyCustomTypeImpl<policy_input_t>::DoParse(input);
}

template <class T>
typename PotentiallyCustomTypeImpl<std::decay_t<T>>::extracted_t ExtractPolicyInputFromParsedInput(T&& input) {
  return PotentiallyCustomTypeImpl<std::decay_t<T>>::DoExtract(std::forward<T>(input));
}

int main(int argc, char** argv) {
  ParseDFlags(&argc, &argv);

  JSONValue const test_data_that_is_empty = JSONObject();

  if (!FLAGS_queries.empty()) {
    std::vector<policy_input_t> inputs;
    {
      current::ProgressLine report;
      report << "Reading " << cyan << FLAGS_queries << reset << " ...";
      current::FileSystem::ReadFileByLines(FLAGS_queries, [&inputs](std::string const& s) {
        inputs.push_back(ParsePolicyInputFromString<policy_input_t>(s));
      });
    }
    std::cout << "Read " << cyan << FLAGS_queries << reset << ", " << magenta << inputs.size() << reset << " queries."
              << std::endl;
    if (!inputs.empty()) {
      std::vector<JSONValue> results;
      results.reserve(inputs.size());
      std::chrono::microseconds t0;
      std::chrono::microseconds t1;
      {
        current::ProgressLine report;
        report << "Running ...";
        t0 = current::time::Now();
        for (policy_input_t const& input : inputs) {
          results.push_back(policy(ExtractPolicyInputFromParsedInput(input), test_data_that_is_empty).pack());
        }
        t1 = current::time::Now();
      }
      auto const dt = (t1 - t0).count();
      double const paps = inputs.size() * 1e6 / dt;
      double const us = 1.0 * dt / inputs.size();
      std::cout << "Result: " << bold << magenta << current::strings::RoundDoubleToString(us, 3) << "us" << reset
                << ", " << bold << green << current::strings::RoundDoubleToString(paps, 3) << " PAPS" << reset
                << std::endl;
      if (!FLAGS_output.empty()) {
        std::ofstream fo(FLAGS_output);
        for (auto const& result : results) {
          fo << "{\"result\":" << AsJSON(result) << "}\n";
        }
      }
    }
    return 0;
  }

  HTTPRoutesScope http_routes;
  if (FLAGS_p) {
    auto& http = HTTP(current::net::BarePort(FLAGS_p));
    http_routes += http.Register("/", URLPathArgs::CountMask::Any, [&test_data_that_is_empty](Request r) {
      auto const json = ParseJSONUniversally(r.body);
      if (Exists<JSONObject>(json)) {
        r("{\"result\":" + AsJSON(policy(Value<JSONObject>(json)["input"], test_data_that_is_empty).pack()) + '}',
          HTTPResponseCode.OK,
          current::net::http::Headers(),
          current::net::constants::kDefaultJSONContentType);
      } else {
        r("Synopsis: `{\"input\":{...}}`.\n", HTTPResponseCode.BadRequest);
      }
    });
    if (FLAGS_d) {
      http.Join();
    }
  }

  std::string test_input;
  while (std::getline(std::cin, test_input)) {
    std::cout << AsJSON(policy(ParseJSONUniversally(test_input), test_data_that_is_empty).pack()) << std::endl;
  }
}
