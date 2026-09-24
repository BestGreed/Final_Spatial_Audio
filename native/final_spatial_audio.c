#define SPATIAL_POLICY_NO_MAIN
#include "spatial_policy.c"
#include "resolver.h"
#include "resource.h"
#include "fluent_menu.h"
#include <shellapi.h>
#include <tlhelp32.h>
#include <process.h>

#define WM_TRAY (WM_APP+1)
#define WM_STATUS (WM_APP+2)
#define MAX_RULES 64
#define WM_NOTICE (WM_APP+3)
static const wchar_t *names[]={L"Stereo",L"5.1",L"7.1",L"Dolby Atmos",L"DTS:X"};
static const GUID encoders[2]={{0xa289735d,0xfa3e,0x4e35,{0x9d,0x7d,0xb6,0xf8,0x96,0xac,0xb2,0xe7}},{0x10201b4a,0x3322,0x4967,{0xbf,0x40,0x2c,0xaa,0x9b,0xaf,0xca,0x44}}};
static HWND window;
static HICON mode_icons[MODE_COUNT];
static HICON app_icon,app_icon_small;
static const int mode_icon_ids[MODE_COUNT]={IDI_FSA_STEREO,IDI_FSA_51,IDI_FSA_71,IDI_FSA_ATMOS,IDI_FSA_DTSX};
static void release_icons(void) {
    for(int i=0;i<MODE_COUNT;i++) { if(mode_icons[i]) DestroyIcon(mode_icons[i]); mode_icons[i]=NULL; }
    if(app_icon) DestroyIcon(app_icon);
    if(app_icon_small) DestroyIcon(app_icon_small);
    app_icon=app_icon_small=NULL;
}
static int load_mode_icons(HINSTANCE instance) {
    HICON replacement[MODE_COUNT]={0};
    for(int i=0;i<MODE_COUNT;i++) {
        replacement[i]=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(mode_icon_ids[i]),IMAGE_ICON,
                                       GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),0);
        if(!replacement[i]) {
            for(int j=0;j<i;j++) DestroyIcon(replacement[j]);
            return 0;
        }
    }
    for(int i=0;i<MODE_COUNT;i++) { if(mode_icons[i]) DestroyIcon(mode_icons[i]); mode_icons[i]=replacement[i]; }
    return 1;
}
static HANDLE wake,stop,thread;
static SRWLOCK state_lock=SRWLOCK_INIT;
static int automatic=0,manual=-1,current=-1;
static wchar_t status[128]=L"正在读取音频状态",root[MAX_PATH],ini[MAX_PATH],logpath[MAX_PATH];
static LONG reload_requested=1;
static LONG control_generation=0;
static LONG worker_read_ok=0;
static LONG foreground_needed=0;
static int enabled_rules=0,has_running=0,has_foreground=0;
static AL_Snapshot profiles[3],spatial_profiles[2];
static SpatialSettings spatial_settings[2];
static unsigned spatial_captured;
static LONG available_modes=0,capture_requested=0;
static wchar_t profile_path[MAX_PATH];
static int quiet_start;
static Rule rules[MAX_RULES];
static wchar_t exes[MAX_RULES][MAX_PATH];
static int rule_count,fallback=MODE_STEREO;
static unsigned debounce_ms=500;

