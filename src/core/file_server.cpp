#include "core/file_server.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <vector>

#include "core/companion_crypto.h"
#include "core/fs.h"
#include "core/md5.h"
#include "core/log.h"
#include "core/strings.h"
#include "core/timefmt.h"
#include "hal/system.h"

namespace pos::files {

namespace {

using hal::ServerRequest;
using hal::ServerResponse;

const char* kAllow = "OPTIONS, GET, HEAD, PUT, DELETE, MKCOL, MOVE, COPY, PROPFIND, PROPPATCH, LOCK, UNLOCK";

// The page for a browser (GET on a folder): lists the folder, uploads with progress (PUT per file), opens subfolders, makes a
// folder (MKCOL), downloads (GET ?download), renames and moves (MOVE), deletes. Plain HTML and JS, no outside resources: it works with no internet.
const char* kPage = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>PaperOS files</title>
<style>
:root{color-scheme:light dark;--ink:#1e1e1e;--paper:#f4f4f0;--line:#c8c8c2;--muted:#5a5a55}
@media (prefers-color-scheme:dark){:root{--ink:#eeeeea;--paper:#1c1c1a;--line:#3a3a36;--muted:#a0a09a}}
body{margin:0;background:var(--paper);color:var(--ink);font:16px/1.4 system-ui,-apple-system,"Segoe UI",sans-serif}
main{max-width:640px;margin:0 auto;padding:16px}
h1{font-size:24px;margin:8px 0 2px}p{margin:4px 0;color:var(--muted)}
.drop{margin:16px 0;padding:20px;border:2px dashed var(--line);border-radius:12px;text-align:center}
.drop.on{border-color:var(--ink)}
button,label.btn{display:inline-block;font:inherit;padding:10px 16px;border-radius:10px;border:2px solid var(--ink);
background:var(--ink);color:var(--paper);cursor:pointer}
button.quiet{background:none;color:var(--ink);padding:6px 10px;border-width:1px}
ul{list-style:none;padding:0;margin:8px 0}
li{display:flex;flex-wrap:wrap;align-items:baseline;gap:6px 8px;padding:10px 0;border-bottom:1px solid var(--line)}
li .n{flex:1;overflow-wrap:anywhere}li .acts{flex-basis:100%;display:flex;gap:6px;flex-wrap:wrap}li .s{color:var(--muted);font-size:14px;white-space:nowrap}a{color:var(--ink)}
#status{min-height:1.4em}
</style></head><body><main>
<h1>PaperOS</h1><p id="where"></p>
<div class="drop" id="drop"><p>Drop books here, or</p>
<label class="btn">Choose files<input id="pick" type="file" multiple hidden></label></div>
<p><button class="quiet" id="mkdir">New folder</button></p>
<p id="status"></p><ul id="list"></ul>
</main><script>
const base = location.pathname.endsWith('/') ? location.pathname : location.pathname + '/';
const $ = id => document.getElementById(id);
$('where').textContent = decodeURIComponent(base) === '/' ? 'Books on the Paper' : 'Books' + decodeURIComponent(base);
const size = n => n < 1024 ? n + ' B' : n < 1048576 ? (n / 1024).toFixed(0) + ' KB' : (n / 1048576).toFixed(1) + ' MB';
function load() {
  fetch(base + '?list').then(r => r.json()).then(items => {
    const ul = $('list'); ul.textContent = '';
    if (base !== '/') items.unshift({name: '..', dir: true, up: true});
    for (const it of items) {
      const li = document.createElement('li');
      const n = document.createElement(it.dir ? 'a' : 'span'); n.className = 'n';
      n.textContent = it.up ? 'Up' : it.dir ? it.name + '/' : it.name;
      if (it.dir) n.href = it.up ? base.replace(/[^/]+\/$/, '') : base + encodeURIComponent(it.name) + '/';
      li.appendChild(n);
      if (!it.up) {
        if (!it.dir) { const s = document.createElement('span'); s.className = 's'; s.textContent = size(it.size); li.appendChild(s); }
        const acts = document.createElement('span'); acts.className = 'acts';
        const btn = (label, fn) => { const b = document.createElement('button'); b.className = 'quiet'; b.textContent = label; b.onclick = fn; acts.appendChild(b); };
        const self = base + encodeURIComponent(it.name) + (it.dir ? '/' : '');
        if (!it.dir) btn('Download', () => {
          const a = document.createElement('a'); a.href = self + '?download'; a.download = it.name;
          document.body.appendChild(a); a.click(); a.remove();
        });
        btn('Rename', () => {
          const to = prompt('New name', it.name);
          if (to && to !== it.name && !to.includes('/')) move(self, base + encodeURIComponent(to) + (it.dir ? '/' : ''));
        });
        btn('Move', () => {
          const to = prompt('Move to which folder? Its path from the top, for example Classics or Classics/Dutch; empty for the top.', '');
          if (to === null) return;
          const parts = to.split('/').filter(p => p && p !== '.' && p !== '..');
          move(self, '/' + parts.map(encodeURIComponent).join('/') + (parts.length ? '/' : '') + encodeURIComponent(it.name) + (it.dir ? '/' : ''));
        });
        btn('Delete', () => { if (confirm('Delete ' + it.name + (it.dir ? ' and everything in it' : '') + '?')) fetch(self, {method: 'DELETE'}).then(load); });
        li.appendChild(acts);
      }
      ul.appendChild(li);
    }
    if (!items.length) $('list').innerHTML = '<li><span class="n">No books yet</span></li>';
  });
}
function send(files, i) {
  if (i >= files.length) { $('status').textContent = files.length + (files.length === 1 ? ' file sent' : ' files sent'); load(); return; }
  const f = files[i], x = new XMLHttpRequest();
  x.open('PUT', base + encodeURIComponent(f.name));
  x.upload.onprogress = e => { $('status').textContent = f.name + ': ' + Math.round(100 * e.loaded / (e.total || 1)) + ' %'; };
  x.onload = () => { if (x.status >= 300) { $('status').textContent = f.name + ': not sent (' + x.status + ')'; return; } send(files, i + 1); };
  x.onerror = () => { $('status').textContent = f.name + ': not sent (connection lost)'; };
  x.send(f);
}
function move(from, to) {
  fetch(from, {method: 'MOVE', headers: {Destination: location.origin + to, Overwrite: 'F'}}).then(r => {
    $('status').textContent = r.ok ? 'Done' : r.status === 412 ? 'There is already something with that name' :
      r.status === 409 ? 'That folder does not exist' : 'Not done (' + r.status + ')';
    load();
  });
}
$('mkdir').onclick = () => {
  const name = prompt('Name of the new folder', '');
  if (name && !name.includes('/')) fetch(base + encodeURIComponent(name) + '/', {method: 'MKCOL'}).then(r => {
    $('status').textContent = r.ok ? 'Folder made' : 'Not made (' + r.status + ')';
    load();
  });
};
$('pick').onchange = e => send([...e.target.files], 0);
const drop = $('drop');
drop.ondragover = e => { e.preventDefault(); drop.classList.add('on'); };
drop.ondragleave = () => drop.classList.remove('on');
drop.ondrop = e => { e.preventDefault(); drop.classList.remove('on'); send([...e.dataTransfer.files], 0); };
load();
</script></body></html>
)HTML";

std::string xmlEscape(const std::string& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

std::string jsonEscape(const std::string& s) {
  std::string out;
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c < 0x20) {
      char b[8];
      std::snprintf(b, sizeof b, "\\u%04x", c);
      out += b;
    } else {
      out += static_cast<char>(c);
    }
  }
  return out;
}

std::string isoDate(int64_t t) {
  const time_t tt = static_cast<time_t>(t);
  struct tm g;
  gmtime_r(&tt, &g);
  char b[32];
  std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", &g);
  return b;
}

std::string contentType(const std::string& path) {
  const auto dot = path.rfind('.');
  std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
  ext = lowerAscii(ext);
  if (ext == "epub") return "application/epub+zip";
  if (ext == "txt") return "text/plain; charset=utf-8";
  if (ext == "pdf") return "application/pdf";
  if (ext == "json") return "application/json";
  if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
  if (ext == "png") return "image/png";
  return "application/octet-stream";
}

ServerResponse status(int code, const std::string& text = "") {
  ServerResponse r;
  r.status = code;
  r.body = text;
  if (!text.empty()) r.headers.emplace_back("Content-Type", "text/plain; charset=utf-8");
  return r;
}

// Reads what is left of a body, so the connection can serve the next request.
void drain(ServerRequest& req) {
  char buf[1024];
  while (req.read && req.read(buf, sizeof buf) > 0) {
  }
}

std::string parentOf(const std::string& path) {
  const auto slash = path.rfind('/');
  return slash == 0 || slash == std::string::npos ? "/" : path.substr(0, slash);
}

std::string nameOf(const std::string& path) { return path.substr(path.rfind('/') + 1); }

// One <D:response> of a PROPFIND.
std::string propResponse(const std::string& href, const std::string& name, bool dir, uint64_t size, int64_t mtime, bool quota) {
  std::string x = "<D:response><D:href>" + xmlEscape(href) + "</D:href><D:propstat><D:prop>";
  x += "<D:displayname>" + xmlEscape(name) + "</D:displayname>";
  x += dir ? "<D:resourcetype><D:collection/></D:resourcetype>" : "<D:resourcetype/>";
  if (!dir) {
    x += "<D:getcontentlength>" + std::to_string(size) + "</D:getcontentlength>";
    x += "<D:getcontenttype>" + contentType(name) + "</D:getcontenttype>";
    x += "<D:getetag>\"" + std::to_string(size) + "-" + std::to_string(mtime) + "\"</D:getetag>";
  }
  if (mtime > 0) {
    x += "<D:getlastmodified>" + httpDate(mtime) + "</D:getlastmodified>";
    x += "<D:creationdate>" + isoDate(mtime) + "</D:creationdate>";
  }
  if (quota) {
    const hal::StorageInfo sd = hal::storageInfo("sd");
    if (sd.present) {
      x += "<D:quota-available-bytes>" + std::to_string(sd.freeBytes) + "</D:quota-available-bytes>";
      x += "<D:quota-used-bytes>" + std::to_string(sd.totalBytes - sd.freeBytes) + "</D:quota-used-bytes>";
    }
  }
  x += "<D:supportedlock><D:lockentry><D:lockscope><D:exclusive/></D:lockscope><D:locktype><D:write/></D:locktype></D:lockentry></D:supportedlock>";
  x += "</D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response>";
  return x;
}

ServerResponse multistatus(std::string inner) {
  ServerResponse r;
  r.status = 207;
  r.body = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<D:multistatus xmlns:D=\"DAV:\">" + inner + "</D:multistatus>";
  r.headers.emplace_back("Content-Type", "application/xml; charset=utf-8");
  return r;
}

bool copyFile(const std::string& from, const std::string& to) {
  fs::Reader in;
  fs::Writer out;
  uint64_t size = 0;
  int64_t mtime = 0;
  if (!fs::fileInfo(from, size, mtime) || !in.open(from) || !out.open(to)) return false;
  std::vector<char> buf(8192);
  for (uint64_t at = 0; at < size;) {
    const size_t n = static_cast<size_t>(std::min<uint64_t>(buf.size(), size - at));
    if (!in.readAt(at, buf.data(), n) || !out.write(buf.data(), n)) return false;
    at += n;
  }
  return out.close();
}

}  // namespace

std::string percentDecode(const std::string& s, bool& ok) {
  ok = true;
  std::string out;
  auto hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '%') {
      out += s[i];
      continue;
    }
    if (i + 2 >= s.size() || hex(s[i + 1]) < 0 || hex(s[i + 2]) < 0) {
      ok = false;
      return "";
    }
    const char c = static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2]));
    if (c == 0) {
      ok = false;
      return "";
    }
    out += c;
    i += 2;
  }
  return out;
}

