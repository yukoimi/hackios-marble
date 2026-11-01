"""Log continuity must reject gaps/another boot despite a damaged oldest prefix."""
import hashlib, importlib.util, tempfile, unittest
from pathlib import Path

spec=importlib.util.spec_from_file_location('stitch',Path(__file__).parents[1]/'marble-stitch-stream.py')
stitch=importlib.util.module_from_spec(spec);spec.loader.exec_module(stitch)


class Continuity(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.stream=b''.join(hashlib.sha256(str(i).encode()).hexdigest().encode()+b'\n' for i in range(40000))

    def snapshot(self,data,final=False,damage=False):
        marker=stitch.FINAL if final else stitch.LIVE
        if len(data)>=stitch.CAP:
            tail=data[-1024*1024:]
            if damage:tail=b'overwritten oldest bytes\n'+tail[24:]
            data=data[:stitch.HEAD]+stitch.NOTE+tail
        return b'loader\n'+marker+data+(stitch.END if final else b'')

    def join(self,first,last,**kw):
        with tempfile.TemporaryDirectory() as d:
            a,b,out=[Path(d)/n for n in ('live','returned','complete')]
            a.write_bytes(self.snapshot(first));b.write_bytes(self.snapshot(last,final=True,**kw))
            stitch.join([a,b],out)
            return out.read_bytes()

    def test_oldest_prefix_overwritten_but_exact_suffix_survives(self):
        # Multiples of 65 keep all newest lines whole.
        first=self.stream[:650000];last=self.stream[:1430000]
        self.assertEqual(self.join(first,last,damage=True),b'loader\n'+stitch.FINAL+last+stitch.END)

    def test_missing_newest_evidence_rejected(self):
        with self.assertRaisesRegex(ValueError,'evidence gap'):
            self.join(self.stream[:520000],self.stream[:2080000])

    def test_another_boot_rejected(self):
        with self.assertRaisesRegex(ValueError,'boot prefix differs'):
            self.join(self.stream[:650000],b'other boot\n'+self.stream[:1430000])


if __name__=='__main__':unittest.main()