static void log_line(const wchar_t *message,HRESULT hr) {
    WIN32_FILE_ATTRIBUTE_DATA attr;
    if(GetFileAttributesExW(logpath,GetFileExInfoStandard,&attr) && attr.nFileSizeLow>1024*1024) {
        wchar_t old[MAX_PATH]; swprintf(old,MAX_PATH,L"%ls.old",logpath);
        MoveFileExW(logpath,old,MOVEFILE_REPLACE_EXISTING);
    }
    FILE *f=_wfopen(logpath,L"a"); if(!f) return;
    SYSTEMTIME t; GetLocalTime(&t);
    fprintf(f,"%04u-%02u-%02u %02u:%02u:%02u %ls hr=0x%08lx\n",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,message,(unsigned long)hr);
    fclose(f);
}
static void publish(int mode,const wchar_t *text) {
    AcquireSRWLockExclusive(&state_lock);
    int changed=current!=mode || wcscmp(status,text);
    current=mode; wcsncpy(status,text,127); status[127]=0;
    ReleaseSRWLockExclusive(&state_lock);
    if(changed) PostMessageW(window,WM_STATUS,0,0);
}
static int profile_id(const wchar_t *value) {
    for(int i=0;i<MODE_COUNT;i++) if(!_wcsicmp(value,names[i])) return i;
    if(!_wcsicmp(value,L"Atmos")) return MODE_ATMOS;
    if(!_wcsicmp(value,L"DTS-X")) return MODE_DTS;
    return -1;
}
#include "profile_store.h"
static void load_rules(void) {
    wchar_t sections[8192],value[MAX_PATH]; rule_count=0;
    enabled_rules=has_running=has_foreground=0;
    GetPrivateProfileStringW(L"Manager",L"Fallback",L"Stereo",value,MAX_PATH,ini);
    fallback=profile_id(value); if(fallback<0) fallback=MODE_STEREO;
    debounce_ms=GetPrivateProfileIntW(L"Manager",L"DebounceMs",500,ini);
    if(debounce_ms<100) debounce_ms=100; if(debounce_ms>5000) debounce_ms=5000;
    GetPrivateProfileSectionNamesW(sections,8192,ini);
    for(wchar_t *s=sections;*s && rule_count<MAX_RULES;s+=wcslen(s)+1) {
        if(wcsncmp(s,L"Rule:",5)) continue;
        Rule r={0};
        r.enabled=GetPrivateProfileIntW(s,L"Enabled",1,ini);
        GetPrivateProfileStringW(s,L"Scope",L"",value,MAX_PATH,ini);
        if(!_wcsicmp(value,L"Foreground")) r.scope=SCOPE_FOREGROUND;
        else if(!_wcsicmp(value,L"Running")) r.scope=SCOPE_RUNNING;
        else { log_line(L"Rule rejected: unsupported Scope",E_INVALIDARG); continue; }
        GetPrivateProfileStringW(s,L"Profile",L"",value,MAX_PATH,ini);
        r.profile=profile_id(value);
        if(r.profile<0) { log_line(L"Rule rejected: unknown Profile",E_INVALIDARG); continue; }
        r.priority=(int)GetPrivateProfileIntW(s,L"Priority",0,ini);
        GetPrivateProfileStringW(s,L"Exe",L"",exes[rule_count],MAX_PATH,ini);
        if(!exes[rule_count][0]) continue;
        if(r.enabled) {
            enabled_rules++;
            if(r.scope==SCOPE_RUNNING) has_running=1;
            if(r.scope==SCOPE_FOREGROUND) has_foreground=1;
        }
        rules[rule_count++]=r;
    }
    log_line(L"Rules loaded",S_OK);
}
static int observe_rules(int *candidate) {
    unsigned char matches[MAX_RULES]={0}; DWORD foreground=0;
    if(!enabled_rules) { *candidate=fallback; return 1; }
    GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    /* Foreground-only configurations need one process name, not the process list.
       Fall back to the original snapshot path if limited access is unavailable. */
    if(!has_running && foreground) {
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,foreground);
        if(process) {
            wchar_t path[32768]; DWORD size=32768;
            BOOL ok=QueryFullProcessImageNameW(process,0,path,&size); CloseHandle(process);
            if(ok) {
                wchar_t *base=wcsrchr(path,L'\\'); base=base?base+1:path;
                for(int i=0;i<rule_count;i++) if(rules[i].enabled && !_wcsicmp(base,exes[i])) matches[i]=1;
                *candidate=resolve(rules,matches,rule_count,fallback); return 1;
            }
        }
    }
    HANDLE h=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if(h==INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W p={0}; p.dwSize=sizeof(p);
    if(!Process32FirstW(h,&p)) { CloseHandle(h); return 0; }
    do {
        for(int i=0;i<rule_count;i++) if(rules[i].enabled && !_wcsicmp(p.szExeFile,exes[i]) &&
            (rules[i].scope==SCOPE_RUNNING || (rules[i].scope==SCOPE_FOREGROUND && p.th32ProcessID==foreground))) matches[i]=1;
    } while(Process32NextW(h,&p));
    CloseHandle(h); *candidate=resolve(rules,matches,rule_count,fallback); return 1;
}
static HRESULT read_spatial(SpatialPolicy *p,SpatialSettings *out) {
    if(!p) {memset(out,0,sizeof(*out));return S_OK;}
    WAVEFORMATEX *f=NULL; SpatialSettings *s=NULL; BYTE *d=NULL; UINT32 n=0;
    HRESULT hr=p->v->get(p,profiles[0].endpoint,FALSE,&f,&s,&n,&d);
    if(SUCCEEDED(hr)) { if(s) *out=*s; else hr=E_UNEXPECTED; }
    CoTaskMemFree(f); CoTaskMemFree(s); CoTaskMemFree(d); return hr;
}
static HRESULT spatial_select(SpatialPolicy *p,int mode) {
    if(!p)return mode<MODE_ATMOS?S_OK:E_NOINTERFACE;
    if(mode>=MODE_ATMOS && (spatial_captured&(1u<<(mode-MODE_ATMOS))))
        return p->v->set(p,profiles[0].endpoint,&spatial_settings[mode-MODE_ATMOS],NULL);
    WAVEFORMATEX *f=NULL; SpatialSettings *s=NULL; BYTE *d=NULL; UINT32 n=0;
    HRESULT hr=p->v->get(p,profiles[0].endpoint,FALSE,&f,&s,&n,&d);
    if(FAILED(hr) || !s) { if(SUCCEEDED(hr)) hr=E_UNEXPECTED; goto done; }
    if(mode<MODE_ATMOS) {
        s->words[0]=0; memset(&s->words[3],0,16); s->words[17]=0;
    } else {
        BYTE *selected=NULL;
        if(n>128 || (n && !d)) { hr=E_UNEXPECTED; goto done; }
        for(UINT32 i=0;i<n;i++) if(d[i*0x342+0x310] && !memcmp(d+i*0x342+0x300,&encoders[mode-MODE_ATMOS],16)) selected=d+i*0x342;
        if(!selected) { hr=HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED); goto done; }
        s->words[0]=1; memcpy(&s->words[1],selected+0x314,4); s->words[2]=1;
        memcpy(&s->words[3],selected+0x300,16); memcpy(&s->words[7],selected+0x300,16);
        s->words[15]=1; uint32_t flag; memcpy(&flag,selected+0x318,4); s->words[17]=flag!=0;
    }
    hr=p->v->set(p,profiles[0].endpoint,s,NULL);
