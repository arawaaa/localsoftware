"""Parametric garage grow enclosure. Geometry in inches; Python standard library only.

These are fabrication study solids, not structural or hygrothermal certification.
Run render.py for drawings. See README.md for joints and unmodeled hardware.
"""
from dataclasses import asdict, dataclass
from pathlib import Path
import argparse
import csv
import json
import math

SHEATH = 0.375
STUD = 2.5                       # actual nominal 2x3 depth
BOARD = 0.25                     # liner substrate; verify product support requirements
LINER = 0.06                     # schematic sealed hygienic liner
WALL = SHEATH + STUD + BOARD + LINER
SILL = 0.125                     # thin capillary break under wall plates, not a platform
FLOOR_LINER = 0.04               # schematic 40-mil waterproof plastic membrane
BASE = FLOOR_LINER               # finished floor directly over existing concrete
JOIST, ROOF_CAP, ROOF_BOARD = 3.5, 0.5, 0.5
ROOF = JOIST + ROOF_CAP           # joists bear directly on wall top plates
CEILING = ROOF_BOARD + LINER     # finish fits inside walls, below the joists
THIN = 1.5


@dataclass
class Config:
    interior_width_in: float = 48.0
    interior_depth_in: float = 96.0
    overall_height_in: float = 84.0
    door_rough_width_in: float = 28.0
    door_rough_height_in: float = 68.0
    max_stud_spacing_in: float = 24.0
    tray_upstand_in: float = 3.5
    roof_crossmember_count: int = 3
    total_hanging_weight_lb: float | None = None
    assumed_roof_dead_load_psf: float = 6.0
    assumed_lumber_e_psi: float = 1000000.0
    inside_temp_c: float = 25.0
    inside_rh_percent: float = 65.0
    garage_temp_c: float = 10.0
    garage_rh_percent: float = 60.0
    makeup_temp_c: float = 10.0
    makeup_rh_percent: float = 60.0
    water_evaporated_l_per_day: float = 3.0
    equipment_watts: float = 300.0
    assumed_wall_r_imperial: float = 9.0
    assumed_roof_r_imperial: float = 12.0

    @classmethod
    def read(cls, path):
        cfg = cls(**json.loads(Path(path).read_text()))
        cfg.validate()
        return cfg

    @property
    def width(self):
        return self.interior_width_in + 2 * WALL

    @property
    def depth(self):
        return self.interior_depth_in + 2 * WALL

    @property
    def clear_height(self):
        return self.overall_height_in - BASE - ROOF - CEILING

    @property
    def door_bottom(self):
        return BASE + self.tray_upstand_in

    @property
    def roof_centers(self):
        # Crossmembers align with side-wall studs. End members close the roof frame.
        first = SHEATH + STUD + THIN/2
        last = self.depth - first
        return [SHEATH+THIN/2] + [first+(last-first)*i/(self.roof_crossmember_count+1)
                for i in range(1,self.roof_crossmember_count+1)] + [self.depth-SHEATH-THIN/2]

    def validate(self):
        for key, value in asdict(self).items():
            if key == 'total_hanging_weight_lb' and value is None:
                continue
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
                raise ValueError(f"{key} must be a finite number")
            if key.endswith('rh_percent') and not 0 < value <= 100:
                raise ValueError(f"{key} must be greater than zero and at most 100")
            if key.endswith('temp_c') and not -40 <= value <= 60:
                raise ValueError(f"{key} must be within the model range -40..60 C")
            if ('_in' in key or '_r_imperial' in key) and value <= 0:
                raise ValueError(f"{key} must be positive")
        if min(self.interior_width_in, self.interior_depth_in) < 24:
            raise ValueError('Interior dimensions must be at least 24 inches')
        if not 4 <= self.max_stud_spacing_in <= 24:
            raise ValueError('Stud spacing must be 4..24 inches for this geometry')
        if not isinstance(self.roof_crossmember_count,int) or not 1 <= self.roof_crossmember_count <= 8:
            raise ValueError('Use 1..8 interior roof crossmembers')
        if min(b-a for a,b in zip(self.roof_centers,self.roof_centers[1:])) <= THIN:
            raise ValueError('Roof members overlap; reduce count or increase depth')
        if self.tray_upstand_in <= SILL or self.tray_upstand_in > 12:
            raise ValueError('Tray upstand must exceed sill thickness and be at most 12 inches')
        if self.total_hanging_weight_lb is not None and self.total_hanging_weight_lb < 0:
            raise ValueError('Hanging load must be nonnegative or null when unknown')
        if self.assumed_roof_dead_load_psf < 0 or self.assumed_lumber_e_psi <= 0:
            raise ValueError('Roof dead load must be nonnegative and lumber E positive')
        if self.door_rough_width_in < 18 or self.door_rough_height_in < 36:
            raise ValueError('Door rough opening must be at least 18 x 36 inches')
        if self.door_rough_width_in + 4 * THIN > self.width - 2 * (SHEATH + STUD):
            raise ValueError('Door leaves insufficient space for king/jack studs and corners')
        if self.door_bottom + self.door_rough_height_in + JOIST + THIN > self.overall_height_in-ROOF:
            raise ValueError('Door/header will not fit below top plate; increase overall height')
        if min(self.water_evaporated_l_per_day, self.equipment_watts) < 0:
            raise ValueError('Water evaporation and equipment power must be nonnegative')


