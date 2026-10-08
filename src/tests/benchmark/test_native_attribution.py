import json, pathlib, tempfile, unittest
from native_attribution import export_guest_map, load_source_map, module_info, match_code, source_anchors

class SourceMaps(unittest.TestCase):
    def test_address_reuse_uses_sample_time(self):
        old=dict(address=4096,size=64,timestamp=100,id=1)
        new=dict(address=4096,size=64,timestamp=200,id=2)
        self.assertIsNone(match_code([old,new],99,4100))
        self.assertEqual(match_code([old,new],150,4100)['id'],1)
        self.assertEqual(match_code([old,new],200,4100)['id'],2)
        self.assertIsNone(match_code([old,new],250,4160))

    def test_traps_inherit_but_arithmetic_stays_unknown(self):
        a=dict(native_offset=64,wasm_offset=23,inlining_id=-1)
        b=dict(native_offset=169,wasm_offset=9,inlining_id=3)
        anchors=source_anchors(dict(positions=[a,b],traps=[64,70,169]))
        self.assertEqual(anchors[64],a);self.assertEqual(anchors[70],a)
        self.assertEqual(anchors[169],b);self.assertNotIn(65,anchors)

    def test_guest_map_round_trip(self):
        with tempfile.TemporaryDirectory() as directory:
            p=pathlib.Path(directory)
            module={'file':'sample.wasm','sha256':'a'*64,'functions':{
                7:{'start':120,'size':35,'name':'f_123','marks':[[2,123,1],[10,127,4],[20,0,2]]}}}
            export_guest_map(module,p)
            columns,rows=load_source_map(next(p.glob('*.map')))
            self.assertEqual(columns,[122,130,140]);self.assertEqual([r[2] for r in rows],[1,2,3])
            data=json.loads(next(p.glob('*.map')).read_text())
            self.assertIn('guest_pc=0x0000007f guest_memory',data['sourcesContent'][0])
            self.assertEqual(data['x_wasm_sha256'],'a'*64)

    def test_unmapped_segments_stay_unmapped(self):
        with tempfile.TemporaryDirectory() as directory:
            p=pathlib.Path(directory)/'test.map'
            p.write_text(json.dumps({'version':3,'sources':['test.cpp'],'mappings':'AAAA,K,KACA'}))
            offsets,rows=load_source_map(p)
            self.assertEqual(offsets,[0,5,10]);self.assertIsNone(rows[1][1]);self.assertEqual(rows[2][2],2)

if __name__=='__main__':unittest.main()