done:
    CoTaskMemFree(f); CoTaskMemFree(s); CoTaskMemFree(d); return hr;
}
static int classify(const AL_Snapshot *a,const SpatialSettings *s) {
    if(s->words[0]) {
        for(int i=0;i<2;i++)if(!memcmp(&s->words[3],&encoders[i],16))return MODE_ATMOS+i;
        return -1;
    }
    if(!al_valid(a))return -1;
    const WAVEFORMATEX *f=(const WAVEFORMATEX*)a->device;
    uint32_t mask=f->wFormatTag==WAVE_FORMAT_EXTENSIBLE?((const WAVEFORMATEXTENSIBLE*)f)->dwChannelMask:a->physical;
    if(f->nChannels==2 && mask==3)return MODE_STEREO;
    if(f->nChannels==6 && (mask==0x3f || mask==0x60f))return MODE_51;
    if(f->nChannels==8 && mask==0x63f)return MODE_71;
    return -1;
}
static HRESULT read_actual(SpatialPolicy *p,AL_Snapshot *a,SpatialSettings *s,int *mode) {
    *mode=-1;HRESULT hr=al_capture(profiles[0].endpoint,a);
    if(SUCCEEDED(hr))hr=read_spatial(p,s);
    if(SUCCEEDED(hr))*mode=classify(a,s);
    return hr;
}
static int matches_profile(int mode,const AL_Snapshot *actual,const SpatialSettings *settings) {
    if(mode<0 || classify(actual,settings)!=mode)return 0;
    if(mode<3)return al_equal(actual,&profiles[mode]);
    const AL_Snapshot *target=(spatial_captured&(1u<<(mode-3)))?&spatial_profiles[mode-3]:&profiles[2];
    return actual->fullrange_present==target->fullrange_present && actual->fullrange==target->fullrange &&
        (!(spatial_captured&(1u<<(mode-3))) || (actual->physical_present==target->physical_present && actual->physical==target->physical));
}
static int actual_equal(const AL_Snapshot *a,const SpatialSettings *s,const AL_Snapshot *b,const SpatialSettings *t) {
    /* al_capture clears unused bytes; only stable selection fields are compared. */
    return !memcmp(a,b,sizeof(*a)) && s->words[0]==t->words[0] && !memcmp(&s->words[3],&t->words[3],16);
}
static void check_capabilities(SpatialPolicy *p) {
    unsigned flags=0;
    for(int i=0;i<3;i++) {
        HRESULT hr=al_supported(&profiles[i]);
        /* S_FALSE means exclusive access disabled, not an unsupported format.
           Preserve the option and let the verified apply decide at selection time. */
        if(SUCCEEDED(hr))flags|=1u<<i;

    }
    if(p) {
        WAVEFORMATEX *f=NULL;SpatialSettings *s=NULL;BYTE *d=NULL;UINT32 n=0;
        HRESULT hr=p->v->get(p,profiles[0].endpoint,FALSE,&f,&s,&n,&d);
        if(SUCCEEDED(hr) && n<=128 && d)for(UINT32 i=0;i<n;i++) {
            DWORD supported=0;memcpy(&supported,d+i*0x342+0x310,4);
            for(int j=0;j<2;j++)if(supported && !memcmp(d+i*0x342+0x300,&encoders[j],16))flags|=1u<<(j+3);
        }
        CoTaskMemFree(f);CoTaskMemFree(s);CoTaskMemFree(d);
    }
    LONG before=InterlockedExchange(&available_modes,(LONG)flags);
    if((unsigned)before!=flags){wchar_t message[80];swprintf(message,80,L"Available mode mask: 0x%02x",flags);log_line(message,S_OK);PostMessageW(window,WM_STATUS,0,0);}
}
static HRESULT capture_intent(SpatialPolicy *p) {
    AL_Snapshot actual;SpatialSettings settings;int mode;
    HRESULT hr=read_actual(p,&actual,&settings,&mode);
    if(FAILED(hr))return hr;
    if(mode<0)return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    if(mode<3) {
        AL_Snapshot previous=profiles[mode];profiles[mode]=actual;
        if(!save_profiles()){profiles[mode]=previous;return E_FAIL;}
    } else {
        int i=mode-3;AL_Snapshot previous=spatial_profiles[i];SpatialSettings old=spatial_settings[i];unsigned flags=spatial_captured;
        spatial_profiles[i]=actual;spatial_settings[i]=settings;spatial_captured|=1u<<i;
        if(!save_profiles()){spatial_profiles[i]=previous;spatial_settings[i]=old;spatial_captured=flags;return E_FAIL;}
    }
    check_capabilities(p);publish(mode,L"意图已捕获并保存");return S_OK;
}
static HRESULT switch_mode(SpatialPolicy *p,int mode) {
    if(mode<0 || mode>=MODE_COUNT || !(InterlockedCompareExchange(&available_modes,0,0)&(1<<mode)))return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    AL_Snapshot before; SpatialSettings original; HRESULT rollback=S_FALSE;
    HRESULT hr=al_capture(profiles[0].endpoint,&before);
    if(SUCCEEDED(hr)) hr=read_spatial(p,&original);
    if(FAILED(hr)) return hr;
    if(mode<MODE_ATMOS) {
        if(original.words[0]) hr=spatial_select(p,mode);
        if(SUCCEEDED(hr)) hr=al_apply(&profiles[mode],&rollback);
    } else {
        /* Set the captured full-range property only: never replay an encoded format. */
        Policy *old=NULL; hr=policy_open(&old);
        if(SUCCEEDED(hr)) {
            const AL_Snapshot *target=(spatial_captured&(1u<<(mode-3)))?&spatial_profiles[mode-3]:&profiles[2];
            if(spatial_captured&(1u<<(mode-3)))hr=write_mask(old,profiles[0].endpoint,&PKEY_AudioEndpoint_PhysicalSpeakers,target->physical_present,target->physical);
            if(SUCCEEDED(hr))hr=write_mask(old,profiles[0].endpoint,&PKEY_AudioEndpoint_FullRangeSpeakers,target->fullrange_present,target->fullrange);
            old->v->release(old);
        }
        if(SUCCEEDED(hr)) hr=spatial_select(p,mode);
    }
    if(SUCCEEDED(hr)) {
        hr=E_FAIL;
        for(int i=0;i<15;i++) {
            AL_Snapshot state;SpatialSettings selection;int actual=-1;
            HRESULT read=read_actual(p,&state,&selection,&actual);
            if(SUCCEEDED(read) && matches_profile(mode,&state,&selection)){hr=S_OK;break;}
            Sleep(100);
        }
    }
    if(FAILED(hr)) {
        /* Restore selection through the service; only PCM snapshots use al_apply. */
        rollback=p?p->v->set(p,profiles[0].endpoint,&original,NULL):S_OK;
        if(SUCCEEDED(rollback) && !original.words[0] && al_valid(&before)) rollback=al_apply(&before,NULL);
        if(SUCCEEDED(rollback) && original.words[0]) {
            Policy *old=NULL; rollback=policy_open(&old);
            if(SUCCEEDED(rollback)) {
                rollback=write_mask(old,profiles[0].endpoint,&PKEY_AudioEndpoint_PhysicalSpeakers,before.physical_present,before.physical);
                if(SUCCEEDED(rollback))rollback=write_mask(old,profiles[0].endpoint,&PKEY_AudioEndpoint_FullRangeSpeakers,before.fullrange_present,before.fullrange);
                old->v->release(old);
            }
        }
        log_line(L"Switch rollback attempt",rollback);
    }
    return hr;
}
static unsigned __stdcall worker(void *unused) {
    (void)unused; HRESULT hr=CoInitializeEx(NULL,COINIT_MULTITHREADED);
    if(FAILED(hr)) { publish(-1,L"COM initialization failed"); return 1; }
    SpatialPolicy *p=NULL;
    hr=CoCreateInstance(&spatial_policy_class,NULL,CLSCTX_ALL,&spatial_policy_iid,(void**)&p);
    if(FAILED(hr))log_line(L"Spatial interface unavailable; PCM only",hr);
    int first=0;
    if(!initialize_profiles(&first)) {publish(-1,L"配置初始化失败；请查看日志");if(p)p->v->release(p);CoUninitialize();return 1;}
    check_capabilities(p);
    if(first && !(available_modes&24) && !quiet_start)PostMessageW(window,WM_NOTICE,1,0);
    AL_Snapshot baseline={0};SpatialSettings baseline_spatial={0};int have_baseline=0;
    ULONGLONG capability_due=GetTickCount64()+30000;
    Debounce d={-1,0}; int cached=-1,verified=-1; ULONGLONG audit=0,retry=0; int failed_target=-1;
    LONG generation=-1;
    HANDLE events[]={stop,wake};
    while(WaitForSingleObject(stop,0)!=WAIT_OBJECT_0) {
        if(InterlockedExchange(&reload_requested,0)) { load_rules(); d.pending=-1; }
        LONG latest=InterlockedCompareExchange(&control_generation,0,0);
        if(latest!=generation) { generation=latest; d.pending=-1; audit=0; }
        if(InterlockedExchange(&capture_requested,0)) {
            hr=capture_intent(p);log_line(L"Intent capture",hr);PostMessageW(window,WM_NOTICE,SUCCEEDED(hr)?2:3,0);audit=0;
        }
        int auto_mode,lock_mode;
        AcquireSRWLockShared(&state_lock); auto_mode=automatic; lock_mode=manual; ReleaseSRWLockShared(&state_lock);
        InterlockedExchange(&foreground_needed,auto_mode && has_foreground);
        ULONGLONG now=GetTickCount64();
        if(now>=audit) {
            AL_Snapshot actual;SpatialSettings settings;
            hr=read_actual(p,&actual,&settings,&cached);
            if(SUCCEEDED(hr)) {
                if(have_baseline && !actual_equal(&actual,&settings,&baseline,&baseline_spatial)) {
                    AcquireSRWLockExclusive(&state_lock);automatic=0;manual=-1;ReleaseSRWLockExclusive(&state_lock);
                    auto_mode=0;lock_mode=-1;d.pending=-1;PostMessageW(window,WM_STATUS,0,0);
                    log_line(L"External audio change: manual lock",S_OK);
                }
                baseline=actual;baseline_spatial=settings;have_baseline=1;verified=matches_profile(cached,&actual,&settings)?cached:-1;
            } else {cached=-1;verified=-1;have_baseline=0;}
            if(now>=capability_due){
                AL_Snapshot endpoint;
                if(SUCCEEDED(al_capture(NULL,&endpoint)) && wcscmp(endpoint.endpoint,profiles[0].endpoint)) {
                    AcquireSRWLockExclusive(&state_lock);automatic=0;manual=-1;ReleaseSRWLockExclusive(&state_lock);
                    auto_mode=0;lock_mode=-1;have_baseline=0;cached=-1;verified=-1;d.pending=-1;
                    int first=0;
                    if(!initialize_profiles(&first))InterlockedExchange(&available_modes,0);
                    else {check_capabilities(p);if(first && !(available_modes&24))PostMessageW(window,WM_NOTICE,1,0);}
                } else check_capabilities(p);
                capability_due=now+30000;
            }
            if(SUCCEEDED(hr)) InterlockedExchange(&worker_read_ok,1);
            publish(cached,FAILED(hr)?L"Endpoint unavailable":L"Ready"); audit=now+10000;
        }
        int target=lock_mode;
        if(auto_mode && !observe_rules(&target)) target=-1;
        if(target>=0 && !(available_modes&(1<<target)))target=-1;
        if(target>=0 && (lock_mode>=0 || settled(&d,target,now,debounce_ms)) && target!=verified &&
           (target!=failed_target || now>=retry)) {
            if(generation!=InterlockedCompareExchange(&control_generation,0,0))continue;
            if(have_baseline) {
                AL_Snapshot live;SpatialSettings live_settings;int mode;
                HRESULT read=read_actual(p,&live,&live_settings,&mode);
                if(FAILED(read)){audit=0;continue;}
                if(!actual_equal(&live,&live_settings,&baseline,&baseline_spatial)) {audit=0;continue;}
            }
            publish(cached,L"Switching...");
            hr=switch_mode(p,target);
            log_line(names[target],hr);
            if(SUCCEEDED(hr)) {
                cached=verified=target;failed_target=-1;publish(cached,L"Verified");
                int observed;have_baseline=SUCCEEDED(read_actual(p,&baseline,&baseline_spatial,&observed));
                AcquireSRWLockExclusive(&state_lock);if(!automatic && manual==target)manual=-1;ReleaseSRWLockExclusive(&state_lock);
            }
            else { cached=verified=-1; failed_target=target; retry=now+30000; publish(-1,L"Switch failed; see log (30s backoff)"); }
            audit=GetTickCount64()+10000;
        }
        ULONGLONG after=GetTickCount64();
        ULONGLONG due=(auto_mode && target>=0 && d.pending>=0 && after-d.since<debounce_ms)?d.since+debounce_ms:0;
        DWORD delay=next_wait(after,audit,auto_mode && enabled_rules,due,
                             target>=0 && target==failed_target && retry>after?retry:0);
        if(WaitForMultipleObjects(2,events,FALSE,delay)==WAIT_OBJECT_0) break;
    }
    if(p)p->v->release(p); CoUninitialize(); return 0;
}
static NOTIFYICONDATAW tray;
static void update_tray(void) {
    AcquireSRWLockShared(&state_lock);
    /* Show the verified output mode even while paused or manually locked.
       The app identity icon is never assigned to the tray. */
    tray.hIcon=current>=0 && current<MODE_COUNT?mode_icons[current]:LoadIconW(NULL,IDI_QUESTION);
    swprintf(tray.szTip,128,L"Final Spatial Audio: %ls\n%ls - %ls",current>=0?names[current]:L"Custom / unknown",automatic?L"自动切换":L"手动锁定",status);
    ReleaseSRWLockShared(&state_lock); Shell_NotifyIconW(NIM_MODIFY,&tray);
}
static int startup_enabled(void) {
    wchar_t value[2*MAX_PATH],path[MAX_PATH],expected[2*MAX_PATH];DWORD bytes=sizeof(value);
    GetModuleFileNameW(NULL,path,MAX_PATH);swprintf(expected,2*MAX_PATH,L"\"%ls\"",path);
    return RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",L"FinalSpatialAudio",RRF_RT_REG_SZ,NULL,value,&bytes)==ERROR_SUCCESS && !_wcsicmp(value,expected);
}
static void toggle_startup(void) {
    HKEY key;LONG result=RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,NULL,0,KEY_SET_VALUE,NULL,&key,NULL);
    if(result==ERROR_SUCCESS) {
        if(startup_enabled())result=RegDeleteValueW(key,L"FinalSpatialAudio");
        else {wchar_t path[MAX_PATH],value[2*MAX_PATH];GetModuleFileNameW(NULL,path,MAX_PATH);swprintf(value,2*MAX_PATH,L"\"%ls\"",path);result=RegSetValueExW(key,L"FinalSpatialAudio",0,REG_SZ,(BYTE*)value,(DWORD)((wcslen(value)+1)*sizeof(wchar_t)));}
        RegCloseKey(key);
    }
    if(result!=ERROR_SUCCESS)MessageBoxW(window,L"无法更新开机自启设置。",L"Final Spatial Audio",MB_ICONERROR);
}
static void open_text(const wchar_t *path) {
    wchar_t argument[2*MAX_PATH];swprintf(argument,2*MAX_PATH,L"\"%ls\"",path);
    ShellExecuteW(window,L"open",L"notepad.exe",argument,NULL,SW_SHOWNORMAL);
}
static void menu(void) {
    static int showing=0;if(showing)return;
    FSA_MenuState state;
    AcquireSRWLockShared(&state_lock);state.selected=current;state.automatic=automatic;ReleaseSRWLockShared(&state_lock);
    state.available=(unsigned)InterlockedCompareExchange(&available_modes,0,0);state.startup=startup_enabled();
    showing=1;UINT command=fsa_popup(window,&state);showing=0;
    if(!IsWindow(window))return;
    if(command>=100 && command<105 && (available_modes&(1<<(command-100)))) {
        AcquireSRWLockExclusive(&state_lock);manual=command-100;automatic=0;ReleaseSRWLockExclusive(&state_lock);
    } else if(command==200) {
        AcquireSRWLockExclusive(&state_lock);manual=-1;automatic=!automatic;ReleaseSRWLockExclusive(&state_lock);
    } else if(command==202)open_text(ini);
    else if(command==203){InterlockedExchange(&reload_requested,1);SetEvent(wake);}
    else if(command==204)open_text(logpath);
    else if(command==205)DestroyWindow(window);
    else if(command==206) {
        AcquireSRWLockExclusive(&state_lock);automatic=0;manual=-1;ReleaseSRWLockExclusive(&state_lock);
        InterlockedExchange(&capture_requested,1);
    } else if(command==207)toggle_startup();
    if((command>=100 && command<105) || command==200 || command==206){InterlockedIncrement(&control_generation);SetEvent(wake);}
    if(IsWindow(window))update_tray();
}
static void CALLBACK foreground_event(HWINEVENTHOOK hook,DWORD event,HWND hwnd,LONG object,LONG child,DWORD tid,DWORD time) {
    (void)hook;(void)event;(void)hwnd;(void)object;(void)child;(void)tid;(void)time;
    if(InterlockedCompareExchange(&foreground_needed,0,0)) SetEvent(wake);
}
static LRESULT CALLBACK wndproc(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    static UINT recreated;
    if(!recreated) recreated=RegisterWindowMessageW(L"TaskbarCreated");
    if(msg==recreated) { Shell_NotifyIconW(NIM_ADD,&tray); return 0; }
    if(msg==WM_TRAY && (l==WM_RBUTTONUP || l==WM_LBUTTONUP)) { menu(); return 0; }
    if(msg==WM_NOTICE) {
        const wchar_t *message=w==1?L"当前输出设备未报告可用的家庭影院空间音频模式（Dolby Atmos / DTS:X）。可能与设备、连接方式或空间音频组件有关。不可用的模式已置灰；可继续使用支持的标准模式。":
            w==2?L"已捕获当前系统音频设置，并更新对应模式的配置。":L"捕获失败：当前状态无法识别为受支持模式，或配置无法保存。请查看日志。";
        MessageBoxW(hwnd,message,L"Final Spatial Audio",MB_OK|(w==2?MB_ICONINFORMATION:MB_ICONWARNING));return 0;
    }
    if(msg==WM_STATUS) { update_tray(); return 0; }
    if(msg==WM_SETTINGCHANGE || msg==WM_DPICHANGED) {
        if(load_mode_icons((HINSTANCE)GetWindowLongPtrW(hwnd,GWLP_HINSTANCE))) update_tray();
        return 0;
    }
    if(msg==WM_TIMER) { DestroyWindow(hwnd); return 0; }
    if(msg==WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    if(msg==WM_DESTROY) { SetEvent(stop); Shell_NotifyIconW(NIM_DELETE,&tray); PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd,msg,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,LPWSTR cmd,int show) {
    (void)previous;(void)show;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    while(*cmd==L' ') cmd++;
    size_t command_length=wcslen(cmd);
    while(command_length && cmd[command_length-1]==L' ') cmd[--command_length]=0;
    if(!wcscmp(cmd,L"--stop")) {
        HWND existing=FindWindowW(L"FinalSpatialAudioWindow",L"Final Spatial Audio");
        if(!existing) return 0;
        DWORD pid=0; GetWindowThreadProcessId(existing,&pid);
        HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,pid);
        if(!process) return 1;
        PostMessageW(existing,WM_CLOSE,0,0);
        DWORD wait=WaitForSingleObject(process,15000); CloseHandle(process);
        return wait==WAIT_OBJECT_0?0:1;
    }
    GetModuleFileNameW(NULL,root,MAX_PATH);
    wchar_t *slash=wcsrchr(root,L'\\'); if(!slash) return 1; *slash=0;
    slash=wcsrchr(root,L'\\'); if(!slash) return 1; *slash=0;
    swprintf(ini,MAX_PATH,L"%ls\\final-spatial-audio.ini",root); swprintf(logpath,MAX_PATH,L"%ls\\final-spatial-audio.log",root);
    log_line(L"Startup",S_OK);
    quiet_start=!wcscmp(cmd,L"--smoke") || !wcscmp(cmd,L"--check");
    if(!wcscmp(cmd,L"--check")) {
        HRESULT hr=CoInitializeEx(NULL,COINIT_MULTITHREADED);if(FAILED(hr))return 1;
        int first=0,ok=initialize_profiles(&first);load_rules();CoUninitialize();return ok?0:1;
    }
    HANDLE mutex=CreateMutexW(NULL,FALSE,L"Local\\FinalSpatialAudio");
    if(!mutex || GetLastError()==ERROR_ALREADY_EXISTS) { if(mutex) CloseHandle(mutex); return 0; }
    wake=CreateEventW(NULL,FALSE,FALSE,NULL); stop=CreateEventW(NULL,TRUE,FALSE,NULL);
    if(!load_mode_icons(instance)) { log_line(L"Mode icon resources unavailable",E_FAIL); return 1; }
    app_icon=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(IDI_FSA_APP),IMAGE_ICON,GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),0);
    app_icon_small=(HICON)LoadImageW(instance,MAKEINTRESOURCEW(IDI_FSA_APP),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),0);
    if(!app_icon || !app_icon_small) { release_icons(); return 1; }
    WNDCLASSW wc={0}; wc.lpfnWndProc=wndproc; wc.hInstance=instance; wc.lpszClassName=L"FinalSpatialAudioWindow";
    wc.hIcon=app_icon;
    if(!wake || !stop || !RegisterClassW(&wc)) return 1;
    window=CreateWindowExW(0,wc.lpszClassName,L"Final Spatial Audio",0,0,0,0,0,NULL,NULL,instance,NULL);
    if(!window) return 1;
    SendMessageW(window,WM_SETICON,ICON_SMALL,(LPARAM)app_icon_small);
    tray.cbSize=sizeof(tray); tray.hWnd=window; tray.uID=1; tray.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;
    tray.uCallbackMessage=WM_TRAY; tray.hIcon=LoadIconW(NULL,IDI_QUESTION); wcscpy(tray.szTip,L"Final Spatial Audio（手动锁定）");
    if(!Shell_NotifyIconW(NIM_ADD,&tray)) { log_line(L"Tray creation failed",HRESULT_FROM_WIN32(GetLastError())); DestroyWindow(window); return 1; }
    thread=(HANDLE)_beginthreadex(NULL,0,worker,NULL,0,NULL);
    if(!thread) { DestroyWindow(window); return 1; }
    if(!wcscmp(cmd,L"--smoke")) SetTimer(window,1,2000,NULL);
    HWINEVENTHOOK hook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,NULL,foreground_event,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    MSG msg; while(GetMessageW(&msg,NULL,0,0)>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    if(hook) UnhookWinEvent(hook); SetEvent(stop); WaitForSingleObject(thread,INFINITE);
    CloseHandle(thread); CloseHandle(wake); CloseHandle(stop); CloseHandle(mutex);
    release_icons();
    return !wcscmp(cmd,L"--smoke") && !worker_read_ok ? 1 : 0;
}
