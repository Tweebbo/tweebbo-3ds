declare const nativeSprite: (...args: number[]) => void;
declare const nativeRect: (...args: number[]) => void;
declare const nativeText: (text: string, x: number, y: number, size: number, color: number, align: number) => void;
declare const nativeMeasure: (text: string, size: number) => number;

export const rgb = (s: string) => {
  if (!s || !s.startsWith('#')) return 0xffffff;
  s = s.slice(1); if (s.length === 3) s = [...s].map(c => c + c).join('');
  return parseInt(s.slice(0, 6), 16);
};
export class NativeSprite {
  constructor(public id: number, public width: number, public height: number, public color: number) {}
}
type Draw = { image: NativeSprite | Canvas; x: number; y: number; sx: number; sy: number; alpha: number };
export class Canvas {
  width = 1; height = 1; className = ''; style = {};
  live = false;
  ops: Draw[] = [];
  ctx = new Context(this);
  getContext() { return this.ctx; }
}
class Context {
  sx = 1; sy = 1; tx = 0; ty = 0;
  globalAlpha = 1; fillStyle = '#ffffff'; strokeStyle = '#000000'; lineWidth = 1;
  font = '11px sans-serif'; textAlign = 'left'; textBaseline = 'alphabetic'; imageSmoothingEnabled = false;
  stack: number[][] = [];
  path: number[] = [];
  constructor(private canvas: Canvas) {}
  setTransform(a: number, _b: number, _c: number, d: number, e: number, f: number) { this.sx=a; this.sy=d; this.tx=e; this.ty=f; }
  translate(x: number, y: number) { this.tx+=x*this.sx; this.ty+=y*this.sy; }
  scale(x: number, y: number) { this.sx*=x; this.sy*=y; }
  save() { this.stack.push([this.sx,this.sy,this.tx,this.ty,this.globalAlpha]); }
  restore() { [this.sx,this.sy,this.tx,this.ty,this.globalAlpha]=this.stack.pop()!; }
  drawImage(image: NativeSprite | Canvas, x: number, y: number) {
    const op={image,x:this.tx+x*this.sx,y:this.ty+y*this.sy,sx:this.sx,sy:this.sy,alpha:this.globalAlpha};
    if (this.canvas.live) this.emit(op, [0,0,this.canvas.width,this.canvas.height]); else this.canvas.ops.push(op);
  }
  private emit(op: Draw, clip: number[]) {
    const {image,x,y,sx,sy,alpha}=op;
    if (image instanceof NativeSprite) { nativeSprite(image.id,x,y,sx,sy,image.color,alpha,...clip); return; }
    const bounds=[Math.min(x,x+image.width*sx),Math.min(y,y+image.height*sy),Math.max(x,x+image.width*sx),Math.max(y,y+image.height*sy)];
    const sub=[Math.max(clip[0],bounds[0]),Math.max(clip[1],bounds[1]),Math.min(clip[2],bounds[2]),Math.min(clip[3],bounds[3])];
    if(sub[0]>=sub[2]||sub[1]>=sub[3])return;
    for(const p of image.ops)this.emit({image:p.image,x:x+p.x*sx,y:y+p.y*sy,sx:sx*p.sx,sy:sy*p.sy,alpha:alpha*p.alpha},sub);
  }
  fillRect(x: number,y: number,w: number,h: number) { if(this.canvas.live)nativeRect(this.tx+x*this.sx,this.ty+y*this.sy,w*this.sx,h*this.sy,0,rgb(this.fillStyle),this.globalAlpha); }
  clearRect() { if(!this.canvas.live)this.canvas.ops=[]; }
  beginPath() { this.path=[]; }
  roundRect(x: number,y: number,w: number,h: number,r: number) { this.path=[this.tx+x*this.sx,this.ty+y*this.sy,w*this.sx,h*this.sy,r*this.sx]; }
  arc(x: number,y: number,r: number) { this.roundRect(x-r,y-r,r*2,r*2,r); }
  fill() { if(this.canvas.live&&this.path.length)nativeRect(...this.path,rgb(this.fillStyle),this.globalAlpha); }
  stroke() { /* Native chat uses an opaque white bubble, as in the hotel. */ }
  size() { return Math.max(10,Math.min(16,Number(/(\d+)px/.exec(this.font)?.[1]||11))); }
  measureText(text: string) { return {width:nativeMeasure(text,this.size())}; }
  fillText(text: string,x: number,y: number) {
    const size=this.size(), baseline=this.textBaseline==='middle'?size/2:this.textBaseline==='alphabetic'?size:0;
    nativeText(text,this.tx+x*this.sx,this.ty+y*this.sy-baseline,size,rgb(this.fillStyle),this.textAlign==='center'?1:this.textAlign==='right'?2:0);
  }
}
(globalThis as any).document={createElement:()=>new Canvas()};
(globalThis as any).window={devicePixelRatio:1};
