// Small JSON-backed persistence: per-app key/value Store and a text Cache, on top of core/fs
// (virtual paths /data/...). ArduinoJson 7 is the only JSON library (docs/ARCHITECTURE.md).
#pragma once

#include <memory>
#include <string>

namespace pos {

// Per-app key/value store persisted at /data/apps/<id>.json. Values: int, bool, double, string.
// A missing file, a missing key or a value of another type gives the default, never a crash.
class Store {
 public:
  explicit Store(std::string path);  // virtual path, e.g. "/data/apps/hello.json"
  ~Store();
  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;

  template <class T>
  T get(const std::string& key, T def) const {
    return getValue(key, def);
  }
  std::string get(const std::string& key, const char* def) const { return getValue(key, std::string(def)); }
  template <class T>
  void set(const std::string& key, T value) {
    setValue(key, value);
  }
  void set(const std::string& key, const char* value) { setValue(key, std::string(value)); }
  bool has(const std::string& key) const;
  void remove(const std::string& key);
  // Writes the file (atomically) if anything changed. False if the write failed.
  bool save();
  bool dirty() const { return dirty_; }

 private:
  struct Impl;
  void load() const;
  int getValue(const std::string& key, int def) const;
  bool getValue(const std::string& key, bool def) const;
  double getValue(const std::string& key, double def) const;
  float getValue(const std::string& key, float def) const { return static_cast<float>(getValue(key, static_cast<double>(def))); }
  std::string getValue(const std::string& key, const std::string& def) const;
  void setValue(const std::string& key, int v);
  void setValue(const std::string& key, bool v);
  void setValue(const std::string& key, double v);
  void setValue(const std::string& key, float v) { setValue(key, static_cast<double>(v)); }
  void setValue(const std::string& key, const std::string& v);

  std::string path_;
  mutable std::unique_ptr<Impl> impl_;
  bool dirty_ = false;
};

// Text cache under /data/cache/<id>/ so apps can show stale data first.
class Cache {
 public:
  explicit Cache(std::string id) : dir_("/data/cache/" + std::move(id)) {}
  // Contents of the cached file, "" if there is none.
  std::string read(const std::string& name) const;
  bool write(const std::string& name, const std::string& text) const;
  bool exists(const std::string& name) const;

 private:
  std::string dir_;
};

}  // namespace pos
