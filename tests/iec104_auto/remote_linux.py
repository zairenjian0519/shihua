from __future__ import annotations

import dataclasses
import pathlib
import time
from typing import Union


@dataclasses.dataclass
class RemoteConfig:
    host: str
    user: str
    password: str
    project_dir: str
    slave_bin: str = "./build/iec104_slave"
    diag_bin: str = "./build/iec104_diag"
    config_file: str = "104config.json5"


class RemoteLinux:
    def __init__(self, cfg: RemoteConfig):
        try:
            import paramiko
        except ImportError as exc:
            raise RuntimeError("paramiko is required for remote management; install tests/iec104_auto/requirements-remote.txt") from exc

        self.cfg = cfg
        self.paramiko = paramiko
        self.client = paramiko.SSHClient()
        self.client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        self.client.connect(cfg.host, username=cfg.user, password=cfg.password, timeout=10)

    def close(self) -> None:
        self.client.close()

    def run(self, command: str, timeout: int = 60, check: bool = True):
        stdin, stdout, stderr = self.client.exec_command(command, timeout=timeout)
        code = stdout.channel.recv_exit_status()
        out = stdout.read().decode("utf-8", errors="replace")
        err = stderr.read().decode("utf-8", errors="replace")
        if check and code != 0:
            raise AssertionError(f"remote command failed code={code}\ncmd={command}\nstdout={out}\nstderr={err}")
        return code, out, err

    def build(self) -> None:
        self.run(f"cd {self.cfg.project_dir} && cmake -S . -B build && cmake --build build -j", timeout=300)

    def stop_slave(self) -> None:
        self.run("pkill -f iec104_slave || true", check=False)

    def start_slave(self) -> None:
        self.stop_slave()
        cmd = (
            f"cd {self.cfg.project_dir} && "
            f"nohup {self.cfg.slave_bin} -c {self.cfg.config_file} > run.log 2>&1 &"
        )
        self.run(cmd, check=False)
        time.sleep(1.0)

    def diag(self, *args: str) -> str:
        quoted = " ".join(args)
        _, out, _ = self.run(f"cd {self.cfg.project_dir} && {self.cfg.diag_bin} {quoted}", timeout=10)
        return out

    def start_tcpdump(self, output: str = "/tmp/iec104_test.pcap") -> None:
        self.run("pkill -f 'tcpdump .*iec104_test.pcap' || true", check=False)
        cmd = f"echo {self.cfg.password} | sudo -S nohup tcpdump -i any -s 0 -w {output} tcp port 2404 >/tmp/iec104_tcpdump.out 2>/tmp/iec104_tcpdump.err &"
        self.run(cmd, check=False)

    def stop_tcpdump(self) -> None:
        self.run("echo sps | sudo -S pkill tcpdump || true", check=False)

    def fetch_text(self, remote_path: str) -> str:
        sftp = self.client.open_sftp()
        try:
            with sftp.open(remote_path, "r") as fh:
                return fh.read().decode("utf-8", errors="replace")
        finally:
            sftp.close()

    def fetch_file(self, remote_path: str, local_path: Union[str, pathlib.Path]) -> None:
        local = pathlib.Path(local_path)
        local.parent.mkdir(parents=True, exist_ok=True)
        sftp = self.client.open_sftp()
        try:
            sftp.get(remote_path, str(local))
        finally:
            sftp.close()
