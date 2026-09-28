"""Modelos de visualizacion: coordenadas STEP x, -y de huella, z hacia arriba."""
from pathlib import Path
import sys, json
ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'.cadquery'))
import cadquery as cq
from cadquery.occ_impl.assembly import Color
OUT=ROOT/'revision_3d_2'/'0-Library.3dshapes'
OUT.mkdir(parents=True,exist_ok=True)
STD=Path(r'C:\Program Files\KiCad\10.0\share\kicad\3dmodels')
black=Color(.055,.06,.065); silver=Color(.7,.72,.74); gold=Color(.83,.65,.23)
red=Color(.62,.045,.035); blue=Color(.045,.19,.52); tan=Color(.59,.42,.23)
white=Color(.88,.88,.84); copper=Color(.65,.26,.07)
def box(w,d,h,x=0,y=0,z=0): return cq.Workplane('XY').box(w,d,h).translate((x,y,z))
def cyl(r,h,x=0,y=0,z=0): return cq.Workplane('XY').circle(r).extrude(h).translate((x,y,z))
def add(a,s,n,c): a.add(s,name=n,color=c)
def label(a,t,x,y,z,size=.8):
 s=cq.Workplane('XY').text(t,size,.015,combine=True,font='Arial').translate((x,y,z))
 add(a,s,'label_'+str(len(a.objects)),white)
def save(a,name):
 a.export(str(OUT/name))
 solids=cq.importers.importStep(str(OUT/name)).solids().vals()
 assert solids and all(s.isValid() for s in solids)
 print(name,'valid solids',len(solids),flush=True)
def header(a,x,y,i):
 # 2.54 mm moulding and 0.64 mm square post, same mounting style as Pico headers.
 shell=box(2.5,2.5,2.54,x,y,1.27).cut(box(.70,.70,3,x,y,1.27))
 post=box(.64,.64,7.0,x,y,.5)
 add(a,shell,f'header_{i}',black); add(a,post,f'post_{i}',gold)
def module_tht_pad(a,x,y,i):
 # Pad propio del modulo, como los agujeros metalizados del Raspberry Pi Pico:
 # anillo en ambas caras, barril metalizado y pin cuadrado atravesando la placa.
 hole_r=.46; ring_r=.88
 top=cyl(ring_r,.07,x,y,3.74).cut(cyl(hole_r,.12,x,y,3.70))
 bottom=cyl(ring_r,.07,x,y,2.47).cut(cyl(hole_r,.12,x,y,2.45))
 barrel=cyl(hole_r,1.20,x,y,2.54).cut(cyl(.35,1.24,x,y,2.52))
 pin=box(.60,.60,5.0,x,y,1.50)
 add(a,top,f'pad_top_{i}',gold); add(a,bottom,f'pad_bottom_{i}',gold)
 add(a,barrel,f'barrel_{i}',gold); add(a,pin,f'pin_{i}',gold)
def smd(a,x,y,z,w,d,i):
 add(a,box(w,d,.65,x,y,z+.325),f'ceramic_{i}',tan)
 for j,sgn in enumerate([-1,1]): add(a,box(.35,d,.7,x+sgn*(w/2-.175),y,z+.35),f'term_{i}_{j}',silver)

def to220():
 a=cq.Assembly(name='FQP30N06L_P3_81')
 standard=cq.importers.importStep(str(STD/'Package_TO_SOT_THT.3dshapes/TO-220-3_Vertical.step'))
 upper=standard.intersect(box(40,40,40,2.54,0,23.5))
 # Preserve the exact installed KiCad encapsulation, flange, bevels and hole.
 metal_mask=box(40,10,40,2.54,6.89,23.5).union(box(40,40,20,2.54,0,23.3))
 metal=upper.intersect(metal_mask)
 plastic=upper.cut(metal_mask)
 add(a,metal,'exposed_tab',silver); add(a,plastic,'moulded_body',black)
 for i,(hx,ex) in enumerate(zip([-1.27,2.54,6.35],[0,2.54,5.08]),1):
  lead=box(.8,.5,3.05,hx,0,-.475)
  bend=(cq.Workplane('XY').workplane(offset=1.0).center(hx,0).rect(.8,.5)
        .workplane(offset=2.65).center(ex-hx,0).rect(.8,.5).loft())
  add(a,lead.union(bend),f'lead_{i}',silver)
 text=cq.Workplane('XZ').text('FQP30N06L',.8,.012,font='Arial').translate((2.54,-1.30,9))
 add(a,text,'marking',white)
 save(a,'TO-220-3_Vertical_P3.81mm.step')