std::string percentEncodePath(const std::string& path) {
  std::string out;
  for (unsigned char c : path) {
    if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      char b[4];
      std::snprintf(b, sizeof b, "%%%02X", c);
      out += b;
    }
  }
  return out;
}

std::string base64Decode(const std::string& s, bool& ok) {
  ok = true;
  std::string out;
  int val = 0, bits = -8;
  for (char c : s) {
    int d;
    if (c >= 'A' && c <= 'Z') d = c - 'A';
    else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
    else if (c >= '0' && c <= '9') d = c - '0' + 52;
    else if (c == '+') d = 62;
    else if (c == '/') d = 63;
    else if (c == '=' || c == ' ') continue;
    else {
      ok = false;
      return "";
    }
    val = (val << 6) | d;
    bits += 6;
    if (bits >= 0) {
      out += static_cast<char>((val >> bits) & 0xFF);
      bits -= 8;
    }
  }
  return out;
}

std::string httpDate(int64_t t) {
  const time_t tt = static_cast<time_t>(t);
  struct tm g;
  gmtime_r(&tt, &g);
  char b[40];
  std::snprintf(b, sizeof b, "%s, %02d %s %04d %02d:%02d:%02d GMT", weekdayShort(g.tm_wday).c_str(), g.tm_mday, monthShort(g.tm_mon).c_str(), g.tm_year + 1900,
                g.tm_hour, g.tm_min, g.tm_sec);
  return b;
}

