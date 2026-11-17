// Files over Wi-Fi (core/file_server, DECISIONS.md D30): the code, paths kept inside the books folder, WebDAV as Finder and
// Android file managers use it, the upload page and its list, the Mac's junk files, the lockout.
#include <doctest.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unistd.h>

#include "core/file_server.h"
#include "core/fs.h"
#include "core/md5.h"

using namespace pos;
using namespace pos::files;

namespace {

struct Card {
  std::string dir;
  Card() {
    char tmpl[] = "/tmp/pos-files-XXXXXX";
    dir = mkdtemp(tmpl);
    fs::setRoots(dir + "/sd", dir + "/data");
    fs::mkdirs("/sd/books");
    fs::mkdirs("/data");
  }
  ~Card() {
    std::string cmd = "rm -rf '" + dir + "'";
    (void)!std::system(cmd.c_str());
  }
};

uint32_t gNow = 1000;

struct Fixture {
  Card card;
  FileServer srv{"/sd/books", "123456", [] { return gNow; }};
  // "paper:123456"
  const std::string auth = "Basic cGFwZXI6MTIzNDU2";

  hal::ServerResponse send(const std::string& method, const std::string& target, const std::string& body = "",
                           std::vector<std::pair<std::string, std::string>> headers = {}, bool withAuth = true) {
    hal::ServerRequest r;
    r.method = method;
    r.target = target;
    r.headers = std::move(headers);
    if (withAuth) r.headers.emplace_back("authorization", auth);
    r.contentLength = static_cast<int64_t>(body.size());
    auto at = std::make_shared<size_t>(0);
    auto data = std::make_shared<std::string>(body);
    r.read = [at, data](char* dst, size_t n) -> long {
      const size_t k = std::min(n, data->size() - *at);
      std::memcpy(dst, data->data() + *at, k);
      *at += k;
      return static_cast<long>(k);
    };
    return srv.handle(r);
  }
};

}  // namespace

TEST_CASE("files: helpers decode, encode and clean paths") {
  bool ok = false;
  CHECK(percentDecode("/Alice%20in%20Wonderland.epub", ok) == "/Alice in Wonderland.epub");
  CHECK(ok);
  percentDecode("/bad%2", ok);
  CHECK_FALSE(ok);
  percentDecode("/nul%00", ok);
  CHECK_FALSE(ok);
  CHECK(percentEncodePath("/a b/é.epub") == "/a%20b/%C3%A9.epub");
  CHECK(base64Decode("cGFwZXI6MTIzNDU2", ok) == "paper:123456");
  CHECK(httpDate(0) == "Thu, 01 Jan 1970 00:00:00 GMT");
  CHECK(cleanPath("/") == "/");
  CHECK(cleanPath("/books/") == "/books");
  CHECK(cleanPath("/a/../b") == "");
  CHECK(cleanPath("/a/%2e%2e/b") == "");
  CHECK(cleanPath("/a//b") == "");
  CHECK(cleanPath("relative") == "");
  CHECK(isHidden("/.paperos/x"));
  CHECK_FALSE(isHidden("/a.b/c"));
  CHECK(isMacJunk("/x/._Alice.epub"));
  CHECK(isMacJunk("/.DS_Store"));
  const std::string c = newCode();
  CHECK(c.size() == 6);
  CHECK(c.find_first_not_of("0123456789") == std::string::npos);
}

TEST_CASE("files: everything but OPTIONS needs the code; ten wrong ones lock for a minute") {
  Fixture f;
  CHECK(f.send("OPTIONS", "/", "", {}, false).status == 200);
  auto r = f.send("PROPFIND", "/", "", {}, false);
  CHECK(r.status == 401);
  CHECK_FALSE(r.headers.empty());
  CHECK(f.send("GET", "/", "", {{"authorization", "Basic cGFwZXI6MDAwMDAw"}}, false).status == 401);  // paper:000000
  CHECK(f.send("GET", "/", "", {{"authorization", "Basic MTIzNDU2Og=="}}, false).status == 200);      // the code as the name
  for (unsigned i = 1; i < FileServer::kMaxRefusals; ++i) f.send("GET", "/", "", {{"authorization", "Basic eDp5"}}, false);
  CHECK(f.send("GET", "/").status == 429);  // even with the right code
  gNow += FileServer::kLockoutMs + 1;
  CHECK(f.send("GET", "/").status == 200);
  CHECK(f.srv.stats().refused == FileServer::kMaxRefusals);
}

