"""Create a printable purchasing worksheet and auditable stock-cut CSVs from the model.

Run: python3 grow_lab/procurement.py
Uses the same config as render.py. Quantities are planning quantities, not a load rating.
"""
import argparse
from collections import Counter, defaultdict
import csv
from datetime import date
import json
import math
import os
from pathlib import Path
import textwrap

os.environ.setdefault('MPLCONFIGDIR','/tmp/grow-lab-matplotlib')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.patches import Rectangle

from model import Config, build, WALL, SHEATH, STUD, THIN, FLOOR_LINER

KERF=.125


def lumber_stock(parts):
    plans={}
    for section in ((1.5,2.5),(1.5,3.5)):
        bins=[]
        pieces=sorted([(max(p.size),p.name) for p in parts if p.material=='lumber'
                       and tuple(sorted(p.size)[:2])==section],reverse=True)
        for length,name in pieces:
            options=[(b['remaining']-length-KERF,i) for i,b in enumerate(bins)
                     if b['remaining']+1e-7>=length+KERF]
            if options:
                _,i=min(options)
            else:
                stock=math.ceil((length+KERF+.25)/24)*24
                stock=max(96,stock)
                bins.append({'stock_in':stock,'remaining':stock-.25,'cuts':[]})
                i=len(bins)-1
            bins[i]['remaining']-=length+KERF
            bins[i]['cuts'].append({'length_in':length,'part':name})
        plans[f'2x{3 if section[1]==2.5 else 4}']=bins
    return plans


def panel_pieces(c,parts):
    """Split wall sheets at stud centerlines; roof has one added seam-blocking line.

    Finished liners follow the substrate support grid conservatively. Different
    approved seam layouts may use fewer sheets. No sheet rotation is assumed.
    """
    groups=defaultdict(list)
    for p in parts:
        if p.material not in ('plywood','substrate','liner') or p.assembly in ('tray','threshold'):
            continue
        t=min(p.size)
        key=(p.material,round(t,4))
        if p.assembly=='roof':
            width,height=p.size[:2]
            xcuts=[0]
            while width-xcuts[-1]>48+1e-7:
                xcuts.append(xcuts[-1]+48)
            xcuts.append(width)
            # Keep a 48x96 ceiling whole. Oversized roof cap splits over members.
            ycuts=([0]+[y-p.origin[1] for y in c.roof_centers[1:-1]]+[height]
                   if height>96+1e-7 else [0,height])
            for a,b in zip(xcuts,xcuts[1:]):
                for u,v in zip(ycuts,ycuts[1:]):
                    groups[key].append({'part':p.name,'width':b-a,'height':v-u})
        else:
            axis=1 if p.assembly in ('left','right') else 0
            start,end=p.origin[axis],p.origin[axis]+p.size[axis]
            supports=sorted({q.origin[axis]+q.size[axis]/2 for q in parts
                             if q.assembly==p.assembly and q.material=='lumber'
                             and q.name in ('Wall stud','Door jack stud')})
            cuts=[start]
            while end-cuts[-1]>48+1e-7:
                candidates=[v for v in supports if cuts[-1]+1e-7<v<=cuts[-1]+48]
                if not candidates:
                    raise ValueError(f'No sheet seam support within 48 in for {p.name}')
                cuts.append(max(candidates))
            cuts.append(end)
            for a,b in zip(cuts,cuts[1:]):
                groups[key].append({'part':p.name,'width':b-a,'height':p.size[2]})
    return groups


