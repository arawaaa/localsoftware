# Garage grow lab — 4 × 8 ft interior

A parametric Python construction study for lighter insulated walls, a roof with
short-span grow-light supports, and a waterproof plastic floor liner laid on
existing concrete. **No raised platform, floor insulation or slab anchors.**
The original request clarified usable interior dimensions, so the revised 4 × 8 ft
size also means clear wall-to-wall interior; the outside footprint is larger.

Open the [construction drawings](output/construction_study.pdf),
[interactive viewer](output/viewer.html), or [roof-support plan](output/roof_supports.png).
The [procurement PDF](output/procurement.pdf) lists stock quantities, allowances,
screw/hardware selection notes and a lumber cut reference. Its companion
[procurement CSV](output/procurement.csv) has blank columns for supplier prices.
The viewer includes an embedded static SVG, so the model remains visible if a file
preview disables JavaScript or canvas. Open the HTML directly in a browser to use
rotation, zoom, assembly separation and material controls.

## Run and edit

```bash
python3 grow_lab/model.py
python3 grow_lab/render.py
python3 grow_lab/procurement.py
python3 -m unittest discover -s grow_lab -p 'test_*.py'
```

`model.py` needs only Python 3.10+ and its standard library. `render.py` also requires
Matplotlib (`python3 -m pip install -r grow_lab/requirements.txt` in your environment).
Edit [config.json](config.json) and regenerate. Both scripts accept `--config PATH`
and `--out DIRECTORY`; default paths are relative to the scripts, not the shell.
Layer thicknesses are constants in [model.py](model.py).

## Revised dimensions

| Item | Default |
| --- | --- |
| Clear interior above tray rim | 48 × 96 in |
| Closed outside envelope | 54.37 × 102.37 × 84 in high |
| Clear height above liner floor | 79.40 in |
| Wall thickness | 3.185 in |
| Wall stud spacing | At most 24 in; extra support where required at doorway/bearings |
| Sill gasket above slab | 0.125 in; capillary break only |
| Floor liner thickness | 0.04 in / 40 mil, schematic |
| Tray upstand | 3.5 in above floor liner |
| Clear tray bottom between upstands | 47.92 × 95.92 in |
| Door rough opening above curb | 28 × 68 in |
| Top of lined doorway curb above slab | 3.54 in |
| Interior light-support crossmembers | Three nominal 2×4 members across the short direction |
| Roof end members | Two, plus side rim segments |
| Roof framing and cap / ceiling finish | 4 in / 0.56 in |

The 7-foot overall height remains an assumption. Allow door swing and working
access outside the closed footprint. A 3.5-inch threshold is a step-over obstruction;
if changing its height, change the tray detail too. This tray is spill containment,
not an engineered reservoir or a claim of resistance to sustained water pressure.

## Walls on concrete

Nominal 2×3 studs (actual 1.5 × 2.5 in) are spaced up to 24 inches, with bearing studs
aligned beneath each interior light-support crossmember. The front panel has king
and jack studs and a single nominal 2×4 door header. The walls use one bottom plate
and one top plate. Their thickness is unchanged through the framing cavity, but
wider spacing and thinner skins reduce material per unit wall area.

From garage to growing space, the modeled layers are 3/8-inch plywood bracing,
2.5-inch framing and insulation, 1/4-inch liner substrate and a sealed interior
liner represented as 0.06 inch. Select actual sheets, insulation and a compatible
washable liner system for humid service. **Verify sheet support requirements at
24-inch wall spacing.** Thin substrate is a placeholder, not a product-approved
span. Add backing/furring or change the substrate when its manufacturer requires
it; this does not necessarily require more full-height wall studs.

The bottom plates sit on the slab over a thin capillary-break sill gasket. This
avoids a raised floor while separating wood from damp concrete. There are no slab
fasteners. Keep wood-based skins off the concrete and close the floor perimeter
with the turned-up liner. Sill material, wood treatment and compatible connectors
need selection for the actual garage conditions.

