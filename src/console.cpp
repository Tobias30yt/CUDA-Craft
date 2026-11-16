#include "cudacraft/console.hpp"
#include <algorithm>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/select.h>
#include <unistd.h>
#endif
namespace cudacraft {
ConsoleInput::ConsoleInput() {
#ifdef _WIN32
    handle_=GetStdHandle(STD_INPUT_HANDLE);DWORD mode=0;
    if(!handle_||handle_==INVALID_HANDLE_VALUE){ended_=true;return;}
    is_console_=GetConsoleMode(handle_,&mode)!=0;original_mode_=mode;
    if(is_console_)SetConsoleMode(handle_,(mode|ENABLE_EXTENDED_FLAGS)&~ENABLE_QUICK_EDIT_MODE);
#endif
}
ConsoleInput::~ConsoleInput(){
#ifdef _WIN32
    if(is_console_)SetConsoleMode(handle_,original_mode_);
#endif
}
void ConsoleInput::feed(char c,std::vector<std::string>& lines) {
    if(c=='\r')return;
    if(c=='\n'){if(!discard_&&!line_.empty())lines.push_back(std::move(line_));line_.clear();discard_=false;return;}
    if(static_cast<unsigned char>(c)<32&&c!='\t'){discard_=true;return;}
    if(line_.size()>=2048){discard_=true;return;}
    if(!discard_)line_+=c;
}
std::vector<std::string> ConsoleInput::poll() {
    std::vector<std::string> lines;if(ended_)return lines;
    char buffer[256]{};unsigned count=0;
#ifdef _WIN32
    if(is_console_) {
        DWORD available=0;if(!GetNumberOfConsoleInputEvents(handle_,&available)||!available)return lines;
        INPUT_RECORD records[128];DWORD read=0;if(!ReadConsoleInputW(handle_,records,std::min<DWORD>(128,available),&read))return lines;
        auto echo=[](const wchar_t* s,DWORD n){DWORD written;WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE),s,n,&written,nullptr);};
        for(DWORD i=0;i<read;++i)if(records[i].EventType==KEY_EVENT&&records[i].Event.KeyEvent.bKeyDown) {
            const auto& key=records[i].Event.KeyEvent;const wchar_t c=key.uChar.UnicodeChar;
            for(unsigned repeat=0;repeat<std::min<unsigned>(key.wRepeatCount,16);++repeat) {
                if(c==L'\r'){
                    echo(L"\r\n",2);
                    if(!discard_&&!wide_line_.empty()) {
                        int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide_line_.data(),static_cast<int>(wide_line_.size()),nullptr,0,nullptr,nullptr);
                        if(size>0&&size<=2048){std::string text(size,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide_line_.data(),static_cast<int>(wide_line_.size()),text.data(),size,nullptr,nullptr);lines.push_back(std::move(text));}
                    }
                    wide_line_.clear();discard_=false;
                } else if(c==L'\b'){if(!wide_line_.empty()){wide_line_.pop_back();echo(L"\b \b",3);}}
                else if(c>=32){if(wide_line_.size()<2048){wide_line_+=c;echo(&c,1);}else discard_=true;}
            }
        }
        return lines;
    }
    DWORD available=0,read=0;const auto type=GetFileType(handle_);
    if(type==FILE_TYPE_PIPE) {if(!PeekNamedPipe(handle_,nullptr,0,nullptr,&available,nullptr)){ended_=true;if(!discard_&&!line_.empty())lines.push_back(std::move(line_));return lines;}if(!available)return lines;}
    else if(type==FILE_TYPE_DISK)available=sizeof(buffer);
    else {ended_=true;return lines;}
    if(!ReadFile(handle_,buffer,std::min<DWORD>(sizeof(buffer),available),&read,nullptr)||!read)ended_=true;
    count=read;
#else
    fd_set set;FD_ZERO(&set);FD_SET(STDIN_FILENO,&set);timeval timeout{};
    if(select(STDIN_FILENO+1,&set,nullptr,nullptr,&timeout)<=0)return lines;
    auto read=::read(STDIN_FILENO,buffer,sizeof(buffer));if(read<=0)ended_=true;else count=static_cast<unsigned>(read);
#endif
    for(unsigned i=0;i<count;++i)feed(buffer[i],lines);
    if(ended_&&!discard_&&!line_.empty())lines.push_back(std::move(line_));
    return lines;
}
}