@dataclass
class Part:
    name: str
    assembly: str
    material: str
    origin: tuple
    size: tuple


def positions(start, end, max_spacing):
    """Include both end studs and distribute intermediate studs at <= max spacing."""
    n = max(1, math.ceil((end - start) / max_spacing))
    return [start + (end - start) * i / n for i in range(n + 1)]


def build(c):
    c.validate()
    parts = []

    def add(name, assembly, material, x, y, z, dx, dy, dz):
        if min(dx, dy, dz) <= 0:
            raise ValueError(f'Nonpositive geometry: {name}')
        parts.append(Part(name, assembly, material, (x, y, z), (dx, dy, dz)))

    w, d, h = c.width, c.depth, c.overall_height_in
    wall_h, z0 = h-ROOF-SILL, SILL
    opening_z = c.door_bottom-SILL
    door_l, door_r = (w - c.door_rough_width_in) / 2, (w + c.door_rough_width_in) / 2
    # Folded tray on concrete. Adjacent solids represent one continuous folded sheet.
    add('Floor liner on concrete','tray','liner',WALL,WALL,0,
        c.interior_width_in,c.interior_depth_in,FLOOR_LINER)
    for x in (WALL,w-WALL-FLOOR_LINER):
        add('Tray side upstand','tray','liner',x,WALL,BASE,FLOOR_LINER,c.interior_depth_in,c.tray_upstand_in)
    for y in (WALL,d-WALL-FLOOR_LINER):
        add('Tray end upstand','tray','liner',WALL+FLOOR_LINER,y,BASE,
            c.interior_width_in-2*FLOOR_LINER,FLOOR_LINER,c.tray_upstand_in)
    # Curb supports the folded front edge at the doorway; no fasteners through liner.
    add('Doorway curb backing','threshold','lumber',door_l,SHEATH,SILL,
        c.door_rough_width_in,WALL-SHEATH,c.tray_upstand_in-SILL)
    add('Liner folded over curb','threshold','liner',door_l,SHEATH,c.tray_upstand_in,
        c.door_rough_width_in,WALL-SHEATH,FLOOR_LINER)
    add('Liner outer curb face','threshold','liner',door_l,SHEATH-FLOOR_LINER,0,
        c.door_rough_width_in,FLOOR_LINER,c.door_bottom)
    add('Curb capillary break','threshold','isolation pad',door_l,SHEATH,0,
        c.door_rough_width_in,WALL-SHEATH,SILL)

    # Local wall coordinate u follows the wall; v points along its thickness.
    def wall(name, length, mapper, front=False):
        def local(label, mat, u, v, z, du, dv, dz):
            x, y, dx, dy = mapper(u, v, du, dv)
            add(label, name, mat, x, y, z0 + z, dx, dy, dz)
        opening_l = door_l - SHEATH
        opening_r = door_r - SHEATH
        ranges = [(0, length)] if not front else [(0, opening_l), (opening_r, length)]
        for a, b in ranges:
            local('Bottom plate', 'lumber', a, 0, 0, b-a, STUD, THIN)
            x,y,dx,dy=mapper(a,0,b-a,STUD)
            add('Sill capillary break',name,'isolation pad',x,y,0,dx,dy,SILL)
        local('Top plate', 'lumber', 0, 0, wall_h-THIN, length, STUD, THIN)
        if front:
            king_l, king_r = opening_l - 2*THIN, opening_r + THIN
            studs = positions(0, king_l, c.max_stud_spacing_in)
            studs += positions(king_r, length-THIN, c.max_stud_spacing_in)
            for u in (opening_l-THIN, opening_r):
                local('Door jack stud', 'lumber', u, 0, THIN, THIN, STUD,
                      opening_z+c.door_rough_height_in-THIN)
            # Two 2x4 plies are too thick for a 2x3 wall. One 2x4 plus a 1-in
            # cavity pack makes this light roof header; member adequacy unverified.
            local('Door header', 'lumber', opening_l-THIN, 0, opening_z+c.door_rough_height_in,
                  c.door_rough_width_in+2*THIN, THIN, JOIST)
            local('Header insulation pack', 'insulation', opening_l-THIN, THIN,
                  opening_z+c.door_rough_height_in, c.door_rough_width_in+2*THIN, STUD-THIN, JOIST)
            upper = wall_h-THIN-opening_z-c.door_rough_height_in-JOIST
            if upper > 0:
                for u in positions(opening_l-THIN, opening_r, c.max_stud_spacing_in):
                    local('Header cripple', 'lumber', u, 0, opening_z+c.door_rough_height_in+JOIST,
                          THIN, STUD, upper)
        elif name in ('left','right'):
            bearings=[0]+[y-(SHEATH+STUD)-THIN/2 for y in c.roof_centers[1:-1]]+[length-THIN]
            studs=sorted({round(v,8) for a,b in zip(bearings,bearings[1:])
                          for v in positions(a,b,c.max_stud_spacing_in)})
        else:
            studs = positions(0, length-THIN, c.max_stud_spacing_in)
        for u in studs:
            local('Wall stud', 'lumber', u, 0, THIN, THIN, STUD, wall_h-2*THIN)
        # Fill actual free wall cavities using all framing boundaries (no double count).
        frame = [p for p in parts if p.assembly == name and p.material == 'lumber']
        # Skins/insulation are added below in world coordinates to permit explicit
        # front opening subtraction and exact insulation partitioning.
        return frame

    wall('front', w-2*SHEATH, lambda u,v,du,dv:(SHEATH+u,SHEATH+v,du,dv), True)
    wall('back', w-2*SHEATH, lambda u,v,du,dv:(SHEATH+u,d-SHEATH-STUD+v,du,dv))
    wall('left', d-2*(SHEATH+STUD), lambda u,v,du,dv:(SHEATH+v,SHEATH+STUD+u,dv,du))
    wall('right', d-2*(SHEATH+STUD), lambda u,v,du,dv:(w-SHEATH-STUD+v,SHEATH+STUD+u,dv,du))

    def skin(name, mat, x, y, z, dx, dy, dz, opening=False):
        if not opening:
            add(name+' '+mat, name, mat, x,y,z,dx,dy,dz)
        else:
            add('Front left '+mat, name, mat, x,y,z,door_l-x,dy,dz)
            add('Front right '+mat, name, mat, door_r,y,z,x+dx-door_r,dy,dz)
            add('Front overdoor '+mat, name, mat, door_l,y,c.door_bottom+c.door_rough_height_in,
                c.door_rough_width_in,dy,z+dz-c.door_bottom-c.door_rough_height_in)

    # Outer plywood ring, then interior substrate and continuous liner rings.
    for mat, outer, thick in [('plywood',0,SHEATH),('substrate',SHEATH+STUD,BOARD),
                               ('liner',SHEATH+STUD+BOARD,LINER)]:
        skin('left',mat,outer,outer,z0,thick,d-2*outer,wall_h)
        skin('right',mat,w-outer-thick,outer,z0,thick,d-2*outer,wall_h)
        skin('front',mat,outer+thick,outer,z0,w-2*(outer+thick),thick,wall_h,True)
        skin('back',mat,outer+thick,d-outer-thick,z0,w-2*(outer+thick),thick,wall_h)

    # Partition wall free space at framing edges; merge is unnecessary for batts,
    # whose CSV dimensions are fit-to-cavity estimates rather than purchase cuts.
    for name in ('front','back','left','right'):
        along = 0 if name in ('front','back') else 1
        across = 1-along
        frames = [p for p in parts if p.assembly==name and p.material=='lumber']
        us = sorted({round(p.origin[along]+s*p.size[along],8) for p in frames for s in (0,1)})
        zs = sorted({round(p.origin[2]+s*p.size[2],8) for p in frames for s in (0,1)})
        cross = min(p.origin[across] for p in frames)
        for a,b in zip(us,us[1:]):
            for za,zb in zip(zs,zs[1:]):
                midu, midz = (a+b)/2, (za+zb)/2
                if name=='front' and door_l < midu < door_r and midz < c.door_bottom+c.door_rough_height_in:
                    continue
                if any(p.origin[along]-1e-7 < midu < p.origin[along]+p.size[along]+1e-7
                       and p.origin[2]-1e-7 < midz < p.origin[2]+p.size[2]+1e-7 for p in frames):
                    continue
                xy = (a,cross,b-a,STUD) if along==0 else (cross,a,STUD,b-a)
                add('Wall cavity insulation',name,'insulation',xy[0],xy[1],za,xy[2],xy[3],zb-za)

    # Short-span roof members bear directly on long-wall top plates, over studs.
    rz = h-ROOF
    add('Ceiling liner','roof','liner',WALL,WALL,rz-CEILING,c.interior_width_in,c.interior_depth_in,LINER)
    add('Ceiling substrate','roof','substrate',WALL,WALL,rz-ROOF_BOARD,
        c.interior_width_in,c.interior_depth_in,ROOF_BOARD)
    for i,y in enumerate(c.roof_centers):
        label='Roof end member' if i in (0,len(c.roof_centers)-1) else f'Light-support crossmember {i}'
        add(label,'roof','lumber',SHEATH,y-THIN/2,rz,w-2*SHEATH,THIN,JOIST)
    for a,b in zip(c.roof_centers,c.roof_centers[1:]):
        for x in (SHEATH,w-SHEATH-THIN):
            add('Roof side rim segment','roof','lumber',x,a+THIN/2,rz,THIN,b-a-THIN,JOIST)
        add('Roof cavity insulation','roof','insulation',SHEATH+THIN,a+THIN/2,rz,
            w-2*(SHEATH+THIN),b-a-THIN,JOIST)
    add('Roof cap','roof','plywood',0,0,h-ROOF_CAP,w,d,ROOF_CAP)

    # Door blank only: adjustable finished leaf requires jamb/stop/hardware shop detail.
    gap = 0.125
    dw, dh = c.door_rough_width_in-2*gap, c.door_rough_height_in-2*gap
    dx, dz = door_l+gap, c.door_bottom+gap
    for mat,y,t in [('plywood',0,SHEATH),('substrate',SHEATH+STUD,BOARD),
                    ('liner',SHEATH+STUD+BOARD,LINER)]:
        add('Door '+mat,'door',mat,dx,y,dz,dw,t,dh)
    for x in (dx,dx+dw-THIN):
        add('Door stile','door','lumber',x,SHEATH,dz,THIN,STUD,dh)
    for z in (dz,dz+dh-THIN):
        add('Door rail','door','lumber',dx+THIN,SHEATH,z,dw-2*THIN,STUD,THIN)
    add('Door insulation','door','insulation',dx+THIN,SHEATH,dz+THIN,dw-2*THIN,STUD,dh-2*THIN)
    return parts


