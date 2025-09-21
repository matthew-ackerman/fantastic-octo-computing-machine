// Lightweight JSON-C wrapper and interface helpers
// Temporary header to unify JSON parsing patterns across file_io.
// Provides RAII for json documents, a thin JsonView with typed getters,
// and a JsonInterface base for model classes to implement from_json.

#pragma once

#include <json-c/json.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <type_traits>

// Owning JSON document (RAII). Holds a root json_object* and releases it.
class JsonDoc {
public:
    JsonDoc() = default;
    explicit JsonDoc(json_object* o) : obj_(o) {}
    JsonDoc(const JsonDoc&) = delete;
    JsonDoc& operator=(const JsonDoc&) = delete;
    JsonDoc(JsonDoc&& other) noexcept : obj_(other.obj_) { other.obj_ = nullptr; }
    JsonDoc& operator=(JsonDoc&& other) noexcept {
        if (this != &other) { if (obj_) json_object_put(obj_); obj_ = other.obj_; other.obj_ = nullptr; }
        return *this;
    }
    ~JsonDoc() { if (obj_) json_object_put(obj_); }

    static JsonDoc from_file(const char* path, std::string* err = nullptr) {
        json_object* root = json_object_from_file(path);
        if (!root) {
            if (err) *err = std::string("failed to read ") + (path ? path : "<null>");
            return JsonDoc();
        }
        // Do not enforce a specific root type here; callers validate as needed.
        return JsonDoc(root);
    }

    json_object* get() const { return obj_; }
    bool valid() const { return obj_ != nullptr; }

private:
    json_object* obj_ = nullptr;
};

// Non-owning JSON view helper.
struct JsonView {
    json_object* p = nullptr;

    bool valid() const { return p != nullptr; }
    bool is_object() const { return p && json_object_is_type(p, json_type_object); }
    bool is_array() const { return p && json_object_is_type(p, json_type_array); }

    // Root view constructor
    explicit JsonView(json_object* obj = nullptr) : p(obj) {}

    // Object field accessors
    bool get_view(const char* key, JsonView& out) const {
        if (!is_object()) return false;
        json_object* v = nullptr;
        if (!json_object_object_get_ex(p, key, &v)) return false;
        out = JsonView(v);
        return true;
    }

    bool get_string(const char* key, std::string& out) const {
        JsonView v; if (!get_view(key, v)) return false;
        if (!json_object_is_type(v.p, json_type_string)) return false;
        out = json_object_get_string(v.p);
        return true;
    }
    bool get_double(const char* key, double& out) const {
        JsonView v; if (!get_view(key, v)) return false;
        out = json_object_get_double(v.p);
        return true;
    }
    bool get_int(const char* key, int& out) const {
        JsonView v; if (!get_view(key, v)) return false;
        out = json_object_get_int(v.p);
        return true;
    }
    bool get_int64(const char* key, int64_t& out) const {
        JsonView v; if (!get_view(key, v)) return false;
        out = json_object_get_int64(v.p);
        return true;
    }
    bool get_bool(const char* key, bool& out) const {
        JsonView v; if (!get_view(key, v)) return false;
        out = json_object_get_boolean(v.p);
        return true;
    }

    // Optional getters with default value
    std::string get_string_opt(const char* key, const std::string& def = {}) const {
        std::string s; return get_string(key, s) ? s : def;
    }
    double get_double_opt(const char* key, double def = 0.0) const {
        double v; return get_double(key, v) ? v : def;
    }
    int get_int_opt(const char* key, int def = 0) const {
        int v; return get_int(key, v) ? v : def;
    }
    int64_t get_int64_opt(const char* key, int64_t def = 0) const {
        int64_t v; return get_int64(key, v) ? v : def;
    }
    bool get_bool_opt(const char* key, bool def = false) const {
        bool v; return get_bool(key, v) ? v : def;
    }

    // Array helpers
    size_t length() const { return is_array() ? json_object_array_length(p) : 0; }
    JsonView index(size_t i) const {
        if (!is_array()) return JsonView();
        return JsonView(json_object_array_get_idx(p, i));
    }
};

// Base class for JSON-serializable objects.
class JsonInterface {
public:
    virtual ~JsonInterface() = default;
    virtual bool from_json(const JsonView& v, std::string* err) = 0;

