from flask import Flask, request, jsonify
import os, shutil
import platform
import psutil

app = Flask(__name__)

# Simple in-memory clipboard
clipboard = {"action": None, "path": None}

@app.route("/clipboard/copy", methods=["POST"])
def clipboard_copy():
    data = request.json
    clipboard["action"] = "copy"
    clipboard["path"] = data["path"]
    return jsonify({"status": "copied", "path": data["path"]})

@app.route("/clipboard/cut", methods=["POST"])
def clipboard_cut():
    data = request.json
    clipboard["action"] = "cut"
    clipboard["path"] = data["path"]
    return jsonify({"status": "cut", "path": data["path"]})

@app.route("/clipboard/paste", methods=["POST"])
def clipboard_paste():
    data = request.json
    target = data["target"]

    if not clipboard["path"]:
        return jsonify({"status": "empty"})

    src = clipboard["path"]
    filename = os.path.basename(src)
    dst = os.path.join(target, filename)

    # If copy → duplicate the file
    if clipboard["action"] == "copy":
        if os.path.isfile(src):
            shutil.copy(src, dst)
        else:
            shutil.copytree(src, dst, dirs_exist_ok=True)

    # If cut → move the file
    elif clipboard["action"] == "cut":
        shutil.move(src, dst)
        clipboard["path"] = None  # clear clipboard after cut

    return jsonify({"status": "pasted", "to": dst})

@app.route("/files/rename", methods=["POST"])
def rename_file():
    data = request.json
    old_path = data["path"]
    new_name = data["new_name"]

    folder = os.path.dirname(old_path)
    new_path = os.path.join(folder, new_name)

    try:
        os.rename(old_path, new_path)
        return jsonify({"status": "renamed", "new_path": new_path})
    except Exception as e:
        return jsonify({"status": "error", "error": str(e)})

@app.route("/files/delete", methods=["POST"])
def delete_file():
    data = request.json
    path = data["path"]

    try:
        if os.path.isfile(path):
            os.remove(path)
        else:
            shutil.rmtree(path)
        return jsonify({"status": "deleted"})
    except Exception as e:
        return jsonify({"status": "error", "error": str(e)})

@app.route("/files/new_folder", methods=["POST"])
def new_folder():
    data = request.json
    path = data["path"]
    name = data["name"]

    folder_path = os.path.join(path, name)

    try:
        os.makedirs(folder_path, exist_ok=True)
        return jsonify({"status": "created", "path": folder_path})
    except Exception as e:
        return jsonify({"status": "error", "error": str(e)})

@app.route("/files/new_file", methods=["POST"])
def new_file():
    data = request.json
    path = data["path"]
    name = data["name"]

    file_path = os.path.join(path, name)

    try:
        with open(file_path, "w") as f:
            f.write("")  # create empty file
        return jsonify({"status": "created", "path": file_path})
    except Exception as e:
        return jsonify({"status": "error", "error": str(e)})

@app.route("/hardware")
def hardware_info():
    # CPU
    cpu_percent = psutil.cpu_percent(interval=0.5)
    cpu_count = psutil.cpu_count()

    # RAM
    ram = psutil.virtual_memory()
    ram_total = ram.total
    ram_used = ram.used
    ram_percent = ram.percent

    # Disk
    disk = psutil.disk_usage("/")
    disk_total = disk.total
    disk_used = disk.used
    disk_percent = disk.percent

    # Battery
    battery = psutil.sensors_battery()
    if battery:
        battery_percent = battery.percent
        charging = battery.power_plugged
    else:
        battery_percent = None
        charging = None

    # OS info
    os_name = platform.system()
    os_version = platform.version()

    return jsonify({
        "cpu_percent": cpu_percent,
        "cpu_count": cpu_count,
        "ram_total": ram_total,
        "ram_used": ram_used,
        "ram_percent": ram_percent,
        "disk_total": disk_total,
        "disk_used": disk_used,
        "disk_percent": disk_percent,
        "battery_percent": battery_percent,
        "charging": charging,
        "os_name": os_name,
        "os_version": os_version
    })

