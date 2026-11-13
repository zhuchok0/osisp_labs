#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>

typedef unsigned long long UtcTicks;

static const UtcTicks TICKS_PER_SECOND = 10000000ULL;
static const UtcTicks SECONDS_PER_DAY  = 86400ULL;

struct ProgramSettings {
    int         studentId          = 47;                  
    int         runId              = 1;                
    int         validityDays       = 7;                  
    int         recordCount        = 100;                   
    unsigned    randomSeed         = 0;                      
    std::string referenceTimeText  = "2026-03-01T00:00:00Z";
    int         repeatCount        = 3;           
    std::string workMode           = "selftest"; 
    bool        dryRun             = false;                 
    std::string logFilePath        = "lab06.log";
};

// ======================================================================
// Logging
static FILE* g_logFile = nullptr;

static void writeLog(const char* format, ...) {
    SYSTEMTIME nowUtc;
    GetSystemTime(&nowUtc);

    char message[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof message, format, args);
    va_end(args);

    char fullLine[1200];
    snprintf(fullLine, sizeof fullLine, "[%02d:%02d:%02d.%03d] %s\n",
             nowUtc.wHour, nowUtc.wMinute, nowUtc.wSecond, nowUtc.wMilliseconds, message);

    fputs(fullLine, stdout);
    if (g_logFile) {
        fputs(fullLine, g_logFile);
        fflush(g_logFile);
    }
}

static void reportApiError(const char* apiFunctionName, LONG errorCode, const char* operationDescription) {
    char errorText[512] = "";
    DWORD textLength = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                      nullptr, (DWORD)errorCode, 0, errorText, sizeof errorText, nullptr);
    while (textLength && (errorText[textLength - 1] == '\r' ||
                          errorText[textLength - 1] == '\n' ||
                          errorText[textLength - 1] == ' ')) {
        errorText[--textLength] = 0;
    }
    writeLog("ERROR function=%s code=%ld text=\"%s\" operation=%s",
             apiFunctionName, errorCode, errorText, operationDescription);
}

// ======================================================================
// Time measurement
static double getElapsedMilliseconds() {
    static LARGE_INTEGER countsPerSecond = [] {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        return frequency;
    }();
    LARGE_INTEGER currentCounter;
    QueryPerformanceCounter(&currentCounter);
    return 1000.0 * (double)currentCounter.QuadPart / (double)countsPerSecond.QuadPart;
}

// ======================================================================
// UTC time conversion
static bool parseUtcTimestamp(const std::string& text, UtcTicks& resultTicks) {
    if (text.size() != 20) return false;

    int year, month, day, hour, minute, second;
    char zoneLetter = 0;
    if (sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%c",
               &year, &month, &day, &hour, &minute, &second, &zoneLetter) != 7 || zoneLetter != 'Z') {
        return false;
    }

    SYSTEMTIME systemTime = {};
    systemTime.wYear   = (WORD)year;
    systemTime.wMonth  = (WORD)month;
    systemTime.wDay    = (WORD)day;
    systemTime.wHour   = (WORD)hour;
    systemTime.wMinute = (WORD)minute;
    systemTime.wSecond = (WORD)second;

    FILETIME fileTime;
    if (!SystemTimeToFileTime(&systemTime, &fileTime)) return false;   // also validates the ranges
    resultTicks = ((UtcTicks)fileTime.dwHighDateTime << 32) | fileTime.dwLowDateTime;
    return true;
}

static std::string formatUtcTimestamp(UtcTicks ticks) {
    FILETIME fileTime;
    fileTime.dwLowDateTime  = (DWORD)(ticks & 0xFFFFFFFFULL);
    fileTime.dwHighDateTime = (DWORD)(ticks >> 32);

    SYSTEMTIME systemTime;
    if (!FileTimeToSystemTime(&fileTime, &systemTime)) return "invalid";

    char text[64];
    snprintf(text, sizeof text, "%04d-%02d-%02dT%02d:%02d:%02dZ",
             systemTime.wYear, systemTime.wMonth, systemTime.wDay,
             systemTime.wHour, systemTime.wMinute, systemTime.wSecond);
    return text;
}

static UtcTicks getCurrentUtcTime() {
    FILETIME fileTime;
    GetSystemTimeAsFileTime(&fileTime);
    return ((UtcTicks)fileTime.dwHighDateTime << 32) | fileTime.dwLowDateTime;
}

// ======================================================================
// Configuration record
struct ConfigRecord {
    std::string recordId;           // identifier - registry value name
    std::string payload;            // stored value
    UtcTicks    createdUtc = 0;   
    UtcTicks    expiresUtc = 0;     
};