    template <typename T>
    static bool load_file(const char* path, T& out, std::string* err = nullptr) {
        static_assert(std::is_base_of<JsonInterface, T>::value, "T must derive from JsonInterface");
        JsonDoc doc = JsonDoc::from_file(path, err);
        if (!doc.valid()) return false;
        return static_cast<JsonInterface&>(out).from_json(JsonView(doc.get()), err);
    }
};

// Parse an array of elements using a converter functor.
// F: bool(const JsonView& in, T& out, std::string* err)
template <typename T, typename F>
inline bool json_array_to_vector(const JsonView& arr, std::vector<T>& out, F convert, std::string* err = nullptr) {
    if (!arr.is_array()) return false;
    size_t n = arr.length();
    out.clear(); out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        JsonView it = arr.index(i);
        T val{};
        if (!convert(it, val, err)) return false;
        out.push_back(std::move(val));
    }
    return true;
}

// Generic typed getter bridge for JsonView objects (object field -> T)
// Specializations/overloads cover basic types.
inline bool json_get(const JsonView& obj, const char* key, std::string& out) { return obj.get_string(key, out); }
inline bool json_get(const JsonView& obj, const char* key, double& out) { return obj.get_double(key, out); }
inline bool json_get(const JsonView& obj, const char* key, int& out) { return obj.get_int(key, out); }
inline bool json_get(const JsonView& obj, const char* key, int64_t& out) { return obj.get_int64(key, out); }
inline bool json_get(const JsonView& obj, const char* key, bool& out) { return obj.get_bool(key, out); }
inline bool json_get(const JsonView& obj, const char* key, float& out) { double d; if (!obj.get_double(key, d)) return false; out = static_cast<float>(d); return true; }

// Clean, overloaded getter API: get_json_value(view, key, &out)
inline bool get_json_value(const JsonView& view, const std::string& key, std::string* out) {
    if (!out) return false;
    std::string tmp;
    if (!view.get_string(key.c_str(), tmp)) return false;
    *out = std::move(tmp);
    return true;
}
inline bool get_json_value(const JsonView& view, const std::string& key, double* out) {
    if (!out) return false;
    double v{};
    if (!view.get_double(key.c_str(), v)) return false;
    *out = v;
    return true;
}
inline bool get_json_value(const JsonView& view, const std::string& key, float* out) {
    if (!out) return false;
    double v{};
    if (!view.get_double(key.c_str(), v)) return false;
    *out = static_cast<float>(v);
    return true;
}
inline bool get_json_value(const JsonView& view, const std::string& key, int* out) {
    if (!out) return false;
    int v{};
    if (!view.get_int(key.c_str(), v)) return false;
    *out = v;
    return true;
}
inline bool get_json_value(const JsonView& view, const std::string& key, int64_t* out) {
    if (!out) return false;
    int64_t v{};
    if (!view.get_int64(key.c_str(), v)) return false;
    *out = v;
    return true;
}
inline bool get_json_value(const JsonView& view, const std::string& key, bool* out) {
    if (!out) return false;
    bool v{};
    if (!view.get_bool(key.c_str(), v)) return false;
    *out = v;
    return true;
}

// JsonValue<T>: declare a named field and resolve it from a JsonView.
// Example:
//   JsonValue<double> velocity("velocity");
//   if (!velocity.load(view, &err, /*required=*/true)) return false;
//   double v = velocity.val;
template <typename T>
struct JsonValue {
    std::string key;
    T val{};
    bool present = false;

    explicit JsonValue(std::string k) : key(std::move(k)) {}

    // Load value from object; if required and missing/invalid, set err and return false
    bool load(const JsonView& obj, std::string* err = nullptr, bool required = false) {
        T tmp{};
        if (json_get(obj, key.c_str(), tmp)) { val = std::move(tmp); present = true; return true; }
        present = false;
        if (required) { if (err) *err = std::string("missing or invalid field: ") + key; return false; }
        return true;
    }

    // Convenience: load optional with default
    bool load_or(const JsonView& obj, const T& def) { present = json_get(obj, key.c_str(), val); if (!present) val = def; return true; }

    // Get value or default if not present
    T value_or(const T& def) const { return present ? val : def; }
};

// Example of a small JSON-backed struct implementing JsonInterface
// (not used by the app directly, provided as a pattern).
struct JsonKeyValue : public JsonInterface {
    std::string key;
    std::string value;
    bool from_json(const JsonView& v, std::string* err) override {
        if (!v.is_object()) { if (err) *err = "expected object"; return false; }
        if (!v.get_string("key", key)) { if (err) *err = "missing key"; return false; }
        value = v.get_string_opt("value");
        return true;
    }
};
