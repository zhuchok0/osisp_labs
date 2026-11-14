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
// Registry helpers
static std::string buildLearningKeyPath(const ProgramSettings& settings) {
    char path[128];
    snprintf(path, sizeof path, "Software\\OSiSP\\Lab06\\Student_%d\\Run_%d",
             settings.studentId, settings.runId);
    return path;
}

struct OpenRegistryKey {
    HKEY handle = nullptr;
    ~OpenRegistryKey() {
        if (handle) {
            LONG status = RegCloseKey(handle);
            if (status == ERROR_SUCCESS) writeLog("REG_CLOSE status=OK");
            else reportApiError("RegCloseKey", status, "close learning key");
        }
    }
};

static bool openLearningKey(const ProgramSettings& settings, bool createIfMissing,
                            REGSAM accessRights, OpenRegistryKey& openedKey) {
    std::string keyPath = buildLearningKeyPath(settings);
    LONG status;

    if (createIfMissing) {
        DWORD disposition = 0;
        status = RegCreateKeyExA(HKEY_CURRENT_USER, keyPath.c_str(), 0, nullptr, 0,
                                 accessRights, nullptr, &openedKey.handle, &disposition);
        if (status != ERROR_SUCCESS) {
            openedKey.handle = nullptr;
            reportApiError("RegCreateKeyExA", status, keyPath.c_str());
            return false;
        }
        writeLog("REG_CREATE key=HKCU\\%s status=OK disposition=%s", keyPath.c_str(),
                 disposition == REG_CREATED_NEW_KEY ? "created" : "opened_existing");
    } else {
        status = RegOpenKeyExA(HKEY_CURRENT_USER, keyPath.c_str(), 0, accessRights, &openedKey.handle);
        if (status != ERROR_SUCCESS) {
            openedKey.handle = nullptr;
            reportApiError("RegOpenKeyExA", status, keyPath.c_str());
            return false;
        }
        writeLog("REG_OPEN key=HKCU\\%s status=OK", keyPath.c_str());
    }
    return true;
}

static bool listValueNames(HKEY keyHandle, std::vector<std::string>& valueNames) {
    valueNames.clear();
    std::vector<char> nameBuffer(16384);   // 16383 is the max value-name length

    for (DWORD valueIndex = 0;; ++valueIndex) {
        DWORD nameLength = (DWORD)nameBuffer.size();
        LONG status = RegEnumValueA(keyHandle, valueIndex, nameBuffer.data(), &nameLength,
                                    nullptr, nullptr, nullptr, nullptr);
        if (status == ERROR_NO_MORE_ITEMS) break;
        if (status != ERROR_SUCCESS) {
            reportApiError("RegEnumValueA", status, "enumerate values");
            return false;
        }
        valueNames.push_back(std::string(nameBuffer.data(), nameLength));
    }
    return true;
}

static bool readStringValue(HKEY keyHandle, const std::string& valueName,
                            std::string& valueText, bool& valueHasWrongType) {
    DWORD valueType = 0;
    DWORD requiredBytes = 0;
    valueHasWrongType = false;

    // First call: only ask for the data size
    LONG status = RegQueryValueExA(keyHandle, valueName.c_str(), nullptr, &valueType, nullptr, &requiredBytes);
    if (status != ERROR_SUCCESS) {
        reportApiError("RegQueryValueExA", status, ("query size of " + valueName).c_str());
        return false;
    }
    if (valueType != REG_SZ) {
        valueHasWrongType = true;
        return false;
    }

    // Second call: read the data
    std::vector<char> dataBuffer(requiredBytes + 1, 0);
    DWORD bytesRead = requiredBytes;
    status = RegQueryValueExA(keyHandle, valueName.c_str(), nullptr, &valueType,
                              (LPBYTE)dataBuffer.data(), &bytesRead);
    if (status != ERROR_SUCCESS) {
        reportApiError("RegQueryValueExA", status, ("read " + valueName).c_str());
        return false;
    }
    dataBuffer[bytesRead] = 0; 
    valueText = dataBuffer.data();
    return true;
}

// ======================================================================
// Test data generation
struct ExpectedOutcome {
    std::string recordId;
    bool        shouldBeDeleted;   // true = record is expired
};

static unsigned nextPseudoRandom(unsigned& generatorState) {
    generatorState = (generatorState * 1103515245u + 12345u) & 0x7FFFFFFFu;
    return generatorState >> 8;
}

