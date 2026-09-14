"""Generate construction study PNG/PDF drawings and an offline interactive HTML model."""
import argparse
import json
import math
import os
from pathlib import Path

os.environ.setdefault('MPLCONFIGDIR', '/tmp/grow-lab-matplotlib')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.patches import Patch, Rectangle, Arc
from mpl_toolkits.mplot3d.art3d import Poly3DCollection

from model import BASE, BOARD, LINER, ROOF, SHEATH, STUD, WALL, SILL, FLOOR_LINER, ROOF_CAP, ROOF_BOARD, CEILING, Config, build, export

COLORS = {'lumber':'#bf8a4e','plywood':'#ccbb98','substrate':'#bbc8cc',
          'liner':'#4aa8a3','insulation':'#edd074','isolation pad':'#53616e'}
FACES = [(0,1,2,3),(4,5,6,7),(0,1,5,4),(2,3,7,6),(1,2,6,5),(0,3,7,4)]


def vertices(p, offset=(0,0,0)):
    x,y,z = [a+b for a,b in zip(p.origin,offset)]
    dx,dy,dz = p.size
    return [(x,y,z),(x+dx,y,z),(x+dx,y+dy,z),(x,y+dy,z),
            (x,y,z+dz),(x+dx,y,z+dz),(x+dx,y+dy,z+dz),(x,y+dy,z+dz)]


def scene(ax,c,parts,mode):
    for p in parts:
        if mode=='frame' and p.material!='lumber' and p.assembly!='tray':
            continue
        if mode=='cutaway' and (p.assembly in ('front','left','door') or p.material=='insulation'):
            continue
        offset = (0,0,12) if mode=='cutaway' and p.assembly=='roof' else (0,0,0)
        if mode=='cutaway' and p.assembly=='roof' and p.material not in ('lumber','liner'):
            continue
        v = vertices(p,offset)
        ax.add_collection3d(Poly3DCollection([[v[i] for i in f] for f in FACES],
            facecolors='#cf663f' if p.name.startswith('Light-support') else COLORS[p.material],
            edgecolors='#4b5663',linewidths=0.25,alpha=0.95))
    ax.set(xlim=(0,c.width),ylim=(0,c.depth),zlim=(0,c.overall_height_in+14),
           xlabel='Width (in)',ylabel='Depth (in)',zlabel='Height (in)')
    ax.set_box_aspect((c.width,c.depth,c.overall_height_in+14))
    ax.view_init(elev=22,azim=-58)
    ax.set_title('Framing with door blank' if mode=='frame' else 'Cutaway • lid lifted 12 in',pad=15)
    ax.tick_params(labelsize=8)


def dimension(ax,a,b,label,vertical=False):
    ax.annotate('',xy=b,xytext=a,arrowprops={'arrowstyle':'|-|','color':'#35485b','lw':1})
    ax.text((a[0]+b[0])/2+(1 if vertical else 0),(a[1]+b[1])/2+(0 if vertical else 1),
            label,rotation=90 if vertical else 0,ha='left' if vertical else 'center',
            va='center' if vertical else 'bottom',fontsize=9,color='#24394b')