Connect the four panels firmly at corners and tie the roof to the top plates.
Plywood sheathing must be properly fastened with backed seams to provide the
intended bracing. The wall ring and roof replace the previous platform's role in
holding the assembly together. No fastening schedule, corner capacity, or
freestanding tip/slip resistance is established by the geometry. Check stability
with the door open and the intended hanging loads; do not count loose trays,
equipment or the gasket as structural anchorage.

## Waterproof plastic floor liner

Lay a durable, compatible waterproof plastic membrane on clean, smooth concrete,
with suitable puncture protection where needed. The model uses 40 mil as a
geometric placeholder; select an actual product for floor contact, folds and humid
service. Thin painter's plastic is not the intended material.

Turn the sheet up 3.5 inches around the inside perimeter. Fold corners without
cutting below the containment rim, or use the membrane manufacturer's approved
welded/bonded corner detail. Seal the wall air/vapor liner to the tray at its upper
edge. Keep screws, staples and service penetrations above the containment rim.
Protect the membrane under equipment feet and from abrasion during maintenance.

At the entrance, support the folded liner with a fixed curb connected to the
jamb/wall framing, not anchored through the membrane into the slab. The liner runs
up the inner curb face, over its top and down the outside. Corner junctions at the
jambs require a compatible waterproof detail. The model shows these fold surfaces,
but they are **one liner, not separate strips to cut from the CSV**. The curb
backing dimension is a concept requiring trimming/assembly to the finished height.

A flat blank of 55 × 103 inches is only the geometric minimum for the interior
floor plus upstands. Add material for corner folds, attachment overlap, and the
extra travel over and down the doorway curb. Verify the final liner template
before cutting. Net surface area is not a purchase quantity.

The floor now has no modeled insulation. Cold concrete can increase heating load
and cause condensation. Protecting the garage from humidity still requires sealed
joints plus dehumidification or exhaust routed outdoors; a liner only contains
liquid water and forms part of the air/vapor boundary.

## Roof and suspended grow lights

Three interior nominal 2×4 crossmembers run **across the 4-foot width**, on edge,
and bear directly on the long-wall top plates. Their full cut length is 53.62 in,
with nominal 2.5-inch bearing at each end and a 51.12-inch center-to-center bearing
span. They are not 8-foot spanning members. Side-wall studs align beneath them.
Two end members and side rim segments complete the perimeter.

Default crossmember centerlines, measured from the front outside face, are
27.405, 51.185 and 74.965 inches; end members are at 1.125 and 101.245 inches.
Maximum support spacing is 26.28 inches. These end bays are wider than 24 inches.
The roof has a modeled 1/2-inch plywood cap, insulation between members and a
1/2-inch ceiling substrate with liner below. **Select roof/ceiling panels rated
for the actual support spacing, orientation and loads, or add suitable furring
and seam backing.** The scripts do not establish those panel capacities.

The ceiling finish fits inside the walls below the roof members, leaving direct
wood-to-wood bearing at wall tops. Use rated hanging hardware attached to the
structural crossmembers, with sealed penetrations through the ceiling liner.
Do not hang lights from panel skins, insulation, or unsupported finish furring.
The dots in the roof drawing are example attachment locations, not supplied
fixtures, approved fasteners, or a rated load distribution.

**Light/hardware weights have not yet been supplied.** Set
`total_hanging_weight_lb` once known. It defaults to `null`, so the report does not
invent a hanging load or claim the supports are adequate. The layout and member
sizes remain provisional until weights, lumber species/grade, actual dead load,
connections, bearing, wall capacity and stability are checked. No walking, storage
or other roof loads are included.

`roof_load_study` in `summary.json` provides a demand estimate when a load is
entered. It conservatively places the entire hanging weight at midspan of one
interior member, plus the largest interior tributary roof dead load. It reports
bending stress, instantaneous deflection and end reactions using simple supported
beam equations. Default 6 psf roof dead load and E = 1,000,000 psi are illustrative.
There is no allowable-stress comparison or pass/fail certification, and no creep,
connector, lateral stability or wall analysis. The formulas are:

- `M = w L² / 8 + P L / 4`, `stress = M / S`
- `deflection = 5 w L⁴ / (384 E I) + P L³ / (48 E I)`
- `reaction = (w L + P) / 2`, `I = b h³ / 12`, `S = b h² / 6`