def environment(c):
    """Steady mass balance at 101.325 kPa; Magnus saturation approximation."""
    def sat(t):
        return 0.61094 * math.exp(17.625*t/(t+243.04))
    def humidity_ratio(t,rh):
        pv = sat(t)*rh/100
        return 0.62198*pv/(101.325-pv)
    gamma = math.log(c.inside_rh_percent/100)+17.625*c.inside_temp_c/(243.04+c.inside_temp_c)
    dew = 243.04*gamma/(17.625-gamma)
    wi = humidity_ratio(c.inside_temp_c,c.inside_rh_percent)
    wm = humidity_ratio(c.makeup_temp_c,c.makeup_rh_percent)
    delta = wi-wm
    # Specific volume at exhaust/inside conditions, m3 / kg dry air.
    v = 0.287042*(c.inside_temp_c+273.15)*(1+1.607858*wi)/101.325
    cfm = (c.water_evaporated_l_per_day/86400/delta*v/0.00047194745
           if delta>0 else None)
    wall_m2 = 2*(c.width+c.depth)*c.clear_height*0.00064516
    roof_m2 = c.width*c.depth*0.00064516
    ua = wall_m2/(c.assumed_wall_r_imperial*0.176110)
    ua += roof_m2/(c.assumed_roof_r_imperial*0.176110)
    conduction = ua*(c.inside_temp_c-c.garage_temp_c)
    return {
        'inside_dew_point_c': round(dew,2),
        'inside_humidity_ratio_kg_per_kg': round(wi,6),
        'makeup_humidity_ratio_kg_per_kg': round(wm,6),
        'minimum_exhaust_cfm_for_moisture_only': None if cfm is None else round(cfm,2),
        'sealed_mode_minimum_water_removal_l_per_day': c.water_evaporated_l_per_day,
        'walls_roof_conductance_w_per_k': round(ua,2),
        'walls_roof_conduction_outward_w_positive_is_heat_loss': round(conduction,1),
        'slab_heat_transfer_w': None,
        'caveat': 'Illustrative inputs, assumed whole-assembly R values. No leakage, solar, '
                  'thermal-bridge/surface-temperature simulation, transient storage, ventilation '
                  'heat, or dehumidifier heat. Uninsulated slab heat transfer is excluded; '
                  'slab temperature and ground coupling are unknown. '
                  'Moisture-only airflow is not a fan selection or a fresh-air requirement.'
    }