static bool generateTestRecords(const ProgramSettings& settings, UtcTicks referenceTimeUtc,
                                unsigned randomSeed, std::vector<ExpectedOutcome>& expectedOutcomes) {
    OpenRegistryKey learningKey;
    if (!openLearningKey(settings, true, KEY_SET_VALUE, learningKey)) return false;

    expectedOutcomes.clear();
    unsigned generatorState = randomSeed;
    const UtcTicks validitySeconds = (UtcTicks)settings.validityDays * SECONDS_PER_DAY;
    int writeErrorCount = 0;

    for (int recordIndex = 0; recordIndex < settings.recordCount; ++recordIndex) {
        UtcTicks ageSeconds;   // how long ago the record was created
        if (recordIndex == 0)      ageSeconds = validitySeconds;
        else if (recordIndex == 1) ageSeconds = validitySeconds - 1;
        else if (recordIndex == 2) ageSeconds = validitySeconds + 1;
        else                       ageSeconds = (UtcTicks)nextPseudoRandom(generatorState) % (2 * validitySeconds);

        char recordIdText[32];
        snprintf(recordIdText, sizeof recordIdText, "rec_%05d", recordIndex + 1);

        char payloadText[48];
        snprintf(payloadText, sizeof payloadText, "value_%u", nextPseudoRandom(generatorState));

        ConfigRecord record;
        record.recordId   = recordIdText;
        record.payload    = payloadText;
        record.createdUtc = referenceTimeUtc - ageSeconds * TICKS_PER_SECOND;
        record.expiresUtc = record.createdUtc + validitySeconds * TICKS_PER_SECOND;

        std::string registryText = recordToRegistryText(record);
        LONG status = RegSetValueExA(learningKey.handle, recordIdText, 0, REG_SZ,
                                     (const BYTE*)registryText.c_str(), (DWORD)registryText.size() + 1);
        if (status != ERROR_SUCCESS) {
            reportApiError("RegSetValueExA", status, recordIdText);
            ++writeErrorCount;
            continue;
        }
        expectedOutcomes.push_back({ recordIdText, ageSeconds >= validitySeconds });
    }

    writeLog("REG_GEN count=%d validity_days=%d seed=%u errors=%d",
             settings.recordCount, settings.validityDays, randomSeed, writeErrorCount);
    return writeErrorCount == 0;
}

// ======================================================================
// Listing of all records with their state
static bool printAllRecords(const ProgramSettings& settings, UtcTicks currentTimeUtc) {
    OpenRegistryKey learningKey;
    if (!openLearningKey(settings, false, KEY_QUERY_VALUE, learningKey)) return false;

    std::vector<std::string> valueNames;
    if (!listValueNames(learningKey.handle, valueNames)) return false;

    for (const std::string& valueName : valueNames) {
        std::string registryText;
        bool valueHasWrongType;
        if (!readStringValue(learningKey.handle, valueName, registryText, valueHasWrongType)) {
            writeLog("REG_LIST id=%s status=UNREADABLE", valueName.c_str());
            continue;
        }
        ConfigRecord record;
        if (!registryTextToRecord(valueName, registryText, record)) {
            writeLog("REG_LIST id=%s status=BAD_FORMAT", valueName.c_str());
            continue;
        }
        writeLog("REG_LIST id=%s created=%s expires=%s state=%s", valueName.c_str(),
                 formatUtcTimestamp(record.createdUtc).c_str(),
                 formatUtcTimestamp(record.expiresUtc).c_str(),
                 isRecordExpired(record, currentTimeUtc) ? "EXPIRED" : "VALID");
    }
    writeLog("REG_LIST count=%zu", valueNames.size());
    return true;
}

// ======================================================================
// Cleanup of expired records
struct CleanupStatistics {
    int    scannedCount       = 0;
    int    expiredFoundCount  = 0; 
    int    deletedCount       = 0; 
    int    keptCount          = 0; 
    int    badFormatCount     = 0;
    int    errorCount         = 0;
    double elapsedMs          = 0;  
};

