import json

def clock_sync_build_json(payload: dict) -> str | None:
  import time as _time
  data = payload.get("data", {})
  if data.get("action") != "ping":
    raise ValueError
  return json.dumps({
    "type": "clockSync",
    "data": {
      "action": "pong",
      "browserSendTime": data.get("browserSendTime"),
      "deviceTime": _time.time() * 1000,  # noqa: TID251
    }
  })
