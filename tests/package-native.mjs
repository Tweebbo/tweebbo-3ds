import assert from 'node:assert/strict';
import {readFile,writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import path from 'node:path';
const sha=bytes=>createHash('sha256').update(bytes).digest('hex'),align=(n,a=64)=>Math.ceil(n/a)*a;
function check(value,message){assert(value,message);}
function region(bytes,offset,size,label){check(Number.isSafeInteger(offset)&&Number.isSafeInteger(size)&&offset>=0&&size>=0&&offset+size<=bytes.length,label+' outside file');return bytes.subarray(offset,offset+size);}
function signatureBody(bytes){const type=bytes.readUInt32BE(0),offset=({0x10000:0x240,0x10001:0x140,0x10002:0x80,0x10003:0x240,0x10004:0x140,0x10005:0x80})[type];check(offset,'Unknown signature type');return offset;}
function title(bytes,offset,little=false){return (little?bytes.readBigUInt64LE(offset):bytes.readBigUInt64BE(offset)).toString(16).padStart(16,'0').toUpperCase();}
function romfsFiles(bytes,label){
 let start=0;
 if(bytes.toString('ascii',0,4)==='IVFC'){
  const block=bytes.readUInt32LE(0x4c);check(block>=9&&block<=20,label+' invalid IVFC block size');start=align(0x60+bytes.readUInt32LE(8),2**block);
 }
 check(bytes.readUInt32LE(start)===0x28,label+' invalid RomFS level3 header');
 const data=bytes.subarray(start),dirOffset=data.readUInt32LE(0xc),dirSize=data.readUInt32LE(0x10),fileOffset=data.readUInt32LE(0x1c),fileSize=data.readUInt32LE(0x20),payload=data.readUInt32LE(0x24);
 const dirs=region(data,dirOffset,dirSize,label+' directory table'),files=region(data,fileOffset,fileSize,label+' file table'),result=new Map(),seenDirs=new Set(),seenFiles=new Set();
 function component(table,offset,length){check(!(length&1),'Invalid UTF16 name');const name=region(table,offset,length,'RomFS name').toString('utf16le');check(!name.includes('/')&&name!=='.'&&name!=='..','Unsafe RomFS component');return name;}
 function walk(offset,parent){
  check(!seenDirs.has(offset),'RomFS directory cycle');seenDirs.add(offset);region(dirs,offset,24,'RomFS directory');
  const name=component(dirs,offset+24,dirs.readUInt32LE(offset+20)),prefix=parent+(name?name+'/':'');let child=dirs.readUInt32LE(offset+12);
  while(child!==0xffffffff){check(!seenFiles.has(child),'RomFS file cycle');seenFiles.add(child);region(files,child,32,'RomFS file');
   const file=prefix+component(files,child+32,files.readUInt32LE(child+28)),position=Number(files.readBigUInt64LE(child+8)),size=Number(files.readBigUInt64LE(child+16));
   check(file&&!result.has(file),'Duplicate/empty RomFS file');const content=region(data,payload+position,size,'RomFS file data');result.set(file,{bytes:content.length,sha256:sha(content)});child=files.readUInt32LE(child+4);
  }
  child=dirs.readUInt32LE(offset+8);while(child!==0xffffffff){walk(child,prefix);child=dirs.readUInt32LE(child+4);}
 }
 walk(0,'');return result;
}
export function inspectCia(bytes,expectedTitle,names={internal:'GoPengui',app:'GoPenguin Dev'}){
 check(bytes.length>=0x2020,'Truncated CIA');const header=bytes.readUInt32LE(0),cert=bytes.readUInt32LE(8),ticket=bytes.readUInt32LE(12),tmd=bytes.readUInt32LE(16),meta=bytes.readUInt32LE(20),contentSize=Number(bytes.readBigUInt64LE(24));
 check(header===0x2020,'Unexpected CIA header size');const certOffset=align(header),ticketOffset=align(certOffset+cert),tmdOffset=align(ticketOffset+ticket),contentOffset=align(tmdOffset+tmd),metaOffset=align(contentOffset+contentSize);
 region(bytes,certOffset,cert,'CIA certificates');const ticketBytes=region(bytes,ticketOffset,ticket,'CIA ticket'),tmdBytes=region(bytes,tmdOffset,tmd,'CIA TMD'),content=region(bytes,contentOffset,contentSize,'CIA content');region(bytes,metaOffset,meta,'CIA metadata');
 check(bytes.length===metaOffset+meta,'Unexpected trailing CIA bytes');check(title(ticketBytes,signatureBody(ticketBytes)+0x9c)===expectedTitle,'Wrong CIA ticket TitleID');const tmdBody=signatureBody(tmdBytes);check(title(tmdBytes,tmdBody+0x4c)===expectedTitle,'Wrong TMD TitleID');check(tmdBytes.readUInt16BE(tmdBody+0x9e)===1,'Expected one CIA content');
 const chunk=tmdBody+0x9c4;check(Number(tmdBytes.readBigUInt64BE(chunk+8))===contentSize,'TMD content size mismatch');check(!(tmdBytes.readUInt16BE(chunk+6)&1),'Unexpected encrypted CIA content');check(tmdBytes.subarray(chunk+16,chunk+48).toString('hex')===sha(content),'CIA content hash mismatch');
 if(manifest.titleVersion!==undefined)check(tmdBytes.readUInt16BE(tmdBody+0x9c)===manifest.titleVersion,'Wrong installed CIA version');
 check(content.toString('ascii',0x100,0x104)==='NCCH','Missing NCCH');check(content.readUInt32LE(0x104)*512===content.length,'NCCH media size mismatch');check(title(content,0x118,true)===expectedTitle,'Wrong NCCH program ID');check(content[0x18f]&4,'Expected unencrypted development NCCH');
 const exheader=region(content,0x200,content.readUInt32LE(0x180),'NCCH exheader');check(exheader.toString('ascii',0,8)===names.internal,'Wrong internal title');check(title(exheader,0x200,true)===expectedTitle,'Wrong exheader program ID');check(exheader[0x20e]>>4===0,'Expected 64MB Old 3DS mode');check((exheader[0x20d]&15)===0,'Expected Legacy memory extension');check((exheader[0x20e]&3)===0&&((exheader[0x20e]>>2)&3)===1,'Expected core0/affinity1');check((exheader[0x20c]&3)===3,'New3DS clock/L2 flags missing');
 check(sha(exheader)===content.subarray(0x160,0x180).toString('hex'),'Exheader hash mismatch');const services=[];for(let i=0;i<34;i++)services.push(exheader.toString('ascii',0x250+i*8,0x258+i*8).replace(/\0.*$/,''));for(const service of ['APT:U','soc:U','ssl:C','dsp::DSP','fs:USER','gsp::Gpu'])check(services.includes(service),'Missing service '+service);
 check(services.includes('am:net'),'Missing updater installation service');
 const exefs=region(content,content.readUInt32LE(0x1a0)*512,content.readUInt32LE(0x1a4)*512,'ExeFS'),entries=new Map();
 for(let i=0;i<10;i++){const at=i*16,name=exefs.toString('ascii',at,at+8).replace(/\0.*$/,'');if(!name)continue;const file=region(exefs,0x200+exefs.readUInt32LE(at+8),exefs.readUInt32LE(at+12),'ExeFS '+name);check(sha(file)===exefs.subarray(0xc0+(9-i)*32,0xe0+(9-i)*32).toString('hex'),'ExeFS hash mismatch '+name);entries.set(name,file);}
 for(const name of ['.code','icon','banner'])check(entries.has(name),'Missing ExeFS '+name);check(entries.get('icon').toString('ascii',0,4)==='SMDH','Missing original icon');check(entries.get('icon').toString('utf16le',0x208,0x288).replace(/\0.*$/,'')===names.app,'Wrong English app title');check(entries.get('banner').toString('ascii',0,4)==='CBMD','Missing CIA banner');
 const romfs=region(content,content.readUInt32LE(0x1b0)*512,content.readUInt32LE(0x1b4)*512,'NCCH RomFS');check(romfs.toString('ascii',0,4)==='IVFC','Missing IVFC RomFS');return {romfs:romfsFiles(romfs,'CIA'),contentOffset,contentSize,services,old3dsMode:'64MB Legacy'};
}
function inspect3dsx(bytes){check(bytes.toString('ascii',0,4)==='3DSX','Missing 3DSX header');check(bytes.readUInt16LE(4)>=44,'3DSX extended header missing');const romfsOffset=bytes.readUInt32LE(0x28),smdh=region(bytes,bytes.readUInt32LE(0x20),bytes.readUInt32LE(0x24),'3DSX icon');check(smdh.toString('ascii',0,4)==='SMDH','3DSX embedded icon missing');return romfsFiles(region(bytes,romfsOffset,bytes.length-romfsOffset,'3DSX RomFS'),'3DSX');}
const folder=path.resolve(process.argv[2]??'build/package-candidate'),manifest=JSON.parse(await readFile(folder+'/manifest.json','utf8')),inventory=JSON.parse(await readFile(path.resolve(folder,'../..',manifest.snapshot,'romfs-inventory.json'),'utf8'));
const cia=await readFile(folder+'/'+manifest.artifacts.cia.file),homebrew=await readFile(folder+'/'+manifest.artifacts['3dsx'].file),parsed=inspectCia(cia,manifest.titleId,{internal:manifest.internalTitle??'GoPengui',app:manifest.title}),homebrewFiles=inspect3dsx(homebrew);
for(const [format,files] of [['CIA',parsed.romfs],['3DSX',homebrewFiles]]){check(files.size===inventory.length,`${format} RomFS file count mismatch: ${files.size}/${inventory.length}; missing=${inventory.filter(item=>!files.has(item.file)).map(item=>item.file).join(',')}`);for(const item of inventory){const actual=files.get(item.file);check(actual&&actual.bytes===item.bytes&&actual.sha256===item.sha256,format+' embedded RomFS mismatch: '+item.file);}}
for(const item of Object.values(manifest.artifacts)){const bytes=await readFile(folder+'/'+item.file);check(bytes.length===item.bytes&&sha(bytes)===item.sha256,'Artifact fingerprint mismatch '+item.file);}
const corrupt=Buffer.from(cia);corrupt[parsed.contentOffset+parsed.contentSize-1]^=1;assert.throws(()=>inspectCia(corrupt,manifest.titleId),/hash mismatch/);assert.throws(()=>inspectCia(cia.subarray(0,cia.length-128),manifest.titleId),/outside file/);
const report={status:'PASS',titleId:manifest.titleId,old3dsMode:parsed.old3dsMode,filesCompared:inventory.length,romfsBytes:manifest.romfsBytes,checks:['CIA ticket/TMD/NCCH/exheader identity','CIA content integrity and truncation rejection','Old3DS 64MB Legacy/core0 capabilities','required network/DSP/filesystem services','ExeFS code/icon/banner hashes and app name','all CIA and3DSX RomFS files byte-identical to input snapshot','artifact fingerprints'],limits:['Structural packaging validation; no installation, emulator run or physical acceptance.']};
await writeFile(folder+'/validation.json',JSON.stringify(report,null,2)+'\n');console.log('PASS native package structure, TitleID, Old3DS flags, original icon/banner and '+inventory.length+' embedded files in both formats');
