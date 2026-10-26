import functools
import importlib.util
import ssl
import threading
from pathlib import Path
from http.server import ThreadingHTTPServer

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('downloads', root/'tools/download-server.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
handler = functools.partial(module.DownloadHandler, directory=root/'build/updater-review/fixture')
http = ThreadingHTTPServer(('0.0.0.0', 18997), handler)
https = ThreadingHTTPServer(('0.0.0.0', 18998), handler)
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain(root/'build/updater-review/tls/cert.pem', root/'build/updater-review/tls/key.pem')
https.socket = context.wrap_socket(https.socket, server_side=True)
threading.Thread(target=http.serve_forever, daemon=True).start()
try:
    https.serve_forever()
finally:
    http.shutdown(); http.server_close(); https.server_close()
