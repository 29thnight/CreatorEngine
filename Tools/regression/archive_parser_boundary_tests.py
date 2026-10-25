import argparse,json,pathlib
from archive_parser_boundary import snapshot,check,BoundaryError
p=argparse.ArgumentParser();p.add_argument('--root',type=pathlib.Path,required=True);p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args()
original=snapshot(a.root);check(original);cases=[]
def reject(name,path,code,extra=None):
    files=dict(original);files[path]=files.get(path,'')+'\n'+code
    if extra:files.update(extra)
    try:check(files)
    except BoundaryError as e:cases.append({'name':name,'result':'rejected','reason':str(e)});return
    raise AssertionError('N1 mutation accepted '+name)
for root in ['Engine/SceneRuntime','Engine/RenderEngine','Engine/Physics','Engine/RuntimeHost','Player','Engine/Network']:
    for payload in ['#include <yaml-cpp/yaml.h>','#include <nlohmann/json.hpp>','#include <ryml/ryml.hpp>','using Leak = ryml::NodeRef;']:
        reject(root+':'+payload,root+'/N1Injected.h',payload)
reject('indirect-neutral','Engine/Utility_Framework/Authoring/DiagnosticDocument.h','#include "N1Hidden.h"',{'Engine/Utility_Framework/N1Hidden.h':'using Hidden=ryml::Tree;'})
reject('indirect-runtime','Engine/SceneRuntime/N1Injected.cpp','#include "N1Hidden.h"',{'Engine/Utility_Framework/N1Hidden.h':'#include <c4/yml/tree.hpp>'})
reject('network-facade','Engine/Network/N1Injected.h','#include "AuthoringDocument.h"')
reject('network-diagnostic','Engine/Network/N1Injected.h','#include "DiagnosticDocument.h"')
reject('network-type','Engine/Network/N1Injected.h','using Alias = Authoring::NodeView;')
reject('macro-include','Engine/Utility_Framework/Authoring/DiagnosticDocument.h','#define NEXT "ryml/ryml.hpp"\n#include NEXT')
reject('continued-include','Engine/SceneRuntime/N1Injected.cpp','#inc\\\nlude <ryml/ryml.hpp>')
reject('forward-backend','Engine/SceneRuntime/N1Injected.h','namespace ryml { class Tree; }')
reject('aliased-backend','Engine/SceneRuntime/N1Injected.cpp','namespace escape = nlohmann; using X = escape::json;')
files=dict(original);del files['Engine/Utility_Framework/Authoring/DiagnosticDocument.h']
try:check(files)
except BoundaryError:cases.append({'name':'removed-neutral-registration','result':'rejected'})
else:raise AssertionError('missing registration accepted')
files=dict(original);files['Engine/SceneRuntime/N1Comments.h']='// ryml::Tree\nconst char* url="https://example/ryml::Tree";\nconst char* raw=R"(nlohmann::json)";'
check(files);cases.append({'name':'comments-strings-not-code','result':'accepted'})
a.output.write_text(json.dumps({'result':'N1_BOUNDARY_CANARIES_OK','cases':cases,'productionSourcesChanged':False},indent=2)+'\n',encoding='utf-8');print('N1_BOUNDARY_CANARIES_OK cases='+str(len(cases)))
