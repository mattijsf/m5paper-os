#include "core/store.h"

#include <algorithm>
#include <cstdlib>

#include <ArduinoJson.h>

#include "core/fs.h"

namespace pos {

struct Store::Impl {
  JsonDocument doc;
  bool loaded = false;
};

Store::Store(std::string path) : path_(std::move(path)), impl_(new Impl) {}
Store::~Store() = default;

void Store::load() const {
  if (impl_->loaded) return;
  impl_->loaded = true;
  std::string text;
  if (fs::readFile(path_, text)) {
    if (deserializeJson(impl_->doc, text)) impl_->doc.clear();  // unreadable: start empty
  }
  if (!impl_->doc.is<JsonObject>()) impl_->doc.to<JsonObject>();
}

bool Store::has(const std::string& key) const {
  load();
  return impl_->doc[key].is<JsonVariantConst>() && !impl_->doc[key].isNull();
}

void Store::remove(const std::string& key) {
  load();
  impl_->doc.remove(key);
  dirty_ = true;
}

int Store::getValue(const std::string& key, int def) const {
  load();
  return impl_->doc[key].is<int>() ? impl_->doc[key].as<int>() : def;
}
bool Store::getValue(const std::string& key, bool def) const {
  load();
  return impl_->doc[key].is<bool>() ? impl_->doc[key].as<bool>() : def;
}
double Store::getValue(const std::string& key, double def) const {
  load();
  return impl_->doc[key].is<double>() ? impl_->doc[key].as<double>() : def;
}
std::string Store::getValue(const std::string& key, const std::string& def) const {
  load();
  return impl_->doc[key].is<const char*>() ? std::string(impl_->doc[key].as<const char*>()) : def;
}

void Store::setValue(const std::string& key, int v) { load(); impl_->doc[key] = v; dirty_ = true; }
void Store::setValue(const std::string& key, bool v) { load(); impl_->doc[key] = v; dirty_ = true; }
void Store::setValue(const std::string& key, double v) { load(); impl_->doc[key] = v; dirty_ = true; }
void Store::setValue(const std::string& key, const std::string& v) { load(); impl_->doc[key] = v; dirty_ = true; }

bool Store::save() {
  if (!dirty_) return true;
  load();
  std::string out;
  serializeJson(impl_->doc, out);
  if (!fs::writeFile(path_, out)) return false;
  dirty_ = false;
  return true;
}

std::string Cache::read(const std::string& name) const {
  std::string s;
  fs::readFile(dir_ + "/" + name, s);
  return s;
}
bool Cache::write(const std::string& name, const std::string& text) const { return fs::writeFile(dir_ + "/" + name, text); }
bool Cache::exists(const std::string& name) const { return fs::exists(dir_ + "/" + name); }

int appBadge(const std::string& appId) { return std::max(0, std::atoi(Cache(appId).read("badge").c_str())); }
void setAppBadge(const std::string& appId, int count) {
  count = std::max(0, count);
  const Cache cache(appId);
  if (cache.exists("badge") && appBadge(appId) == count) return;  // nothing new: no write to the flash
  cache.write("badge", std::to_string(count));
}

}  // namespace pos
