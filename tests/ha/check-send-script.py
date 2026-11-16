"""Runs the guards of script.paperos_whatsapp_send (ha/paperos_inbox.yaml) against 24 cases: the script's steps and the
rate-limit sensor sensor.paperos_whatsapp_sends are rendered in plain Jinja2 with stand-ins for Home Assistant's
extensions (int with a default, the match test, now(), state_attr, native result types). It checks the templates' syntax
and logic, not Home Assistant's runtime (the live check is in docs/STATUS.md). Nothing is sent anywhere.

  uv run -q --no-project --with jinja2 --with pyyaml python -I tests/ha/check-send-script.py ha/paperos_inbox.yaml
"""
import ast, re, sys
import jinja2, yaml

class Loader(yaml.SafeLoader): pass
doc = yaml.load(open(sys.argv[1]), Loader=Loader)
script = doc["script"]["paperos_whatsapp_send"]
sensor = [t for t in doc["template"] if t["triggers"][0]["event_type"] == "paperos_whatsapp_send_log"][0]["sensor"][0]
assert script["mode"] == "single"

NOW = [1768480000.0]
STATE = {"times": []}

class Now:
    def timestamp(self): return NOW[0]
def ha_int(v, default=0):
    try: return int(float(v)) if not isinstance(v, bool) else int(v)
    except Exception: return default
env = jinja2.Environment(undefined=jinja2.StrictUndefined, extensions=["jinja2.ext.do"])
env.filters["int"] = ha_int
env.tests["match"] = lambda v, p: re.match(p, str(v)) is not None
env.globals["now"] = lambda: Now()
env.globals["state_attr"] = lambda e, a: (STATE.get(a) if e == "sensor.paperos_whatsapp_sends" else None)

NUMERIC = re.compile(r"^[+-]?(?!0\d)\d*(?:\.\d*)?$")
def parse(s):  # Home Assistant's native types for a rendered template
    s = s.strip()
    try: r = ast.literal_eval(s)
    except Exception: return s
    if isinstance(r, (str, complex)): return s
    if isinstance(r, (int, float)) and not isinstance(r, bool) and NUMERIC.match(s) is None: return s
    return r
def render(tpl, vars_):
    if not isinstance(tpl, str): return tpl
    if isinstance(tpl, str) and "{" not in tpl: return tpl
    t = env.from_string(tpl)
    return parse(t.render(**vars_))
def render_complex(v, vars_):
    if isinstance(v, dict): return {k: render_complex(x, vars_) for k, x in v.items()}
    if isinstance(v, list): return [render_complex(x, vars_) for x in v]
    return render(v, vars_)

def run(fields, send_ok=True):
    vars_ = dict(fields)
    log = []
    for step in script["sequence"]:
        if "variables" in step:
            for k, v in step["variables"].items():
                vars_[k] = render_complex(v, vars_)
        elif "if" in step:
            cond = render(step["if"][0]["value_template"], vars_)
            if cond is True:
                for s2 in step["then"]:
                    if "variables" in s2:
                        for k, v in s2["variables"].items(): vars_[k] = render_complex(v, vars_)
                    if "stop" in s2: return vars_[s2["response_variable"]], log
        elif "event" in step:
            data = render_complex(step["event_data"], vars_)
            log.append(("event", data))
            fire_sensor(data)
        elif "action" in step:
            data = render_complex(step["data"], vars_)
            log.append(("send", data))
            if send_ok: vars_[step["response_variable"]] = {"success": True, "message_id": "3EB0ABC"}
        elif "stop" in step:
            return vars_[step["response_variable"]], log
    raise AssertionError("no stop")

def fire_sensor(data):
    class Ev: pass
    trig = {"event": type("E", (), {"data": data})()}
    this = type("T", (), {"attributes": dict(STATE)})()
    v = {"trigger": trig, "this": this}
    STATE["times"] = render(sensor["attributes"]["times"], v)
    st = render(sensor["state"], v)
    assert isinstance(st, int), st

def check(name, cond):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond: check.failed = True
check.failed = False

G = "120363012345678901@g.us"
P = "31612345678@s.whatsapp.net"
r, log = run({"chat": G, "text": "  Somdoku 12:34, no mistakes \n"})
check("group send ok", r == {"ok": True, "reason": "", "message_id": "3EB0ABC"})
check("message trimmed, target is the JID", log[-1] == ("send", {"target": G, "message": "Somdoku 12:34, no mistakes"}))
check("send logged before sending", log[0][0] == "event" and STATE["times"] == [1768480000])
NOW[0] += 5
r, log = run({"chat": P, "text": "hi"})
check("5 s later: too_soon, nothing sent", r["ok"] is False and r["reason"] == "too_soon" and not log)
NOW[0] += 6
r, log = run({"chat": P, "text": "42"})
check("11 s later: ok (text '42' that renders as a number)", r["ok"] is True and log[-1][1]["message"] == 42)
for bad in ["", "   ", None]:
    NOW[0] += 20
    r, log = run({"chat": P, "text": bad} if bad is not None else {"chat": P})
    check(f"empty text {bad!r}: empty", r["reason"] == "empty" and not log)
NOW[0] += 20
r, _ = run({"chat": P, "text": "x" * 1001})
check("1001 characters: too_long", r["reason"] == "too_long")
NOW[0] += 20
r, _ = run({"chat": P, "text": "é" * 1000})
check("1000 characters (2000 bytes): ok", r["ok"] is True)
for chat in ["31612345678", "31612345678@lid", "a@s.whatsapp.net", P + ",31600000000@s.whatsapp.net", "status@broadcast",
             "123@g.us.evil", "", " " + P + "x"]:
    NOW[0] += 20
    r, _ = run({"chat": chat, "text": "hi"})
    check(f"chat {chat!r}: bad_chat", r["reason"] == "bad_chat")
NOW[0] += 20
r, _ = run({"chat": "123456789-1600000000@g.us", "text": "old group id"})
check("old-style group id: ok", r["ok"] is True)
NOW[0] += 20
r, _ = run({"chat": P, "text": "fails"}, send_ok=False)
check("integration error: send_failed, counted", r == {"ok": False, "reason": "send_failed", "message_id": ""})
# hourly limit: fill up to 20 in the last hour
STATE["times"] = []
NOW[0] += 4000
sent = 0
for i in range(25):
    NOW[0] += 11
    r, _ = run({"chat": P, "text": f"n{i}"})
    sent += r["ok"] is True
    if i == 20: check("21st within the hour: hourly_limit", r["reason"] == "hourly_limit")
check("20 sent in the hour", sent == 20)
NOW[0] += 3600
r, _ = run({"chat": P, "text": "an hour later"})
check("an hour later: ok again, old times dropped", r["ok"] is True and len(STATE["times"]) == 1)
check("times list capped", all(isinstance(t, int) for t in STATE["times"]))
sys.exit(1 if check.failed else 0)
