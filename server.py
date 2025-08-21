from flask import Flask, request, jsonify
import os, shutil

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
