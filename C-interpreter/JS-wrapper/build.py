"""Builds unilang.html: compiles ../main.c with Emscripten (wasm embedded as base64)
and pastes the generated JS into template.html. Output is one standalone file."""
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "main.c")
TMP = tempfile.mkdtemp()
JS_OUT = os.path.join(TMP, "unilang.js")
TEMPLATE = os.path.join(HERE, "template.html")
HTML_OUT = os.path.join(HERE, "unilang.html")
PLACEHOLDER = "/*__UNILANG_JS__*/"

emcc = shutil.which("emcc")
if not emcc:
    sys.exit("emcc not found. Install emsdk and run emsdk_env.bat first (see README.md).")

cmd = [
    emcc, SRC,
    "-O2",
    "-sUSE_SDL=2",
    "-sASYNCIFY",
    "-sALLOW_MEMORY_GROWTH=1",
    "-sMODULARIZE=1",
    "-sEXPORT_NAME=createUnilang",
    "-sEXIT_RUNTIME=1",
    "-sSINGLE_FILE=1",
    "-sFORCE_FILESYSTEM=1",
    "-sENVIRONMENT=web",
    "-sEXPORTED_FUNCTIONS=_main,_emscripten_force_exit",
    "-sEXPORTED_RUNTIME_METHODS=FS,FS_createDataFile",
    "-o", JS_OUT,
]
print(" ".join(cmd))
if subprocess.call(cmd) != 0:
    sys.exit("emcc failed.")

with open(JS_OUT, "r", encoding="utf-8") as f:
    js = f.read()
js = js.replace("</script", "<\\/script")

with open(TEMPLATE, "r", encoding="utf-8") as f:
    html = f.read()
if PLACEHOLDER not in html:
    sys.exit("Placeholder not found in template.html.")
html = html.replace(PLACEHOLDER, js)

with open(HTML_OUT, "w", encoding="utf-8") as f:
    f.write(html)
shutil.rmtree(TMP, ignore_errors=True)
print("Wrote", HTML_OUT, "(%d KB)" % (len(html) // 1024))
