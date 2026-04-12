#include <iostream>
#include <map>
#include <vector>
#include <fstream>
#include <string>
#include <set>
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

struct Entity {
    int start;
    int end;
    std::string label;
};

const std::set<std::string> PERSON_TYPES = {
    "FIO_I", "FIO_R", "FIO_D", "FIO_V", "FIO_T", "FIO_P"
};

std::map<std::string, size_t> globalCounters;

int globalDocumentCounter = 1;


std::map<std::string, std::vector<std::string>> loadAllDictionaries(
    const std::map<std::string, std::string>& fileMapping,
    const std::string& basePath = "") {

    std::map<std::string, std::vector<std::string>> dicts;

    for (auto it = fileMapping.begin(); it != fileMapping.end(); ++it) {
        const std::string& token = it->first;
        const std::string& filename = it->second;

        std::string fullPath = basePath.empty() ? filename : basePath + "/" + filename;
        std::ifstream file(fullPath);

        if (!file.is_open()) {
            std::cerr << "Warning: Cannot open file " << fullPath << std::endl;
            dicts[token] = {};
            continue;
        }

        std::vector<std::string> lines;
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (!line.empty()) {
                lines.push_back(line);
            }
        }

        dicts[token] = lines;
        std::cout << "Loaded " << token << ": " << lines.size() << " items" << std::endl;
    }

    return dicts;
}


void parseToken(const std::string& token, std::string& base, std::string& index) {
    size_t lastUnderscore = token.find_last_of('_');

    if (lastUnderscore != std::string::npos) {
        std::string suffix = token.substr(lastUnderscore + 1);
        bool isNumber = !suffix.empty();
        for (char c : suffix) {
            if (c < '0' || c > '9') {
                isNumber = false;
                break;
            }
        }

        if (isNumber) {
            base = token.substr(0, lastUnderscore);
            index = suffix;
            return;
        }
    }

    base = token;
    index = "";
}


size_t getNextIndex(const std::string& dictKey, size_t dictSize) {
    if (dictSize == 0) return 0;

    size_t& counter = globalCounters[dictKey];
    size_t index = counter % dictSize;
    counter++;
    return index;
}


void analyzeTemplate(
    const std::string& templ,
    std::map<std::string, int>& maxInstanceIndex,
    std::set<std::string>& allTokens) {

    size_t pos = 0;
    while ((pos = templ.find('{', pos)) != std::string::npos) {
        size_t endPos = templ.find('}', pos);
        if (endPos == std::string::npos) break;

        std::string token = templ.substr(pos + 1, endPos - pos - 1);
        allTokens.insert(token);

        std::string base, index;
        parseToken(token, base, index);

        int idx = index.empty() ? 1 : std::atoi(index.c_str());

        if (PERSON_TYPES.count(base)) {
            for (const auto& pt : PERSON_TYPES) {
                maxInstanceIndex[pt] = maxInstanceIndex[pt] > idx ? maxInstanceIndex[pt] : idx;
            }
        }
        else {
            maxInstanceIndex[base] = maxInstanceIndex[base] > idx ? maxInstanceIndex[base] : idx;
        }

        pos = endPos + 1;
    }
}


