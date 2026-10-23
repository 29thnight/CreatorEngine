import json,struct,math,pathlib,sys
out=pathlib.Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
def cube(name,color):
 p=[(-.4,-.4,-.4),(.4,-.4,-.4),(.4,.4,-.4),(-.4,.4,-.4),(-.4,-.4,.4),(.4,-.4,.4),(.4,.4,.4),(-.4,.4,.4)]
 faces=[(0,3,2,1),(4,5,6,7),(0,1,5,4),(3,7,6,2),(0,4,7,3),(1,2,6,5)]
 pos=[];normal=[];tangent=[];uv=[];idx=[]
 for face in faces:
  vertices=[p[i] for i in face];a=[vertices[1][i]-vertices[0][i] for i in range(3)];b=[vertices[2][i]-vertices[0][i] for i in range(3)]
  n=[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];n=[x/math.sqrt(sum(y*y for y in n)) for x in n];t=[x/math.sqrt(sum(y*y for y in a)) for x in a]
  base=len(pos);pos+=vertices;normal += [n]*4;tangent += [t+[1]]*4;uv += [(0,0),(1,0),(1,1),(0,1)];idx += [base,base+1,base+2,base,base+2,base+3]
 streams=[b''.join(struct.pack('<3f',*v) for v in pos),b''.join(struct.pack('<3f',*v) for v in normal),b''.join(struct.pack('<4f',*v) for v in tangent),b''.join(struct.pack('<2f',*v) for v in uv),struct.pack('<36H',*idx)]
 data=b''.join(streams);views=[];offset=0
 for stream in streams:views.append({'buffer':0,'byteOffset':offset,'byteLength':len(stream)});offset+=len(stream)
 d={'asset':{'version':'2.0'},'scene':0,'scenes':[{'nodes':[0]}],'nodes':[{'name':name,'mesh':0}],'meshes':[{'primitives':[{'attributes':{'POSITION':0,'NORMAL':1,'TANGENT':2,'TEXCOORD_0':3},'indices':4,'material':0}]}],'materials':[{'name':name,'pbrMetallicRoughness':{'baseColorFactor':color+[1],'metallicFactor':0,'roughnessFactor':1},'emissiveFactor':[c*.3 for c in color],'doubleSided':True}],'buffers':[{'byteLength':len(data)}],'bufferViews':views,'accessors':[{'bufferView':0,'componentType':5126,'count':24,'type':'VEC3','min':[-.4]*3,'max':[.4]*3},{'bufferView':1,'componentType':5126,'count':24,'type':'VEC3'},{'bufferView':2,'componentType':5126,'count':24,'type':'VEC4'},{'bufferView':3,'componentType':5126,'count':24,'type':'VEC2'},{'bufferView':4,'componentType':5123,'count':36,'type':'SCALAR'}]}
 j=json.dumps(d,separators=(',',':')).encode();j+=b' '*((-len(j))%4);data+=b'\0'*((-len(data))%4)
 glb=struct.pack('<III',0x46546c67,2,12+8+len(j)+8+len(data))+struct.pack('<II',len(j),0x4e4f534a)+j+struct.pack('<II',len(data),0x004e4942)+data
 (out/(name+'.glb')).write_bytes(glb)
cube('N3Green',[.03,.9,.05]);cube('N3Red',[.9,.03,.02])