## Door, moisture and environmental assumptions

The door blank remains insulated and outward opening. Its bottom is above the
lined curb. Finalize jamb returns, stops, hinges, gasket compression and threshold
sealing before cutting the leaf; the modeled blank is only 1/8 inch smaller than
the rough opening on each edge. Use a latch operable from inside.

Seal all corners, ceiling/wall joints, door gaskets and service sleeves. Collect
dehumidifier condensate in a closed container with overflow protection or an
appropriate sealed drain path. Alternatively, use controlled makeup air and
outdoor exhaust; never discharge grow exhaust into the garage. Evaluate garage
air contaminants and combustion-appliance backdrafting if using garage makeup air.

The intended interior vapor-control layer is conditional on the seasonal garage
and grow-room conditions. A hot/humid garage around a cool grow room can reverse
the vapor drive. Select liner permeance and drying direction for the actual
conditions; do not add a second vapor-tight exterior facing without evaluating
moisture accumulation. Test tray/corner seals, gasket contact and moisture control
before operating, then monitor inside and garage temperature/RH and cold surfaces.

The environmental inputs remain illustrative: 25°C/65% RH inside; 10°C/60% RH garage
and makeup air; 3 L/day evaporation; 300 W equipment. They are not user-confirmed.
For these inputs, dew point is 17.96°C and theoretical moisture-only exhaust is
7.62 CFM. That airflow is not fan sizing and excludes ventilation/heat requirements.
If makeup air is equally humid or wetter by humidity ratio, airflow is `null`.
The ideal sealed-mode water-removal floor is 3 L/day actual capacity at operating
conditions; peaks, leakage and door opening add load.

Heat estimates use assumed whole-assembly R-9 walls/door and R-12 roof. The prior
R-10 floor assumption has been removed. Slab heat transfer is `null` and excluded
because slab temperature/ground coupling are unknown; the wall/roof estimate is
not a complete HVAC load. Environmental calculations also omit solar, leakage,
thermal bridging, transient storage and dehumidifier heat.

## Outputs and limits

| File | Contents |
| --- | --- |
| `output/construction_study.pdf` | Four pages: 3D, plan/elevation, layers/floor fold, roof supports |
| `output/viewer.html` / `framing.svg` | Interactive model with static fallback / standalone static model |
| `output/overview.png`, `plan_elevation.png`, `layer_sections.png`, `roof_supports.png` | Shareable drawings |
| `output/model.json` / `parts.csv` | Part solids, inch coordinates and dimensions |
| `output/cut_list.csv` | Grouped concept piece sizes, not stock-sheet nesting |
| `output/summary.json` | Dimensions, liner blank minimum, material areas, roof/environment studies |
| `output/procurement.pdf` / `procurement.csv` | Six-page purchasing worksheet / editable quote list |
| `output/lumber_stock_plan.csv` / `sheet_stock_plan.csv` | Board cuts / kerf-aware stock-sheet allocations |
| `output/procurement_basis.json` | Configuration and quantity assumptions used for procurement |

Large sheet extents require joined stock sheets, proper grain/span orientation,
backing and waste allowance. Hardware, gasket joints, corner folds, wall seam
backing, hangers, light fixtures and their cables are not fully modeled or counted.
The drawings are construction studies, not a complete fabrication/fastening plan.

References consulted September 8, 2026:

- [DOE: sill plate air sealing](https://bsesc.energy.gov/energy-basics/floor-framing-air-sealing-sill-plates)
  and [moisture flow](https://bsesc.energy.gov/energy-basics/building-enclosure-building-science-intro-moisture-flow):
  sill gaskets and capillary breaks at concrete/wood interfaces.
- [American Wood Council: loads and span tables](https://awc.org/resource-hub/span-table-tutorial/):
  species, grade, stiffness, loads and deflection are needed for member selection.
- [DOE: vapor barriers](https://bsesc.energy.gov/energy-basics/vapor-barriers):
  climate-dependent vapor control and drying.
- [University of Minnesota Extension: moisture control](https://extension.umn.edu/garden-and-home/home-maintenance/caring-for-a-home/controlling-moisture-problems-in-your-home):
  source control, ventilation and dehumidification.
