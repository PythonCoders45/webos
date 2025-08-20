# storage_server.py
import os, sys, json, urllib.parse
from flask import Flask, jsonify, send_file, request, abort
from stat import S_ISDIR
from datetime import datetime

app = Flask(__name__)

# Configure candidate mount roots (can be extended via env var)
CANDIDATE_ROOTS = [
    '/run/media',    # many distros: /run/media/<user>/<label>
    '/media',        # /media/<user>/<label>
    '/mnt',
    '/Volumes'       # macOS
]

if os.name == 'nt':
    # Windows candidate: list drive letters
    CANDIDATE_ROOTS = []

def list_removable_linux():
    devices = []
    # search candidate roots
    for root in CANDIDATE_ROOTS:
        if not os.path.exists(root): continue
        for a in os.listdir(root):
            full = os.path.join(root, a)
            # if container user dir like /run/media/username, inspect inside
            if os.path.isdir(full):
                # if inside there's labels, iterate
                for b in os.listdir(full):
                    path = os.path.join(full, b)
                    if os.path.ismount(path):
                        try:
                            st = os.statvfs(path)
                            total = st.f_frsize * st.f_blocks
                            free = st.f_frsize * st.f_bavail
                        except Exception:
                            total = None; free = None
                        devices.append({
                            'name': b,
                            'label': b,
                            'mount': path,
                            'total': total,
                            'free': free
                        })
                # also consider the folder itself as mount
                if os.path.ismount(full):
                    try:
                        st = os.statvfs(full)
                        total = st.f_frsize * st.f_blocks
                        free = st.f_frsize * st.f_bavail
                    except Exception:
                        total = None; free = None
                    devices.append({
                        'name': a,
                        'label': a,
                        'mount': full,
                        'total': total,
                        'free': free
                    })
    # dedupe by mount
    uniq = {}
    for d in devices:
        uniq[d['mount']] = d
    return list(uniq.values())

def list_drives_windows():
    # simple: list all letters with drive type removable or fixed
    drives=[]
    import string, ctypes
    bitmask = ctypes.windll.kernel32.GetLogicalDrives()
    for i in range(26):
        if bitmask & (1 << i):
            drive = f"{string.ascii_uppercase[i]}:\\"
            # get drive type
            dtype = ctypes.windll.kernel32.GetDriveTypeW(drive)
            # 2=removable,3=fixed,4=remote,5=cdrom
            drives.append({'name':drive,'label':drive,'mount':drive,'type':dtype})
    return drives

@app.route('/api/storage')
def api_storage():
    if os.name == 'nt':
        devices = list_drives_windows()
        # simplify payload
        out=[]
        for d in devices:
            out.append({'name': d['name'], 'label': d['name'], 'mount': d['mount'], 'total': None, 'free': None})
        return jsonify({'devices':out})
    else:
        devices = list_removable_linux()
        return jsonify({'devices': devices})

def safe_path(path):
    # basic safety: reject paths with .. that go outside mount roots
    if '..' in path:
        abort(400)
    return os.path.abspath(path)

@app.route('/api/list')
def api_list():
    path = request.args.get('path')
    if not path:
        return jsonify({'error':'missing path'}), 400
    path = urllib.parse.unquote(path)
    path = safe_path(path)
    if not os.path.exists(path):
        return jsonify({'error':'not_found'}), 404
    entries=[]
    try:
        for name in os.listdir(path):
            full = os.path.join(path, name)
            st = os.lstat(full)
            is_dir = S_ISDIR(st.st_mode)
            entries.append({
                'name': name,
                'path': full,
                'is_dir': bool(is_dir),
                'size': None if is_dir else st.st_size,
                'mtime': int(st.st_mtime)
            })
    except PermissionError:
        return jsonify({'error':'permission_denied'}), 403
    # parent directory
    parent = os.path.dirname(path) if os.path.dirname(path) != path else None
    return jsonify({'entries': entries, 'parent': parent})

@app.route('/api/download')
def api_download():
    path = request.args.get('path')
    if not path:
        return jsonify({'error':'missing path'}), 400
    path = urllib.parse.unquote(path)
    path = safe_path(path)
    if not os.path.isfile(path):
        return jsonify({'error':'not_file'}), 400
    # return file for download
    return send_file(path, as_attachment=True)

if __name__ == '__main__':
    print('Starting storage server on http://127.0.0.1:5000')
    app.run(host='0.0.0.0', port=5000, debug=True)
