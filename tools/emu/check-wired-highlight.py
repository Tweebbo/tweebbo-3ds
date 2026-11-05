#!/usr/bin/env python3
"""Compare opaque plant pixels in actual Old 3DS captures with official AIR matrices."""
import json
from pathlib import Path
from PIL import Image

ROOT=Path(__file__).resolve().parents[3]
folder=ROOT/'hotel/docs/expansion/old3ds-wired-highlight'
baseline=Image.open(folder/'old3ds-wired-baseline.png').convert('RGB')
red=Image.open(folder/'old3ds-wired-red-tint.png').convert('RGB')
# Actual visible Yukka plant at the right edge of this fixed Azahar top-screen capture.
# The mask uses only independent red-vs-baseline change and excludes antialiased edges.
roi=(485,193,533,260)
changed=set()
for y in range(roi[1],roi[3]):
    for x in range(roi[0],roi[2]):
        if max(abs(a-b)for a,b in zip(baseline.getpixel((x,y)),red.getpixel((x,y))))>10:
            changed.add((x,y))
opaque=[(x,y)for x,y in sorted(changed)if all((x+dx,y+dy)in changed for dx in(-1,0,1)for dy in(-1,0,1))]
assert len(opaque)>=250,'The visible test plant was moved, occluded or not highlighted.'

bright=Image.open(folder/'old3ds-wired-brighten.png').convert('RGB')
calibration=next(point for point in opaque if baseline.getpixel(point)==(0,0,0))
offset=bright.getpixel(calibration)
phase=min(range(0,76,5),key=lambda strength:sum(abs(round(36*strength/100)-channel)for channel in offset))
assert all(abs(round(36*phase/100)-channel)<=1 for channel in offset)

def matrix(rgb,kind):
    r,g,b=rgb
    if kind==3:return(r+47.25,.043875*r+.1225*g,.043875*r+.1225*b)
    if kind==4:return tuple(channel*(1+.78*phase/100)+36*phase/100 for channel in rgb)
    return tuple(channel*.675 for channel in rgb)

report={'status':'passed','scope':'Actual private 3DSX in Old 3DS/2DS Azahar emulation; source pixel comparison, not physical hardware acceptance','roi':list(roi),'mask':'Red-vs-baseline delta greater than 10, eroded by one pixel in all eight directions; excludes antialiased and partial-alpha edge pixels','opaquePixels':len(opaque),'tolerancePerChannel':8,'source':'Official WIN63-202611041140-327405090 FurnitureHighlightFilterFactory.as; five-percent quantization','brightenPulseSample':{'wireStrength':76,'animation':1,'sampledFactoryStrength':phase,'calibrationPoint':list(calibration),'calibrationRGB':list(offset),'method':'An independent opaque black contour pixel establishes the additive offset; all other RGB samples verify the corresponding multiplier. Source Brighten always pulses; this capture is not a constant-strength 75 test.'},'results':{}}
for name,kind in [('red-tint',3),('brighten',4),('darken',2)]:
    actual=Image.open(folder/('old3ds-wired-'+name+'.png')).convert('RGB');errors=[];samples=[]
    for index,point in enumerate(opaque):
        if kind==4 and point==calibration:continue
        expected=tuple(max(0,min(255,round(channel)))for channel in matrix(baseline.getpixel(point),kind))
        observed=actual.getpixel(point)
        errors.extend(abs(a-b)for a,b in zip(expected,observed))
        if index in [0,len(opaque)//3,len(opaque)//2,len(opaque)-1]:samples.append({'point':list(point),'baseline':list(baseline.getpixel(point)),'expected':list(expected),'actual':list(observed)})
    ordered=sorted(errors);result={'maximumChannelError':max(errors),'meanChannelError':round(sum(errors)/len(errors),4),'p95ChannelError':ordered[int(len(ordered)*.95)],'samples':samples};report['results'][name]=result
    assert result['maximumChannelError']<=8,(name,result)
restored=Image.open(folder/'old3ds-wired-restored.png').convert('RGB')
report['restoredPlantMaximumChannelError']=max(abs(a-b)for point in opaque for a,b in zip(baseline.getpixel(point),restored.getpixel(point)))
assert report['restoredPlantMaximumChannelError']==0
ui=(173,260,493,498)
reference=baseline.crop(ui).tobytes()
report['uiRegion']=list(ui)
report['uiUnchanged']=all(Image.open(folder/('old3ds-wired-'+name+'.png')).convert('RGB').crop(ui).tobytes()==reference for name in ['red-tint','brighten','darken','restored'])
assert report['uiUnchanged'],'Highlight changed the ordinary bottom-screen interface.'
(folder/'pixel-checks.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({key:report[key]for key in ['status','opaquePixels','results','restoredPlantMaximumChannelError','uiUnchanged']}))
