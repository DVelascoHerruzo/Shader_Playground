#pragma once
#include <string>
#include <unordered_map>
#include <filesystem>

// ---------------------------------------------------------------------------
//  Config  — simple INI-style key/value persistence.
//  File is saved next to the executable as "shader_playground.ini".
// ---------------------------------------------------------------------------
class Config {
public:
    void Load(const std::filesystem::path& path);
    void Save(const std::filesystem::path& path) const;

    void   SetFloat (const std::string& key, float v);
    void   SetInt   (const std::string& key, int   v);
    void   SetBool  (const std::string& key, bool  v);
    void   SetString(const std::string& key, const std::string& v);

    float       GetFloat (const std::string& key, float defVal = 0.0f) const;
    int         GetInt   (const std::string& key, int   defVal = 0)    const;
    bool        GetBool  (const std::string& key, bool  defVal = false) const;
    std::string GetString(const std::string& key, const std::string& def = "") const;

private:
    std::unordered_map<std::string, std::string> m_data;
};
