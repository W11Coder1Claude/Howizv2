/*
 * SPDX-FileCopyrightText: 2026 Howizard Project
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "audio_engine.h"
#include <string>
#include <vector>

/**
 * @brief Profile save/load manager for Howizard audio settings
 *
 * Stores profiles as key=value text files on SD card at /sd/Profiles/<name>.hwz
 * Mounts/unmounts SD per operation following existing BSP pattern.
 */
class ProfileManager {
public:
    static constexpr const char* PROFILES_DIR = "/sd/Profiles";
    static constexpr const char* DEFAULT_FILE = "/sd/Profiles/.default";
    static constexpr const char* FILE_EXT = ".hwz";
    static constexpr const char* FILE_HEADER = "# Howizard Audio Profile v1";

    /**
     * @brief Save current params to a named profile on SD card
     * @return true on success
     */
    static bool saveProfile(const std::string& name, const AudioEngineParams& params);

    /**
     * @brief Load a named profile from SD card into params
     * @return true on success
     */
    static bool loadProfile(const std::string& name, AudioEngineParams& params);

    /**
     * @brief Delete a named profile from SD card
     * @return true on success
     */
    static bool deleteProfile(const std::string& name);

    /**
     * @brief List all available profile names on SD card
     * @return vector of profile names (without extension)
     */
    static std::vector<std::string> listProfiles();

    /**
     * @brief Set a profile as the default (auto-loaded on boot)
     * @return true on success
     */
    static bool setDefaultProfile(const std::string& name);

    /**
     * @brief Get the name of the current default profile
     * @return profile name, or empty string if none set
     */
    static std::string getDefaultProfile();

    /**
     * @brief Full cycle: mount SD, read default name, load profile, unmount
     * @return true if a default profile was loaded
     */
    static bool loadDefaultProfile(AudioEngineParams& params);

    /**
     * @brief Check if SD card is accessible
     * @return true if SD card can be mounted
     */
    static bool isSdCardAccessible();

    /**
     * @brief Format SD card to FAT32
     * @return true on success
     */
    static bool formatSdCard();

private:
    static bool mountSd();
    static void unmountSd();
    static bool ensureDirectory();
    static std::string profilePath(const std::string& name);
    static bool serialize(const std::string& path, const AudioEngineParams& params);
    static bool deserialize(const std::string& path, AudioEngineParams& params);
};