std::string cleanPath(const std::string& raw) {
  bool ok = false;
  const std::string p = percentDecode(raw, ok);
  if (!ok || p.empty() || p[0] != '/') return "";
  std::string out;
  size_t i = 1;
  while (i <= p.size()) {
    size_t e = p.find('/', i);
    if (e == std::string::npos) e = p.size();
    const std::string seg = p.substr(i, e - i);
    i = e + 1;
    if (seg.empty()) {
      if (e >= p.size()) break;  // a trailing slash
      return "";                 // "//"
    }
    if (seg == "." || seg == ".." || seg.find('\\') != std::string::npos) return "";
    out += "/" + seg;
  }
  return out.empty() ? "/" : out;
}

bool isHidden(const std::string& path) { return path.find("/.") != std::string::npos; }

bool isMacJunk(const std::string& path) {
  const std::string n = nameOf(path);
  return n.rfind("._", 0) == 0 || n == ".DS_Store";
}

std::string newCode() {
  uint8_t b[4];
  companion::randomBytes(b, sizeof b);
  const uint32_t v = (static_cast<uint32_t>(b[0]) << 24 | b[1] << 16 | b[2] << 8 | b[3]) % 1000000u;
  char s[8];
  std::snprintf(s, sizeof s, "%06u", static_cast<unsigned>(v));
  return s;
}

