#include "Config.h"
#include <fstream>
#include <sstream>
#include <algorithm>

// ---------------------------------------------------------------------------
void Config::Load(const std::filesystem::path& path)
{
    m_data.clear();
    std::ifstream f(path);
    if (!f.is_open()) return;

    std::string line;
    while (std::getline(f, line)) {
        // Trim
        auto l = line.find_first_not_of(" \t\r\n");
        if (l == std::string::npos) continue;
        line = line.substr(l);

        // Skip comments
        if (line[0] == ';' || line[0] == '#') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        // Trim key/val
        auto trim = [](std::string& s) {
            auto b = s.find_first_not_of(" \t");
            auto e = s.find_last_not_of(" \t\r\n");
            if (b == std::string::npos) s.clear();
            else s = s.substr(b, e - b + 1);
        };
        trim(key); trim(val);
        if (!key.empty())
            m_data[key] = val;
    }
}

void Config::Save(const std::filesystem::path& path) const
{
    std::ofstream f(path);
    if (!f.is_open()) return;
    f << "; ShaderPlayground config\n";
    for (auto& [k, v] : m_data)
        f << k << " = " << v << "\n";
}

// ---------------------------------------------------------------------------
void Config::SetFloat(const std::string& k, float v)
{
    m_data[k] = std::to_string(v);
}
void Config::SetInt(const std::string& k, int v)
{
    m_data[k] = std::to_string(v);
}
void Config::SetBool(const std::string& k, bool v)
{
    m_data[k] = v ? "1" : "0";
}
void Config::SetString(const std::string& k, const std::string& v)
{
    m_data[k] = v;
}

// ---------------------------------------------------------------------------
float Config::GetFloat(const std::string& k, float def) const
{
    auto it = m_data.find(k);
    if (it == m_data.end()) return def;
    try { return std::stof(it->second); } catch(...) { return def; }
}
int Config::GetInt(const std::string& k, int def) const
{
    auto it = m_data.find(k);
    if (it == m_data.end()) return def;
    try { return std::stoi(it->second); } catch(...) { return def; }
}
bool Config::GetBool(const std::string& k, bool def) const
{
    return GetInt(k, def ? 1 : 0) != 0;
}
std::string Config::GetString(const std::string& k, const std::string& def) const
{
    auto it = m_data.find(k);
    return (it != m_data.end()) ? it->second : def;
}