TEST_CASE("files: upload, list, download, rename, copy, delete inside the books folder") {
  Fixture f;
  CHECK(f.send("PUT", "/Alice%20in.epub", "EPUBDATA").status == 201);
  std::string got;
  REQUIRE(fs::readFile("/sd/books/Alice in.epub", got));
  CHECK(got == "EPUBDATA");
  CHECK(f.send("PUT", "/Alice%20in.epub", "NEW").status == 204);  // replaced
  REQUIRE(fs::readFile("/sd/books/Alice in.epub", got));
  CHECK(got == "NEW");
  CHECK(f.srv.stats().received == 2);
  CHECK(f.srv.stats().last == "Received Alice in.epub");

  auto list = f.send("GET", "/?list");
  CHECK(list.status == 200);
  CHECK(list.body == "[{\"name\":\"Alice in.epub\",\"dir\":false,\"size\":3}]");
  auto page = f.send("GET", "/");
  CHECK(page.body.find("<title>PaperOS files</title>") != std::string::npos);
  auto dl = f.send("GET", "/Alice%20in.epub");
  CHECK(dl.status == 200);
  CHECK(dl.file == "/sd/books/Alice in.epub");
  CHECK(dl.fileSize == 3);
  bool attachment = false;
  for (const auto& h : f.send("GET", "/Alice%20in.epub?download").headers)
    attachment = attachment || (h.first == "Content-Disposition" &&
                                h.second == "attachment; filename=\"Alice in.epub\"; filename*=UTF-8''Alice%20in.epub");
  CHECK(attachment);  // the page's Download button
  for (const auto& h : dl.headers) CHECK(h.first != "Content-Disposition");  // a plain GET (Finder, apps) stays plain

  CHECK(f.send("MKCOL", "/Classics").status == 201);
  CHECK(f.send("MKCOL", "/Classics").status == 405);
  CHECK(f.send("MKCOL", "/No/Parent").status == 409);
  CHECK(f.send("PUT", "/Missing/x.txt", "x").status == 409);
  CHECK(f.send("MOVE", "/Alice%20in.epub", "", {{"destination", "http://192.168.1.5/Classics/Alice.epub"}}).status == 201);
  CHECK(fs::exists("/sd/books/Classics/Alice.epub"));
  CHECK_FALSE(fs::exists("/sd/books/Alice in.epub"));
  CHECK(f.send("COPY", "/Classics/Alice.epub", "", {{"destination", "/Alice%20copy.epub"}}).status == 201);
  REQUIRE(fs::readFile("/sd/books/Alice copy.epub", got));
  CHECK(got == "NEW");
  CHECK(f.send("COPY", "/Classics/Alice.epub", "", {{"destination", "/Alice%20copy.epub"}, {"overwrite", "F"}}).status == 412);
  CHECK(f.send("DELETE", "/Classics").status == 204);  // with what is in it
  CHECK_FALSE(fs::exists("/sd/books/Classics/Alice.epub"));
  CHECK(f.send("DELETE", "/").status == 403);
  CHECK(f.send("DELETE", "/nothing").status == 404);
}

TEST_CASE("files: nothing outside the books folder, hidden names stay hidden, the Mac's junk is swallowed") {
  Fixture f;
  fs::writeFile("/sd/paperos_config", "HA_TOKEN=secret");
  fs::mkdirs("/sd/books/.paperos");
  fs::writeFile("/sd/books/.paperos/cache", "c");
  CHECK(f.send("GET", "/../paperos_config").status == 400);
  CHECK(f.send("GET", "/%2e%2e/paperos_config").status == 400);
  CHECK(f.send("PUT", "/../evil", "x").status == 400);
  CHECK(f.send("GET", "/.paperos/cache").status == 404);
  CHECK(f.send("DELETE", "/.paperos").status == 403);
  CHECK(f.send("PUT", "/.paperos/x", "x").status == 403);
  CHECK(f.send("MOVE", "/x", "", {{"destination", "/../y"}}).status == 400);
  // Finder writes "._name" and ".DS_Store" next to every file: accepted, never stored or listed.
  CHECK(f.send("PUT", "/._Alice.epub", "resourcefork").status == 201);
  CHECK(f.send("PUT", "/.DS_Store", "x").status == 201);
  CHECK_FALSE(fs::exists("/sd/books/._Alice.epub"));
  CHECK_FALSE(fs::exists("/sd/books/.DS_Store"));
  CHECK(f.send("GET", "/._Alice.epub").status == 404);
  auto list = f.send("GET", "/?list");
  CHECK(list.body == "[]");
}

