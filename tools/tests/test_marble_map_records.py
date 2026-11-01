"""Concurrent console prefixes must not hide maps or double-count IOLog copies."""
import importlib.util, unittest
from pathlib import Path

spec=importlib.util.spec_from_file_location('context',Path(__file__).parents[1]/'marble-gpu-context-report.py')
context=importlib.util.module_from_spec(spec);spec.loader.exec_module(context)
RECORD='marble-gpu-live-map: map=0 ranges=1 leaves=8 generation=391 published=1 tlb_removed=1 PASS=1'


class MapRecords(unittest.TestCase):
    def test_interleaved_prefix_counts_original_once(self):
        text='launchd: unfinished message'+RECORD+'\n[ 5441.153778]: '+RECORD+'\n'
        rows=context.map_records(text)
        self.assertEqual(len(rows),1)
        self.assertEqual(rows[0]['generation'],391)
        self.assertEqual(rows[0]['map'],0)

    def test_malformed_record_rejected(self):
        with self.assertRaisesRegex(ValueError,'incomplete'):
            context.map_records(RECORD[:-4]+'\n')

    def test_multiple_markers_rejected(self):
        with self.assertRaisesRegex(ValueError,'multiple'):
            context.map_records(RECORD+RECORD+'\n')

    def test_duplicate_originals_remain_visible(self):
        # Generation validation must see both; never silently deduplicate.
        self.assertEqual(len(context.map_records(RECORD+'\n'+RECORD+'\n')),2)


if __name__=='__main__':unittest.main()