def write_drawings(c,parts,out):
    plt.rcParams.update({'font.family':'DejaVu Sans','font.size':10,'axes.spines.top':False,
                         'axes.spines.right':False,'figure.facecolor':'#fafbfc'})
    with PdfPages(out/'construction_study.pdf') as pdf:
        fig = plt.figure(figsize=(14,9))
        fig.suptitle(f'GARAGE GROW LAB  /  {c.interior_width_in:g} × {c.interior_depth_in:g} in interior',fontsize=19,fontweight='bold')
        for i,mode in enumerate(('frame','cutaway'),1):
            scene(fig.add_subplot(1,2,i,projection='3d'),c,parts,mode)
        fig.legend(handles=[Patch(color=COLORS[k],label=k.title()) for k in ('lumber','plywood','liner')]+
                   [Patch(color='#cf663f',label='Light-support crossmembers')],
                   loc='lower center',ncol=4,bbox_to_anchor=(0.5,0.055),frameon=False)
        fig.text(.5,.025,f'Outside {c.width:.2f} × {c.depth:.2f} × {c.overall_height_in:g} in  |  '
                 f'Clear height {c.clear_height:.2f} in  |  Slab-supported walls; folded liner floor; no slab anchors',ha='center')
        fig.subplots_adjust(top=.87,bottom=.16,left=.01,right=.97,wspace=.05)
        fig.savefig(out/'overview.png',dpi=160)
        pdf.savefig(fig); plt.close(fig)

        fig,axs = plt.subplots(1,2,figsize=(14,9))
        fig.suptitle('PLAN + FRONT FRAMING  /  dimensions in inches',fontsize=18,fontweight='bold')
        ax=axs[0]
        z=BASE+30
        for p in parts:
            if p.assembly=='door' or not p.origin[2]<=z<p.origin[2]+p.size[2]:
                continue
            ax.add_patch(Rectangle(p.origin[:2],*p.size[:2],facecolor=COLORS[p.material],edgecolor='#53616e',lw=.35))
        dl=(c.width-c.door_rough_width_in)/2
        ax.add_patch(Arc((dl,0),2*c.door_rough_width_in,2*c.door_rough_width_in,theta1=270,theta2=360,
                         linestyle='--',color='#68869e'))
        ax.plot([dl,dl],[0,-c.door_rough_width_in],color=COLORS['liner'],lw=3)
        dimension(ax,(WALL,c.depth*.38),(c.width-WALL,c.depth*.38),f'{c.interior_width_in:g} clear')
        dimension(ax,(c.width*.7,WALL),(c.width*.7,c.depth-WALL),f'{c.interior_depth_in:g} clear',True)
        dimension(ax,(0,c.depth+6),(c.width,c.depth+6),f'{c.width:.2f} outside')
        ax.text(c.width/2,-c.door_rough_width_in-5,'Allow outward door swing + working access',ha='center',fontsize=9)
        ax.set(xlim=(-8,c.width+8),ylim=(-c.door_rough_width_in-10,c.depth+12),aspect='equal',title='Plan at mid-wall; front at bottom')
        ax.axis('off')
        ax=axs[1]
        for p in parts:
            if (p.assembly=='front' and p.material=='lumber') or p.assembly=='threshold':
                ax.add_patch(Rectangle((p.origin[0],p.origin[2]),p.size[0],p.size[2],
                    facecolor=COLORS[p.material],edgecolor='#53616e',lw=.5))
        ax.add_patch(Rectangle((0,c.overall_height_in-ROOF),c.width,ROOF,fill=False,ls='--',ec='#687c8a'))
        dimension(ax,(dl,c.door_bottom+25),(dl+c.door_rough_width_in,c.door_bottom+25),f'{c.door_rough_width_in:g} rough')
        dimension(ax,(c.width/2,c.door_bottom),(c.width/2,c.door_bottom+c.door_rough_height_in),f'{c.door_rough_height_in:g} rough',True)
        dimension(ax,(c.width+6,0),(c.width+6,c.overall_height_in),f'{c.overall_height_in:g} overall',True)
        ax.text(c.width/2,-5,f'Curb top: {c.door_bottom:.2f} above slab\nLiner floor on concrete; no platform',ha='center',fontsize=9)
        ax.set(xlim=(-5,c.width+14),ylim=(-10,c.overall_height_in+10),aspect='equal',title='Front panel; door leaf hidden')
        ax.axis('off')
        fig.text(.5,.025,'Assembly dimensions, not a fastening schedule. Door jamb/hinge detail must be finalized before cutting the door blank.',ha='center',fontsize=10)
        fig.tight_layout(rect=(0,.06,1,.93))
        fig.savefig(out/'plan_elevation.png',dpi=160)
        pdf.savefig(fig); plt.close(fig)

        fig,axs=plt.subplots(1,3,figsize=(14,8))
        fig.suptitle('ENCLOSURE LAYERS  /  continuity at every joint',fontsize=18,fontweight='bold')
        assemblies=[('Wall: garage → grow space',[(SHEATH,'plywood','Plywood bracing'),(STUD,'insulation','2×3 frame + cavity insulation'),
                     (BOARD,'substrate','Moisture-resistant substrate'),(LINER,'liner','Sealed air/vapor-control liner')]),
                    ('Roof: top → grow space',[(ROOF_CAP,'plywood','Plywood cap; verify panel span'),(3.5,'insulation','2×4 crossmembers + insulation'),
                     (ROOF_BOARD,'substrate','Ceiling substrate; verify support'),(LINER,'liner','Continuous sealed liner')])]
        for ax,(title,layers) in zip(axs[:2],assemblies):
            y=0
            for t,mat,label in layers:
                display=max(t,.32)
                ax.add_patch(Rectangle((0,y),2.4,display,facecolor=COLORS[mat],edgecolor='#53616e'))
                ax.text(2.6,y+display/2,f'{t:g} in\n{label}',va='center',fontsize=9,wrap=True)
                y+=display
            ax.set(xlim=(-.2,8),ylim=(y+.4,-.4),title=title)
            ax.axis('off')
        ax=axs[2]
        ax.add_patch(Rectangle((-1,-.5),8,.5,fc='#cbd0d3',ec='#62717b'))
        ax.add_patch(Rectangle((0,SILL),STUD,1.5,fc=COLORS['lumber'],ec='#53616e'))
        ax.add_patch(Rectangle((0,0),STUD,SILL,fc=COLORS['isolation pad']))
        ax.plot([STUD,STUD],[1.7,5],color=COLORS['substrate'],lw=6)
        ax.plot([6,STUD+.15,STUD+.15],[.06,.06,c.tray_upstand_in],color=COLORS['liner'],lw=4)
        ax.text(3,.6,'Waterproof plastic\non concrete',fontsize=9)
        ax.text(2.9,c.tray_upstand_in-.3,f'{c.tray_upstand_in:g} in upstand\nFold corners; seal to wall',fontsize=9)
        ax.text(0,2,'Wall bottom plate',fontsize=9)
        ax.text(-.7,-1,'Thin sill gasket; no slab fasteners',fontsize=9)
        ax.set(xlim=(-1,7),ylim=(-1.5,5.5),title='Floor-to-wall fold (schematic)')
        ax.axis('off')
        fig.text(.07,.19,'Seal wall-to-ceiling transitions, corner seams, door stops and every hanging/service penetration.\n'
                 'Use one continuous floor liner with folded corners; wrap it over the supported doorway curb.\n'
                 'Keep attachments above the tray rim; protect the liner beneath equipment feet.\n'
                 'Wall plates rest on a thin capillary-break gasket on the slab. No raised floor or floor insulation.',fontsize=11,linespacing=1.6)
        fig.text(.07,.055,'Layer thicknesses labeled; thin layers exaggerated for readability. Cavity R values are not whole-wall R values.\n'
                 'Orange roof members are intended light supports; weights, connections and material capacities remain unverified.',fontsize=10)
        fig.subplots_adjust(top=.85,bottom=.34,wspace=.25)
        fig.savefig(out/'layer_sections.png',dpi=160)
        pdf.savefig(fig); plt.close(fig)

        fig,(ax,notes)=plt.subplots(1,2,figsize=(14,9),gridspec_kw={'width_ratios':[1,1.2]})
        fig.suptitle('ROOF + LIGHT SUSPENSION  /  short-span crossmembers',fontsize=18,fontweight='bold')
        for p in parts:
            if p.assembly=='roof' and p.material=='lumber':
                color='#cf663f' if p.name.startswith('Light-support') else COLORS['lumber']
                ax.add_patch(Rectangle(p.origin[:2],*p.size[:2],fc=color,ec='#53616e',lw=.6))
        for i,y in enumerate(c.roof_centers[1:-1],1):
            ax.text(c.width/2,y+2,f'Light-support member {i}',ha='center',fontsize=9)
            ax.scatter([WALL+c.interior_width_in*.25,WALL+c.interior_width_in*.75],[y,y],s=30,c='#24394b')
        dimension(ax,(WALL,c.depth+5),(c.width-WALL,c.depth+5),f'{c.interior_width_in:g} clear width')
        ax.set(xlim=(-5,c.width+6),ylim=(-5,c.depth+12),aspect='equal',title='Top view; dots indicate example hanger locations')
        ax.axis('off')
        max_gap=max(b-a for a,b in zip(c.roof_centers,c.roof_centers[1:]))
        notes.text(0,.95,f'{c.roof_crossmember_count} interior 2×4 crossmembers + 2 end members\n'
            f'Actual crossmember section: 1.5 × 3.5 in, on edge\n'
            f'Crossmember cut length: {c.width-2*SHEATH:.2f} in\n'
            f'Maximum roof support spacing: {max_gap:.2f} in\n\n'
            'Load path:\n'
            'Lights → rated hanging hardware → crossmember\n'
            '→ wall top plate → aligned side-wall stud → slab\n\n'
            'Crossmembers bear directly on wall top plates.\n'
            'Ceiling finish fits between walls below the framing.\n'
            'Use hangers fixed into structural wood, with sealed\n'
            'penetrations through the ceiling air/vapor liner.\n\n'
            'Verify roof and ceiling panel span/support requirements;\n'
            'add seam backing or light furring where required.\n'
            'Do not hang loads from panel skins or insulation.\n\n'
            f'Total hanging load: {"not supplied" if c.total_hanging_weight_lb is None else str(c.total_hanging_weight_lb)+" lb"}\n'
            'This layout is not yet load-rated. See summary.json\n'
            'for the editable beam-demand study and its limits.',va='top',fontsize=12,linespacing=1.55)
        notes.axis('off')
        fig.tight_layout(rect=(0,.03,1,.93))
        fig.savefig(out/'roof_supports.png',dpi=160)
        pdf.savefig(fig); plt.close(fig)