FileServer::FileServer(std::string root, std::string code, std::function<uint32_t()> millis)
    : root_(std::move(root)), code_(std::move(code)), millis_(std::move(millis)) {
  lastRequestMs_ = millis_();
  uint8_t b[16];
  companion::randomBytes(b, sizeof b);
  static const char* kHex = "0123456789abcdef";
  for (uint8_t v : b) {
    nonce_ += kHex[v >> 4];
    nonce_ += kHex[v & 15];
  }
}

Stats FileServer::stats() const {
  std::lock_guard<std::mutex> lock(mu_);
  return stats_;
}

uint32_t FileServer::lastRequestMs() const {
  std::lock_guard<std::mutex> lock(mu_);
  return lastRequestMs_;
}

bool FileServer::authorised(const ServerRequest& req) {
  const std::string h = req.header("authorization");
  if (h.rfind("Digest ", 0) == 0) {
    // name="value" pairs (quoted or not), split on commas outside quotes.
    std::string key, val;
    std::vector<std::pair<std::string, std::string>> kv;
    bool inKey = true, quoted = false;
    for (size_t i = 7; i <= h.size(); ++i) {
      const char c = i < h.size() ? h[i] : ',';
      if (inKey) {
        if (c == '=') inKey = false;
        else if (c != ' ' && c != ',') key += c;
      } else if (c == '"') {
        quoted = !quoted;
      } else if (c == ',' && !quoted) {
        kv.emplace_back(key, val);
        key.clear();
        val.clear();
        inKey = true;
      } else {
        val += c;
      }
    }
    auto get = [&kv](const char* k) {
      for (const auto& p : kv)
        if (p.first == k) return p.second;
      return std::string();
    };
    if (get("nonce") != nonce_ || get("realm") != kRealm) return false;
    const std::string ha1 = md5Hex(get("username") + ":" + kRealm + ":" + code_);
    const std::string ha2 = md5Hex(req.method + ":" + get("uri"));
    const std::string qop = get("qop");
    const std::string want = qop.empty() ? md5Hex(ha1 + ":" + nonce_ + ":" + ha2)
                                         : md5Hex(ha1 + ":" + nonce_ + ":" + get("nc") + ":" + get("cnonce") + ":" + qop + ":" + ha2);
    std::string got = get("response");
    got = lowerAscii(got);
    return got == want;
  }
  if (h.rfind("Basic ", 0) != 0) return false;
  bool ok = false;
  const std::string pair = base64Decode(h.substr(6), ok);
  const auto colon = pair.find(':');
  if (!ok || colon == std::string::npos) return false;
  auto digits = [](std::string s) {
    s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
    return s;
  };
  return digits(pair.substr(colon + 1)) == code_ || digits(pair.substr(0, colon)) == code_;
}

