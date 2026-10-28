#include <ce/string.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <Windows.h>
#include <imm.h>
#include <d3d11.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include "../../../Editor/ImGuiHelper/Utf16Text.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
static void check(bool b,const char* m) { if(!b) { std::fprintf(stderr,"FAIL %s\n",m); std::exit(1); } }
static void frame(ce::string& text,ce::utf16_input_state& state,bool focus=false,bool multiline=false) {
  ImGui::NewFrame(); ImGui::Begin("UTF-16 input");
  if(focus) ImGui::SetKeyboardFocusHere();
  auto r=multiline?editor::text::InputTextMultiline("TextMultiline",text,state):editor::text::InputText("Text",text,state); check(bool(r.status),"input status");
  ImGui::End(); ImGui::Render();
}
static void synthetic() {
  ImGui::CreateContext(); auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize={600,300}; io.DeltaTime=1.f/60;
  io.Fonts->AddFontDefault(); unsigned char* pixels; int w,h; io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
  ce::string text; ce::utf16_input_state state;
  frame(text,state,true); frame(text,state);
  io.AddInputCharacterUTF16(0xd55c); io.AddInputCharacterUTF16(0xae00);
  io.AddInputCharacterUTF16(0xd83d); io.AddInputCharacterUTF16(0xde00);
  frame(text,state);
  check(text.view()==ce::string_view(u"한글\U0001F600"),"UTF16 event Korean emoji");
  io.AddKeyEvent(ImGuiKey_Backspace,true); frame(text,state); io.AddKeyEvent(ImGuiKey_Backspace,false); frame(text,state);
  check(text.view()==ce::string_view(u"한글"),"backspace supplementary scalar");
  std::string bulk(4096,'x'); io.AddInputCharactersUTF8(bulk.c_str()); frame(text,state);
  check(text.size()==4098 && state.buffer().size()==4102,"resize callback growth");
  ce::utf8_conversion_cache cache; ImGui::NewFrame(); ImGui::Begin("Display"); check(bool(editor::text::TextUnformatted(text.view(),cache)),"display"); ImGui::End(); ImGui::Render();
  ce::string multiline; ce::utf16_input_state multiState;
  frame(multiline,multiState,true,true); frame(multiline,multiState,false,true);
  io.AddInputCharactersUTF8("line1\nline2"); frame(multiline,multiState,false,true);
  check(multiline.view()==ce::string_view(u"line1\nline2"),"multiline newline");
  ImGui::NewFrame(); ImGui::Begin("Hints");
  ce::string hinted; ce::utf16_input_state hintedState;
  check(bool(editor::text::InputTextWithHint("Hint","UTF-8 hint",hinted,hintedState).status),"hint adapter");
  auto unsupported=editor::text::InputText("Unsupported",hinted,hintedState,ImGuiInputTextFlags_EnterReturnsTrue);
  check(!unsupported.status && unsupported.status.error==ce::unicode_error::unsupported_input_flags,"unsupported flags rejected in Release");
  ImGui::End(); ImGui::Render();
  ImGui::DestroyContext(); std::puts("IMGUI_ADAPTER_PASS events=Korean+surrogate backspace=scalar resize=4096 (synthetic; not IME)");
}
static FILE* evidence;
static bool compositionSeen=false,resultSeen=false;
static LRESULT CALLBACK proc(HWND window,UINT message,WPARAM wp,LPARAM lp) {
  if(message==WM_IME_STARTCOMPOSITION) { compositionSeen=true; std::fprintf(evidence,"IME_START\n"); std::fflush(evidence); }
  if(message==WM_IME_COMPOSITION) {
    HIMC context=ImmGetContext(window);
    if(context) {
      for(DWORD flag : {DWORD(GCS_COMPSTR),DWORD(GCS_RESULTSTR)}) if((lp&flag)!=0) {
        wchar_t buffer[512]{}; LONG bytes=ImmGetCompositionStringW(context,flag,buffer,sizeof(buffer));
        std::fprintf(evidence,"IME_%s bytes=%ld units=",flag==GCS_COMPSTR?"COMPOSITION":"RESULT",bytes);
        for(LONG i=0;i<bytes/2 && i<512;++i) std::fprintf(evidence,"%04x ",unsigned(buffer[i]));
        std::fprintf(evidence,"\n"); std::fflush(evidence);
        if(flag==GCS_COMPSTR) compositionSeen=true;
        if(flag==GCS_RESULTSTR) resultSeen=true;
      } ImmReleaseContext(window,context);
    }
  }
  if(ImGui_ImplWin32_WndProcHandler(window,message,wp,lp)) return 1;
  if(message==WM_DESTROY) { PostQuitMessage(0); return 0; }
  return DefWindowProcW(window,message,wp,lp);
}
static int live(const char* path) {
  fopen_s(&evidence,path,"w"); check(evidence!=nullptr,"evidence file");
  auto instance=GetModuleHandleW(nullptr); WNDCLASSW wc{}; wc.hInstance=instance; wc.lpfnWndProc=proc; wc.lpszClassName=L"CreatorUTF16ImeProbe"; RegisterClassW(&wc);
  HWND window=CreateWindowW(wc.lpszClassName,L"CreatorEngine UTF-16 Korean IME validation",WS_OVERLAPPEDWINDOW,100,100,900,400,nullptr,nullptr,instance,nullptr); check(window!=nullptr,"window");
  DXGI_SWAP_CHAIN_DESC sd{}; sd.BufferCount=2; sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow=window; sd.SampleDesc.Count=1; sd.Windowed=TRUE; sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
  ID3D11Device* device{}; ID3D11DeviceContext* context{}; IDXGISwapChain* swap{};
  check(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&sd,&swap,&device,nullptr,&context)),"D3D11");
  ID3D11Texture2D* back{}; ID3D11RenderTargetView* target{}; swap->GetBuffer(0,IID_PPV_ARGS(&back)); device->CreateRenderTargetView(back,nullptr,&target); back->Release();
  ImGui::CreateContext(); ImGui::GetIO().IniFilename=nullptr;
  const char* font="C:\\Windows\\Fonts\\malgun.ttf";
  ImGui::GetIO().Fonts->AddFontFromFileTTF(font,20.f,nullptr,ImGui::GetIO().Fonts->GetGlyphRangesKorean());
  ImGui_ImplWin32_Init(window); ImGui_ImplDX11_Init(device,context); ShowWindow(window,SW_SHOW); UpdateWindow(window);
  HKL layouts[64]; int n=GetKeyboardLayoutList(64,layouts); std::fprintf(evidence,"LAYOUTS"); for(int i=0;i<n;++i) std::fprintf(evidence," %p",layouts[i]); std::fprintf(evidence,"\n"); std::fflush(evidence);
  ce::string text; ce::utf16_input_state state; ce::utf8_conversion_cache cache; bool first=true; size_t previous=SIZE_MAX;
  ULONGLONG deadline=GetTickCount64()+300000;
  while(GetTickCount64()<deadline) {
    MSG msg; bool quit=false; while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { if(msg.message==WM_QUIT) quit=true; TranslateMessage(&msg); DispatchMessageW(&msg); } if(quit) break;
    ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
    ImGui::SetNextWindowPos({10,10},ImGuiCond_Always); ImGui::SetNextWindowSize({850,300},ImGuiCond_Always);
    ImGui::Begin("Korean IME / UTF-16",nullptr,ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize);
    ImGui::TextUnformatted("Enter Korean with the system IME. Internal text: char16_t UTF-16.");
    if(ImGui::Button("Enable Korean IME for this window")) {
      HKL korean=LoadKeyboardLayoutW(L"00000412",0); HKL old=ActivateKeyboardLayout(korean,0);
      HIMC imc=ImmGetContext(window); BOOL opened=FALSE,converted=FALSE; DWORD conversion=0,sentence=0;
      if(imc) { ImmGetConversionStatus(imc,&conversion,&sentence); opened=ImmSetOpenStatus(imc,TRUE); converted=ImmSetConversionStatus(imc,conversion|IME_CMODE_NATIVE,sentence); ImmGetConversionStatus(imc,&conversion,&sentence); ImmReleaseContext(window,imc); }
      std::fprintf(evidence,"KOREAN_ENABLE layout=%p previous=%p opened=%d converted=%d conversion=%lu\n",korean,old,int(opened),int(converted),conversion); std::fflush(evidence);
      first=true;
    }
    if(first) { ImGui::SetKeyboardFocusHere(); first=false; }
    auto r=editor::text::InputText("Text",text,state); check(bool(r.status),"live conversion");
    (void)editor::text::TextUnformatted(text.view(),cache);
    ImGui::Text("UTF-16 units: %zu | UTF-8 bytes: %zu",text.size(),state.buffer().size());
    ImGui::End(); ImGui::Render();
    float clear[4]={.1f,.1f,.1f,1.f}; context->OMSetRenderTargets(1,&target,nullptr); context->ClearRenderTargetView(target,clear); ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData()); swap->Present(1,0);
    if(text.size()!=previous || r.changed) { previous=text.size(); std::fprintf(evidence,"TEXT units="); for(size_t i=0;i<text.size();++i) std::fprintf(evidence,"%04x ",unsigned(text[i])); std::fprintf(evidence,"\n"); std::fflush(evidence); }
    if(text.view()==ce::string_view(u"한글") && compositionSeen && resultSeen) { std::fprintf(evidence,"ACTUAL_IME_PASS text=한글 composition=observed result=observed\n"); std::fflush(evidence); break; }
  }
  bool passed=text.view()==ce::string_view(u"한글") && compositionSeen && resultSeen;
  ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext(); target->Release(); swap->Release(); context->Release(); device->Release(); DestroyWindow(window); fclose(evidence);
  return passed?0:2;
}
int main(int argc,char** argv) { if(argc>1) return live(argv[1]); synthetic(); }