def ads():
 a=cq.Assembly(name='ADS1115_1x10')
 pcb=box(18.27,25.4,1.2,7.865,-11.43,3.14)
 for i in range(10):
  y=-i*2.54; pcb=pcb.cut(cyl(.45,2,0,y,2.4)); header(a,0,y,i)
  ring=cyl(.85,.06,0,y,3.74).cut(cyl(.45,.1,0,y,3.72)); add(a,ring,f'pad_{i}',gold)
 add(a,pcb,'module_pcb',blue)
 add(a,box(3,3,1,9,-11.43,4.26),'ADS1115',black)
 for side in [-1,1]:
  for i in range(5): add(a,box(.7,.23,.18,9+side*1.7,-10.43-i*.5,3.9),f'IC_{side}_{i}',silver)
 for i,(x,y) in enumerate([(6,-4),(13,-5),(13,-17),(5,-18)]): smd(a,x,y,3.75,2,1,i)
 label(a,'ADS1115',9,-8,3.76,1.2); label(a,'16 BIT ADC',9,-20.5,3.76,.9)
 save(a,'ADS1115_Modulo_1x10.step')

def s09():
 a=cq.Assembly(name='S09_5V_headers')
 pcb=box(19,14,1.2,0,0,3.14)
 # Exact footprint coordinates with Y inverted for STEP, including EN.
 pins=[(7.9,4),(7.9,-4),(-7.9,-4),(-7.9,4),(7.9,6.54)]
 for i,(x,y) in enumerate(pins):
  pcb=pcb.cut(cyl(.46,2,x,y,2.4)); module_tht_pad(a,x,y,i)
 add(a,pcb,'red_pcb',red)
 for i,(x,y) in enumerate([(-3.0,2.7),(3,-2.5)]):
  add(a,cyl(2.55,.55,x,y,3.75).union(cyl(1.6,2.3,x,y,4.0)).union(cyl(2.55,.55,x,y,6.0)),f'ferrite_{i}',black)
  for j in range(7):
   ring=cyl(2.12,.20,x,y,4.35+j*.22).cut(cyl(1.60,.23,x,y,4.34+j*.22))
   add(a,ring,f'winding_{i}_{j}',copper)
 add(a,box(2.7,3.4,.9,1.8,2.8,4.2),'controller',black)
 for side in [-1,1]:
  for i in range(4): add(a,box(.8,.3,.2,1.8+side*1.65,1.65+i*.76,3.95),f'IC_{side}_{i}',silver)
 for i,(x,y) in enumerate([(-4.7,-3.2),(5.2,1.0)]): smd(a,x,y,3.75,2.5,1.4,i)
 add(a,box(2.8,1.6,.85,-1.2,-2.9,4.18),'schottky',black)
 add(a,box(.3,1.6,.02,-2.1,-2.9,4.62),'diode_band',white)
 label(a,'5V',-4.8,-5.7,3.76,.9); label(a,'VIN',4.6,5.5,3.76,.8)
 save(a,'S09_5V_BuckBoost_19x14mm.step')

def terminal():
 a=cq.Assembly(name='Terminal_2P_P5_visual')
 shell=box(10.5,7.8,9,2.25,.1,4.65)
 for i,x in enumerate([0,5]):
  shell=shell.cut(cyl(1.75,3,x,.1,6.2))
  mouth=box(3.5,3.2,3.2,x,-2.5,3.6)
  shell=shell.cut(mouth)
  screw=cyl(1.5,.7,x,.1,7.3).cut(box(.55,3.1,.35,x,.1,7.92))
  add(a,screw,f'screw_{i}',silver)
  add(a,box(.8,.8,3.5,x,0,-1.3),f'pin_{i}',silver)
  add(a,box(2.8,.4,2.4,x,-1.05,3.5),f'clamp_{i}',silver)
 add(a,shell,'housing',Color(.045,.25,.65))
 save(a,'Terminal_2P_P5_visual.step')

if __name__=='__main__':
 to220(); ads(); s09(); terminal()
