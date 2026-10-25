#include "../SDK/Disk.h"
#include <commdlg.h>
#include <array>
#include <atomic>
#include <cstring>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {
constexpr COLORREF Ink=RGB(18,27,43),Blue=RGB(36,99,235),White=RGB(255,255,255),Gray=RGB(234,239,247);
HWND window{},key{},capacity{},mode{},path{},readOnly{},createNew{},start{},stop{},renew{},browse{},message{};
HFONT font{},heading{},small{};std::thread worker;std::mutex gate;std::atomic_bool busy=false;
std::unique_ptr<UserDisk::Session> disk;UD_STATISTICS latest{},pending{};
bool preview=false,opened=false;unsigned frame=0;double pendingBar=0;
std::wstring detail=L"Create one explicitly owned disk. Only your application supplies its block data.";

std::wstring Text(HWND h){const int n=GetWindowTextLengthW(h);std::wstring value(static_cast<size_t>(n)+1,L'\0');GetWindowTextW(h,value.data(),n+1);value.resize(n);return value;}
std::wstring Wide(const std::string& value){if(value.empty())return {};int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);if(!size){std::wstring result(value.begin(),value.end());return result;}std::wstring result(static_cast<size_t>(size),L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size);return result;}
void TextAt(HDC dc,int x,int y,const std::wstring& value,HFONT selected,COLORREF color=Ink){SelectObject(dc,selected);SetTextColor(dc,color);SetBkMode(dc,TRANSPARENT);TextOutW(dc,x,y,value.c_str(),static_cast<int>(value.size()));}
void Card(HDC dc,RECT r,COLORREF color,int radius=16){HBRUSH brush=CreateSolidBrush(color);HPEN pen=CreatePen(PS_SOLID,1,color);auto oldBrush=SelectObject(dc,brush),oldPen=SelectObject(dc,pen);RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(pen);DeleteObject(brush);}
HWND Control(HWND owner,const wchar_t* type,const wchar_t* title,DWORD style,int x,int y,int width,int height,int id=0){HWND h=CreateWindowExW(0,type,title,WS_CHILD|WS_VISIBLE|style,x,y,width,height,owner,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return h;}
void Enable(){const bool idle=!busy.load();EnableWindow(start,idle&&!latest.active);EnableWindow(stop,idle);EnableWindow(renew,idle&&opened);EnableWindow(mode,idle&&!latest.active);EnableWindow(capacity,idle&&!latest.active);EnableWindow(path,idle&&!latest.active&&SendMessageW(mode,CB_GETCURSEL,0,0)==1);EnableWindow(browse,IsWindowEnabled(path));EnableWindow(createNew,IsWindowEnabled(path));EnableWindow(readOnly,idle&&!latest.active);}
void Message(const std::wstring& value){SetWindowTextW(message,value.c_str());if(window){RECT r{28,450,788,525};InvalidateRect(window,&r,FALSE);}}

void Run(unsigned operation){
 if(busy.exchange(true))return;if(worker.joinable())worker.join();
 uint64_t bytes=64ull*1024*1024;bool image=false,readonly=true,creating=false;std::wstring file;std::string access;
 try{
  if(operation!=2){auto value=Text(key);for(wchar_t c:value){if(c>127)throw std::runtime_error("Access keys contain ASCII characters only.");access.push_back(static_cast<char>(c));}SecureZeroMemory(value.data(),value.size()*sizeof(wchar_t));}
  if(operation==1){auto value=Text(capacity);if(value.empty()||value.size()>4||value.find_first_not_of(L"0123456789")!=std::wstring::npos)throw std::runtime_error("Enter a capacity from 64 to 1024 MiB.");const auto mib=std::stoull(value);if(mib<64||mib>1024)throw std::runtime_error("Capacity must be 64..1024 MiB.");bytes=mib*1024*1024;image=SendMessageW(mode,CB_GETCURSEL,0,0)==1;file=Text(path);readonly=SendMessageW(readOnly,BM_GETCHECK,0,0)==BST_CHECKED;creating=SendMessageW(createNew,BM_GETCHECK,0,0)==BST_CHECKED;if(image&&file.empty())throw std::runtime_error("Choose a regular image-file path.");}
 }catch(const std::exception& e){SecureZeroMemory(access.data(),access.size());detail=Wide(e.what());busy=false;Message(detail);Enable();return;}
 Enable();SetTimer(window,1,16,nullptr);Message(L"Working... Authorization and backend setup run outside the interface thread.");
 try{worker=std::thread([operation,bytes,image,readonly,creating,file=std::move(file),access=std::move(access)]()mutable{
  try{std::lock_guard<std::mutex> guard(gate);
   if(preview){pending={};pending.active=operation==2?0u:1u;detail=L"OFFLINE PREVIEW: editor state only. No driver, image file or memory backend was opened.";opened=operation!=2;}
   else{
    if(!disk)disk=std::make_unique<UserDisk::Session>();std::string result;
    if(operation==2){disk->Stop();detail=L"Backend stopped. Its virtual disk is offline; no physical device was changed.";}
    else{if(operation==1)disk->Stop();result=disk->Authorize(access);if(result.empty()&&operation==1){auto backend=image?UserDisk::FileBackend(file,bytes,creating):UserDisk::RamBackend(bytes);result=disk->Start(bytes,readonly,std::move(backend));}detail=result.empty()?(operation==3?L"Handle permission renewed. The existing backend was retained.":L"Backend active. No initialization, formatting or drive letter was automated."):Wide(result);}
    opened=true;pending=disk->Status();auto failure=disk->LastError();if(operation!=2&&result.empty()&&!failure.empty())detail=Wide(failure);
   }
  }catch(const std::exception& e){std::lock_guard<std::mutex> guard(gate);detail=Wide(e.what());}
  SecureZeroMemory(access.data(),access.size());PostMessageW(window,WM_APP+1,0,0);
 });}catch(...){SecureZeroMemory(access.data(),access.size());busy=false;SetTimer(window,1,200,nullptr);Enable();Message(L"Not enough resources to start the operation.");}
}

void Browse(){wchar_t filename[32768]{};auto value=Text(path);if(value.size()<std::size(filename))wcscpy_s(filename,value.c_str());OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=window;dialog.lpstrFilter=L"Disk images (*.img;*.raw)\0*.img;*.raw\0All files\0*.*\0";dialog.lpstrFile=filename;dialog.nMaxFile=static_cast<DWORD>(std::size(filename));dialog.Flags=OFN_NOCHANGEDIR|OFN_PATHMUSTEXIST;bool creating=SendMessageW(createNew,BM_GETCHECK,0,0)==BST_CHECKED;BOOL selected=creating?GetSaveFileNameW(&dialog):GetOpenFileNameW(&dialog);if(selected)SetWindowTextW(path,filename);}

LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l){switch(m){
 case WM_CREATE:{
  font=CreateFontW(18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");heading=CreateFontW(30,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");small=CreateFontW(15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
  Control(h,L"STATIC",L"Backend",0,38,165,250,22);mode=Control(h,L"COMBOBOX",L"",WS_TABSTOP|CBS_DROPDOWNLIST,38,193,260,120,10);SendMessageW(mode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Memory (temporary)"));SendMessageW(mode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Regular image file"));SendMessageW(mode,CB_SETCURSEL,0,0);
  Control(h,L"STATIC",L"Capacity / MiB",0,324,165,180,22);capacity=Control(h,L"EDIT",L"64",WS_TABSTOP|ES_NUMBER|WS_BORDER,324,193,160,30);SendMessageW(capacity,EM_SETLIMITTEXT,4,0);
  readOnly=Control(h,L"BUTTON",L"Read only",WS_TABSTOP|BS_AUTOCHECKBOX,518,193,170,30);SendMessageW(readOnly,BM_SETCHECK,BST_CHECKED,0);
  Control(h,L"STATIC",L"Image path (file backend only)",0,38,248,380,22);path=Control(h,L"EDIT",L"",WS_TABSTOP|ES_AUTOHSCROLL|WS_BORDER,38,277,630,31);SendMessageW(path,EM_SETLIMITTEXT,32760,0);browse=Control(h,L"BUTTON",L"Browse",WS_TABSTOP,682,275,92,34,4);
  createNew=Control(h,L"BUTTON",L"Create a new image; existing files are never overwritten",WS_TABSTOP|BS_AUTOCHECKBOX,38,320,680,27);
  Control(h,L"STATIC",L"Product access key",0,38,371,550,22);key=Control(h,L"EDIT",L"",WS_TABSTOP|ES_PASSWORD|ES_AUTOHSCROLL|WS_BORDER,38,401,445,32);SendMessageW(key,EM_SETLIMITTEXT,81,0);
  start=Control(h,L"BUTTON",L"Authorize & create",WS_TABSTOP|BS_OWNERDRAW,506,399,268,38,1);stop=Control(h,L"BUTTON",L"Stop",WS_TABSTOP|BS_OWNERDRAW,684,91,90,34,2);renew=Control(h,L"BUTTON",L"Renew access",WS_TABSTOP|BS_OWNERDRAW,529,91,143,34,3);
  message=Control(h,L"STATIC",detail.c_str(),0,38,459,736,57);ShowWindow(message,SW_HIDE);Enable();SetTimer(h,1,200,nullptr);return 0;}
 case WM_COMMAND:if(HIWORD(w)==BN_CLICKED){if(LOWORD(w)==1)Run(1);if(LOWORD(w)==2)Run(2);if(LOWORD(w)==3)Run(3);if(LOWORD(w)==4)Browse();}if(LOWORD(w)==10&&HIWORD(w)==CBN_SELCHANGE)Enable();return 0;
 case WM_APP+1:if(worker.joinable())worker.join();{std::lock_guard<std::mutex> guard(gate);latest=pending;Message(detail);}busy=false;SetTimer(h,1,200,nullptr);Enable();InvalidateRect(h,nullptr,FALSE);return 0;
 case WM_TIMER:{++frame;if(busy){RECT header{24,86,780,132};InvalidateRect(h,&header,FALSE);return 0;}if(!preview&&opened){std::unique_lock<std::mutex> guard(gate,std::try_to_lock);if(guard.owns_lock()){auto previous=latest;try{latest=disk->Status();auto error=disk->LastError();if(!error.empty()){detail=Wide(error);Message(detail);}}catch(const std::exception& e){detail=Wide(e.what());Message(detail);}Enable();if(memcmp(&previous,&latest,sizeof(latest))){pendingBar=static_cast<double>(latest.pending)/UD_PENDING;RECT stats{24,540,788,702},header{24,86,780,132};InvalidateRect(h,&stats,FALSE);InvalidateRect(h,&header,FALSE);}}}return 0;}
 case WM_DRAWITEM:{auto item=reinterpret_cast<DRAWITEMSTRUCT*>(l);const bool enabled=(item->itemState&ODS_DISABLED)==0;const bool primary=item->CtlID==1;Card(item->hDC,item->rcItem,enabled&&primary?Blue:Gray,12);auto label=Text(item->hwndItem);SelectObject(item->hDC,font);SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,enabled&&primary?White:Ink);RECT r=item->rcItem;DrawTextW(item->hDC,label.c_str(),-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);if(item->itemState&ODS_FOCUS){InflateRect(&r,-4,-4);DrawFocusRect(item->hDC,&r);}return TRUE;}
 case WM_CTLCOLORSTATIC:SetTextColor(reinterpret_cast<HDC>(w),Ink);SetBkColor(reinterpret_cast<HDC>(w),White);return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
 case WM_ERASEBKGND:return 1;
 case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(h,&ps);RECT bounds;GetClientRect(h,&bounds);HDC buffer=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,bounds.right,bounds.bottom);auto old=SelectObject(buffer,bitmap);FillRect(buffer,&bounds,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
  TextAt(buffer,28,24,L"User-Backed Virtual Disk",heading);TextAt(buffer,28,64,preview?L"OFFLINE PREVIEW / no driver access":L"Windows block I/O, backed by your application",font,Blue);
  TextAt(buffer,38,100,latest.active?L"BACKEND ACTIVE":L"OFFLINE",font,latest.active?Blue:Ink);if(busy){const int x=180+static_cast<int>(frame%70)*4;Card(buffer,{x,108,x+48,113},Blue,4);}
  Card(buffer,{24,145,788,444},Gray);Card(buffer,{28,149,784,440},White);
  SelectObject(buffer,font);SetTextColor(buffer,Ink);SetBkMode(buffer,TRANSPARENT);RECT description{38,459,774,523};auto notice=Text(message);DrawTextW(buffer,notice.c_str(),-1,&description,DT_WORDBREAK|DT_NOPREFIX);
  Card(buffer,{24,540,788,691},Gray);TextAt(buffer,42,556,L"READS",small);TextAt(buffer,225,556,L"WRITES",small);TextAt(buffer,408,556,L"COMPLETED",small);TextAt(buffer,591,556,L"FAILED / TIMEOUT",small);
  TextAt(buffer,42,583,std::to_wstring(latest.reads),heading);TextAt(buffer,225,583,std::to_wstring(latest.writes),heading);TextAt(buffer,408,583,std::to_wstring(latest.completed),heading);TextAt(buffer,591,583,std::to_wstring(latest.failed)+L" / "+std::to_wstring(latest.timeouts),heading);
  TextAt(buffer,42,638,L"Pending "+std::to_wstring(latest.pending)+L" / "+std::to_wstring(UD_PENDING),small);Card(buffer,{230,642,753,650},White,4);if(pendingBar>0.001)Card(buffer,{230,642,230+static_cast<int>(523*pendingBar),650},Blue,4);
  TextAt(buffer,28,712,L"One owned virtual disk / no raw physical-disk access / initialization and formatting stay manual",small);
  BitBlt(dc,0,0,bounds.right,bounds.bottom,buffer,0,0,SRCCOPY);SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);EndPaint(h,&ps);return 0;}
 case WM_SETCURSOR:if(reinterpret_cast<HWND>(w)==start||reinterpret_cast<HWND>(w)==stop||reinterpret_cast<HWND>(w)==renew||reinterpret_cast<HWND>(w)==browse||reinterpret_cast<HWND>(w)==mode||reinterpret_cast<HWND>(w)==readOnly||reinterpret_cast<HWND>(w)==createNew){SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;}break;
 case WM_CLOSE:if(busy){MessageBoxW(h,L"Wait for the current authorization or stop operation to finish.",L"Working",MB_OK);return 0;}DestroyWindow(h);return 0;
 case WM_DESTROY:KillTimer(h,1);if(worker.joinable())worker.join();if(disk)disk->Stop();DeleteObject(font);DeleteObject(heading);DeleteObject(small);PostQuitMessage(0);return 0;
 }return DefWindowProcW(h,m,w,l);}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command,int show){preview=command&&wcsstr(command,L"--preview");SetProcessDPIAware();WNDCLASSW c{};c.lpfnWndProc=Proc;c.hInstance=instance;c.lpszClassName=L"TitanUserBackedDiskStudio";c.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&c);window=CreateWindowW(c.lpszClassName,L"User-Backed Virtual Disk SDK",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,830,790,nullptr,nullptr,instance,nullptr);if(!window)return 1;ShowWindow(window,show);MSG m;while(GetMessageW(&m,nullptr,0,0)>0)if(!IsDialogMessageW(window,&m)){TranslateMessage(&m);DispatchMessageW(&m);}return 0;}