static bool deleteExpiredRecords(const ProgramSettings& settings, UtcTicks currentTimeUtc,
                                 CleanupStatistics& statistics) {
    statistics = CleanupStatistics();

    OpenRegistryKey learningKey;
    if (!openLearningKey(settings, false, KEY_QUERY_VALUE | KEY_SET_VALUE, learningKey)) return false;

    double startTimeMs = getElapsedMilliseconds();

    std::vector<std::string> valueNames;
    if (!listValueNames(learningKey.handle, valueNames)) return false;
    writeLog("REG_ENUM key=Run_%d count=%zu", settings.runId, valueNames.size());

    for (const std::string& valueName : valueNames) {
        ++statistics.scannedCount;

        std::string registryText;
        bool valueHasWrongType;
        if (!readStringValue(learningKey.handle, valueName, registryText, valueHasWrongType)) {
            if (valueHasWrongType) writeLog("REG_SKIP id=%s reason=not_REG_SZ", valueName.c_str());
            ++statistics.errorCount;
            ++statistics.keptCount;            // unreadable: never delete
            continue;
        }

        ConfigRecord record;
        if (!registryTextToRecord(valueName, registryText, record)) {
            writeLog("REG_SKIP id=%s reason=bad_format", valueName.c_str());
            ++statistics.badFormatCount;
            ++statistics.keptCount;            // malformed: never delete
            continue;
        }

        if (!isRecordExpired(record, currentTimeUtc)) {
            ++statistics.keptCount;            // still valid: keep
            continue;
        }

        ++statistics.expiredFoundCount;
        std::string expiresText = formatUtcTimestamp(record.expiresUtc);

        if (settings.dryRun) {
            writeLog("REG_DELETE id=%s expires=%s status=DRY_RUN", valueName.c_str(), expiresText.c_str());
            continue;
        }

        LONG status = RegDeleteValueA(learningKey.handle, valueName.c_str());
        if (status == ERROR_SUCCESS) {
            ++statistics.deletedCount;
            writeLog("REG_DELETE id=%s expires=%s status=OK", valueName.c_str(), expiresText.c_str());
        } else {
            reportApiError("RegDeleteValueA", status, valueName.c_str());
            ++statistics.errorCount;
            ++statistics.keptCount;            // deletion failed: record is still there
        }
    }

    statistics.elapsedMs = getElapsedMilliseconds() - startTimeMs;
    writeLog("REG_CLEAN scanned=%d expired=%d deleted=%d kept=%d bad_format=%d errors=%d time_ms=%.3f",
             statistics.scannedCount, statistics.expiredFoundCount, statistics.deletedCount,
             statistics.keptCount, statistics.badFormatCount, statistics.errorCount, statistics.elapsedMs);
    return true;
}


static bool deleteLearningRunKey(const ProgramSettings& settings) {
    std::string keyPath = buildLearningKeyPath(settings);
    LONG status = RegDeleteTreeA(HKEY_CURRENT_USER, keyPath.c_str());
    if (status == ERROR_SUCCESS) {
        writeLog("REG_RESET key=HKCU\\%s status=OK", keyPath.c_str());
        return true;
    }
    if (status == ERROR_FILE_NOT_FOUND) {
        writeLog("REG_RESET key=HKCU\\%s status=NOT_PRESENT", keyPath.c_str());
        return true;
    }
    reportApiError("RegDeleteTreeA", status, keyPath.c_str());
    return false;
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
                "usage: %s --mode gen|list|clean|reset [--student N] [--run N] [--ttl 1|7|30]\n"
                "       [--count N] [--seed N] [--now ISO_UTC|system] [--repeat N] [--dry-run] [--log file]\n",
                arguments[0]);
        return 2;
    }
    g_logFile = fopen(settings.logFilePath.c_str(), "a");

    UtcTicks referenceTimeUtc;
    if (settings.referenceTimeText == "system") {
        referenceTimeUtc = getCurrentUtcTime();
    } else if (!parseUtcTimestamp(settings.referenceTimeText, referenceTimeUtc)) {
        fprintf(stderr, "bad --now, expected YYYY-MM-DDTHH:MM:SSZ\n");
        return 2;
    }

    if (settings.studentId > 32) {
        writeLog("WARNING student=%d is outside the 1..32 range from the assignment", settings.studentId);
    }
    writeLog("START mode=%s student=%d run=%d validity_days=%d count=%d seed=%u now=%s",
             settings.workMode.c_str(), settings.studentId, settings.runId, settings.validityDays,
             settings.recordCount, settings.randomSeed, formatUtcTimestamp(referenceTimeUtc).c_str());

    int exitCode = 0;

    if (settings.workMode == "gen") {
        std::vector<ExpectedOutcome> expectedOutcomes;
        exitCode = generateTestRecords(settings, referenceTimeUtc, settings.randomSeed, expectedOutcomes) ? 0 : 1;

    } else if (settings.workMode == "list") {
        exitCode = printAllRecords(settings, referenceTimeUtc) ? 0 : 1;

    } else if (settings.workMode == "clean") {
        CleanupStatistics statistics;
        exitCode = deleteExpiredRecords(settings, referenceTimeUtc, statistics) ? 0 : 1;

    } else if (settings.workMode == "reset") {
        exitCode = deleteLearningRunKey(settings) ? 0 : 1;

    } else {
        fprintf(stderr, "unknown mode: %s\n", settings.workMode.c_str());
        exitCode = 2;
    }

    writeLog("END exit_code=%d", exitCode);
    if (g_logFile) fclose(g_logFile);
    return exitCode;
}
