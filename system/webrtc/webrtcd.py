#!/usr/bin/env python3

import argparse
import asyncio
import json
import logging
import os
import ssl
import subprocess
import uuid
from dataclasses import dataclass, field
from typing import Any, TYPE_CHECKING

# aiortc and its dependencies have lots of internal warnings :(
import warnings
warnings.filterwarnings("ignore", category=DeprecationWarning)
warnings.filterwarnings("ignore", category=RuntimeWarning) # TODO: remove this when google-crc32c publish a python3.12 wheel

import capnp
from aiohttp import web
if TYPE_CHECKING:
  from aiortc.rtcdatachannel import RTCDataChannel

from openpilot.system.webrtc.schema import generate_field
from cereal import messaging, log


class CerealOutgoingMessageProxy:
  def __init__(self, sm: messaging.SubMaster):
    self.sm = sm
    self.channels: list[RTCDataChannel] = []

  def add_channel(self, channel: 'RTCDataChannel'):
    self.channels.append(channel)

  def to_json(self, msg_content: Any):
    if isinstance(msg_content, capnp._DynamicStructReader):
      msg_dict = msg_content.to_dict()
    elif isinstance(msg_content, capnp._DynamicListReader):
      msg_dict = [self.to_json(msg) for msg in msg_content]
    elif isinstance(msg_content, bytes):
      msg_dict = msg_content.decode()
    else:
      msg_dict = msg_content

    return msg_dict

  def update(self):
    # this is blocking in async context...
    self.sm.update(0)
    for service, updated in self.sm.updated.items():
      if not updated:
        continue
      msg_dict = self.to_json(self.sm[service])
      mono_time, valid = self.sm.logMonoTime[service], self.sm.valid[service]
      outgoing_msg = {"type": service, "logMonoTime": mono_time, "valid": valid, "data": msg_dict}
      encoded_msg = json.dumps(outgoing_msg).encode()
      for channel in self.channels:
        channel.send(encoded_msg)


class CerealIncomingMessageProxy:
  def __init__(self, pm: messaging.PubMaster):
    self.pm = pm

  def send(self, message: bytes):
    msg_json = json.loads(message)
    msg_type, msg_data = msg_json["type"], msg_json["data"]
    size = None
    if not isinstance(msg_data, dict):
      size = len(msg_data)

    msg = messaging.new_message(msg_type, size=size)
    setattr(msg, msg_type, msg_data)
    self.pm.send(msg_type, msg)


class CerealProxyRunner:
  def __init__(self, proxy: CerealOutgoingMessageProxy):
    self.proxy = proxy
    self.is_running = False
    self.task = None
    self.logger = logging.getLogger("webrtcd")

  def start(self):
    assert self.task is None
    self.task = asyncio.create_task(self.run())

  def stop(self):
    if self.task is None or self.task.done():
      return
    self.task.cancel()
    self.task = None

  async def run(self):
    from aiortc.exceptions import InvalidStateError

    while True:
      try:
        self.proxy.update()
      except InvalidStateError:
        self.logger.warning("Cereal outgoing proxy invalid state (connection closed)")
        break
      except Exception:
        self.logger.exception("Cereal outgoing proxy failure")
      await asyncio.sleep(0.01)


class DynamicPubMaster(messaging.PubMaster):
  def __init__(self, *args, **kwargs):
    super().__init__(*args, **kwargs)
    self.lock = asyncio.Lock()

  async def add_services_if_needed(self, services):
    async with self.lock:
      for service in services:
        if service not in self.sock:
          self.sock[service] = messaging.pub_sock(service)


