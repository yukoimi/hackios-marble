"""Public host configuration; no device serials, credentials, or keys shipped."""
import os
import shutil

ANDROID_SERIAL = os.environ.get('MARBLE_ANDROID_SERIAL', 'YOUR_ANDROID_SERIAL')
ADB = os.environ.get('MARBLE_ADB') or shutil.which('adb') or ('C:/adb/adb.exe' if os.name == 'nt' else 'adb')
FASTBOOT = os.environ.get('MARBLE_FASTBOOT') or shutil.which('fastboot') or ('C:/adb/fastboot.exe' if os.name == 'nt' else 'fastboot')
