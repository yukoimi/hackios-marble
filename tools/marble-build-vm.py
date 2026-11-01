#!/usr/bin/env python3
"""Local Linux build VM; no phone access, host disks, shared folders or USB passthrough."""
import argparse
import io
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT.parent / 'toolchains' / 'marble-linux'
sys.path.insert(0, str(ROOT.parent / 'toolchains' / 'python-libs'))
VMRUN = Path('C:/Program Files/VMware/VMware Workstation/vmrun.exe')


def prepare():
    import pycdlib
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    from cryptography.hazmat.primitives import serialization
    BASE.mkdir(exist_ok=True)
    key = BASE / 'build-key'
    if not key.exists():
        private = Ed25519PrivateKey.generate()
        key.write_bytes(private.private_bytes(serialization.Encoding.PEM,
                        serialization.PrivateFormat.OpenSSH, serialization.NoEncryption()))
    private = serialization.load_ssh_private_key(key.read_bytes(), password=None)
    public = private.public_key().public_bytes(serialization.Encoding.OpenSSH,
                                             serialization.PublicFormat.OpenSSH).decode()
    userdata = f'''#cloud-config
hostname: marble-build
manage_etc_hosts: true
users:
  - name: builder
    shell: /bin/bash
    sudo: ALL=(ALL) NOPASSWD:ALL
    ssh_authorized_keys:
      - {public}
ssh_pwauth: false
disable_root: true
runcmd:
  - [sh, -c, 'echo MARBLE_BUILD_VM_IP=$(hostname -I) > /dev/ttyS0']
  - [sh, -c, 'apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y open-vm-tools build-essential git curl ca-certificates clang lld python3 perl xz-utils flex bison m4 gawk autoconf automake libtool pkg-config libxml2-dev libssl-dev cmake ninja-build rsync libzstd-dev zlib1g-dev uuid-dev libbsd-dev > /var/log/marble-packages.log 2>&1']
  - [sh, -c, 'echo MARBLE_PACKAGES_DONE > /dev/ttyS0']
'''
    iso = pycdlib.PyCdlib()
    iso.new(interchange_level=3, vol_ident='cidata', rock_ridge='1.09', joliet=3)
    for name, content in [('user-data', userdata), ('meta-data',
                          'instance-id: marble-build-20261023\nlocal-hostname: marble-build\n')]:
        raw = content.encode()
        iso.add_fp(io.BytesIO(raw), len(raw), iso_path='/' + name.upper().replace('-', '_') + ';1',
                   rr_name=name, joliet_path='/' + name)
    iso.write(str(BASE / 'seed.iso'))
    iso.close()
    disk = BASE / 'build-generic.vmdk'
    if not disk.exists():
        img = ROOT.parent / 'toolchains/qemu/qemu-img.exe'
        src = ROOT.parent / 'toolchains/downloads/debian-13-generic-amd64.qcow2'
        subprocess.run([str(img), 'convert', '-O', 'vmdk', '-o',
                        'subformat=monolithicSparse,adapter_type=lsilogic', str(src), str(disk)], check=True)
    img = ROOT.parent / 'toolchains/qemu/qemu-img.exe'
    size = json.loads(subprocess.check_output([str(img), 'info', '--output=json', str(disk)]))['virtual-size']
    if size < 24 * 1024**3:
        subprocess.run([str(VMRUN.parent / 'vmware-vdiskmanager.exe'), '-x', '24GB', str(disk)], check=True)
    vmx = '''.encoding = "UTF-8"
config.version = "8"
virtualHW.version = "20"
displayName = "Marble Linux build"
guestOS = "debian12-64"
memsize = "6144"
numvcpus = "6"
cpuid.coresPerSocket = "6"
firmware = "bios"
pciBridge0.present = "TRUE"
pciBridge4.present = "TRUE"
pciBridge4.virtualDev = "pcieRootPort"
pciBridge4.functions = "8"
pciBridge5.present = "TRUE"
pciBridge5.virtualDev = "pcieRootPort"
pciBridge5.functions = "8"
scsi0.present = "TRUE"
scsi0.virtualDev = "lsilogic"
scsi0:0.present = "TRUE"
scsi0:0.fileName = "build-generic.vmdk"
ide1:0.present = "TRUE"
ide1:0.deviceType = "cdrom-image"
ide1:0.fileName = "seed.iso"
ethernet0.present = "TRUE"
ethernet0.connectionType = "nat"
ethernet0.virtualDev = "vmxnet3"
ethernet0.addressType = "generated"
serial0.present = "TRUE"
serial0.fileType = "file"
serial0.fileName = "serial.log"
serial0.yieldOnMsrRead = "TRUE"
usb.present = "FALSE"
ehci.present = "FALSE"
sound.present = "FALSE"
floppy0.present = "FALSE"
sharedFolder.maxNum = "0"
tools.syncTime = "TRUE"
msg.autoAnswer = "TRUE"
gui.exitOnCLIHLT = "TRUE"
'''
    (BASE / 'build.vmx').write_text(vmx, encoding='utf8')
    print(BASE / 'build.vmx')


def address():
    log = (BASE / 'serial.log').read_text(errors='replace')
    ips = re.findall(r'MARBLE_BUILD_VM_IP=(\d+\.\d+\.\d+\.\d+)', log)
    if not ips:
        raise RuntimeError('guest has not announced its IP; inspect serial.log')
    return ips[-1]


def connect():
    import paramiko
    c = paramiko.SSHClient()
    # This is a new private NAT guest. Persist its key and reject changes thereafter.
    known = BASE / 'known_hosts'
    if known.exists():
        c.load_host_keys(str(known))
        c.set_missing_host_key_policy(paramiko.RejectPolicy())
    else:
        c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    c.connect(address(), username='builder', key_filename=str(BASE / 'build-key'),
              allow_agent=False, look_for_keys=False, timeout=20)
    if not known.exists():
        c.save_host_keys(str(known))
    return c


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    for name in ('prepare', 'start', 'stop', 'status'):
        sub.add_parser(name)
    cmd = sub.add_parser('exec')
    cmd.add_argument('shell_command')
    for name in ('put', 'get'):
        s = sub.add_parser(name)
        s.add_argument('source')
        s.add_argument('destination')
    a = p.parse_args()
    if a.command == 'prepare':
        prepare()
    elif a.command in ('start', 'stop'):
        subprocess.run([str(VMRUN), '-T', 'ws', a.command, str(BASE / 'build.vmx'),
                        'nogui' if a.command == 'start' else 'soft'], check=True)
    elif a.command == 'status':
        print('Guest IP:', address())
        print((BASE / 'serial.log').read_text(errors='replace')[-1800:])
    else:
        client = connect()
        try:
            if a.command == 'exec':
                _, stdout, stderr = client.exec_command(a.shell_command)
                # Combine channels so neither can fill its buffer while the other is read.
                stdout.channel.set_combine_stderr(True)
                while True:
                    raw = stdout.channel.recv(32768)
                    if not raw:
                        break
                    sys.stdout.buffer.write(raw)
                    sys.stdout.buffer.flush()
                raise SystemExit(stdout.channel.recv_exit_status())
            with client.open_sftp() as sftp:
                if a.command == 'put':
                    sftp.put(a.source, a.destination)
                else:
                    sftp.get(a.source, a.destination)
        finally:
            client.close()


if __name__ == '__main__':
    main()
