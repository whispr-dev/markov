#include <iostream>
#include <fstream>
#include <sstream>
#include <random>
#include <map>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <ctime>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class MarkovBot {
public:
    std::map<std::string, std::vector<std::string>> chain;
    std::mt19937 rng;
    int min_length = 6;
    int max_length = 15;

    MarkovBot() {
        rng.seed(std::time(nullptr));
    }

    void train_from_file(const std::string& filename) {
        std::ifstream file(filename);
        std::string word, prev = "";
        while (file >> word) {
            clean_word(word);
            if (!word.empty()) {
                chain[prev].push_back(word);
                prev = word;
            }
        }
    }

std::string generate_sentence() {
    std::uniform_int_distribution<int> len_dist(min_length, max_length);
    int sentence_len = len_dist(rng);

    // Choose a random valid starting key
    auto it = chain.begin();
    std::advance(it, rng() % chain.size());
    std::string word = it->first;

    // Ensure the starting word isn't empty or weird
    while (word.empty() || chain[word].empty()) {
        it = chain.begin();
        std::advance(it, rng() % chain.size());
        word = it->first;
    }

    std::string sentence;
    for (int i = 0; i < sentence_len; ++i) {
        const auto& next_words = chain[word];
        if (next_words.empty()) break;
        std::uniform_int_distribution<int> dist(0, next_words.size() - 1);
        word = next_words[dist(rng)];
        sentence += word + " ";
    }

    if (!sentence.empty()) {
        sentence[0] = std::toupper(sentence[0]);
        sentence.pop_back();  // Remove trailing space
        sentence += ".";
    } else {
        sentence = "[No sentence generated!]";
    }

    return sentence;
}

    void clean_word(std::string& word) {
        word.erase(remove_if(word.begin(), word.end(), [](unsigned char c) {
            return !std::isalnum(c) && c != '-' && c != '\'';
        }), word.end());
    }

    void save_memory(const std::string& filename) {
        std::ofstream file(filename);
        file << json(chain);
    }

    void load_memory(const std::string& filename) {
        std::ifstream file(filename);
        if (!file) return;
        json j;
        file >> j;
        chain = j.get<std::map<std::string, std::vector<std::string>>>();
    }
};

int main() {
    MarkovBot bot;
    bot.load_memory("mutarkov_brain.json");

    while (true) {
        std::string sentence = bot.generate_sentence();
        std::cout << "Generated sentence: " << sentence << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cout << "Generating sentences...\n";
    for (int i = 0; i < 20; ++i) {
        std::cout << "Generated sentence: " << bot.generate_sentence() << "\n";
    }

    return 0;
}
