import hashlib
import importlib.util
from pathlib import Path
import struct
import sys
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
spec=importlib.util.spec_from_file_location('marble_run',ROOT/'tools/marble-run.py')
runner=importlib.util.module_from_spec(spec); spec.loader.exec_module(runner)

class TouchTests(unittest.TestCase):
    def test_mailbox_stays_outside_support_filesystem(self):
        kernel=(ROOT/'prebuilt/mach.development.bcm2837').read_bytes()
        image=runner.payload(kernel,b'APFS-CONTENT',7,30,input_mailbox=True)
        pos=8; entries={}
        while True:
            name,size,_=struct.unpack_from('<16sII',image,pos); pos+=24
            if not name.strip(b'\0'): break
            entries[name.rstrip(b'\0')]=image[pos:pos+size]; pos+=(size+15)&~15
        self.assertEqual(pos,len(image))
        rd=entries[b'ramdisk']; self.assertEqual(len(rd),0x804000)
        self.assertEqual(rd[:12],b'APFS-CONTENT')
        self.assertFalse(any(rd[12:0x800000]))
        self.assertEqual(struct.unpack_from('<Q4I',rd,0x800000),(0x314843554f544d,1,32,128,0))
        self.assertNotIn(b'-v',entries[b'cmdline'])
        with self.assertRaises(ValueError): runner.payload(kernel,bytes(0x800001),7,30,input_mailbox=True)

    def test_static_bridge_has_valid_kernel_entry_and_no_dyld(self):
        d=(ROOT/'out/marble-touch-bridge').read_bytes()
        magic,cpu,sub,typ,count,size,flags,res=struct.unpack_from('<8I',d)
        self.assertEqual((magic,cpu,typ,flags),(0xfeedfacf,0x100000c,2,1))
        pos=32; entry=None; executable=[]
        for _ in range(count):
            cmd,n=struct.unpack_from('<II',d,pos)
            self.assertGreaterEqual(n,8); self.assertLessEqual(pos+n,32+size)
            self.assertIn(cmd,(0x19,2,5))
            if cmd==5:
                self.assertEqual(n,288)
                self.assertEqual(struct.unpack_from('<II',d,pos+8),(6,68))
                entry=struct.unpack_from('<Q',d,pos+16+32*8)[0]
            if cmd==0x19:
                va,vm,off,fs,maxprot,prot=struct.unpack_from('<4Q2I',d,pos+24)
                self.assertLessEqual(off+fs,len(d))
                if prot&4: executable.append((va,va+vm))
            pos+=n
        self.assertEqual(pos,32+size)
        self.assertIsNotNone(entry)
        self.assertTrue(any(lo<=entry<hi for lo,hi in executable))

if __name__=='__main__': unittest.main()
