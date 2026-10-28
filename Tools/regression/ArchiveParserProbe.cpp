#include "ArchiveSchemaFixture.h"
#include "../../Engine/Utility_Framework/Authoring/DiagnosticDocument.h"
#include "../../Engine/Utility_Framework/Reflection/ReflgenAuthoring.h"
#include "../../Engine/Utility_Framework/Authoring/AuthoringCookedDocument.h"
#include "../../Engine/Utility_Framework/Authoring/AuthoringParsedDocument.h"
#include <reflgen/serial/serializer.h>
#include <fstream>
#include <iostream>
#include <limits>
unsigned checks;
void Check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    Check(argc==2,"work directory required");std::filesystem::path work(argv[1]);std::filesystem::create_directories(work);
    ArchiveSchemaFixture source{43,"boundary quoted \"value\"",true,{1,-7,900}};
    Authoring::WriteDocument document;
    Authoring::ReflgenWriter writer(document.Root());reflgen::serialize(writer,source);
    const auto initial=Authoring::GetTextParseTelemetry().calls;
    std::string error;auto text=Authoring::ParsedDocument::ParseText(document.Dump(),error);
    Check(bool(text)&&error.empty(),"authoring parse");
    ArchiveSchemaFixture fromText;Authoring::ReflgenReader textReader(text.Root());reflgen::deserialize(textReader,fromText);
    Check(fromText.id==source.id&&fromText.name==source.name&&fromText.active&&fromText.samples==source.samples,"shared schema authoring");
    std::vector<std::byte> bytes;Check(Authoring::EncodeCookedDocument(document.Root().Read(),bytes,error),"cook");
    auto cooked=Authoring::ParsedDocument::ParseCooked(bytes,error);Check(bool(cooked),"cooked strict read");
    ArchiveSchemaFixture fromCooked;Authoring::ReflgenReader cookedReader(cooked.Root());reflgen::deserialize(cookedReader,fromCooked);
    Check(fromCooked.id==source.id&&fromCooked.name==source.name&&fromCooked.active&&fromCooked.samples==source.samples,"shared schema cooked");
    Check(Authoring::GetTextParseTelemetry().calls==initial+1,"cooked entered text parser");
    const auto baseline=Authoring::GetTextParseTelemetry().calls;
    for(std::size_t length=0;length<bytes.size();++length)
        Check(!Authoring::ParsedDocument::ParseCooked(std::span(bytes).first(length),error),"truncated cooked accepted");
    for(unsigned offset: {0u,4u,6u,8u,12u,16u,17u}){
        auto broken=bytes;broken[offset]^=std::byte{0x7f};
        Check(!Authoring::ParsedDocument::ParseCooked(broken,error),"corrupt header/node accepted");
    }
    auto trailing=bytes;trailing.push_back(std::byte{});Check(!Authoring::ParsedDocument::ParseCooked(trailing,error),"trailing byte accepted");
    auto yaml=std::as_bytes(std::span("id: 1",5));Check(!Authoring::ParsedDocument::ParseCooked(yaml,error),"strict cooked accepted text fallback");
    Check(Authoring::GetTextParseTelemetry().calls==baseline,"invalid cooked entered text parser");
    std::string envelope;Check(Authoring::EncodeCookedDocumentTextEnvelope(document.Root().Read(),envelope,error),"embedded cook");
    Check(bool(Authoring::DecodeCookedDocumentTextEnvelope(envelope,error)),"embedded read");
    Check(!Authoring::DecodeCookedDocumentTextEnvelope("id: 1",error),"embedded text fallback");
    std::ofstream file(work/"fixture.cedo",std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());file.close();
    Check(bool(Authoring::ParsedDocument::ParseFilePath(work/"fixture.cedo",error)),"existing file dispatcher lost cooked path");
    Check(Authoring::GetTextParseTelemetry().calls==baseline,"cooked file entered parser");
    Check(!Authoring::ParsedDocument::ParseText("a: [unterminated",error)&&!error.empty(),"malformed authoring did not fail safely");
    Check(Authoring::GetTextParseTelemetry().calls==baseline+1,"parser telemetry missing");
    Diagnostics::Document diagnostics;auto root=diagnostics.Root();root.SetMap();
    root["schemaVersion"]<<1;root["source"]<<"product-live";
    root["flag"]<<true;root["disabled"]<<false;root["booleanText"]<<"true";root["numberText"]<<"123";
    root["empty"]<<"";root["escape"]<<"quoted \"line\"\nslash\\";
    root["max"]<<std::numeric_limits<std::uint64_t>::max();root["min"]<<std::numeric_limits<std::int64_t>::min();root["fraction"]<<0.125;
    {std::string key="temporary-key",value="temporary-value";root[key]<<value;}
    root["draws"].SetSequence();auto item=root["draws"].Append();item.SetMap();item["meshId"]<<"00000000-0000-0000-0000-000000000001";
    root["emptyArray"].SetSequence();root["emptyObject"].SetMap();
    Check(root.Has("temporary-key")&&!root.Has("missing"),"diagnostic child lookup");
    Diagnostics::Document moved=std::move(diagnostics);item["frameId"]<<std::uint64_t{17};
    moved.Root()["draws"][std::size_t{0}]["visible"]<<true;
    bool rejected=false;try{moved.Root()["draws"][std::size_t{1}];}catch(const std::out_of_range&){rejected=true;}Check(rejected,"invalid sequence index");
    rejected=false;try{Diagnostics::Node{}.SetMap();}catch(const std::logic_error&){rejected=true;}Check(rejected,"invalid diagnostic node");
    std::ofstream json(work/"diagnostic.json",std::ios::binary);const auto jsonText=moved.ToJson();json.write(jsonText.data(),static_cast<std::streamsize>(jsonText.size()));json.close();
    Check(Authoring::GetTextParseTelemetry().calls==baseline+1,"diagnostic output called text parser");
    std::cout<<"ARCHIVE_PARSER_OK checks="<<checks<<" cookedTextParserCalls=0\n";return 0;
}catch(const std::exception& e){std::cerr<<"ARCHIVE_PARSER_FAILED "<<e.what()<<"\n";return 1;}}
