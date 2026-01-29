/*
 * SPDX-FileCopyrightText: 2026 Howizard Project
 *
 * SPDX-License-Identifier: MIT
 */
#include "profile_manager.h"
#include <mooncake_log.h>
#include <bsp/m5stack_tab5.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>

static const char* TAG = "ProfileMgr";

// ─────────────────────────────────────────────────────────────────────────────
// SD mount/unmount (follows existing scanSdCard() pattern)
// ─────────────────────────────────────────────────────────────────────────────

bool ProfileManager::mountSd()
{
    if (bsp_sdcard_init(const_cast<char*>("/sd"), 25) != ESP_OK) {
        mclog::tagError(TAG, "failed to mount SD card");
        return false;
    }
    return true;
}

void ProfileManager::unmountSd()
{
    bsp_sdcard_deinit(const_cast<char*>("/sd"));
}

bool ProfileManager::ensureDirectory()
{
    struct stat st;
    if (stat(PROFILES_DIR, &st) == 0) {
        return true;  // Already exists
    }
    if (mkdir(PROFILES_DIR, 0755) != 0) {
        mclog::tagError(TAG, "failed to create {}", PROFILES_DIR);
        return false;
    }
    return true;
}

std::string ProfileManager::profilePath(const std::string& name)
{
    return std::string(PROFILES_DIR) + "/" + name + FILE_EXT;
}

// ─────────────────────────────────────────────────────────────────────────────
// Serialization (key=value text format)
// ─────────────────────────────────────────────────────────────────────────────

bool ProfileManager::serialize(const std::string& path, const AudioEngineParams& params)
{
    FILE* f = fopen(path.c_str(), "w");
    if (!f) {
        mclog::tagError(TAG, "failed to open for write: {}", path);
        return false;
    }

    fprintf(f, "%s\n", FILE_HEADER);
    fprintf(f, "micGain=%.1f\n", params.micGain);
    fprintf(f, "hpfEnabled=%d\n", params.hpfEnabled ? 1 : 0);
    fprintf(f, "hpfFrequency=%.1f\n", params.hpfFrequency);
    fprintf(f, "lpfEnabled=%d\n", params.lpfEnabled ? 1 : 0);
    fprintf(f, "lpfFrequency=%.1f\n", params.lpfFrequency);
    fprintf(f, "eqLowGain=%.1f\n", params.eqLowGain);
    fprintf(f, "eqMidGain=%.1f\n", params.eqMidGain);
    fprintf(f, "eqHighGain=%.1f\n", params.eqHighGain);
    fprintf(f, "nsEnabled=%d\n", params.nsEnabled ? 1 : 0);
    fprintf(f, "nsMode=%d\n", params.nsMode);
    fprintf(f, "agcEnabled=%d\n", params.agcEnabled ? 1 : 0);
    fprintf(f, "agcMode=%d\n", params.agcMode);
    fprintf(f, "agcCompressionGainDb=%d\n", params.agcCompressionGainDb);
    fprintf(f, "agcLimiterEnabled=%d\n", params.agcLimiterEnabled ? 1 : 0);
    fprintf(f, "agcTargetLevelDbfs=%d\n", params.agcTargetLevelDbfs);
    fprintf(f, "veEnabled=%d\n", params.veEnabled ? 1 : 0);
    fprintf(f, "veBlend=%.2f\n", params.veBlend);
    fprintf(f, "veStepSize=%.2f\n", params.veStepSize);
    fprintf(f, "veFilterLength=%d\n", params.veFilterLength);
    fprintf(f, "veMaxAttenuation=%.2f\n", params.veMaxAttenuation);
    fprintf(f, "veRefGain=%.2f\n", params.veRefGain);
    fprintf(f, "veRefHpf=%.1f\n", params.veRefHpf);
    fprintf(f, "veRefLpf=%.1f\n", params.veRefLpf);
    fprintf(f, "veMode=%d\n", params.veMode);
    fprintf(f, "veAecMode=%d\n", params.veAecMode);
    fprintf(f, "veAecFilterLen=%d\n", params.veAecFilterLen);
    fprintf(f, "veVadEnabled=%d\n", params.veVadEnabled ? 1 : 0);
    fprintf(f, "veVadMode=%d\n", params.veVadMode);
    fprintf(f, "outputGain=%.2f\n", params.outputGain);
    fprintf(f, "outputVolume=%d\n", params.outputVolume);
    fprintf(f, "outputMute=%d\n", params.outputMute ? 1 : 0);

    fclose(f);
    return true;
}