std::pair<std::string, std::vector<Entity>> renderDocument(
    const std::string& templ,
    std::map<std::string, std::vector<std::string>>& dicts) {

    std::map<std::string, int> maxInstanceIndex;
    std::set<std::string> allTokens;
    analyzeTemplate(templ, maxInstanceIndex, allTokens);

    std::map<std::string, size_t> assignedIndexes;

    for (const auto& pair : maxInstanceIndex) {
        const std::string& base = pair.first;
        int maxIdx = pair.second;
        if (dicts.find(base) != dicts.end() && !dicts[base].empty()) {
            for (int i = 1; i <= maxIdx; i++) {
                std::string key = base + "_" + std::to_string(i);
                assignedIndexes[key] = getNextIndex(base, dicts[base].size());
            }
        }
    }

    std::string result = templ;
    std::vector<Entity> entities;
    std::map<std::string, std::string> usedValues;

    size_t pos = 0;
    while ((pos = result.find('{', pos)) != std::string::npos) {
        size_t endPos = result.find('}', pos);
        if (endPos == std::string::npos) break;

        std::string token = result.substr(pos + 1, endPos - pos - 1);

        if (usedValues.find(token) != usedValues.end()) {
            std::string value = usedValues[token];
            entities.push_back({ (int)pos, (int)(pos + value.length()), token });
            result.replace(pos, endPos - pos + 1, value);
            pos += value.length();
            continue;
        }

        std::string base, index;
        parseToken(token, base, index);

        std::string lookupKey = base + "_" + (index.empty() ? "1" : index);

        std::string value;
        if (assignedIndexes.find(lookupKey) != assignedIndexes.end()) {
            size_t idx = assignedIndexes[lookupKey];
            if (idx < dicts[base].size()) {
                value = dicts[base][idx];
            }
            else {
                value = "{" + token + "}";
            }
        }
        else {
            value = "{" + token + "}";
        }

        usedValues[token] = value;

        entities.push_back({ (int)pos, (int)(pos + value.length()), token });
        result.replace(pos, endPos - pos + 1, value);
        pos += value.length();
    }

    return { result, entities };
}


std::string getDocumentFilename(int docNumber) {
    std::ostringstream filename;
    filename << "doc_" << std::setw(6) << std::setfill('0') << docNumber;
    return filename.str();
}

std::string formatEntitiesJson(const std::vector<Entity>& entities) {
    std::ostringstream json;
    json << "{\n  \"entities\": [\n";

    for (size_t i = 0; i < entities.size(); ++i) {
        json << "    {\n";
        json << "      \"start\": " << entities[i].start << ",\n";
        json << "      \"end\": " << entities[i].end << ",\n";
        json << "      \"label\": \"" << entities[i].label << "\"\n";
        json << "    }";
        if (i != entities.size() - 1) {
            json << ",";
        }
        json << "\n";
    }

    json << "  ]\n}";
    return json.str();
}

void saveTextDocument(const std::string& filepath, const std::string& text) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot create file " << filepath << std::endl;
        return;
    }
    file << text;
    file.close();
}

void saveJsonDocument(const std::string& filepath, const std::vector<Entity>& entities) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot create file " << filepath << std::endl;
        return;
    }
    file << formatEntitiesJson(entities);
    file.close();
}

void appendToJsonl(const std::string& filename,
    const std::string& text,
    const std::vector<Entity>& entities) {

    std::ofstream file(filename, std::ios::app);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file " << filename << std::endl;
        return;
    }

    std::string escapedText;
    for (size_t i = 0; i < text.length(); i++) {
        char c = text[i];
        switch (c) {
        case '"':  escapedText += "\\\""; break;
        case '\\': escapedText += "\\\\"; break;
        case '\n': escapedText += "\\n"; break;
        case '\r': escapedText += "\\r"; break;
        case '\t': escapedText += "\\t"; break;
        case '\b': escapedText += "\\b"; break;
        case '\f': escapedText += "\\f"; break;
        default:   escapedText += c; break;
        }
    }

    file << "{\"text\":\"" << escapedText << "\",\"entities\":[";

    for (size_t i = 0; i < entities.size(); ++i) {
        file << "[" << entities[i].start << ","
            << entities[i].end << ",\""
            << entities[i].label << "\"]";
        if (i != entities.size() - 1) {
            file << ",";
        }
    }

    file << "]}\n";
    file.close();
}


