"""Check purchasing allocations cannot reuse stock or omit modeled material."""
from collections import Counter
import unittest

from model import Config, build
from procurement import KERF, lumber_stock, panel_pieces, sheet_stock


class ProcurementTests(unittest.TestCase):
    def test_lumber_allocations_cover_all_cuts_and_fit_stock(self):
        parts=build(Config())
        for section,bins in lumber_stock(parts).items():
            depth=2.5 if section=='2x3' else 3.5
            expected=Counter((p.name,round(max(p.size),4)) for p in parts
                if p.material=='lumber' and tuple(sorted(p.size)[:2])==(1.5,depth))
            actual=Counter((p['part'],round(p['length_in'],4)) for b in bins for p in b['cuts'])
            self.assertEqual(expected,actual)
            for b in bins:
                self.assertLessEqual(sum(p['length_in']+KERF for p in b['cuts'])+.25,b['stock_in']+1e-7)
                for p in b['cuts']:
                    if p['length_in']>96:
                        self.assertGreaterEqual(b['stock_in'],120)

    def test_sheet_allocations_cover_panels_without_overlaps(self):
        c=Config(); parts=build(c)
        for (material,thickness),pieces in panel_pieces(c,parts).items():
            expected=sum(sorted(p.size)[1]*sorted(p.size)[2] for p in parts
                if p.material==material and abs(min(p.size)-thickness)<1e-7
                and p.assembly not in ('tray','threshold'))
            self.assertAlmostEqual(expected,sum(p['width']*p['height'] for p in pieces),places=5)
            sheets=sheet_stock(pieces)
            allocated=[p for sheet in sheets for p in sheet['pieces']]
            self.assertEqual(len(allocated),len(pieces))
            self.assertAlmostEqual(expected,sum(p['width']*p['height'] for p in allocated),places=5)
            for sheet in sheets:
                for i,a in enumerate(sheet['pieces']):
                    self.assertGreaterEqual(a['x'],0);self.assertGreaterEqual(a['y'],0)
                    self.assertLessEqual(a['x']+a['width'],48+1e-7)
                    self.assertLessEqual(a['y']+a['height'],96+1e-7)
                    for b in sheet['pieces'][i+1:]:
                        overlap_x=min(a['x']+a['width'],b['x']+b['width'])-max(a['x'],b['x'])
                        overlap_y=min(a['y']+a['height'],b['y']+b['height'])-max(a['y'],b['y'])
                        self.assertFalse(overlap_x>1e-7 and overlap_y>1e-7)


if __name__=='__main__':unittest.main()
