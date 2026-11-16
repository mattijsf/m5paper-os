#!/usr/bin/env python3
"""Writes tests/fixtures/companion/vectors.json: fixed test vectors for docs/COMPANION.md (protocol v1), computed with an
implementation independent of the Paper's libsodium code (pyca/cryptography). Fixed keys and nonces: never use them for real.

  uv run -q --no-project --with cryptography python -I tests/fixtures/companion/make-vectors.py tests/fixtures/companion/vectors.json
"""
import base64, json, struct, sys
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
from cryptography.hazmat.primitives import hashes, hmac
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

def b64(b): return base64.urlsafe_b64encode(b).rstrip(b"=").decode()
def hk_extract(salt, ikm):
    h = hmac.HMAC(salt, hashes.SHA256()); h.update(ikm); return h.finalize()
def hk_expand(prk, info, n):
    out, t, i = b"", b"", 1
    while len(out) < n:
        h = hmac.HMAC(prk, hashes.SHA256()); h.update(t + info + bytes([i])); t = h.finalize(); out += t; i += 1
    return out[:n]

paper_sk = bytes(range(0x00, 0x20))
phone_sk = bytes(range(0x40, 0x60))
pid = bytes.fromhex("a1a2a3a4a5a6a7a8")
def pub(sk): return X25519PrivateKey.from_private_bytes(sk).public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)
paper_pk, phone_pk = pub(paper_sk), pub(phone_sk)
ss = X25519PrivateKey.from_private_bytes(paper_sk).exchange(X25519PublicKey.from_public_bytes(phone_pk))
transcript = pid + paper_pk + phone_pk
prk = hk_extract(b"PaperOS companion v1", ss)
k_p2d = hk_expand(prk, b"PaperOS p2d" + transcript, 32)
k_d2p = hk_expand(prk, b"PaperOS d2p" + transcript, 32)
code = struct.unpack(">I", hk_expand(prk, b"PaperOS code" + transcript, 4))[0] % 1000000

def seal(key, dirc, ctr, nonce, pt):
    head = bytes([1, ord(dirc)]) + pid + struct.pack(">Q", ctr)
    return head + nonce + ChaCha20Poly1305(key).encrypt(nonce, pt, head)
def seal_hello(nonce, pt):
    head = bytes([1, ord("H")]) + pid + phone_pk
    return head + nonce + ChaCha20Poly1305(k_p2d).encrypt(nonce, pt, head)

hello_pt = b'{"t":"hello","name":"Test Phone","app":"0.1"}'
post_pt = ('{"t":"post","k":"0|com.whatsapp|1|test|10001","app":"whatsapp","chat":"Family","reply":true,"ts":1768480000,'
           '"msgs":[{"from":"Anna","text":"Are you coming at six? €5","ts":1768479990}]}').encode()
reply_pt = b'{"t":"reply","k":"0|com.whatsapp|1|test|10001","text":"See you at six","ts":1768480100}'
n1, n2, n3 = bytes.fromhex("000102030405060708090a0b"), bytes.fromhex("101112131415161718191a1b"), bytes.fromhex("202122232425262728292a2b")
hello = seal_hello(n1, hello_pt)
post = seal(k_p2d, "P", 1, n2, post_pt)
reply = seal(k_d2p, "D", 1, n3, reply_pt)

def flip(b, i): b = bytearray(b); b[i] ^= 1; return bytes(b)
out = {
  "comment": "docs/COMPANION.md v1 test vectors (made by make-vectors.py with pyca/cryptography). Hex for keys, base64url without padding for envelopes.",
  "paper_sk": paper_sk.hex(), "phone_sk": phone_sk.hex(), "pid": pid.hex(), "pid_b64": b64(pid),
  "paper_pk": paper_pk.hex(), "phone_pk": phone_pk.hex(), "paper_pk_b64": b64(paper_pk),
  "qr_text": "paperos1:" + b64(pid) + ":" + b64(paper_pk) + ":https://ha.example.test",
  "shared_secret": ss.hex(), "prk": prk.hex(), "k_p2d": k_p2d.hex(), "k_d2p": k_d2p.hex(), "code": "%06d" % code,
  "hello": {"nonce": n1.hex(), "plaintext": hello_pt.decode(), "envelope": b64(hello)},
  "post": {"counter": 1, "nonce": n2.hex(), "plaintext": post_pt.decode(), "envelope": b64(post)},
  "reply": {"counter": 1, "nonce": n3.hex(), "plaintext": reply_pt.decode(), "envelope": b64(reply)},
  "reject_to_paper": {
    "flipped_tag": b64(flip(post, len(post) - 1)),
    "flipped_counter": b64(flip(post, 17)),
    "wrong_pid": b64(flip(post, 2)),
    "wrong_dir_reply_as_post": b64(reply),
    "oversize": b64(seal(k_p2d, "P", 2, n2, b"{" + b" " * 1000 + b"}")),
  },
}
json.dump(out, open(sys.argv[1], "w"), indent=1, ensure_ascii=False)
open(sys.argv[1], "a").write("\n")
