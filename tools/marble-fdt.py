"""Bounded FDT v17 reader for retained firmware configuration snapshots only."""
import struct
def parse(data,maximum=65536,*,maximum_nodes=4096,maximum_properties=32768):
 if not 1<=maximum_nodes<=16384 or not 1<=maximum_properties<=131072:raise ValueError('FDT parser limits')
 if not 40<=len(data)<=maximum:raise ValueError('FDT length')
 magic,total,structure,strings,reserve,version,last,cpu,ns,nt=struct.unpack_from('>10I',data)
 if magic!=0xd00dfeed or total!=len(data) or version!=17 or last>17 or last<16 or structure&3 or reserve&7 or structure<40 or strings<40 or reserve<40 or not ns or not nt or nt&3:raise ValueError('FDT header')
 if structure>total or nt>total-structure or strings>total or ns>total-strings or not (structure+nt<=strings or strings+ns<=structure):raise ValueError('FDT block bounds')
 pos=reserve;reserved=[]
 while True:
  if pos>total-16 or pos<structure+nt and structure<pos+16 or pos<strings+ns and strings<pos+16:raise ValueError('FDT reservation bounds/overlap')
  address,size=struct.unpack_from('>QQ',data,pos);pos+=16
  if not address and not size:break
  if not size or address>(1<<64)-size or len(reserved)>=128:raise ValueError('FDT reservation geometry')
  reserved.append((address,size))
 nodes={};stack=[];pos=structure;limit=structure+nt;ended=False;properties=0
 while pos<limit:
  token=struct.unpack_from('>I',data,pos)[0];pos+=4
  if token==1:
   end=data.find(b'\0',pos,limit)
   if end<0:raise ValueError('FDT node name terminator')
   name=data[pos:end].decode('ascii');pos=(end+4)&~3
   if pos>limit or '/' in name or (not stack and (nodes or name)) or (stack and not name) or len(stack)>=64:raise ValueError('FDT node identity/depth')
   stack.append(name);path='/'+('/'.join(stack[1:]))
   if path in nodes or len(nodes)>=maximum_nodes:raise ValueError('FDT duplicate/excess nodes')
   nodes[path]={}
  elif token==2:
   if not stack:raise ValueError('FDT unmatched end-node')
   stack.pop()
  elif token==3:
   if not stack or pos>limit-8:raise ValueError('FDT property outside node/header')
   size,offset=struct.unpack_from('>II',data,pos);pos+=8
   if offset>=ns or size>limit-pos:raise ValueError('FDT property bounds')
   end=data.find(b'\0',strings+offset,strings+ns)
   if end<0:raise ValueError('FDT property name terminator')
   name=data[strings+offset:end].decode('ascii');path='/'+('/'.join(stack[1:]))
   if not name or name in nodes[path] or properties>=maximum_properties:raise ValueError('FDT duplicate/excess property')
   nodes[path][name]=data[pos:pos+size];properties+=1;pos=(pos+size+3)&~3
   if pos>limit:raise ValueError('FDT property padding')
  elif token==4:continue
  elif token==9:
   if stack or '/' not in nodes or any(data[pos:limit]):raise ValueError('FDT unfinished tree/trailing structure')
   ended=True;break
  else:raise ValueError('FDT unknown token')
 if not ended:raise ValueError('FDT missing END')
 return nodes