ServerResponse FileServer::handle(ServerRequest& req) {
  const uint32_t now = millis_();
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (lockedUntilMs_ && static_cast<int32_t>(lockedUntilMs_ - now) > 0) {
      drain(req);
      return status(429, "Too many wrong codes: wait a minute\n");
    }
  }
  const std::string target = req.target;
  const auto q = target.find('?');
  const std::string rawPath = target.substr(0, q);
  const std::string query = q == std::string::npos ? "" : target.substr(q + 1);
  if (req.method == "OPTIONS") {  // no code needed: clients probe before they ask for one
    drain(req);
    ServerResponse r = status(200);
    r.headers.emplace_back("DAV", "1, 2");
    r.headers.emplace_back("MS-Author-Via", "DAV");
    r.headers.emplace_back("Allow", kAllow);
    return r;
  }
  if (!authorised(req)) {
    drain(req);
    std::lock_guard<std::mutex> lock(mu_);
    if (!req.header("authorization").empty() && ++stats_.refused % kMaxRefusals == 0) {
      lockedUntilMs_ = now + kLockoutMs;
      POS_LOG("files", "%u wrong codes: locked for a minute", stats_.refused);
    }
    ServerResponse r = status(401, "The code is on the Paper's screen\n");
    r.headers.emplace_back("WWW-Authenticate", std::string("Digest realm=\"") + kRealm + "\", qop=\"auth\", algorithm=MD5, nonce=\"" + nonce_ + "\"");
    r.headers.emplace_back("WWW-Authenticate", std::string("Basic realm=\"") + kRealm + "\", charset=\"UTF-8\"");
    return r;
  }
  {
    std::lock_guard<std::mutex> lock(mu_);
    lastRequestMs_ = now;
  }
  const std::string path = cleanPath(rawPath);
  if (path.empty()) {
    drain(req);
    return status(400, "Bad path\n");
  }
  ServerResponse res = route(req, path, query);
  POS_LOG("files", "%s %s -> %d", req.method.c_str(), path.c_str(), res.status);
  return res;
}