static std::string recordToRegistryText(const ConfigRecord& record) {
    return formatUtcTimestamp(record.createdUtc) + "|" +
           formatUtcTimestamp(record.expiresUtc) + "|" +
           record.payload;
}

static bool registryTextToRecord(const std::string& recordId, const std::string& registryText,
                                 ConfigRecord& record) {
    size_t firstSeparator = registryText.find('|');
    if (firstSeparator == std::string::npos) return false;
    size_t secondSeparator = registryText.find('|', firstSeparator + 1);
    if (secondSeparator == std::string::npos) return false;

    record.recordId = recordId;
    record.payload  = registryText.substr(secondSeparator + 1);

    std::string createdText = registryText.substr(0, firstSeparator);
    std::string expiresText = registryText.substr(firstSeparator + 1, secondSeparator - firstSeparator - 1);
    if (!parseUtcTimestamp(createdText, record.createdUtc)) return false;
    if (!parseUtcTimestamp(expiresText, record.expiresUtc)) return false;
    return record.expiresUtc > record.createdUtc;
}

static bool isRecordExpired(const ConfigRecord& record, UtcTicks currentTimeUtc) {
    return record.expiresUtc <= currentTimeUtc;
}

// ======================================================================
// Command line
static bool parseCommandLine(int argumentCount, char** arguments, ProgramSettings& settings) {
    for (int i = 1; i < argumentCount; ++i) {
        std::string option = arguments[i];

        auto readOptionValue = [&](const char* optionName) -> const char* {
            if (i + 1 >= argumentCount) {
                fprintf(stderr, "missing value for %s\n", optionName);
                return nullptr;
            }
            return arguments[++i];
        };

        const char* optionValue;
        if (option == "--dry-run") {
            settings.dryRun = true;
        } else if (option == "--student") {
            if (!(optionValue = readOptionValue("--student"))) return false;
            settings.studentId = atoi(optionValue);
        } else if (option == "--run") {
            if (!(optionValue = readOptionValue("--run"))) return false;
            settings.runId = atoi(optionValue);
        } else if (option == "--ttl") {
            if (!(optionValue = readOptionValue("--ttl"))) return false;
            settings.validityDays = atoi(optionValue);
        } else if (option == "--count") {
            if (!(optionValue = readOptionValue("--count"))) return false;
            settings.recordCount = atoi(optionValue);
        } else if (option == "--seed") {
            if (!(optionValue = readOptionValue("--seed"))) return false;
            settings.randomSeed = (unsigned)strtoul(optionValue, nullptr, 10);
        } else if (option == "--now") {
            if (!(optionValue = readOptionValue("--now"))) return false;
            settings.referenceTimeText = optionValue;
        } else if (option == "--repeat") {
            if (!(optionValue = readOptionValue("--repeat"))) return false;
            settings.repeatCount = atoi(optionValue);
        } else if (option == "--mode") {
            if (!(optionValue = readOptionValue("--mode"))) return false;
            settings.workMode = optionValue;
        } else if (option == "--log") {
            if (!(optionValue = readOptionValue("--log"))) return false;
            settings.logFilePath = optionValue;
        } else {
            fprintf(stderr, "unknown argument: %s\n", option.c_str());
            return false;
        }
    }

    if (settings.studentId < 1 || settings.runId < 1 || settings.validityDays < 1 ||
        settings.recordCount < 3 || settings.repeatCount < 1) {
        fprintf(stderr, "invalid parameter value (student, run, ttl, repeat >= 1; count >= 3)\n");
        return false;
    }

    if (settings.randomSeed == 0) settings.randomSeed = 1000u + (unsigned)settings.studentId * 97u;
    return true;
}


int main(int argumentCount, char** arguments) {
    ProgramSettings settings;
    if (!parseCommandLine(argumentCount, arguments, settings)) {
        fprintf(stderr,
                "usage: %s --mode (none yet) [--student N] [--run N] [--ttl 1|7|30]\n"
                "       [--count N] [--seed N] [--now ISO_UTC|system] [--repeat N] [--dry-run] [--log file]\n",
                arguments[0]);
        return 2;
    }
    g_logFile = fopen(settings.logFilePath.c_str(), "a");

    if (settings.studentId > 32) {
        writeLog("WARNING student=%d is outside the 1..32 range from the assignment", settings.studentId);
    }
    writeLog("START mode=%s student=%d run=%d validity_days=%d count=%d seed=%u",
             settings.workMode.c_str(), settings.studentId, settings.runId, settings.validityDays,
             settings.recordCount, settings.randomSeed);

    int exitCode = 0;

    fprintf(stderr, "unknown mode: %s\n", settings.workMode.c_str());
    exitCode = 2;

    writeLog("END exit_code=%d", exitCode);
    if (g_logFile) fclose(g_logFile);
    return exitCode;
}