def static_preview(c,parts):
    """Visible HTML content even when a file preview disables JavaScript/canvas."""
    width,height=1100,700
    yaw,pitch=-.65,.35
    scale=min(width/(c.width+c.depth+65),height/(c.overall_height_in+60))
    def project(v):
        x,y,z=v[0]-c.width/2,v[1]-c.depth/2,v[2]-c.overall_height_in/2
        u=math.cos(yaw)*x-math.sin(yaw)*y
        depth=math.sin(yaw)*x+math.cos(yaw)*y
        return (width/2+u*scale,height/2-(z*math.cos(pitch)+depth*math.sin(pitch))*scale,
                depth*math.cos(pitch)-z*math.sin(pitch))
    faces=[]
    for p in parts:
        if p.material!='lumber' and p.assembly!='tray':
            continue
        vs=[project(v) for v in vertices(p)]
        for face in FACES:
            points=[vs[i] for i in face]
            coords=' '.join(f'{v[0]:.2f},{v[1]:.2f}' for v in points)
            color='#cf663f' if p.name.startswith('Light-support') else COLORS[p.material]
            polygon=f'<polygon points="{coords}" fill="{color}" stroke="#455363" stroke-width="0.6"/>'
            faces.append((sum(v[2] for v in points)/4,polygon))
    polygons='\n'.join(poly for _,poly in sorted(faces,key=lambda f:f[0],reverse=True))
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" '
            'role="img" aria-label="Grow lab framing, light-support crossmembers and folded liner floor">'
            '<title>Grow lab framing, light supports and folded floor liner</title>'+polygons+'</svg>')


