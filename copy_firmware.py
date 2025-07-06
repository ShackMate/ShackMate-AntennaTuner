import os
import shutil
import json
Import("env")

def get_project_name(env):
    config = env.GetProjectConfig()
    try:
        return config.get("project", "project_name")
    except Exception:
        return env.GetProjectOption("project_name") or "ShackMate-CIV"

def get_version(env):
    config = env.GetProjectConfig()
    try:
        return config.get("project", "version")
    except Exception:
        return env.GetProjectOption("version") or "dev"

def after_build(source, target, env):
    print(f"[copy_firmware.py] Script started")
    project_name = get_project_name(env)
    version = get_version(env)
    build_dir = env.subst("$BUILD_DIR")

    print(f"[copy_firmware.py] Build directory contents: {os.listdir(build_dir)}")

    firmware_src = os.path.join(build_dir, "firmware.bin")
    firmware_dir = os.path.join(env['PROJECT_DIR'], "firmware")
    os.makedirs(firmware_dir, exist_ok=True)
    firmware_dest = os.path.join(firmware_dir, f"{project_name}-v{version}.bin")

    if os.path.exists(firmware_src):
        print(f"[copy_firmware.py] Attempting to copy from {firmware_src} to {firmware_dest}")
        shutil.copyfile(firmware_src, firmware_dest)
        print(f"[copy_firmware.py] Copied firmware to {firmware_dest}")
    else:
        print(f"[copy_firmware.py] Firmware not found at {firmware_src}")

    fs_filename = None
    for fs_image in ["spiffs.bin", "littlefs.bin"]:
        fs_src = os.path.join(build_dir, fs_image)
        if os.path.exists(fs_src):
            fs_dest = os.path.join(firmware_dir, f"{project_name}-fs-v{version}.bin")
            shutil.copyfile(fs_src, fs_dest)
            fs_filename = os.path.basename(fs_dest)
            print(f"[copy_firmware.py] Copied filesystem image to {fs_dest}")
            break

    if fs_filename:
        print(f"[copy_firmware.py] Filesystem image copied as {fs_filename}")
    else:
        print("[copy_firmware.py] No filesystem image found.")

    version_json_path = os.path.join(firmware_dir, "version.json")
    version_data = {
        "project": project_name,
        "version": version,
        "firmware_filename": os.path.basename(firmware_dest),
        "fs_filename": fs_filename
    }
    with open(version_json_path, "w") as f:
        json.dump(version_data, f, indent=2)
    print(f"[copy_firmware.py] Wrote {version_json_path}")
    print(f"[copy_firmware.py] Version file contents: {json.dumps(version_data, indent=2)}")

env.AddPostAction("buildprog", after_build)