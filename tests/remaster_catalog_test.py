"""Synthetic completeness tests; no source artwork or release exemptions."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from remaster_catalog import audit, pixel_hash, make_ledger
from remaster_pack import encode, quantize, read_bake


class CoverageTest(unittest.TestCase):
    def test_shop_customer_identity_references_are_required(self):
        catalog=dict(snapshot_sha256='synthetic',files=[],families={},assets=[
            dict(id='guest:0',shop_action_sprite_offsets=[6,6]),
            dict(id='guest:1',shop_action_sprite_offsets=[12,7])])
        parts=[dict(id=f'MSPR-0:{i}',empty=False,owners=[],frame_uses=[],seated_uses=[]) for i in (1,2)]
        with patch('remaster_catalog.build',return_value=catalog), \
             patch('remaster_catalog.bank_records',side_effect=[parts,[]]):
            ledger=make_ledger(Path('unused'),Path('unused'))
        self.assertEqual(parts[0]['owners'],['guest:0'])
        self.assertEqual([p['pose'] for p in parts[0]['seated_uses']],[0,1])
        self.assertEqual(parts[1]['owners'],['guest:1'])
        self.assertTrue(all(p['required'] for p in parts))
        self.assertEqual(ledger['source_problems'][0]['table'],'shop_action_sprite_offsets')
        self.assertIn('shop_customer_substitution',ledger['runtime_channels'])

    def test_direct_frame_ownership_does_not_follow_animation_index(self):
        frame=dict(id=1,next=0,parts=[dict(sprite_index=1,sprite_offset=6,rider_marker=False)],
                   element_termination='end')
        catalog=dict(snapshot_sha256='synthetic',files=[],assets=[
            dict(id='guest:0',references=[],frame_references=[dict(field='slot:11',frame=1)])],
            families={'M':dict(chains=[],frames=[dict(id=0),frame],unreachable_frames=[1])})
        parts=[dict(id='MSPR-0:1',empty=False,owners=[],frame_uses=[],seated_uses=[])]
        with patch('remaster_catalog.build',return_value=catalog), \
             patch('remaster_catalog.bank_records',side_effect=[parts,[]]):
            ledger=make_ledger(Path('unused'),Path('unused'))
        self.assertEqual(ledger['frames'][0]['owners'],['guest:0'])
        self.assertEqual(parts[0]['owners'],['guest:0'])
        self.assertEqual(ledger['animations'],[])
        self.assertEqual(ledger['source_problems'],[])

    def test_original_fallback_and_concepts_never_complete(self):
        ledger=dict(parts=[dict(id='sprite:1',required=True,source_hash='00')],
                    assets=[dict(id='guest:0')],animations=[],resources=[],source_problems=[])
        with tempfile.TemporaryDirectory() as d:
            for treatment in ('original', 'concept', 'placeholder', 'recolour'):
                r=audit(ledger,dict(parts={'sprite:1':dict(treatment=treatment)}),Path(d))
                self.assertFalse(r['complete'])
                self.assertIn('authored remaster treatment',r['blockers'][0]['missing'])

    def test_truncated_animation_not_complete(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);p=root/'evidence';p.write_text('synthetic evidence')
            file=dict(path='evidence',sha256=hashlib.sha256(p.read_bytes()).hexdigest())
            ledger=dict(parts=[],assets=[],resources=[],source_problems=[],
                        animations=[dict(id='M:1',frames=['M:1','M:2'],loop_start=0)])
            done=dict(state_trace=file,normal_capture=file,visitor_capture=file,
                      frames=['M:1'],loop_start=0,riders_expressions_items_checked=True)
            r=audit(ledger,dict(animations={'M:1':done}),root)
            self.assertFalse(r['complete'])
            self.assertIn('complete source sequence and loop',r['blockers'][0]['missing'])
            done['frames']=['M:1','M:2']
            self.assertTrue(audit(ledger,dict(animations={'M:1':done}),root)['complete'])
            p.write_text('changed evidence')
            self.assertFalse(audit(ledger,dict(animations={'M:1':done}),root)['complete'])

    def test_unclassified_resource_is_not_implicitly_ui(self):
        ledger=dict(parts=[],animations=[],assets=[],source_problems=[],
                    resources=[dict(name='UNKNOWN.DAT',role='scope-unresolved')])
        with tempfile.TemporaryDirectory() as d:
            self.assertFalse(audit(ledger,{},Path(d))['complete'])

    def test_pack_dimensions_and_transparency(self):
        rows=[[None,1,2],[3,4,None]]
        meta=dict(bank=0,index=1,width=3,height=2,source_hash=f'{pixel_hash(rows):016x}')
        raw=encode([(meta,rows)])
        self.assertEqual(raw[:8],b'TPRART01')
        self.assertEqual(len(raw),12+16+12)
        self.assertEqual(struct.unpack_from('<H',raw,28)[0],256)
        with self.assertRaises(ValueError):encode([(meta,rows),(meta,rows)])

    def test_bake_sampling_does_not_replace_ui_palette(self):
        palette=bytes([i%64 for i in range(768)])
        original=palette[:]
        rgba=bytes([255,0,0,0,10,20,30,255]*2)
        rows=quantize((2,2,rgba),2,2,palette)
        self.assertIsNone(rows[0][0]);self.assertIsNotNone(rows[0][1])
        self.assertEqual(palette,original)
        with self.assertRaises(ValueError):quantize((2,2,rgba),3,2,palette)

    def test_bake_rejects_truncation(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'bake'
            p.write_bytes(b'TPBAKE01'+struct.pack('<II',2,2)+b'\0'*4)
            with self.assertRaises(ValueError):read_bake(p)


if __name__=='__main__':unittest.main()