std::vector<std::string> findAllTemplates(const std::string& rootPath) {
    std::vector<std::string> templates;

    if (!fs::exists(rootPath) || !fs::is_directory(rootPath)) {
        std::cerr << "Error: Templates folder not found: " << rootPath << "\n";
        return templates;
    }

    try {
        for (const auto& entry : fs::recursive_directory_iterator(rootPath)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                if (ext == ".txt" || ext == ".template") {
                    templates.push_back(entry.path().string());
                }
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error scanning templates: " << e.what() << "\n";
    }

    return templates;
}


int main() {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
#endif

    srand((unsigned int)time(NULL));

    // ===== SETTINGS =====
    std::string dataFolder = "./data";
    std::string templatesRoot = "./templates";
    std::string outputFolder = "./output";
    std::string outputJsonl = "dataset.jsonl";  
    int documentsPerTemplate = 10;
    // =====================

    if (!fs::exists(outputFolder)) {
        fs::create_directories(outputFolder);
        std::cout << "Created output folder: " << outputFolder << "\n";
    }

    std::map<std::string, std::string> fileMapping;
    fileMapping["COMPANY"] = "corporation.txt";
    fileMapping["FIO_R"] = "person_r.txt";
    fileMapping["FIO_I"] = "person.txt";
    fileMapping["FIO_D"] = "person_d.txt";
    fileMapping["FIO_V"] = "person_v.txt";
    fileMapping["FIO_T"] = "person_t.txt";
    fileMapping["FIO_P"] = "person_p.txt";
    fileMapping["PHONE"] = "phone.txt";
    fileMapping["EMAIL"] = "email.txt";
    fileMapping["PASS"] = "passport.txt";
    fileMapping["ADDR"] = "adress.txt";
    fileMapping["INN_PER"] = "pers_tin.txt";
    fileMapping["INN_CORP"] = "corp_tin.txt";
    fileMapping["SNILS"] = "snils.txt";
    fileMapping["DATE"] = "date.txt";

    std::cout << "Loading dictionaries...\n";
    std::map<std::string, std::vector<std::string>> dicts = loadAllDictionaries(fileMapping, dataFolder);
    std::cout << "Dictionaries loaded.\n\n";

    std::cout << "Searching for templates...\n";
    std::vector<std::string> templates = findAllTemplates(templatesRoot);

    if (templates.empty()) {
        std::cerr << "ERROR: No templates found in " << templatesRoot << "\n";
        return 1;
    }

    std::cout << "Found " << templates.size() << " template(s)\n";
    for (const auto& t : templates) {
        std::cout << "  - " << t << "\n";
    }
    std::cout << "\n";

    for (auto& pair : globalCounters) {
        pair.second = 0;
    }

    std::string fullJsonlPath = outputFolder + "/" + outputJsonl;
    std::ofstream clearFile(fullJsonlPath);
    clearFile.close();

    int totalDocuments = 0;
    int processedTemplates = 0;

    for (size_t t = 0; t < templates.size(); t++) {
        const std::string& templatePath = templates[t];

        std::ifstream templFile(templatePath);
        if (!templFile.is_open()) {
            std::cerr << "  ERROR: Cannot open template, skipping...\n";
            continue;
        }

        std::string templ((std::istreambuf_iterator<char>(templFile)),
            std::istreambuf_iterator<char>());
        templFile.close();

        if (templ.empty()) {
            std::cerr << "  WARNING: Empty template, skipping...\n";
            continue;
        }

        for (int i = 0; i < documentsPerTemplate; i++) {
            auto [text, entities] = renderDocument(templ, dicts);

            std::string baseFilename = getDocumentFilename(globalDocumentCounter);
            std::string txtPath = outputFolder + "/" + baseFilename + ".txt";
            std::string jsonPath = outputFolder + "/" + baseFilename + ".json";

            saveTextDocument(txtPath, text);
            saveJsonDocument(jsonPath, entities);

            appendToJsonl(fullJsonlPath, text, entities);

            globalDocumentCounter++;
        }

        totalDocuments += documentsPerTemplate;
        processedTemplates++;
    }

    std::cout << "\n========================================\n";
    std::cout << "  GENERATION COMPLETE\n";
    std::cout << "========================================\n";
    std::cout << "Templates processed:  " << processedTemplates << "\n";
    std::cout << "Total documents:      " << totalDocuments << "\n";
    std::cout << "========================================\n";

    return 0;
}