def write_html(c,parts,out):
    data={'parts':[{'name':p.name,'assembly':p.assembly,'material':p.material,
                    'vertices':vertices(p)} for p in parts],
          'faces':FACES,'colors':COLORS,'width':c.width,'depth':c.depth,'height':c.overall_height_in}
    template=Path(__file__).with_name('viewer_template.html').read_text()
    preview=static_preview(c,parts)
    (out/'framing.svg').write_text(preview+'\n')
    html=template.replace('__STATIC_PREVIEW__',preview).replace('__MODEL_DATA__',json.dumps(data))
    html=html.replace('__DIMENSIONS__',f'Outside: {c.width:.2f} × {c.depth:.2f} × {c.overall_height_in:.2f} in')
    (out/'viewer.html').write_text(html)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config',type=Path,default=Path(__file__).with_name('config.json'))
    parser.add_argument('--out',type=Path,default=Path(__file__).with_name('output'))
    args=parser.parse_args()
    try:
        c=Config.read(args.config)
        parts=build(c)
        export(c,parts,args.out)
        write_drawings(c,parts,args.out)
        write_html(c,parts,args.out)
    except (ValueError,TypeError,OSError) as exc:
        parser.error(str(exc))
    print(f'Wrote drawings, viewer and schedules to {args.out.resolve()}')


if __name__=='__main__':
    main()
