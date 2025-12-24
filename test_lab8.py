import argparse
import socket
import subprocess
import sys
import time
from pathlib import Path

from tests.lab8 import resource_retrieve_test, structure_parse_test, uri_mapping_test, web_echo_test


def run_and_summary(test_instance, test_title):
    test_instance.run_all_cases()
    test_instance.print_results()
    results = test_instance.to_dict()
    if results["success_cnt"] != results["total_cnt"]:
        print(f"{test_title} Test Failed")
        print(f"Success: {results['success_cnt']} Threads")
        print(f"Runtime Error: {results['error_cnt']} Threads")
        print(f"Time Limit Exceed: {results['timeout_cnt']} Threads")
        return False
    print(f"{test_title} Test Passed")
    return True


def wait_for_port(host, port, timeout=3.0, poll_interval=0.1, proc=None):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if proc is not None and proc.poll() is not None:
            raise RuntimeError("Server process exited early.")
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(poll_interval)
            try:
                sock.connect((host, port))
                return
            except OSError:
                time.sleep(poll_interval)
    raise TimeoutError(f"Timed out waiting for {host}:{port}")


def pick_free_port(host):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind((host, 0))
        return sock.getsockname()[1]


def start_server(exe_path, host, port, mode, assets_root=None, cwd=None):
    cmd = [str(exe_path), "-host", host, "-port", str(port), "-mode", mode]
    if assets_root:
        cmd += ["-root", str(assets_root)]
    proc = subprocess.Popen(
        cmd,
        cwd=str(cwd) if cwd else None,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        wait_for_port(host, port, proc=proc)
    except Exception:
        stdout, stderr = proc.communicate(timeout=1)
        raise RuntimeError(
            f"Failed to start server.\nstdout:\n{stdout}\nstderr:\n{stderr}"
        )
    return proc


def stop_server(proc):
    if proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=2)


def run_test_mode(mode, host, port, exe_path, assets_root, cwd):
    test_map = {
        "parse": (structure_parse_test, "Lab8 Structure Parse"),
        "echo": (web_echo_test, "Lab8 Web Echo"),
        "map": (uri_mapping_test, "Lab8 URI Mapping"),
        "full": (resource_retrieve_test, "Lab8 Resource Retrieve"),
    }
    test_func, title = test_map[mode]
    proc = start_server(
        exe_path,
        host,
        port,
        mode,
        assets_root=assets_root if mode == "full" else None,
        cwd=cwd,
    )
    try:
        test = test_func(host, port)
        return run_and_summary(test, title)
    finally:
        stop_server(proc)


def main():
    parser = argparse.ArgumentParser(description="Run lab8 tests with local server.")
    parser.add_argument("--host", type=str, default="127.0.0.1", help="Server host")
    parser.add_argument("--port", type=int, default=0, help="Server port (0 for auto)")
    parser.add_argument(
        "--mode",
        type=str,
        default="all",
        choices=["parse", "echo", "map", "full", "all"],
        help="Server mode to test",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent
    exe_path = repo_root / "lab8" / "lab8"
    if not exe_path.exists():
        print(f"Executable not found: {exe_path}")
        return 2

    assets_root = repo_root / "assets"
    if args.mode in ("full", "all") and not assets_root.exists():
        print(f"Assets directory not found: {assets_root}")
        return 2

    modes = ["parse", "echo", "map", "full"] if args.mode == "all" else [args.mode]
    all_passed = True
    for mode in modes:
        port = args.port if args.port else pick_free_port(args.host)
        print(f"Starting server: mode={mode} host={args.host} port={port}")
        passed = run_test_mode(
            mode,
            args.host,
            port,
            exe_path,
            assets_root,
            repo_root,
        )
        all_passed = all_passed and passed

    return 0 if all_passed else 1


if __name__ == "__main__":
    sys.exit(main())