def sheet_stock(pieces):
    """Conservative guillotine layout on 48x96 sheets, with kerf, no rotation."""
    sheets=[]
    for p in sorted(pieces,key=lambda p:(p['height'],p['width']),reverse=True):
        w,h=p['width'],p['height']
        if w>48+1e-7 or h>96+1e-7:
            raise ValueError(f'Piece exceeds stock: {p}')
        choices=[]
        for i,sheet in enumerate(sheets):
            for j,(x,y,rw,rh) in enumerate(sheet['free']):
                if w<=rw+1e-7 and h<=rh+1e-7:
                    choices.append((rw*rh-w*h,i,j))
        if choices:
            _,i,j=min(choices)
        else:
            sheets.append({'free':[(0,0,48,96)],'pieces':[]});i=len(sheets)-1;j=0
        x,y,rw,rh=sheets[i]['free'].pop(j)
        sheets[i]['pieces'].append({**p,'x':x,'y':y})
        # Two nonoverlapping reusable offcuts, separated from the part by saw kerf.
        if rw-w>KERF:
            sheets[i]['free'].append((x+w+KERF,y,rw-w-KERF,h))
        if rh-h>KERF:
            sheets[i]['free'].append((x,y+h+KERF,rw,rh-h-KERF))
    return sheets


def data(c,parts):
    lumber=lumber_stock(parts)
    sheets={key:sheet_stock(value) for key,value in panel_pieces(c,parts).items()}
    area={key:sum(p['width']*p['height'] for s in bins for p in s['pieces'])/144
          for key,bins in sheets.items()}
    insulation=defaultdict(float)
    for p in parts:
        if p.material=='insulation':
            thickness=p.size[2] if p.assembly=='roof' else (p.size[0] if p.assembly in ('left','right') else p.size[1])
            insulation[round(thickness,3)]+=math.prod(p.size)/thickness/144
    # Add 15% and round coverage up to 10 ft²; actual bags depend on selected product.
    insulation_buy={t:math.ceil(a*1.15/10)*10 for t,a in insulation.items()}
    floor_w=c.interior_width_in+2*c.tray_upstand_in+6
    floor_h=c.interior_depth_in+2*c.tray_upstand_in+(WALL-SHEATH)+c.door_bottom+6
    floor_stock=(math.ceil(floor_w/12),math.ceil(floor_h/12))
    gasket_ft=(2*(c.interior_width_in+c.interior_depth_in)+2*(c.door_rough_width_in+c.door_rough_height_in))/12
    return lumber,sheets,area,insulation,insulation_buy,floor_stock,gasket_ft


def write_csvs(out,lumber,sheets,shopping):
    with (out/'procurement.csv').open('w',newline='') as f:
        writer=csv.writer(f)
        writer.writerow(['category','item','order_quantity','unit','basis_or_selection_note','unit_price','extended_price'])
        writer.writerows([*row,'',''] for row in shopping)
    with (out/'lumber_stock_plan.csv').open('w',newline='') as f:
        writer=csv.writer(f)
        writer.writerow(['nominal_section','board_id','stock_length_in','part','cut_length_in','offcut_after_all_cuts_in'])
        for section,bins in lumber.items():
            for i,b in enumerate(bins,1):
                for p in b['cuts']:
                    writer.writerow([section,i,b['stock_in'],p['part'],round(p['length_in'],4),round(b['remaining'],4)])
    with (out/'sheet_stock_plan.csv').open('w',newline='') as f:
        writer=csv.writer(f)
        writer.writerow(['material','thickness_in','sheet_id','part','x_on_sheet_in','y_on_sheet_in','width_in','height_in'])
        for (mat,t),bins in sheets.items():
            for i,b in enumerate(bins,1):
                for p in b['pieces']:
                    writer.writerow([mat,t,i,p['part'],*[round(p[k],4) for k in ('x','y','width','height')]])


