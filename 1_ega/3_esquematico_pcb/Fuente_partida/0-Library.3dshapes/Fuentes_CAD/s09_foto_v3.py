"""S09 reference-photo visualization. Pin centers follow existing PCB.
Board envelope and parts are photographic approximations, not manufacturing CAD.
"""
import modelos_revision2 as m
from modelos_revision2 import cq,box,cyl,add,label,smd,Color

name='S09_5V_Pads_Borde_v3.step'
pins=[(7.9,4),(7.9,-4),(-7.9,-4),(-7.9,4),(7.9,6.54)]
a=cq.Assembly(name='S09_photo_edge_pads_v3')
# Hole-row spacing fixed by user's footprint. Photo places board asymmetrically
# around the two main rows; upper fifth terminal remains inside the laminate.
pcb=box(19,14,1.2,0,1.30,3.14)
metal=Color(.78,.79,.77)
for i,(x,y) in enumerate(pins):
    edge=9.5 if x>0 else -9.5
    hole=cyl(.65,2,x,y,2.35)
    scallop=cyl(.56,2,edge,y,2.35)
    pcb=pcb.cut(hole).cut(scallop)
    for face,z in [('top',3.74),('bottom',2.48)]:
        land=cyl(1.13,.06,x,y,z).union(box(abs(edge-x),2.26,.06,(edge+x)/2,y,z+.03))
        land=land.cut(cyl(.60,.2,x,y,z-.05)).cut(cyl(.51,.2,edge,y,z-.05))
        add(a,land,f'{face}_edge_land_{i}',metal)
    barrel=cyl(.65,1.2,x,y,2.54).cut(cyl(.60,1.24,x,y,2.52))
    notch=cyl(.56,1.2,edge,y,2.54).cut(cyl(.51,1.24,edge,y,2.52))
    notch=notch.intersect(box(19,14,2,0,1.30,3.14))
    add(a,barrel,f'hole_wall_{i}',metal);add(a,notch,f'castellation_{i}',metal)
    add(a,box(.60,.60,6,x,y,1),f'mounting_pin_{i}',m.gold)
add(a,pcb,'red_laminate',Color(.72,.07,.04))
# Two shielded drum inductors side by side, matching supplied photograph.
for i,x in enumerate([-3.9,2.0]):
    y=5.28
    add(a,box(5.3,4.9,.18,x,y,3.85),f'inductor_base_{i}',metal)
    core=box(4.7,4.5,1.55,x,y,4.8).edges('|Z').fillet(1.05)
    add(a,core,f'inductor_body_{i}',Color(.21,.23,.25))
    for side in [-1,1]:
        add(a,box(.35,.8,.6,x+side*2.35,y,4.2),f'inductor_terminal_{i}_{side}',metal)
for i,(x,y,w,d) in enumerate([(-7.8,-.7,1.5,3.2),(7.8,.25,1.5,3.2),(-1.0,1.5,3.0,1.5)]):
    smd(a,x,y,3.75,w,d,i)
add(a,box(2.5,4.2,1.1,-5.4,-2,4.31),'SS24_body',m.black)
for j,y in enumerate([.35,-4.35]):add(a,box(2.2,.6,.15,-5.4,y,3.86),f'SS24_lead_{j}',metal)
add(a,box(2.5,.28,.02,-5.4,-.38,4.87),'SS24_band',m.silver)
label(a,'SS24',-5.4,-2.1,4.88,.75)
add(a,box(2.0,2.8,.75,4.5,.85,4.15),'controller',m.black)
for side in [-1,1]:
    for j in range(4):add(a,box(.75,.25,.2,4.5+side*1.3,-.2+j*.67,3.9),f'ic_lead_{side}_{j}',metal)
for i,(x,y) in enumerate([(-2.0,-2.0),(1.8,-3.8),(4.9,-3.8),(6.2,6.9)]):
    smd(a,x,y,3.75,1.1,.65,10+i)
for i,(x,y) in enumerate([(-3,-3.5),(-1.4,-3.5),(.1,-3.5),(-3,-4.9),(-1.4,-4.9),(.1,-4.9)]):
    add(a,box(.9,.55,.12,x,y,3.85),f'configuration_land_{i}',metal)
m.save(a,name)
# Verify actual pin sections, independently of the visible lands.
model=cq.importers.importStep(str(m.OUT/name))
slab=model.intersect(box(40,40,.04,0,0,-.5))
centers=[((s.BoundingBox().xmin+s.BoundingBox().xmax)/2,(s.BoundingBox().ymin+s.BoundingBox().ymax)/2) for s in slab.solids().vals()]
assert len(centers)==5
for x,y in pins:assert any(abs(x-u)<.001 and abs(y-v)<.001 for u,v in centers)
print('PASS: 5 mounting pins centered; large edge lands and castellations generated',flush=True)
