from flask import Flask, send_from_directory, jsonify, request
import os

app = Flask(__name__)

FILES_DIR = "files"
os.makedirs(FILES_DIR, exist_ok=True)

# Serve the WebOS desktop
@app.route("/")
def desktop():
    return send_from_directory("templates", "desktop.html")

# Serve files in the Files app
@app.route("/files/<filename>")
def serve_file(filename):
    path = os.path.join(FILES_DIR, filename)
    if os.path.exists(path):
        return send_from_directory(FILES_DIR, filename)
    return "File not found", 404

# List all files (for Files app)
@app.route("/api/list_files")
def list_files():
    return jsonify(os.listdir(FILES_DIR))

# Upload a file
@app.route("/api/upload", methods=["POST"])
def upload_file():
    if "file" not in request.files:
        return "No file uploaded", 400
    f = request.files["file"]
    f.save(os.path.join(FILES_DIR, f.filename))
    return "File uploaded", 200

if __name__ == "__main__":
    app.run(debug=True)