ServerResponse FileServer::route(ServerRequest& req, const std::string& path, const std::string& query) {
  const std::string& m = req.method;
  const bool hidden = isHidden(path);
  if (hidden && isMacJunk(path)) {  // the Mac's metadata files: accepted and thrown away, never stored or listed
    drain(req);
    if (m == "PUT" || m == "LOCK") return status(201);
    if (m == "DELETE" || m == "UNLOCK" || m == "PROPPATCH") return status(204);
    return status(404);
  }
  if (hidden) {
    drain(req);
    return status(m == "GET" || m == "HEAD" || m == "PROPFIND" ? 404 : 403);
  }
  uint64_t size = 0;
  int64_t mtime = 0;
  const bool isFile = fs::fileInfo(real(path), size, mtime);
  std::vector<fs::DirEntry> probe;
  const bool isDir = !isFile && (path == "/" ? (fs::mkdirs(root_), true) : fs::listDir(real(path), probe));

  if (m == "GET" || m == "HEAD") {
    drain(req);
    if (isDir && query == "list") {
      std::vector<fs::DirEntry> items;
      fs::listDir(real(path == "/" ? "" : path), items);
      std::string j = "[";
      for (const fs::DirEntry& e : items) {
        if (!e.name.empty() && e.name[0] == '.') continue;
        if (j.size() > 1) j += ",";
        j += "{\"name\":\"" + jsonEscape(e.name) + "\",\"dir\":" + (e.isDir ? "true" : "false") + ",\"size\":" + std::to_string(e.size) + "}";
      }
      ServerResponse r = status(200);
      r.body = j + "]";
      r.headers.emplace_back("Content-Type", "application/json");
      r.headers.emplace_back("Cache-Control", "no-store");
      r.headOnly = m == "HEAD";
      return r;
    }
    if (isDir) {
      ServerResponse r = status(200);
      r.body = kPage;
      r.headers.emplace_back("Content-Type", "text/html; charset=utf-8");
      r.headers.emplace_back("Cache-Control", "no-store");
      r.headOnly = m == "HEAD";
      return r;
    }
    if (!isFile) return status(404);
    ServerResponse r = status(200);
    r.file = real(path);
    r.fileSize = size;
    r.headOnly = m == "HEAD";
    r.headers.emplace_back("Content-Type", contentType(path));
    if (mtime > 0) r.headers.emplace_back("Last-Modified", httpDate(mtime));
    r.headers.emplace_back("ETag", "\"" + std::to_string(size) + "-" + std::to_string(mtime) + "\"");
    if (query == "download") {  // the page's Download: saved under its own name, not opened (an EPUB would start a reader app)
      std::string ascii;
      for (unsigned char c : nameOf(path)) ascii += (c >= 0x20 && c < 0x7F && c != '"' && c != '\\') ? static_cast<char>(c) : '_';
      std::string encoded = percentEncodePath(nameOf(path));
      r.headers.emplace_back("Content-Disposition", "attachment; filename=\"" + ascii + "\"; filename*=UTF-8''" + encoded);
    }
    return r;
  }
  if (m == "PROPFIND") return propfind(req, path);
  if (m == "PUT") return put(req, path);
  if (m == "MOVE" || m == "COPY") return moveOrCopy(req, path, m == "MOVE");
  if (m == "LOCK") return lock(req, path);
  drain(req);
  if (m == "UNLOCK") return status(204);
  if (m == "PROPPATCH") {  // dead properties are not kept; saying they were makes Finder happy
    return multistatus("<D:response><D:href>" + xmlEscape(percentEncodePath(path)) +
                       "</D:href><D:propstat><D:prop/><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response>");
  }
  if (m == "DELETE") {
    if (path == "/") return status(403, "The books folder itself stays\n");
    if (!isFile && !isDir) return status(404);
    if (!fs::removeAll(real(path))) return status(500, "Could not delete\n");
    std::lock_guard<std::mutex> lock(mu_);
    ++stats_.deleted;
    stats_.last = "Deleted " + nameOf(path);
    return status(204);
  }
  if (m == "MKCOL") {
    if (isFile || isDir) return status(405);
    std::vector<fs::DirEntry> parent;
    if (parentOf(path) != "/" && !fs::listDir(real(parentOf(path)), parent)) return status(409);
    if (!fs::mkdirs(real(path))) return status(500, "Could not make the folder\n");
    std::lock_guard<std::mutex> lock(mu_);
    stats_.last = "Made folder " + nameOf(path);
    return status(201);
  }
  ServerResponse r = status(405);
  r.headers.emplace_back("Allow", kAllow);
  return r;
}

