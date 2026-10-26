declare const nativeSprite:(...args:number[])=>void;
declare const nativeHead:(...args:number[])=>void;
declare const nativeRect:(...args:number[])=>void;
declare const nativeText:(text:string,...args:number[])=>void;
declare const nativeMeasure:(text:string,size:number)=>number;
type Matrix=[number,number,number,number,number,number];
const identity=():Matrix=>[1,0,0,1,0,0];
const multiply=(a:Matrix,b:Matrix):Matrix=>[a[0]*b[0]+a[2]*b[1],a[1]*b[0]+a[3]*b[1],a[0]*b[2]+a[2]*b[3],a[1]*b[2]+a[3]*b[3],a[0]*b[4]+a[2]*b[5]+a[4],a[1]*b[4]+a[3]*b[5]+a[5]];
const point=(m:Matrix,x:number,y:number)=>[m[0]*x+m[2]*y+m[4],m[1]*x+m[3]*y+m[5]];
const color=(s:string)=>{if(!s.startsWith('#'))throw Error('Unsupported native Fast Food color');s=s.slice(1);if(s.length===3)s=[...s].map(c=>c+c).join('');return parseInt(s,16);};
export class Sprite {constructor(readonly ids:number[],readonly width:number,readonly height:number,readonly resolution:number) {}}
export class Head {width=64;height=112;constructor(readonly look:number) {}}
type Image=Sprite|Head|Canvas;
type Draw={image:Image,matrix:Matrix,clip:number[],alpha:number};
export class Canvas {
  width=1;height=1;live=false;ops:Draw[]=[];
  private context=new Context(this);
  getContext(){return this.context;}
}
class Context {
  private matrix=identity();
  private stack:{matrix:Matrix,alpha:number,fill:string,font:string,align:string,baseline:string}[]=[];
  globalAlpha=1;fillStyle='#fff';font='11px sans-serif';textAlign='left';textBaseline='alphabetic';imageSmoothingEnabled=true;
  constructor(private canvas:Canvas){}
  setTransform(...matrix:Matrix){this.matrix=matrix;}
  translate(x:number,y:number){this.matrix=multiply(this.matrix,[1,0,0,1,x,y]);}
  scale(x:number,y:number){this.matrix=multiply(this.matrix,[x,0,0,y,0,0]);}
  rotate(radians:number){const quarter=Math.round(radians/(Math.PI/2));if(Math.abs(radians-quarter*Math.PI/2)>1e-6)throw Error('Native Fast Food rotation must be a quarter turn');const q=(quarter%4+4)%4,c=[1,0,-1,0][q],s=[0,1,0,-1][q];this.matrix=multiply(this.matrix,[c,s,-s,c,0,0]);}
  save(){this.stack.push({matrix:[...this.matrix],alpha:this.globalAlpha,fill:this.fillStyle,font:this.font,align:this.textAlign,baseline:this.textBaseline});}
  restore(){const v=this.stack.pop();if(!v)return;this.matrix=v.matrix;this.globalAlpha=v.alpha;this.fillStyle=v.fill;this.font=v.font;this.textAlign=v.align;this.textBaseline=v.baseline;}
  drawImage(image:Image,...args:number[]){
    let sx=0,sy=0,sw=image.width,sh=image.height,dx:number,dy:number,dw=sw,dh=sh;
    if(args.length===2)[dx,dy]=args;
    else if(args.length===4)[dx,dy,dw,dh]=args;
    else if(args.length===8)[sx,sy,sw,sh,dx,dy,dw,dh]=args;
    else throw Error('Unsupported native drawImage signature');
    if(!sw||!sh||!dw||!dh)return;
    const m=multiply(this.matrix,[dw/sw,0,0,dh/sh,dx!-sx*dw/sw,dy!-sy*dh/sh]);
    const p=point(m,sx,sy),q=point(m,sx+sw,sy+sh),clip=[Math.min(p[0],q[0]),Math.min(p[1],q[1]),Math.max(p[0],q[0]),Math.max(p[1],q[1])];
    const op={image,matrix:m,clip,alpha:this.globalAlpha};
    if(this.canvas.live)this.emit(op,[0,0,this.canvas.width,this.canvas.height]);else this.canvas.ops.push(op);
  }
  private emit(op:Draw,outer:number[]){
    const clip=[Math.max(outer[0],op.clip[0]),Math.max(outer[1],op.clip[1]),Math.min(outer[2],op.clip[2]),Math.min(outer[3],op.clip[3])];
    if(clip[0]>=clip[2]||clip[1]>=clip[3])return;
    const {image,matrix:m,alpha}=op;
    if(image instanceof Canvas){for(const child of image.ops){const a=point(m,child.clip[0],child.clip[1]),b=point(m,child.clip[2],child.clip[3]);this.emit({image:child.image,matrix:multiply(m,child.matrix),clip:[Math.min(a[0],b[0]),Math.min(a[1],b[1]),Math.max(a[0],b[0]),Math.max(a[1],b[1])],alpha:alpha*child.alpha},clip);}return;}
    if(image instanceof Head){nativeHead(image.look,m[4],m[5],m[0],...clip);return;}
    const quarter=m[1]!==0?(m[1]>0?1:3):m[0]<0&&m[3]<0?2:0;
    const rotated=quarter!==0,id=image.ids[quarter]??image.ids[0];
    if(rotated&&image.ids.length!==4)throw Error('Rotated sprite was not packed');
    const p=point(m,0,0),q=point(m,image.width,image.height);
    let sx=(Math.abs(m[0])||Math.abs(m[1]))*image.resolution,sy=(Math.abs(m[3])||Math.abs(m[2]))*image.resolution;
    if(quarter%2)[sx,sy]=[sy,sx];
    const flip=!rotated&&m[0]<0;
    nativeSprite(id,flip?Math.max(p[0],q[0]):Math.min(p[0],q[0]),Math.min(p[1],q[1]),flip?-sx:sx,sy,0xffffff,alpha,...clip);
  }
  fillRect(x:number,y:number,w:number,h:number){const p=point(this.matrix,x,y),q=point(this.matrix,x+w,y+h);if(this.canvas.live)nativeRect(p[0],p[1],q[0]-p[0],q[1]-p[1],0,color(this.fillStyle),this.globalAlpha);}
  private size(){return Math.max(8,Math.min(24,Number(/(\d+)px/.exec(this.font)?.[1]||11)*Math.abs(this.matrix[3])));}
  measureText(text:string){return{width:nativeMeasure(text,this.size())/Math.abs(this.matrix[0])};}
  fillText(text:string,x:number,y:number){const p=point(this.matrix,x,y),size=this.size();nativeText(text,p[0],p[1]-(this.textBaseline==='alphabetic'?size:this.textBaseline==='middle'?size/2:0),size,color(this.fillStyle),this.textAlign==='center'?1:this.textAlign==='right'?2:0);}
}
(globalThis as any).document={createElement:()=>new Canvas()};
(globalThis as any).devicePixelRatio=1;
