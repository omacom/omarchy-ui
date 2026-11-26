"""Start the Qt test without Hyprland, then make a quiet instance available."""

import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading


def serve_options(server, finished):
    while not finished.is_set():
        try:
            connection, _ = server.accept()
        except TimeoutError:
            continue
        with connection:
            connection.settimeout(2)
            request = connection.recv(1024)
            reply = b'{"int":8}' if request == b"j/getoption decoration:rounding" else b"{}"
            connection.sendall(reply)


def main(binary):
    with tempfile.TemporaryDirectory(prefix="oqh-") as temporary:
        fixture = Path(temporary)
        home = fixture / "home"
        runtime = fixture / "run"
        home.mkdir()
        runtime.mkdir()
        cache = os.environ.get("XDG_CACHE_HOME") or str(Path.home() / ".cache")
        env = dict(os.environ, HOME=str(home), XDG_RUNTIME_DIR=str(runtime),
                   XDG_CACHE_HOME=cache, OMARCHY_UI_LIVE_HOME=str(home), OMARCHY_UI_ROUNDING="")
        env.pop("OMARCHY_UI_THEME_DIR", None)
        env.pop("HYPRLAND_INSTANCE_SIGNATURE", None)
        test = Path(__file__).with_name("tst_hyprland_reconnect.qml")
        finished = threading.Event()
        with (socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as options,
              socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as events,
              subprocess.Popen([binary, "-input", str(test)], env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True) as runner):
            worker = None
            deadline = threading.Timer(15, runner.kill)
            deadline.start()
            try:
                for line in runner.stdout:
                    print(line, end="", flush=True)
                    if "start Hyprland fixture" in line:
                        instance = runtime / "hypr" / "test"
                        instance.mkdir(parents=True)
                        options.bind(str(instance / ".socket.sock"))
                        options.listen()
                        options.settimeout(0.2)
                        events.bind(str(instance / ".socket2.sock"))
                        events.listen()
                        worker = threading.Thread(target=serve_options, args=(options, finished))
                        worker.start()
                return runner.wait(timeout=2)
            finally:
                deadline.cancel()
                finished.set()
                if worker is not None:
                    worker.join()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
