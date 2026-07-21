#ifndef CONFIG_PARSER_HPP
#define CONFIG_PARSER_HPP

#include <string>
#include <map>

class ConfigParser {
private:
    std::map<std::string, std::string> config;
    std::string configFilePath;

    // Helper methods
    std::string trim(const std::string& str);
    
public:
    // Constructor
    ConfigParser();
    explicit ConfigParser(const std::string& configFile);
    
    // Methods
    bool loadConfig(const std::string& configFile);
    std::string getValue(const std::string& key, const std::string& defaultValue = "") const;
    int getIntValue(const std::string& key, int defaultValue = 0) const;
    bool hasKey(const std::string& key) const;
    void printConfig() const;
    
    // Destructor
    ~ConfigParser();
};

#endif // CONFIG_PARSER_HPP