ServerResponse FileServer::propfind(ServerRequest& req, const std::string& path) {
  drain(req);  // the body (which properties) is not read: every client gets them all
  uint64_t size = 0;
  int64_t mtime = 0;
  const std::string depth = req.header("depth");
  if (fs::fileInfo(real(path), size, mtime)) {
    return multistatus(propResponse(percentEncodePath(path), nameOf(path), false, size, mtime, false));
  }
  std::vector<fs::DirEntry> items;
  if (path == "/") fs::mkdirs(root_);
  if (!fs::listDir(real(path == "/" ? "" : path), items)) return status(404);
  const std::string base = path == "/" ? "/" : path + "/";
  std::string x = propResponse(percentEncodePath(base), path == "/" ? "Books" : nameOf(path), true, 0, 0, true);
  if (depth != "0") {
    for (const fs::DirEntry& e : items) {
      if (e.name.empty() || e.name[0] == '.') continue;
      uint64_t s = e.size;
      int64_t t = 0;
      fs::fileInfo(real(base + e.name), s, t);
      x += propResponse(percentEncodePath(base + e.name + (e.isDir ? "/" : "")), e.name, e.isDir, e.isDir ? 0 : s, t, false);
    }
  }
  return multistatus(x);
}

ServerResponse FileServer::put(ServerRequest& req, const std::string& path) {
  if (path == "/") {
    drain(req);
    return status(405);
  }
  std::vector<fs::DirEntry> probe;
  if (fs::listDir(real(path), probe)) {
    drain(req);
    return status(405, "That is a folder\n");
  }
  if (parentOf(path) != "/" && !fs::listDir(real(parentOf(path)), probe)) {
    drain(req);
    return status(409, "The folder does not exist\n");
  }
  uint64_t oldSize = 0;
  int64_t oldTime = 0;
  const bool existed = fs::fileInfo(real(path), oldSize, oldTime);
  // Written aside under a hidden name, renamed over the file when complete: a broken upload leaves nothing half.
  const std::string temp = real(parentOf(path) == "/" ? "" : parentOf(path)) + "/.upload-" + nameOf(path);
  fs::Writer out;
  if (!out.open(temp)) {
    drain(req);
    return status(500, "Could not write to the card\n");
  }
  {
    std::lock_guard<std::mutex> lock(mu_);
    stats_.busy = true;
    stats_.receiving = nameOf(path);
    stats_.receivingBytes = 0;
  }
  std::vector<char> buf(8192);
  uint64_t got = 0;
  bool ok = true;
  for (;;) {
    const long n = req.read ? req.read(buf.data(), buf.size()) : 0;
    if (n < 0) {
      ok = false;
      break;
    }
    if (n == 0) break;
    if (!out.write(buf.data(), static_cast<size_t>(n))) {
      POS_LOG("files", "write to the card failed at %llu bytes (errno %d)", static_cast<unsigned long long>(got), errno);
      ok = false;
      drain(req);
      break;
    }
    got += static_cast<uint64_t>(n);
    std::lock_guard<std::mutex> lock(mu_);
    stats_.receivingBytes = got;
    lastRequestMs_ = millis_();  // a long upload is use too
  }
  ok = out.close() && ok && (req.contentLength < 0 || got == static_cast<uint64_t>(req.contentLength));
  if (ok && existed) fs::removeAll(real(path));
  ok = ok && fs::rename(temp, real(path));
  std::lock_guard<std::mutex> lock(mu_);
  stats_.busy = false;
  stats_.receiving.clear();
  if (!ok) {
    fs::removeAll(temp);
    POS_LOG("files", "upload failed after %llu bytes", static_cast<unsigned long long>(got));
    return status(500, "The upload broke off\n");
  }
  if (got > 0) {  // Finder makes an empty file first, then writes it again with its contents: that one counts
    ++stats_.received;
    stats_.bytes += got;
    stats_.last = "Received " + nameOf(path);
  }
  POS_LOG("files", "received %llu bytes%s", static_cast<unsigned long long>(got), out.retries() ? " (a card write needed another try)" : "");
  return status(existed ? 204 : 201);
}

