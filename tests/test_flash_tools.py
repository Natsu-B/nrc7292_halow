"""Host-only safety tests; no serial device is opened."""
import hashlib, importlib.util, pathlib, struct, unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('flash_video',ROOT/'tools/flash_video.py')
f=importlib.util.module_from_spec(spec);spec.loader.exec_module(f)

def table(video_offset=0x200000):
    b=b''
    for kind,offset,size,name in [(1,0x9000,0x6000,b'nvs'),(1,0xf000,0x1000,b'phy'),(0,0x10000,0x1f0000,b'factory'),(1,video_offset,0x200000,b'video')]:
        b+=struct.pack('<HBBII16sI',0x50aa,kind,0,offset,size,name,0)
    b+=b'\xeb\xeb'+b'\xff'*14+hashlib.md5(b).digest()
    return b+b'\xff'*(4096-len(b))
class FlashSafety(unittest.TestCase):
    def test_layout(self):self.assertEqual(f.video_partition(table()),(0x200000,0x200000))
    def test_corrupt_table(self):
        b=bytearray(table());b[20]^=1
        with self.assertRaises(ValueError):f.video_partition(bytes(b))
    def test_overlap(self):
        with self.assertRaises(ValueError):f.video_partition(table(0x100000))
    def test_no_md5(self):
        with self.assertRaises(ValueError):f.video_partition(table()[:128]+b'\xff'*128)
    def test_clips(self):
        for name in ['video.bin','test_h264_1080p.bin','test_mjpeg.bin']:f.validate_clip((ROOT/'assets'/name).read_bytes())
    def test_corrupt_clip(self):
        b=bytearray((ROOT/'assets/video.bin').read_bytes());b[-1]^=1
        with self.assertRaises(ValueError):f.validate_clip(bytes(b))
if __name__=='__main__':unittest.main()
