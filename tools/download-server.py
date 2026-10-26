#!/usr/bin/env python3
"""Serve the curated LAN download directory with validated, resumable ranges."""
import argparse
import hashlib
import os
import re
import threading
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer


class DownloadHandler(SimpleHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    _tags = {}
    _tag_lock = threading.Lock()

    def send_head(self):
        path = self.translate_path(self.path)
        if os.path.isdir(path):
            path = os.path.join(path, 'index.html')
        try:
            stream = open(path, 'rb')
        except OSError:
            self.send_error(404, 'File not found')
            return None
        stat = os.fstat(stream.fileno())
        key = (stat.st_dev, stat.st_ino, stat.st_size, stat.st_mtime_ns)
        with self._tag_lock:
            tag = self._tags.get(key)
            if tag is None:
                digest = hashlib.file_digest(stream, 'sha256').hexdigest()
                stream.seek(0)
                tag = '"sha256-' + digest + '"'
                if len(self._tags) >= 64:
                    self._tags.clear()
                self._tags[key] = tag
        modified = self.date_time_string(stat.st_mtime)
        if self.headers.get('If-None-Match') == tag:
            self.send_response(304)
            self.send_header('ETag', tag)
            self.end_headers()
            stream.close()
            return None
        first, last, status = 0, stat.st_size - 1, 200
        value = self.headers.get('Range', '')
        validator = self.headers.get('If-Range')
        match = re.fullmatch(r'bytes=(\d*)-(\d*)', value) if len(value) < 64 else None
        if match and any(match.groups()) and (not validator or validator in (tag, modified)):
            start, stop = match.groups()
            if start:
                first = int(start)
                last = min(int(stop), last) if stop else last
            else:
                first = max(0, stat.st_size - int(stop))
                if int(stop) == 0:
                    first = stat.st_size
            if first > last:
                self.send_response(416)
                self.send_header('Content-Range', f'bytes */{stat.st_size}')
                self.send_header('Content-Length', '0')
                self.send_header('ETag', tag)
                self.end_headers()
                stream.close()
                return None
            status = 206
        self.send_response(status)
        self.send_header('Content-Type', self.guess_type(path))
        self.send_header('Content-Length', str(last - first + 1))
        self.send_header('Accept-Ranges', 'bytes')
        self.send_header('ETag', tag)
        self.send_header('Last-Modified', modified)
        self.send_header('Cache-Control', 'no-cache')
        if status == 206:
            self.send_header('Content-Range', f'bytes {first}-{last}/{stat.st_size}')
        self.end_headers()
        stream.seek(first)
        self.remaining = last - first + 1
        return stream

    def copyfile(self, source, outputfile):
        try:
            while self.remaining:
                block = source.read(min(128 * 1024, self.remaining))
                if not block:
                    self.close_connection = True
                    break
                outputfile.write(block)
                self.remaining -= len(block)
        except (BrokenPipeError, ConnectionResetError):
            self.close_connection = True


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('port', type=int)
    parser.add_argument('--bind', required=True)
    parser.add_argument('--directory', required=True)
    args = parser.parse_args()
    with ThreadingHTTPServer((args.bind, args.port), partial(DownloadHandler, directory=args.directory)) as server:
        server.serve_forever()