class StreamSession:
  shared_pub_master = DynamicPubMaster([])

  def __init__(self, sdp: str, cameras: list[str], incoming_services: list[str], outgoing_services: list[str], debug_mode: bool = False):
    from aiortc.mediastreams import VideoStreamTrack
    from openpilot.system.webrtc.device.video import LiveStreamVideoStreamTrack
    from teleoprtc import WebRTCAnswerBuilder
    from teleoprtc.info import parse_info_from_offer

    config = parse_info_from_offer(sdp)
    builder = WebRTCAnswerBuilder(sdp)

    assert len(cameras) == config.n_expected_camera_tracks, "Incoming stream has misconfigured number of video tracks"
    for cam in cameras:
      builder.add_video_stream(cam, LiveStreamVideoStreamTrack(cam) if not debug_mode else VideoStreamTrack())

    self.stream = builder.stream()
    self.identifier = str(uuid.uuid4())

    self.incoming_bridge: CerealIncomingMessageProxy | None = None
    self.incoming_bridge_services = incoming_services
    self.outgoing_bridge: CerealOutgoingMessageProxy | None = None
    self.outgoing_bridge_runner: CerealProxyRunner | None = None
    if len(incoming_services) > 0:
      self.incoming_bridge = CerealIncomingMessageProxy(self.shared_pub_master)
    if len(outgoing_services) > 0:
      self.outgoing_bridge = CerealOutgoingMessageProxy(messaging.SubMaster(outgoing_services))
      self.outgoing_bridge_runner = CerealProxyRunner(self.outgoing_bridge)

    self.run_task: asyncio.Task | None = None
    self.logger = logging.getLogger("webrtcd")
    self.logger.info("New stream session (%s), cameras %s, incoming services %s, outgoing services %s",
                      self.identifier, cameras, incoming_services, outgoing_services)

  def start(self):
    self.run_task = asyncio.create_task(self.run())

  def stop(self):
    if self.run_task.done():
      return
    self.run_task.cancel()
    self.run_task = None
    asyncio.run(self.post_run_cleanup())

  async def get_answer(self):
    return await self.stream.start()

  async def message_handler(self, message: bytes):
    assert self.incoming_bridge is not None
    try:
      self.incoming_bridge.send(message)
    except Exception:
      self.logger.exception("Cereal incoming proxy failure")

  async def run(self):
    try:
      await self.stream.wait_for_connection()
      if self.stream.has_messaging_channel():
        if self.incoming_bridge is not None:
          await self.shared_pub_master.add_services_if_needed(self.incoming_bridge_services)
          self.stream.set_message_handler(self.message_handler)
        if self.outgoing_bridge_runner is not None:
          channel = self.stream.get_messaging_channel()
          self.outgoing_bridge_runner.proxy.add_channel(channel)
          self.outgoing_bridge_runner.start()
      self.logger.info("Stream session (%s) connected", self.identifier)

      await self.stream.wait_for_disconnection()
      await self.post_run_cleanup()

      self.logger.info("Stream session (%s) ended", self.identifier)
    except Exception:
      self.logger.exception("Stream session failure")

  async def post_run_cleanup(self):
    await self.stream.stop()
    if self.outgoing_bridge is not None:
      self.outgoing_bridge_runner.stop()


@dataclass
class StreamRequestBody:
  sdp: str
  cameras: list[str]
  bridge_services_in: list[str] = field(default_factory=list)
  bridge_services_out: list[str] = field(default_factory=list)


async def get_stream(request: 'web.Request'):
  stream_dict, debug_mode = request.app['streams'], request.app['debug']
  raw_body = await request.json()
  body = StreamRequestBody(**raw_body)

  session = StreamSession(body.sdp, body.cameras, body.bridge_services_in, body.bridge_services_out, debug_mode)
  answer = await session.get_answer()
  session.start()

  stream_dict[session.identifier] = session

  return web.json_response({"sdp": answer.sdp, "type": answer.type})


async def get_schema(request: 'web.Request'):
  services = request.query["services"].split(",")
  services = [s for s in services if s]
  assert all(s in log.Event.schema.fields and not s.endswith("DEPRECATED") for s in services), "Invalid service name"
  schema_dict = {s: generate_field(log.Event.schema.fields[s]) for s in services}
  return web.json_response(schema_dict)


TRUST_HTML = """<!DOCTYPE html>
<html><head><title>comma body</title>
<style>
  body { background: #111; color: #fff; font-family: -apple-system, system-ui, sans-serif;
         display: flex; align-items: center; justify-content: center; height: 100vh; margin: 0; }
  .card { text-align: center; max-width: 400px; padding: 40px; }
  h1 { font-size: 24px; margin-bottom: 8px; }
  p { color: #aaa; font-size: 14px; }
  .check { font-size: 64px; margin-bottom: 16px; }
</style></head>
<body><div class="card">
  <h1>SSL Certificate Accepted</h1>
  <p>You can close this tab and return to the connect app.</p>
  <script>
    if (window.opener) {
      window.opener.postMessage({ type: 'ssl_cert_accepted' }, '*');
    }
    setTimeout(() => window.close(), 100);
  </script>
</div></body></html>"""