bool ProfileManager::deserialize(const std::string& path, AudioEngineParams& params)
{
    FILE* f = fopen(path.c_str(), "r");
    if (!f) {
        mclog::tagError(TAG, "failed to open for read: {}", path);
        return false;
    }

    char line[128];
    while (fgets(line, sizeof(line), f)) {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        // Find the '=' separator
        char* eq = strchr(line, '=');
        if (!eq) continue;

        *eq = '\0';
        const char* key = line;
        const char* val = eq + 1;

        // Strip trailing newline from value
        char* nl = strchr(const_cast<char*>(val), '\n');
        if (nl) *nl = '\0';
        nl = strchr(const_cast<char*>(val), '\r');
        if (nl) *nl = '\0';

        // Parse each field
        if (strcmp(key, "micGain") == 0)              params.micGain = strtof(val, nullptr);
        else if (strcmp(key, "hpfEnabled") == 0)      params.hpfEnabled = atoi(val) != 0;
        else if (strcmp(key, "hpfFrequency") == 0)    params.hpfFrequency = strtof(val, nullptr);
        else if (strcmp(key, "lpfEnabled") == 0)      params.lpfEnabled = atoi(val) != 0;
        else if (strcmp(key, "lpfFrequency") == 0)    params.lpfFrequency = strtof(val, nullptr);
        else if (strcmp(key, "eqLowGain") == 0)       params.eqLowGain = strtof(val, nullptr);
        else if (strcmp(key, "eqMidGain") == 0)       params.eqMidGain = strtof(val, nullptr);
        else if (strcmp(key, "eqHighGain") == 0)      params.eqHighGain = strtof(val, nullptr);
        else if (strcmp(key, "nsEnabled") == 0)        params.nsEnabled = atoi(val) != 0;
        else if (strcmp(key, "nsMode") == 0)           params.nsMode = atoi(val);
        else if (strcmp(key, "agcEnabled") == 0)       params.agcEnabled = atoi(val) != 0;
        else if (strcmp(key, "agcMode") == 0)          params.agcMode = atoi(val);
        else if (strcmp(key, "agcCompressionGainDb") == 0) params.agcCompressionGainDb = atoi(val);
        else if (strcmp(key, "agcLimiterEnabled") == 0) params.agcLimiterEnabled = atoi(val) != 0;
        else if (strcmp(key, "agcTargetLevelDbfs") == 0) params.agcTargetLevelDbfs = atoi(val);
        else if (strcmp(key, "veEnabled") == 0)        params.veEnabled = atoi(val) != 0;
        else if (strcmp(key, "veBlend") == 0)          params.veBlend = strtof(val, nullptr);
        else if (strcmp(key, "veStepSize") == 0)       params.veStepSize = strtof(val, nullptr);
        else if (strcmp(key, "veFilterLength") == 0)   params.veFilterLength = atoi(val);
        else if (strcmp(key, "veMaxAttenuation") == 0) params.veMaxAttenuation = strtof(val, nullptr);
        else if (strcmp(key, "veRefGain") == 0)        params.veRefGain = strtof(val, nullptr);
        else if (strcmp(key, "veRefHpf") == 0)         params.veRefHpf = strtof(val, nullptr);
        else if (strcmp(key, "veRefLpf") == 0)         params.veRefLpf = strtof(val, nullptr);
        else if (strcmp(key, "veMode") == 0)           params.veMode = atoi(val);
        else if (strcmp(key, "veAecMode") == 0)        params.veAecMode = atoi(val);
        else if (strcmp(key, "veAecFilterLen") == 0)   params.veAecFilterLen = atoi(val);
        else if (strcmp(key, "veVadEnabled") == 0)     params.veVadEnabled = atoi(val) != 0;
        else if (strcmp(key, "veVadMode") == 0)        params.veVadMode = atoi(val);
        else if (strcmp(key, "outputGain") == 0)       params.outputGain = strtof(val, nullptr);
        else if (strcmp(key, "outputVolume") == 0)     params.outputVolume = atoi(val);
        else if (strcmp(key, "outputMute") == 0)       params.outputMute = atoi(val) != 0;
    }

    fclose(f);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

bool ProfileManager::saveProfile(const std::string& name, const AudioEngineParams& params)
{
    if (!mountSd()) return false;
    if (!ensureDirectory()) { unmountSd(); return false; }

    std::string path = profilePath(name);
    bool ok = serialize(path, params);

    if (ok) {
        mclog::tagInfo(TAG, "saved profile: {}", name);
    }

    unmountSd();
    return ok;
}

bool ProfileManager::loadProfile(const std::string& name, AudioEngineParams& params)
{
    if (!mountSd()) return false;

    std::string path = profilePath(name);
    bool ok = deserialize(path, params);

    if (ok) {
        mclog::tagInfo(TAG, "loaded profile: {}", name);
    }

    unmountSd();
    return ok;
}

bool ProfileManager::deleteProfile(const std::string& name)
{
    if (!mountSd()) return false;

    std::string path = profilePath(name);
    int rc = remove(path.c_str());

    if (rc == 0) {
        mclog::tagInfo(TAG, "deleted profile: {}", name);
    } else {
        mclog::tagError(TAG, "failed to delete: {}", name);
    }

    unmountSd();
    return rc == 0;
}

std::vector<std::string> ProfileManager::listProfiles()
{
    std::vector<std::string> names;

    if (!mountSd()) return names;
    ensureDirectory();

    DIR* dir = opendir(PROFILES_DIR);
    if (!dir) {
        mclog::tagError(TAG, "failed to open profiles dir");
        unmountSd();
        return names;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type != DT_REG) continue;

        std::string fname = entry->d_name;
        // Filter .hwz files, skip hidden files
        if (fname.size() > 4 && fname[0] != '.' &&
            fname.substr(fname.size() - 4) == FILE_EXT) {
            names.push_back(fname.substr(0, fname.size() - 4));
        }
    }

    closedir(dir);
    unmountSd();
    return names;
}

