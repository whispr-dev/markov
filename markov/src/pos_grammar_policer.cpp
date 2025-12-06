#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <cctype>
#include <regex>

std::string to_lower(const std::string& input) {
    std::string result;
    for (char c : input) result += std::tolower(c);
    return result;
}

bool is_alpha(const std::string& word) {
    return std::all_of(word.begin(), word.end(), ::isalpha);
}

std::vector<std::string> tokenize(const std::string& sentence) {
    std::vector<std::string> words;
    std::istringstream iss(sentence);
    std::string word;
    while (iss >> word) {
        word = to_lower(std::regex_replace(word, std::regex("[^a-zA-Z]"), ""));
        if (!word.empty() && is_alpha(word)) words.push_back(word);
    }
    return words;
}

std::unordered_map<std::string, std::string> load_pos_map(const std::string& path) {
    std::unordered_map<std::string, std::string> pos_map;
    std::ifstream file(path);
    std::string line, current_tag;

    while (std::getline(file, line)) {
        if (line.rfind("==", 0) == 0) {
            current_tag = line.substr(3, line.size() - 6);
        } else {
            std::stringstream ss(line);
            std::string word;
            while (std::getline(ss, word, ',')) {
                word.erase(std::remove_if(word.begin(), word.end(), ::isspace), word.end());
                if (!word.empty()) {
                    pos_map[word] = current_tag;
                }
            }
        }
    }
    return pos_map;
}

bool is_grammatically_healthy(const std::vector<std::string>& words,
                               const std::unordered_map<std::string, std::string>& pos_map) {
    bool has_noun = false, has_verb = false;
    for (const auto& w : words) {
        auto it = pos_map.find(w);
        if (it != pos_map.end()) {
            if (it->second == "NOUN") has_noun = true;
            if (it->second == "VERB") has_verb = true;
        }
    }
    return has_noun && has_verb;
}

int main() {
    std::string pos_file = "custom_pos_library.txt";
    auto pos_map = load_pos_map(pos_file);

    std::cout << "Enter a sentence: ";
    std::string input;
    std::getline(std::cin, input);

    auto words = tokenize(input);
    if (is_grammatically_healthy(words, pos_map)) {
        std::cout << "✅ Sentence accepted. It’s grammatically viable!\n";
    } else {
        std::cout << "❌ Rejected. Sentence lacks noun/verb structure.\n";
    }

    return 0;
}