TEST_CASE("files: PROPFIND, LOCK, PROPPATCH as Finder sends them") {
  Fixture f;
  f.send("PUT", "/a.txt", "hello");
  f.send("MKCOL", "/sub");
  auto r = f.send("PROPFIND", "/", "", {{"depth", "1"}});
  CHECK(r.status == 207);
  CHECK(r.body.find("<D:href>/</D:href>") != std::string::npos);
  CHECK(r.body.find("<D:href>/a.txt</D:href>") != std::string::npos);
  CHECK(r.body.find("<D:getcontentlength>5</D:getcontentlength>") != std::string::npos);
  CHECK(r.body.find("<D:href>/sub/</D:href>") != std::string::npos);
  CHECK(r.body.find("<D:collection/>") != std::string::npos);
  CHECK(r.body.find("quota-available-bytes") != std::string::npos);
  auto zero = f.send("PROPFIND", "/", "", {{"depth", "0"}});
  CHECK(zero.body.find("a.txt") == std::string::npos);
  CHECK(f.send("PROPFIND", "/nope").status == 404);
  // A lock on a new name makes an empty file (Finder locks, then PUTs).
  auto lock = f.send("LOCK", "/new.epub", "<?xml version=\"1.0\"?><D:lockinfo xmlns:D=\"DAV:\"/>");
  CHECK(lock.status == 201);
  CHECK(fs::exists("/sd/books/new.epub"));
  bool token = false;
  for (const auto& h : lock.headers) token = token || (h.first == "Lock-Token" && h.second.find("opaquelocktoken:") != std::string::npos);
  CHECK(token);
  CHECK(f.send("UNLOCK", "/new.epub").status == 204);
  CHECK(f.send("PROPPATCH", "/a.txt", "<x/>").status == 207);
  auto opt = f.send("OPTIONS", "/");
  bool dav2 = false;
  for (const auto& h : opt.headers) dav2 = dav2 || (h.first == "DAV" && h.second == "1, 2");
  CHECK(dav2);  // class 2: without locks Finder mounts read-only
}

TEST_CASE("files: a broken upload leaves nothing behind") {
  Fixture f;
  hal::ServerRequest r;
  r.method = "PUT";
  r.target = "/half.epub";
  r.headers.emplace_back("authorization", f.auth);
  r.contentLength = 100;
  int calls = 0;
  r.read = [&calls](char* dst, size_t) -> long {
    if (calls++ == 0) {
      std::memcpy(dst, "abc", 3);
      return 3;
    }
    return -1;  // the connection dropped
  };
  CHECK(f.srv.handle(r).status == 500);
  CHECK_FALSE(fs::exists("/sd/books/half.epub"));
  std::vector<fs::DirEntry> left;
  fs::listDir("/sd/books", left);
  for (const fs::DirEntry& e : left) CHECK(e.name.rfind(".upload-", 0) != 0);  // no temp file left behind
  CHECK(f.srv.stats().received == 0);
  CHECK_FALSE(f.srv.stats().busy);
}

TEST_CASE("files: a broken upload over a book keeps the book") {
  Fixture f;
  REQUIRE(fs::writeFile("/sd/books/keep.txt", "the old text"));
  hal::ServerRequest r;
  r.method = "PUT";
  r.target = "/keep.txt";
  r.headers.emplace_back("authorization", f.auth);
  r.contentLength = 100;
  int calls = 0;
  r.read = [&calls](char* dst, size_t) -> long {
    if (calls++ == 0) {
      std::memcpy(dst, "new", 3);
      return 3;
    }
    return -1;  // the connection dropped
  };
  CHECK(f.srv.handle(r).status == 500);
  std::string text;
  CHECK(fs::readFile("/sd/books/keep.txt", text));
  CHECK(text == "the old text");
}

TEST_CASE("files: Digest sign-in (what macOS sends over plain HTTP)") {
  CHECK(md5Hex("") == "d41d8cd98f00b204e9800998ecf8427e");
  CHECK(md5Hex("The quick brown fox jumps over the lazy dog") == "9e107d9d372bb6826bd81d3542a419d6");
  CHECK(md5Hex(std::string(100, 'a')) == "36a92cc94a9e0fa21f625f8bfb007adf");
  Fixture f;
  auto challenge = f.send("PROPFIND", "/", "", {}, false);
  REQUIRE(challenge.status == 401);
  std::string nonce;
  for (const auto& h : challenge.headers)
    if (h.first == "WWW-Authenticate" && h.second.rfind("Digest ", 0) == 0) {
      const auto a = h.second.find("nonce=\"") + 7;
      nonce = h.second.substr(a, h.second.find('"', a) - a);
    }
  REQUIRE(nonce.size() == 32);
  auto digest = [&](const std::string& code, const std::string& n) {
    const std::string ha1 = md5Hex("paper:PaperOS:" + code), ha2 = md5Hex("PROPFIND:/");
    const std::string resp = md5Hex(ha1 + ":" + n + ":00000001:abc:auth:" + ha2);
    return "Digest username=\"paper\", realm=\"PaperOS\", nonce=\"" + n + "\", uri=\"/\", qop=auth, nc=00000001, cnonce=\"abc\", response=\"" +
           resp + "\", algorithm=MD5";
  };
  CHECK(f.send("PROPFIND", "/", "", {{"authorization", digest("123456", nonce)}}, false).status == 207);
  CHECK(f.send("PROPFIND", "/", "", {{"authorization", digest("654321", nonce)}}, false).status == 401);
  CHECK(f.send("PROPFIND", "/", "", {{"authorization", digest("123456", std::string(32, '0'))}}, false).status == 401);
}