class Document:
    def __init__(self,path):
        self.pdf=PdfPages(path)
        self.page=0

    def start(self,title,subtitle):
        self.page+=1
        self.fig,self.ax=plt.subplots(figsize=(8.5,11))
        self.fig.subplots_adjust(left=0,right=1,bottom=0,top=1)
        self.ax.set(xlim=(0,8.5),ylim=(0,11));self.ax.axis('off')
        self.ax.add_patch(Rectangle((0,10.75),8.5,.25,fc='#248d86'))
        self.ax.text(.55,10.36,'GROW LAB  /  PROCUREMENT',fontsize=10,color='#248d86',weight='bold')
        self.ax.text(.55,9.91,title,fontsize=20,color='#24394b',weight='bold')
        self.y=9.5
        self.paragraph(subtitle,9.2)

    def paragraph(self,text,size=9.2):
        lines=textwrap.wrap(text,width=int(7.4*72/(size*.51)))
        height=len(lines)*size/72*1.4+.15
        if self.y-height<.65:
            raise ValueError(f'Page {self.page} overflow: {text[:40]}')
        self.ax.text(.55,self.y,'\n'.join(lines),fontsize=size,va='top',color='#24394b',linespacing=1.4)
        self.y-=height

    def table(self,headers,rows,widths,font=8.8):
        x=.55
        self.ax.add_patch(Rectangle((x,self.y-.34),sum(widths),.34,fc='#24394b'))
        for head,width in zip(headers,widths):
            self.ax.text(x+.07,self.y-.17,head,fontsize=font,color='white',va='center',weight='bold');x+=width
        self.y-=.34
        for i,row in enumerate(rows):
            cells=[textwrap.wrap(str(v),width=max(6,int((w-.14)*72/(font*.53)))) or ['']
                   for v,w in zip(row,widths)]
            height=max(map(len,cells))*font/72*1.35+.15
            if self.y-height<.65:
                raise ValueError(f'Page {self.page} table overflow at {row}')
            self.ax.add_patch(Rectangle((.55,self.y-height),sum(widths),height,fc='#eef3f5' if i%2==0 else '#ffffff'))
            x=.55
            for lines,width in zip(cells,widths):
                self.ax.text(x+.07,self.y-.075,'\n'.join(lines),fontsize=font,va='top',color='#24394b',linespacing=1.35);x+=width
            self.y-=height
        self.y-=.18

    def finish(self):
        self.ax.text(.55,.35,'4 × 8 ft clear interior  |  Planning quantities; verify marked selections before ordering',fontsize=8,color='#657889')
        self.ax.text(7.95,.35,str(self.page),fontsize=8,ha='right',color='#657889')
        self.pdf.savefig(self.fig);plt.close(self.fig)