ServerResponse FileServer::moveOrCopy(ServerRequest& req, const std::string& path, bool move) {
  drain(req);
  std::string dest = req.header("destination");
  // "http://host/a/b": only the path counts (and only on this server).
  const auto scheme = dest.find("://");
  if (scheme != std::string::npos) {
    const auto slash = dest.find('/', scheme + 3);
    dest = slash == std::string::npos ? "/" : dest.substr(slash);
  }
  const std::string to = cleanPath(dest.substr(0, dest.find('?')));
  if (to.empty() || to == "/" || path == "/") return status(400, "Bad destination\n");
  if (isHidden(to)) return status(isMacJunk(to) ? 201 : 403);
  if (to == path) return status(403);
  if (to.rfind(path + "/", 0) == 0) return status(409, "Into itself\n");
  uint64_t size = 0;
  int64_t mtime = 0;
  std::vector<fs::DirEntry> probe;
  const bool srcFile = fs::fileInfo(real(path), size, mtime);
  if (!srcFile && !fs::listDir(real(path), probe)) return status(404);
  if (parentOf(to) != "/" && !fs::listDir(real(parentOf(to)), probe)) return status(409);
  const bool destExists = fs::fileInfo(real(to), size, mtime) || fs::listDir(real(to), probe);
  const std::string overwrite = req.header("overwrite");
  if (destExists && (overwrite == "F" || overwrite == "f")) return status(412);
  if (destExists) fs::removeAll(real(to));
  bool ok;
  if (move) ok = fs::rename(real(path), real(to));
  else if (srcFile) ok = copyFile(real(path), real(to));
  else return status(501, "Copying folders is not supported\n");
  if (!ok) return status(500);
  std::lock_guard<std::mutex> lock(mu_);
  stats_.last = (move ? "Renamed " : "Copied ") + nameOf(path);
  return status(destExists ? 204 : 201);
}

ServerResponse FileServer::lock(ServerRequest& req, const std::string& path) {
  drain(req);
  // Locks are pretended (one writer at a time is the Paper's case anyway): Finder mounts read-only without them.
  uint64_t size = 0;
  int64_t mtime = 0;
  std::vector<fs::DirEntry> probe;
  bool created = false;
  if (!fs::fileInfo(real(path), size, mtime) && !(path == "/" || fs::listDir(real(path), probe))) {
    if (parentOf(path) != "/" && !fs::listDir(real(parentOf(path)), probe)) return status(409);
    fs::Writer w;  // RFC 4918: a lock on an unmapped name makes an empty file
    created = w.open(real(path)) && w.close();
  }
  std::string token;
  {
    std::lock_guard<std::mutex> lock(mu_);
    char b[64];
    std::snprintf(b, sizeof b, "opaquelocktoken:paperos-%08x-%u", static_cast<unsigned>(millis_()), ++tokenCounter_);
    token = b;
  }
  ServerResponse r;
  r.status = created ? 201 : 200;
  r.headers.emplace_back("Lock-Token", "<" + token + ">");
  r.headers.emplace_back("Content-Type", "application/xml; charset=utf-8");
  r.body = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<D:prop xmlns:D=\"DAV:\"><D:lockdiscovery><D:activelock>"
           "<D:locktype><D:write/></D:locktype><D:lockscope><D:exclusive/></D:lockscope><D:depth>0</D:depth>"
           "<D:timeout>Second-600</D:timeout><D:locktoken><D:href>" + token + "</D:href></D:locktoken>"
           "<D:lockroot><D:href>" + xmlEscape(percentEncodePath(path)) + "</D:href></D:lockroot>"
           "</D:activelock></D:lockdiscovery></D:prop>";
  return r;
}

}  // namespace pos::files
