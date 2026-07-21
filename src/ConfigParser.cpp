#include "../include/ConfigParser.hpp"
#include "../include/Logger.h"
#include <fstream>
#include <iostream>

ConfigParser::ConfigParser() : configFilePath("") {}

ConfigParser::ConfigParser(const std::string& configFile) : configFilePath(configFile) {
    loadConfig(configFile);
}

ConfigParser::~ConfigParser() {
    config.clear();
}

std::string ConfigParser::trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    size_t end = str.find_last_not_of(" \t\r\n");
    
    if (start == std::string::npos) {
        return "";
    }
    
    return str.substr(start, end - start + 1);
}

bool ConfigParser::loadConfig(const std::string& configFile) {
    config.clear();
    std::ifstream file(configFile);
    
    if (!file.is_open()) {
        std::cerr << "[ERROR] Could not open config file: " << configFile << std::endl;
        return false;
    }
    
    std::string line;
    std::string currentSection;
    
    while (std::getline(file, line)) {
        line = trim(line);
        
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        
        // Check for section header [section]
        if (line[0] == '[' && line[line.length()-1] == ']') {
            currentSection = line.substr(1, line.length()-2);
            continue;
        }
        
        // Parse key=value
        size_t delimPos = line.find('=');
        if (delimPos != std::string::npos) {
            std::string key = trim(line.substr(0, delimPos));
            std::string value = trim(line.substr(delimPos + 1));
            
            // Store with section prefix
            std::string fullKey = currentSection.empty() ? key : currentSection + "." + key;
            config[fullKey] = value;
        }
    }
    
    file.close();
    configFilePath = configFile;
    return true;
}

std::string ConfigParser::getValue(const std::string& key, const std::string& defaultValue) const {
    auto it = config.find(key);
    if (it != config.end()) {
        return it->second;
    }
    return defaultValue;
}

int ConfigParser::getIntValue(const std::string& key, int defaultValue) const {
    auto it = config.find(key);
    if (it != config.end()) {
        try {
            return std::stoi(it->second);
        } catch (...) {
            std::cerr << "[ERROR] Invalid integer value for key: " << key << std::endl;
        }
    }
    return defaultValue;
}

bool ConfigParser::hasKey(const std::string& key) const {
    return config.find(key) != config.end();
}

void ConfigParser::printConfig() const {
    LOG_INFO("Configuration loaded from: " + configFilePath);
    for (const auto& pair : config) {
        LOG_INFO("  " + pair.first + " = " + pair.second);
    }
}