async def get_trust(request: 'web.Request'):
  return web.Response(content_type="text/html", text=TRUST_HTML)


async def post_notify(request: 'web.Request'):
  try:
    payload = await request.json()
  except Exception as e:
    raise web.HTTPBadRequest(text="Invalid JSON") from e

  for session in list(request.app.get('streams', {}).values()):
    try:
      ch = session.stream.get_messaging_channel()
      ch.send(json.dumps(payload))
    except Exception:
      continue

  return web.Response(status=200, text="OK")

async def on_shutdown(app: 'web.Application'):
  for session in app['streams'].values():
    session.stop()
  del app['streams']


CERT_PATH = "/data/webrtc_cert.pem"
KEY_PATH = "/data/webrtc_key.pem"


def create_ssl_cert():
  logger = logging.getLogger("webrtcd")
  try:
    subprocess.run([
      "openssl", "req", "-x509", "-newkey", "rsa:4096", "-nodes",
      "-out", CERT_PATH, "-keyout", KEY_PATH,
      "-days", "365", "-subj", "/C=US/ST=California/O=commaai/OU=comma body",
    ], capture_output=True, text=True, check=True)
  except subprocess.CalledProcessError as ex:
    raise ValueError(f"Error creating SSL certificate:\n[stdout]\n{ex.stdout}\n[stderr]\n{ex.stderr}") from ex
  logger.info("SSL certificate created")


def create_ssl_context():
  logger = logging.getLogger("webrtcd")
  if not os.path.exists(CERT_PATH) or not os.path.exists(KEY_PATH):
    logger.info("Creating SSL certificate...")
    create_ssl_cert()
  else:
    logger.info("SSL certificate exists")
  ssl_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
  ssl_ctx.load_cert_chain(CERT_PATH, KEY_PATH)
  return ssl_ctx


def create_app(debug: bool) -> web.Application:
  app = web.Application()
  app['streams'] = dict()
  app['debug'] = debug
  app.on_shutdown.append(on_shutdown)
  app.router.add_post("/stream", get_stream)
  app.router.add_post("/notify", post_notify)
  app.router.add_get("/schema", get_schema)
  app.router.add_get("/trust", get_trust)
  return app


def webrtcd_thread(host: str, port: int, debug: bool):
  logging.basicConfig(level=logging.CRITICAL, handlers=[logging.StreamHandler()])
  logging_level = logging.DEBUG if debug else logging.INFO
  logging.getLogger("WebRTCStream").setLevel(logging_level)
  logging.getLogger("webrtcd").setLevel(logging_level)
  logger = logging.getLogger("webrtcd")

  app = create_app(debug)
  https_port = port + 1

  loop = asyncio.new_event_loop()
  asyncio.set_event_loop(loop)

  runner = web.AppRunner(app)

  async def start():
    await runner.setup()

    http_site = web.TCPSite(runner, host, port)
    await http_site.start()
    logger.info("HTTP server running on %s:%d", host, port)

    https_site = web.TCPSite(runner, host, https_port, ssl_context=create_ssl_context())
    await https_site.start()
    logger.info("HTTPS server running on %s:%d", host, https_port)

  try:
    loop.run_until_complete(start())
    loop.run_forever()
  finally:
    loop.run_until_complete(runner.cleanup())
    loop.close()


def main():
  parser = argparse.ArgumentParser(description="WebRTC daemon")
  parser.add_argument("--host", type=str, default="0.0.0.0", help="Host to listen on")
  parser.add_argument("--port", type=int, default=5001, help="Port to listen on")
  parser.add_argument("--debug", action="store_true", help="Enable debug mode")
  args = parser.parse_args()

  webrtcd_thread(args.host, args.port, args.debug)


if __name__=="__main__":
  main()
