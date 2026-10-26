import concurrent.futures
import functools
import http.client
import importlib.util
import tempfile
import threading
from pathlib import Path
from http.server import ThreadingHTTPServer

spec = importlib.util.spec_from_file_location('downloads', Path(__file__).resolve().parents[1]/'tools/download-server.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)/'asset.cia'
    data = bytes(range(256))*4096
    path.write_bytes(data)
    server = ThreadingHTTPServer(('127.0.0.1', 0), functools.partial(module.DownloadHandler, directory=directory))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    def fetch(headers=None, method='GET'):
        connection = http.client.HTTPConnection(*server.server_address)
        connection.request(method, '/asset.cia', headers=headers or {})
        response = connection.getresponse()
        result = response.status, dict(response.getheaders()), response.read()
        connection.close()
        return result
    try:
        status, headers, body = fetch()
        assert status == 200 and body == data
        tag = headers['ETag']
        for value, expected in [('bytes=0-0', data[:1]), ('bytes=123-', data[123:]), ('bytes=-31', data[-31:]), ('bytes=9-700', data[9:701])]:
            status, headers, body = fetch({'Range': value, 'If-Range': tag})
            assert status == 206 and body == expected
            assert int(headers['Content-Length']) == len(expected)
        status, headers, body = fetch({'Range': f'bytes={len(data)}-'})
        assert status == 416 and not body and headers['Content-Range'] == f'bytes */{len(data)}'
        status, _, body = fetch({'Range': 'bytes=0-10', 'If-Range': '"old"'})
        assert status == 200 and body == data
        status, _, body = fetch({'If-None-Match': tag})
        assert status == 304 and not body
        status, headers, body = fetch({'Range': 'bytes=1-10'}, 'HEAD')
        assert status == 206 and headers['Content-Length'] == '10' and not body
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            for status, _, body in pool.map(lambda _: fetch({'Range': 'bytes=123-456'}), range(12)):
                assert status == 206 and body == data[123:457]
        replacement = Path(directory)/'next'
        replacement.write_bytes(b'new revision')
        replacement.replace(path)
        status, headers, body = fetch({'Range': 'bytes=0-1', 'If-Range': tag})
        assert status == 200 and body == b'new revision' and headers['ETag'] != tag
        print('PASS: full/ranged/suffix/HEAD/416/304, validators, concurrent requests and atomic revision replacement')
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
