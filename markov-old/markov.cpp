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
#include <algorithm>

// Use your bundled single-header nlohmann JSON:
#include "json.hpp"
using json = nlohmann::json;

class MarkovBot {
public:
    std::map<std::string, std::vector<std::string>> chain;
    std::mt19937 rng;
    int min_length = 6;
    int max_length = 15;

    MarkovBot() { rng.seed(static_cast<unsigned>(std::time(nullptr))); }

    void clean_word(std::string& word) {
        word.erase(std::remove_if(word.begin(), word.end(), [](unsigned char c) {
            return !std::isalnum(c) && c != '-' && c != '\'';
        }), word.end());
    }

    void train_from_file(const std::string& filename) {
        std::ifstream file(filename);
        if (!file) {
            std::cerr << "[WARN] Could not open corpus: " << filename << "\n";
            return;
        }
        std::string word, prev = "";
        size_t added = 0;
        while (file >> word) {
            clean_word(word);
            if (!word.empty()) {
                chain[prev].push_back(word);
                prev = word;
                ++added;
            }
        }
        // Link last token to empty to allow sentence termination paths
        chain[prev]; // touch to ensure key exists
        std::cerr << "[INFO] Trained from " << filename << " (" << added << " tokens)\n";
    }

    bool load_memory(const std::string& filename) {
        std::ifstream file(filename);
        if (!file) return false;
        json j; file >> j;
        chain = j.get<std::map<std::string, std::vector<std::string>>>();
        std::cerr << "[INFO] Loaded brain: " << filename
                  << " (states=" << chain.size() << ")\n";
        return true;
    }

    bool save_memory(const std::string& filename) {
        std::ofstream file(filename);
        if (!file) return false;
        file << json(chain);
        std::cerr << "[INFO] Saved brain: " << filename
                  << " (states=" << chain.size() << ")\n";
        return true;
    }

    // Choose a solid starting key: prefer a key that leads somewhere and is not empty
    std::string choose_start_key() {
        if (chain.empty()) return "";
        // try up to N random keys that have outgoing edges
        for (int tries = 0; tries < 64; ++tries) {
            auto it = chain.begin();
            std::advance(it, rng() % chain.size());
            if (!it->first.empty() && !it->second.empty()) return it->first;
        }
        // fallback: if "" has next words, start from ""
        if (!chain[""].empty()) return "";
        // last resort: first key with outgoing edges
        for (const auto& kv : chain) if (!kv.second.empty()) return kv.first;
        return "";
    }

    std::string generate_sentence() {
        if (chain.empty()) return "[No sentence generated! Empty model]";
        std::uniform_int_distribution<int> len_dist(min_length, max_length);
        int sentence_len = len_dist(rng);

        std::string word = choose_start_key();
        if (word.empty() && chain[word].empty()) {
            return "[No sentence generated! No viable starts]";
        }

        std::string sentence;
        for (int i = 0; i < sentence_len; ++i) {
            auto it = chain.find(word);
            if (it == chain.end() || it->second.empty()) break;
            const auto& next_words = it->second;
            std::uniform_int_distribution<size_t> dist(0, next_words.size() - 1);
            word = next_words[dist(rng)];
            sentence += word + " ";
        }

        if (!sentence.empty()) {
            sentence.pop_back(); // trim space
            if (std::isalpha(static_cast<unsigned char>(sentence[0])))
                sentence[0] = static_cast<char>(std::toupper(sentence[0]));
            sentence += ".";
        } else {
            sentence = "[No sentence generated!]";
        }
        return sentence;
    }
};

struct Args {
    std::string brain_in  = "mutarkov_brain.json";
    std::string brain_out = "mutarkov_brain.json";
    std::string corpus;           // optional
    int min_len = 6;
    int max_len = 15;
    int delay_ms = 500;
    int count = -1;               // -1 = infinite
    bool save_after_train = true;
};

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        auto require_val = [&](const char* name) {
            if (i + 1 >= argc) { std::cerr << "[ERR] Missing value for " << name << "\n"; std::exit(2); }
            return std::string(argv[++i]);
        };
        if      (s == "--brain-in")     a.brain_in = require_val(s.c_str());
        else if (s == "--brain-out")    a.brain_out = require_val(s.c_str());
        else if (s == "--corpus")       a.corpus = require_val(s.c_str());
        else if (s == "--min")          a.min_len = std::stoi(require_val(s.c_str()));
        else if (s == "--max")          a.max_len = std::stoi(require_val(s.c_str()));
        else if (s == "--delay-ms")     a.delay_ms = std::stoi(require_val(s.c_str()));
        else if (s == "--count")        a.count = std::stoi(require_val(s.c_str()));
        else if (s == "--no-save")      a.save_after_train = false;
        else if (s == "--help" || s == "-h") {
            std::cout <<
R"(Mutarkov — simple persistent Markov text bot

Usage:
  mutarkov [--brain-in FILE] [--brain-out FILE]
           [--corpus FILE] [--min N] [--max N]
           [--delay-ms MS] [--count N] [--no-save]

Defaults:
  --brain-in  mutarkov_brain.json
  --brain-out mutarkov_brain.json
  --min       6
  --max       15
  --delay-ms  500
  --count     -1  (infinite)

Examples:
  # Train from corpus and run forever, saving merged brain
  ./mutarkov --corpus text_data_filtered.txt

  # Load existing brain and print 20 sentences quickly
  ./mutarkov --count 20 --delay-ms 50
)";
            std::exit(0);
        } else {
            std::cerr << "[WARN] Unknown arg: " << s << "\n";
        }
    }
    return a;
}

int main(int argc, char** argv) {
    Args args = parse_args(argc, argv);

    MarkovBot bot;
    bot.min_length = args.min_len;
    bot.max_length = args.max_len;

    // Load existing brain if present
    bot.load_memory(args.brain_in);

    // Optionally train from corpus and save
    if (!args.corpus.empty()) {
        bot.train_from_file(args.corpus);
        if (args.save_after_train) bot.save_memory(args.brain_out);
    }

    // Generate
    if (args.count < 0) {
        while (true) {
            std::cout << "Generated sentence: " << bot.generate_sentence() << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(args.delay_ms));
        }
    } else {
        for (int i = 0; i < args.count; ++i) {
            std::cout << "Generated sentence: " << bot.generate_sentence() << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(args.delay_ms));
        }
    }
    return 0;
}