def roof_load_study(c):
    """Demand estimates only. No allowable stresses, connector check or load rating."""
    # Center-to-center bearing distance; no eight-foot spanning light supports.
    span=c.width-2*SHEATH-STUD
    centers=c.roof_centers
    tributary=max((centers[i+1]-centers[i-1])/2 for i in range(1,len(centers)-1))
    w=c.assumed_roof_dead_load_psf*tributary/144  # lb / linear inch
    inertia=THIN*JOIST**3/12
    section_modulus=THIN*JOIST**2/6
    p=c.total_hanging_weight_lb
    return {
        'interior_light_support_members':c.roof_crossmember_count,
        'end_members':2,
        'member_actual_section_in':[THIN,JOIST],
        'member_cut_length_in':round(c.width-2*SHEATH,3),
        'bearing_center_span_in':round(span,3),
        'nominal_bearing_length_each_end_in':STUD,
        'support_centers_from_front_outside_in':centers,
        'largest_roof_support_spacing_in':round(max(b-a for a,b in zip(centers,centers[1:])),3),
        'total_hanging_weight_lb':p,
        'load_case':'Conservatively put the entire entered hanging load at midspan of one interior crossmember, plus its tributary roof dead load.',
        'maximum_bending_stress_demand_psi':None if p is None else round((w*span**2/8+p*span/4)/section_modulus,2),
        'instantaneous_deflection_in':None if p is None else round((5*w*span**4/384+p*span**3/48)/(c.assumed_lumber_e_psi*inertia),4),
        'reaction_at_each_end_lb':None if p is None else round((w*span+p)/2,2),
        'status':'NOT LOAD RATED. Confirm light/hardware weights, lumber species/grade, service conditions, roof/ceiling panel spans, bearing, joints, wall bracing and freestanding stability. E and dead load are illustrative; creep is excluded.'
    }


