import re,sys,shutil
from pathlib import Path
project=Path(sys.argv[1]);repo=Path(__file__).resolve().parents[2]
p=project/'Assets/Scenes/TR0_Native_Workload.creator'
s=p.read_text(encoding='utf-8-sig').replace('m_childrenIndices: [1, 2, 3]','m_childrenIndices: [1, 2, 3, 4]')
parts=re.split(r'(?m)(?=^  - Entity:)',(repo/'Dynamic_CPP/Assets/Scenes/FT_Primitives.creator').read_text(encoding='utf-8-sig'))
cube=parts[5].replace('position: {x: -5.25, y: 0.5, z: 0, w: 0}','position: {x: 0, y: 0, z: 3, w: 0}').replace('        m_Material:','        m_modelGuid: 8a69bd42-f950-8265-9c0e-0ff597095141\n        m_Material:')
cube=cube.replace('m_baseColorTexName: Cube_Mat_BaseColor.png','m_baseColorTexName: ""')
cube=re.sub(r'(?ms)^          m_propertyValues:.*?(?=^          m_keywordSelections:)','          m_propertyValues: ~\n',cube)
# Move only the authored ground transform below the test camera.
start=s.index('    m_name: Ground');tail=s[start:];tail=tail.replace('position: {x: 0, y: 0, z: 0, w: 0}','position: {x: 0, y: -1, z: 0, w: 0}',1);s=s[:start]+tail+cube
p.write_text(s,encoding='utf-8')
for name in ('Prim_Cube.glb','Prim_Cube.glb.meta'):
 shutil.copy2(repo/'Dynamic_CPP/Assets/Models'/name,project/'Assets/Models'/name)
for name in ('Cube_Mat_BaseColor.png','Cube_Mat_BaseColor.png.meta'):
 shutil.copy2(repo/'Dynamic_CPP/Assets/Materials'/name,project/'Assets/Materials'/name)