bool ProfileManager::setDefaultProfile(const std::string& name)
{
    if (!mountSd()) return false;
    if (!ensureDirectory()) { unmountSd(); return false; }

    FILE* f = fopen(DEFAULT_FILE, "w");
    if (!f) {
        mclog::tagError(TAG, "failed to write default file");
        unmountSd();
        return false;
    }

    fprintf(f, "%s\n", name.c_str());
    fclose(f);

    mclog::tagInfo(TAG, "default profile set: {}", name);
    unmountSd();
    return true;
}

std::string ProfileManager::getDefaultProfile()
{
    std::string result;

    if (!mountSd()) return result;

    FILE* f = fopen(DEFAULT_FILE, "r");
    if (f) {
        char buf[128];
        if (fgets(buf, sizeof(buf), f)) {
            // Strip newline
            char* nl = strchr(buf, '\n');
            if (nl) *nl = '\0';
            nl = strchr(buf, '\r');
            if (nl) *nl = '\0';
            result = buf;
        }
        fclose(f);
    }

    unmountSd();
    return result;
}

bool ProfileManager::loadDefaultProfile(AudioEngineParams& params)
{
    if (!mountSd()) return false;
    ensureDirectory();

    // Read default profile name
    FILE* f = fopen(DEFAULT_FILE, "r");
    if (!f) {
        mclog::tagInfo(TAG, "no default profile configured");
        unmountSd();
        return false;
    }

    char buf[128];
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        unmountSd();
        return false;
    }
    fclose(f);

    // Strip newline
    char* nl = strchr(buf, '\n');
    if (nl) *nl = '\0';
    nl = strchr(buf, '\r');
    if (nl) *nl = '\0';

    if (strlen(buf) == 0) {
        unmountSd();
        return false;
    }

    std::string name = buf;
    std::string path = profilePath(name);
    bool ok = deserialize(path, params);

    if (ok) {
        mclog::tagInfo(TAG, "auto-loaded default profile: {}", name);
    } else {
        mclog::tagWarn(TAG, "default profile '{}' not found", name);
    }

    unmountSd();
    return ok;
}

bool ProfileManager::isSdCardAccessible()
{
    if (!mountSd()) {
        return false;
    }
    unmountSd();
    return true;
}

bool ProfileManager::formatSdCard()
{
    // Note: Formatting requires unmounting first, then using FATFS formatting
    // For now, just try to mount and create the profiles directory
    mclog::tagInfo(TAG, "attempting to prepare SD card...");

    if (!mountSd()) {
        mclog::tagError(TAG, "cannot mount SD card for formatting");
        return false;
    }

    // Try to create the profiles directory
    bool ok = ensureDirectory();

    unmountSd();

    if (ok) {
        mclog::tagInfo(TAG, "SD card prepared successfully");
    }
    return ok;
}
