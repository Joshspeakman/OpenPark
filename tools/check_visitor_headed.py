#!/usr/bin/env python3
"""Bounded, muted, visible-window test of ONLY the separate Visitor 3D Study.

The user explicitly requested headed testing. Copies saves into a unique test
profile; captures only this process's window, never the entire desktop. A failed
launch/capture is reported as a failure, not replaced by an offline render.
"""
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import time


def main():
    root = Path(__file__).resolve().parents[1]
    profiles = {
        '': ('OpenPark Visitor 3D Study', 'visitor3dstudy', 'visitor-3d'),
        '--reimagined': ('OpenPark Reimagined Study', 'reimaginedstudy', 'reimagined'),
        '--faithful-detail': ('OpenPark Faithful Detail Study', 'faithfuldetailstudy', 'faithful-detail'),
        '--faithful-layered': ('OpenPark Faithful Layered Study', 'faithfullayeredstudy', 'faithful-layered'),
    }
    option = sys.argv[1] if len(sys.argv) == 2 else ''
    if len(sys.argv)>2 or option not in profiles:
        raise SystemExit('usage: check_visitor_headed.py [--reimagined|--faithful-detail|--faithful-layered]')
    name, suffix, directory = profiles[option]
    identity = 'com.joshspeakman.openpark.' + suffix
    app = Path.home() / f"Applications/{name}.app"
    info = plistlib.loads((app / "Contents/Info.plist").read_bytes())
    if info.get("CFBundleIdentifier") != identity:
        raise RuntimeError("Refusing to test a different app")
    study = root / 'work' / directory
    source = study / 'profile'
    out = Path(tempfile.mkdtemp(prefix="headed-", dir=study))
    profile = out / "profile"
    profile.mkdir()
    (profile / "game").symlink_to(source / "game", target_is_directory=True)
    shutil.copytree(source / "saves", profile / "saves")
    if (source / "visitor3d").exists():
        shutil.copytree(source / "visitor3d", profile / "visitor3d")
    if (source / 'reimagined').exists():
        shutil.copytree(source / 'reimagined', profile / 'reimagined')
    if (source / 'faithful').exists():
        shutil.copytree(source / 'faithful', profile / 'faithful')
    control = out / "control.txt"
    control.write_text("show\nsize 960 600\nsleep 5\nkey 1c0d\nsleep 5\n"
                       "key 1454\nsleep 0.15\nkey 1245\nsleep 0.15\n"
                       "key 1f53\nsleep 0.15\nkey 1454\nsleep 0.15\n"
                       "key 1950\nsleep 0.15\nkey 1e41\nsleep 0.15\n"
                       "key 1352\nsleep 0.15\nkey 254b\nsleep 2\n"
                       "key 1c0d\nsleep 5\nkey 3d00\nsleep 10\n"
                       "walk 57.46 89.62\nsleep 2\n")
    # Focus the new approach on Toy Land before scaling to other models.
    # Front/side/rear coverage still needs human inspection.
    poses = [("midway", 57.46, 89.62, -71.565),
             ("toyland-front", 69.5, 86.5, -90),
             ("toyland-side", 75, 81, 180),
             ("toyland-rear", 69.5, 76, 90),
             ("toyland-quarter", 65, 85.5, -45)]
    if option == '--faithful-layered':
        poses = [('castle-front', 56.12, 114.7, -90),
                 ('castle-quarter', 53.3, 114, -55),
                 ('castle-side', 60.8, 109.9, 180),
                 ('castle-rear', 56.12, 105.1, 90),
                 ('castle-context', 56.12, 116.5, -90)]
    for name, x, y, yaw in poses:
        with control.open("a") as f:
            f.write(f"walkteleport {x} {y} {yaw}\nsleep 1\n"
                    f"walkshot {out / (name + '.png')}\n"
                    f"windowinfo {out / (name + '.window.json')}\n"
                    f"ack {name}\nsleep 2\n")
    with control.open("a") as f:
        f.write(f"sleep 1\nwalkshot {out / 'same-view-later.png'}\n"
                f"walkcmd 6\nsleep 1\nwalkshot {out / 'toyland-original.png'}\n"
                "ack complete\nsleep 1\nquit\n")
    env = dict(os.environ, DS_TEST="1", W32_AUDIO_MUTE="1", TP_SKIP_INTRO="1",
               TP_DATA=str(profile), TP_CONTROL=str(control), TP_TEST="65:quit",
               TP_WALK_MESH="1", TP_WALK_MESH_LOG="1")
    result = {"status": "failed", "app": str(app), "captures": [],
              "visual_quality": "unreviewed", "profile": str(profile)}
    print(f"Headed test output: {out}", flush=True)
    started = time.monotonic()
    with (out / "launch.log").open("wb") as log:
        proc = subprocess.Popen([str(app / "Contents/MacOS/ThemePark"),
                                 "-Sound", "NO", "-StartFullScreen", "NO",
                                 "-PlayIntro", "NO", "-FreeMapSize", "128"],
                                env=env, stdout=log, stderr=log)
        captured = set()
        try:
            while proc.poll() is None and time.monotonic() - started < 70:
                for name, *_ in poses:
                    meta = out / (name + ".window.json")
                    if name in captured or not meta.exists():
                        continue
                    window = json.loads(meta.read_text())
                    captured.add(name)
                    shot = out / (name + "-window.png")
                    valid = window["visible"] and window["pid"] == proc.pid and window["window_id"] > 0
                    cap = subprocess.run(["/usr/sbin/screencapture", "-x",
                                          "-l", str(window["window_id"]), str(shot)],
                                         capture_output=True, timeout=5) if valid else None
                    result["captures"].append(dict(name=name, window=window,
                        captured=bool(cap and cap.returncode == 0 and shot.exists()),
                        error=cap.stderr.decode(errors="replace") if cap else "Window not visible/owned"))
                time.sleep(.1)
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=5)
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()
        result["exit_code"] = proc.returncode
    ack = Path(str(control) + ".ack")
    complete = ack.exists() and "complete" in ack.read_text().splitlines()
    result["seconds"] = round(time.monotonic() - started, 2)
    result["control_complete"] = complete
    if complete and len(result["captures"]) == len(poses) and all(c["captured"] for c in result["captures"]):
        result["status"] = "captured; visual review required"
    (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    return 0 if result["status"].startswith("captured") else 1


if __name__ == "__main__":
    sys.exit(main())