def export(c, parts, out):
    out.mkdir(parents=True,exist_ok=True)
    summary = {'exterior_in':[c.width,c.depth,c.overall_height_in],
               'clear_interior_in':[c.interior_width_in,c.interior_depth_in,c.clear_height],
               'interior_volume_ft3':c.interior_width_in*c.interior_depth_in*c.clear_height/1728,
               'wall_thickness_in':WALL,'floor_top_in':BASE,'roof_thickness_in':ROOF,
               'ceiling_finish_thickness_in':CEILING,
               'tray_upstand_in':c.tray_upstand_in,
               'tray_clear_bottom_in':[c.interior_width_in-2*FLOOR_LINER,c.interior_depth_in-2*FLOOR_LINER],
               'minimum_unfolded_liner_blank_in':[c.interior_width_in+2*c.tray_upstand_in,
                                                 c.interior_depth_in+2*c.tray_upstand_in],
               'liner_blank_note':'Geometric minimum only; add material for corner folds, attachment overlap and wrapping the doorway curb.',
               'door_rough_opening_in':[c.door_rough_width_in,c.door_rough_height_in],
               'door_opening_bottom_above_slab_in':c.door_bottom,
               'roof_load_study':roof_load_study(c),
               'environment':environment(c)}
    (out/'model.json').write_text(json.dumps({'units':'inches','config':asdict(c),
        'summary':summary,'parts':[asdict(p) for p in parts]},indent=2)+'\n')
    with (out/'parts.csv').open('w',newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['id','assembly','part','material','x_in','y_in','z_in','dx_in','dy_in','dz_in'])
        for i,p in enumerate(parts,1):
            writer.writerow([i,p.assembly,p.name,p.material,*[round(v,4) for v in (*p.origin,*p.size)]])
    groups = {}
    for p in parts:
        dims = tuple(sorted(round(v,4) for v in p.size))
        key = (p.material,p.name,*dims)
        groups[key] = groups.get(key,0)+1
    with (out/'cut_list.csv').open('w',newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['material','part','thickness_or_smallest_in','middle_in','longest_in','quantity'])
        for k,n in sorted(groups.items()):
            writer.writerow([*k,n])
    areas = {}
    for p in parts:
        if p.material not in ('lumber','insulation'):
            dims = sorted(p.size)
            key = f'{p.material} {dims[0]:g} in'
            areas[key] = areas.get(key,0)+dims[1]*dims[2]/144
    (out/'summary.json').write_text(json.dumps({**summary,'net_sheet_area_ft2':areas,
        'sheet_note':'Net area only: add seams/blocking, nesting and waste. Large panels need joined sheets.',
        'cut_note':'Concept pieces. Tray solids describe folds in one sheet, not separate pieces to cut. Jamb returns, stops, seals, seam blocking, fasteners and hanging hardware not quantified.'},indent=2)+'\n')
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config',type=Path,default=Path(__file__).with_name('config.json'))
    parser.add_argument('--out',type=Path,default=Path(__file__).with_name('output'))
    args = parser.parse_args()
    try:
        c = Config.read(args.config)
        summary = export(c,build(c),args.out)
    except (ValueError,TypeError,OSError) as exc:
        parser.error(str(exc))
    print(json.dumps(summary,indent=2))
    print(f'Wrote geometry and material schedules to {args.out.resolve()}')


if __name__ == '__main__':
    main()
