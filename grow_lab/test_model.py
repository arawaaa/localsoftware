"""Check geometrical fit and physical calculation behavior, not construction adequacy."""
import math
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path

from model import BASE, WALL, SILL, SHEATH, STUD, THIN, FLOOR_LINER, Config, build, environment, export, roof_load_study


def overlap(a,b):
    return all(min(a.origin[i]+a.size[i],b.origin[i]+b.size[i])-
               max(a.origin[i],b.origin[i])>1e-6 for i in range(3))


class ModelTests(unittest.TestCase):
    def test_user_clear_interior_and_bounds(self):
        c=Config()
        self.assertAlmostEqual(c.width-2*WALL,48)
        self.assertAlmostEqual(c.depth-2*WALL,96)
        self.assertAlmostEqual(c.width,54.37)
        self.assertAlmostEqual(c.clear_height,79.4)
        for p in build(c):
            for i,limit in enumerate((c.width,c.depth,c.overall_height_in)):
                self.assertGreater(p.size[i],0,p.name)
                self.assertGreaterEqual(p.origin[i],0,p.name)
                self.assertLessEqual(p.origin[i]+p.size[i],limit+1e-6,p.name)

    def test_assemblies_do_not_interpenetrate(self):
        for c in (Config(),replace(Config(),interior_width_in=60,interior_depth_in=36,
                                    overall_height_in=96,door_rough_height_in=76)):
            parts=build(c)
            for i,a in enumerate(parts):
                for b in parts[i+1:]:
                    self.assertFalse(overlap(a,b),f'{a} overlaps {b}')

    def test_rough_door_opening_is_free_of_frame_and_skins(self):
        from model import Part
        c=Config()
        void=Part('opening','test','void',((c.width-c.door_rough_width_in)/2,0,c.door_bottom),
                  (c.door_rough_width_in,WALL,c.door_rough_height_in))
        for p in build(c):
            if p.assembly!='door':
                self.assertFalse(overlap(p,void),p.name)

    def test_interior_volume_is_clear(self):
        from model import Part
        c=Config()
        void=Part('interior','test','void',(WALL,WALL,c.door_bottom),(48,96,c.clear_height-c.tray_upstand_in))
        for p in build(c):
            self.assertFalse(overlap(p,void),p.name)

    def test_bad_configuration_rejected(self):
        for edits in ({'inside_rh_percent':0},{'garage_rh_percent':101},
                      {'interior_width_in':-1},{'overall_height_in':60},
                      {'door_rough_width_in':60},{'max_stud_spacing_in':25},
                      {'roof_crossmember_count':0},{'total_hanging_weight_lb':-1},
                      {'water_evaporated_l_per_day':-1},{'inside_temp_c':math.nan},
                      {'assumed_wall_r_imperial':0}):
            with self.subTest(edits=edits),self.assertRaises(ValueError):
                build(replace(Config(),**edits))

    def test_moisture_balance_edge_cases(self):
        c=Config()
        e=environment(c)
        self.assertAlmostEqual(e['inside_dew_point_c'],17.96,places=2)
        saturated=environment(replace(c,inside_rh_percent=100))
        self.assertAlmostEqual(saturated['inside_dew_point_c'],c.inside_temp_c,places=2)
        humid=environment(replace(c,makeup_temp_c=30,makeup_rh_percent=90))
        self.assertIsNone(humid['minimum_exhaust_cfm_for_moisture_only'])
        zero=environment(replace(c,water_evaporated_l_per_day=0))
        self.assertEqual(zero['minimum_exhaust_cfm_for_moisture_only'],0)
        doubled=environment(replace(c,water_evaporated_l_per_day=6))
        self.assertAlmostEqual(doubled['minimum_exhaust_cfm_for_moisture_only'],
                               2*e['minimum_exhaust_cfm_for_moisture_only'],delta=.02)

    def test_exports(self):
        c=Config()
        with tempfile.TemporaryDirectory() as tmp:
            out=Path(tmp)/'nested'
            summary=export(c,build(c),out)
            self.assertEqual(summary['clear_interior_in'][:2],[48,96])
            for name in ('model.json','summary.json','parts.csv','cut_list.csv'):
                self.assertGreater((out/name).stat().st_size,100)

    def test_slab_and_tray_replace_platform(self):
        c=Config(); parts=build(c)
        self.assertFalse(any(p.assembly=='base' for p in parts))
        self.assertFalse(any(p.name in ('Base joist','Floor deck','Bottom skin') for p in parts))
        floor=next(p for p in parts if p.name=='Floor liner on concrete')
        self.assertEqual(floor.origin[2],0)
        self.assertEqual(floor.size,(48,96,FLOOR_LINER))
        self.assertEqual(len([p for p in parts if 'upstand' in p.name]),4)
        self.assertTrue(all(p.origin[2]==SILL for p in parts if p.name=='Bottom plate'))
        self.assertIsNone(environment(c)['slab_heat_transfer_w'])

    def test_roof_spans_short_direction_and_bears_over_studs(self):
        c=Config(); parts=build(c)
        supports=[p for p in parts if p.name.startswith('Light-support crossmember')]
        self.assertEqual(len(supports),3)
        for beam in supports:
            self.assertAlmostEqual(beam.size[0],c.width-2*SHEATH)
            for side in ('left','right'):
                self.assertTrue(any(p.assembly==side and p.name=='Wall stud'
                    and abs(p.origin[1]-beam.origin[1])<1e-6 for p in parts))
        self.assertIsNone(roof_load_study(c)['maximum_bending_stress_demand_psi'])
        loaded=roof_load_study(replace(c,total_hanging_weight_lb=40))
        self.assertGreater(loaded['maximum_bending_stress_demand_psi'],0)
        self.assertGreater(loaded['reaction_at_each_end_lb'],20)


if __name__=='__main__':
    unittest.main()