def generate(c,out):
    parts=build(c)
    lumber,sheets,areas,insulation,insulation_buy,floor_stock,_=data(c,parts)
    out.mkdir(parents=True,exist_ok=True)
    shopping=[]
    core=[]
    for section,bins in lumber.items():
        counts=Counter(b['stock_in'] for b in bins)
        for stock,n in sorted(counts.items()):
            extra=(3 if section=='2x3' else 2) if stock==96 else 0
            note=f'{n} allocated to modeled cuts'
            if extra:
                note+=f' + {extra} allowance boards'
                note+=' for backing/spares.' if section=='2x3' else ' (one seam-backing allowance, one spare).'
            if stock>96:
                note+=' Long plates exceed 8 ft; do not substitute short stock without a splice detail.'
            if section=='2x4':note+=' Grade/species and roof load check pending.'
            item=f'{section} × {stock/12:g} ft lumber'
            core.append([n+extra,item,note]);shopping.append(['Lumber',item,n+extra,'boards',note])
    curb=next(p for p in parts if p.name=='Doorway curb backing')
    note=f'One piece at least {curb.size[0]+2:g} in long; machine to {curb.size[0]:g} × {curb.size[1]:.3f} × {curb.size[2]:.3f} in. Finalize curb detail first.'
    core.append([1,'4x4 curb offcut',note]);shopping.append(['Lumber','4x4 curb offcut',1,'piece',note])
    perimeter=2*(c.interior_width_in+c.interior_depth_in)/12
    furring=math.ceil(perimeter*1.2/8)
    note='Finish/perimeter support allowance only; actual profile and attachment follow panel requirements.'
    core.append([furring,'1x3 × 8 ft furring',note]);shopping.append(['Lumber','1x3 × 8 ft furring',furring,'boards',note])
    doc=Document(out/'procurement.pdf')
    doc.start('Lumber & purchasing basis',f'Prepared {date.today().isoformat()}. Based on the current Python model: {c.interior_width_in:g} × {c.interior_depth_in:g} in inside; {c.width:.2f} × {c.depth:.2f} × {c.overall_height_in:g} in outside. Screw assembly, three interior light supports, slab-supported walls and folded floor liner.')
    doc.paragraph('Order quantities include the allowances stated below. They are not a fastening schedule or a structural approval. Prices are intentionally blank in the companion CSV for your supplier quotes. No floor deck, platform joists, floor insulation, slab anchors or nails are on this list.')
    doc.table(['Qty','Stock / item','Basis and selection notes'],core,[.55,2.05,4.8])
    doc.paragraph('Select straight, dry framing lumber. Confirm availability of 10-foot 2x3 stock with the lumberyard. Long bottom plates need moisture-compatible wood selection and a capillary break. Changing lumber dimensions or finish thickness changes the enclosure geometry.')
    doc.paragraph('Roof member size is provisional: total light and hanging-hardware weight has not been supplied. Do not purchase a connector package or claim a roof capacity from these quantities alone.')
    doc.finish()

    doc.start('Sheets, insulation & floor liner','Sheet counts come from a conservative, kerf-aware 48 × 96 in cutting layout, not net area divided by sheet area. Stock-sheet layouts are included as CSV coordinates.')
    panel_rows=[]
    for (mat,t),bins in sorted(sheets.items()):
        label={'plywood':'Plywood','substrate':'Finish substrate','liner':'Wall/ceiling liner'}[mat]
        note=f'{areas[(mat,t)]:.1f} ft² net; {len(bins)*32} ft² purchased. '
        note+=('Verify bracing or roof span rating.' if mat=='plywood' else 'Select compatible product and verify support/finish requirements.')
        item=f'{t:g} in {label.lower()}, 4 × 8 ft'
        panel_rows.append([len(bins),item,note]);shopping.append(['Sheet goods',item,len(bins),'sheets',note])
    doc.table(['Qty','Sheet material','Basis / confirm before ordering'],panel_rows,[.55,2.7,4.15],8.4)
    ins_rows=[]
    for t,net in sorted(insulation.items()):
        if t==1:
            ins_rows.append(['Offcut','1 in header pack',f'{net:.1f} ft² net; use compatible insulation offcut.'])
            shopping.append(['Insulation','1 in header pack',round(net,1),'ft² net','Use compatible offcut; bag quantity not specified.'])
        else:
            note=f'{net:.1f} ft² net + 15%, rounded up. Actual bag count and cavity fit depend on selected product.'
            ins_rows.append([f'{insulation_buy[t]} ft²',f'{t:g} in cavity insulation',note])
            shopping.append(['Insulation',f'{t:g} in cavity insulation',insulation_buy[t],'ft² coverage',note])
    floor_item=f'{floor_stock[0]} × {floor_stock[1]} ft waterproof plastic liner'
    note=f'One continuous sheet; model assumes 40 mil. Includes fold/curb allowance; verify template, membrane suitability and corners before cutting.'
    ins_rows.append([1,floor_item,note]);shopping.append(['Floor',floor_item,1,'sheet',note])
    doc.table(['Qty','Insulation / floor','Basis / selection notes'],ins_rows,[.75,2.5,4.15],8.4)
    doc.paragraph('Floor liner and wall/ceiling liner are separate systems. Do not buy multiple floor strips from the geometric cut list. The membrane must remain continuous around folded corners and over the supported doorway curb. Finish liner thickness is schematic; match actual material and adhesive requirements.',8.6)
    doc.finish()

    doc.start('Sealing, door & screw supplies','Allowance quantities below support budgeting and supplier quotes. Final screw diameters, lengths, spacing and connector models still require a joint-by-joint schedule.')
    floor_area=c.interior_width_in*c.interior_depth_in/144
    door_gasket=math.ceil(2*(c.door_rough_width_in+c.door_rough_height_in)/12*1.2/5)*5
    seal_rows=[
        ['1 roll','Sill gasket, at least 3 in wide',f'At least {math.ceil(2*(c.width+c.depth)/12*1.15)} linear ft; thin capillary break, not anchorage.'],
        [f'{math.ceil(floor_area*1.2/10)*10} ft²','Membrane protection layer','Compatible underlayment / equipment-foot protection; choose for slab roughness and liner.'],
        [f'{door_gasket} ft','Door compression gasket','Compatible with the finished jamb, latch and threshold.'],
        ['180 ft','Compatible seam tape / joint system','Planning allowance for panel, perimeter and door transitions; use approved welded/bonded joints if required instead.'],
        ['6 tubes','Compatible sealant, approx. 10 oz','Allowance only; substrate, joint width and membrane chemistry determine actual usage.'],
        [f'{math.ceil(areas[("liner",.06)]*1.15/10)*10} ft²','Liner adhesive coverage','Only if the selected liner requires adhesive; divide by manufacturer coverage for container quantity.'],
        ['3','Door hinges','Provisional count; select for actual leaf weight, size and screw holding.'],
        ['1 set','Compression latch and handles','Operable from inside; match gasket compression and door thickness.'],
        ['1 set','Jamb/stop stock and threshold seals','Final profile and lengths follow the finished door detail; not fully modeled.'],
    ]
    for qty,item,note in seal_rows:shopping.append(['Seals / door',item,qty,'allowance',note])
    doc.table(['Qty','Item','Selection / allowance'],seal_rows,[.85,2.3,4.25],8.4)
    doc.paragraph('All membrane fastening stays above the tray rim. Seal ceiling penetrations for hanging hardware. Select corrosion protection suitable for humid service and any treated lumber. These supplies do not replace active dehumidification or outdoor exhaust.',8.8)
    doc.finish()

    doc.start('Fasteners & items to finalize','A drill/driver-based build is intended. Structural wood screws and metal-connector screws are different products; do not substitute generic deck or drywall screws for specified structural fasteners.')
    screw_rows=[
        ['250','Structural wood screws','Purchase allowance for framing/corners. Size, length and end/edge distances TBD; use load-rated products for the selected joints.'],
        ['500','Plywood bracing/roof screws','Allowance only. Select screws with a verified sheathing attachment schedule; do not copy a nail schedule.'],
        ['250','Interior substrate screws','Allowance only; choose for the selected panel and support system. Avoid overdriving or penetrating the floor membrane.'],
        [str(2*(c.roof_crossmember_count+2)),'Roof ties / angles','One per member end as a planning allowance. Exact screw-installable connector, count and orientation need load/geometry verification.'],
        ['8','Wall-corner ties / angles','Two per corner as a planning allowance; not a verified bracing or stability solution.'],
        ['TBD','Connector screws','Use only the exact screws and hole counts approved for each chosen metal connector. Quantity follows connector selection.'],
        ['TBD','Rated light-hanging kits','Quantity follows light count and manufacturer suspension points. Confirm total lights + drivers + cables + hanger weight.'],
        ['1 set','Tools / consumables','Drill/driver, correct bits, clamps, tape, square, level and cutting tools; check what you already own.'],
    ]
    for qty,item,note in screw_rows:shopping.append(['Fasteners / hardware',item,qty,'allowance',note])
    doc.table(['Qty','Item','Use / decision required'],screw_rows,[.65,2.1,4.65],8.4)
    doc.paragraph('Not priced or sized: grow lights, drivers, fans, dehumidifier, ducts, controls, power distribution, service sleeves, condensate collection and grow equipment. Choose these after operating conditions, electrical needs and light loads are known.',8.8)
    doc.finish()

    doc.start('Lumber cut reference','Dimensions below are model dimensions in inches, before field fitting. Nominal lumber names differ from actual section sizes. Do not round all decimal cuts down; finalize the joinery and measure actual stock first.')
    groups=Counter((p.name,tuple(round(x,4) for x in sorted(p.size))) for p in parts if p.material=='lumber')
    rows=[]
    # Combine identically sized numbered light supports for a compact reference.
    merged=Counter()
    for (name,dims),n in groups.items():
        merged[(name if not name.startswith('Light-support') else 'Light-support crossmember',dims)]+=n
    for (name,dims),n in sorted(merged.items(),key=lambda entry:(entry[0][1][:2],entry[0][0])):
        rows.append([n,name,' × '.join(f'{x:g}' for x in dims[:2]),f'{dims[2]:g}'])
    doc.table(['Qty','Part','Actual section (in)','Cut length (in)'],rows,[.5,3.1,1.9,1.9],8.5)
    doc.paragraph('The curb is a shaped 4x4 offcut, not standard 2x stock. Corner backing, sheet-seam blocks, finish furring and door stops are allowance materials, not included in the modeled cut lengths above.',8.8)
    doc.paragraph('lumber_stock_plan.csv assigns modeled cuts to each board, allowing 1/4 in total end trim and 1/8 in kerf per cut. Purchase extras are outside that allocation. sheet_stock_plan.csv shows every panel piece on its allocated stock sheet, without rotation.',8.8)
    doc.finish()

    doc.start('Before ordering / source notes','Use this page to resolve the remaining product choices with your supplier or builder. The model has no verified hanging-load capacity, fastening schedule or freestanding stability calculation.')
    items=[
        'LIGHT LOAD: Enter total suspended weight in config.json. Confirm roof lumber grade/species, connections, bearing and wall stability before treating the 2x4 members as adequate.',
        'PANEL SUPPORT: Walls are framed at up to 24 in; roof support spacing reaches 26.28 in. Verify the actual plywood/substrate span rating, orientation and support requirements. Thin finish substrate may require additional furring.',
        'SHEET JOINTS: Wall panel splits use modeled stud centerlines. Roof-cap strips require a backed longitudinal seam; one spare 2x4 is allowed for that backing in the default design. Support ceiling perimeter and all required edges. Actual sheet sizes, damaged edges and installation gaps may change cuts.',
        'WATERPROOFING: Confirm actual floor membrane, corner method, joint chemistry, adhesive coverage and liner blank template. The default 6x10 ft floor sheet includes allowance, but verify the doorway wrapping and corner folds before cutting.',
        'DOOR: Finalize leaf fit, jamb stops, curb/threshold, hinges and inside-operable latch. The modeled door blank is provisional and influences sheet purchases.',
        'STOCK QUANTITIES: Sheet counts reflect one feasible conservative cutting layout, not an optimum. Different approved seam layouts may save material. No additional full spare sheets are included; inspect stock and consider returns policy.',
    ]
    for item in items:doc.paragraph(item,9)
    doc.paragraph('Technical references (general guidance; none certifies this enclosure):',9)
    sources=[
        ('Simpson Strong-Tie: fastener installation guide','https://www.strongtie.com/resources/product-installers-guide/fasteners'),
        ('Simpson Strong-Tie: approved connector screws','https://www.strongtie.com/products/fastening-systems/technical-notes/sd-connector-screw-approved-connectors'),
        ('American Wood Council: loads and span-table tutorial','https://awc.org/resource-hub/span-table-tutorial/'),
        ('DOE: sill plate air sealing','https://bsesc.energy.gov/energy-basics/floor-framing-air-sealing-sill-plates'),
    ]
    for label,url in sources:
        doc.ax.text(.55,doc.y,label,fontsize=9,color='#14796f',va='top',url=url)
        doc.y-=.27
    doc.finish();doc.pdf.close()
    write_csvs(out,lumber,sheets,shopping)
    (out/'procurement_basis.json').write_text(json.dumps({
        'config':c.__dict__,'lumber_stock_counts':{k:dict(Counter(b['stock_in'] for b in v)) for k,v in lumber.items()},
        'sheet_counts':{f'{k[0]} {k[1]:g} in':len(v) for k,v in sheets.items()},
        'insulation_net_ft2':insulation,'insulation_purchase_coverage_ft2':insulation_buy,
        'floor_membrane_stock_ft':floor_stock,'saw_kerf_in':KERF,'lumber_end_trim_in':.25,
        'note':'Planning quantities. Connector/screw schedule, product support ratings, roof capacity and final door fit unresolved.'},indent=2)+'\n')
    return out/'procurement.pdf'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config',type=Path,default=Path(__file__).with_name('config.json'))
    parser.add_argument('--out',type=Path,default=Path(__file__).with_name('output'))
    args=parser.parse_args()
    print(f'Created {generate(Config.read(args.config),args.out).resolve()}')


if __name__=='__main__':main()
