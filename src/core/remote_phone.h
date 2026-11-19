// Remote control from the paired phone (task P7-21, DECISIONS.md D40): the companion app signs in to wireless development
// (platform/posix/remote_server, core/remote.h) with a key derived from the pairing, and gets the screen and touch only.
//
//   key   = HKDF-Expand(prk = k_p2d, info = "PaperOS remote", 32)          (COMPANION.md section 10)
//   proof = HMAC-SHA256(key, "paperos-remote:" + nonce hex)                  the nonce of "#hello paperos 1 <build> <nonce>"
//   line  = ">auth-phone <hex of proof>"
//
// A phone session may send >shot, >refresh, >tap, >long and >swipe; anything else is answered "#err not from the phone"
// (no files, no settings, no updates, no restart). The Paper tells the phone where to find it with a `remote` envelope
// over the relay when wireless development starts listening. Unit-tested (tests/unit/remote_phone_test.cpp).
#pragma once

#include <cstdint>
#include <string>

#include "core/companion_crypto.h"
#include "core/remote.h"

namespace pos {
class Os;
}

namespace pos::remotephone {

companion::Key remoteKey(const companion::Key& p2d);
// Lower-case hex of the proof (64 characters).
std::string proof(const companion::Key& key, const std::string& nonceHex);
// True when `line` starts with ">auth-phone ".
bool isPhoneSignIn(const std::string& line);
// True when `line` is ">auth-phone <proof>" for this nonce and key (constant-time compare).
bool authOk(const std::string& nonceHex, const std::string& line, const companion::Key& key);
// The sign-in of the paired phone: loads the pairing (two curve operations, ~50 ms on the device; once per connection).
// False when no phone is paired.
bool pairedAuthOk(const std::string& nonceHex, const std::string& line);
// What a phone session may do.
bool allowedForPhone(RemoteCommand::Kind kind);

// Wireless development listens at `address`: tell the paired phone (a `remote` envelope pushed to the relay, once per address
// per boot; nothing when no phone is paired). Called on every server pass while listening: cheap when there is nothing to
// do, and a failed push (or no phone paired yet) is tried again after 5 minutes. UI thread.
void announce(Os& os, const std::string& address, uint16_t port, uint32_t nowMs);

}  // namespace pos::remotephone
