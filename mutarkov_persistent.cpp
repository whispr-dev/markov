#include <iostream>
#include <fstream>
#include <sstream>
#include <random>
#include <unordered_map>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>
#include <chrono>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

class MarkovBot {
public:
    using Chain = std::unordered_map<std::string, std::vector<std::string>>;

    MarkovBot(int min_len = 6, int max_len = 18)
        : min_length(min_len),
          max_length(max_len),
          rng(static_cast<uint32_t>(
              std::chrono::high_resolution_clock::now().time_since_epoch().count())) {}

    void clear() { chain.clear(); }

    void train_from_file(const std::string& filename) {
        std::ifstream file(filename);
        if (!file) {
            throw std::runtime_error("Could not open training file: " + filename);
        }

        std::string word;
        std::string prev = START_TOKEN;

        while (file >> word) {
            clean_word(word);
            if (word.empty()) continue;

            chain[prev].push_back(word);
            prev = word;
        }
    }

    void train_from_string(const std::string& text) {
        std::istringstream iss(text);
        std::string word;
        std::string prev = START_TOKEN;

        while (iss >> word) {
            clean_word(word);
            if (word.empty()) continue;

            chain[prev].push_back(word);
            prev = word;
        }
    }

    bool load_json(const std::string& filename) {
        std::ifstream file(filename);
        if (!file) return false;

        json j;
        try {
            file >> j;
        } catch (...) {
            return false;
        }

        if (!j.is_object()) return false;

        chain.clear();

        for (auto it = j.begin(); it != j.end(); ++it) {
            if (!it.value().is_array()) continue;

            std::vector<std::string> next;
            next.reserve(it.value().size());

            for (const auto& v : it.value()) {
                if (v.is_string()) next.push_back(v.get<std::string>());
            }

            if (!next.empty()) {
                chain.emplace(it.key(), std::move(next));
            }
        }

        return !chain.empty();
    }

    bool save_json(const std::string& filename) const {
        json j = json::object();
        for (const auto& kv : chain) {
            j[kv.first] = kv.second;
        }

        std::ofstream file(filename);
        if (!file) return false;

        file << j.dump(2);
        return true;
    }

    std::string generate_sentence() {
        if (chain.empty()) return "";

        std::uniform_int_distribution<int> len_dist(min_length, max_length);
        int target_len = len_dist(rng);

        std::vector<std::string> out;
        out.reserve(static_cast<size_t>(target_len));

        std::string prev = pick_start_state();

        for (int i = 0; i < target_len; ++i) {
            auto it = chain.find(prev);

            if (it == chain.end() || it->second.empty()) {
                prev = pick_random_state();
                it = chain.find(prev);
                if (it == chain.end() || it->second.empty()) break;
            }

            const auto& options = it->second;
            std::uniform_int_distribution<size_t> pick(0, options.size() - 1);
            const std::string& next = options[pick(rng)];

            out.push_back(next);
            prev = next;
        }

        std::string sentence;
        for (size_t i = 0; i < out.size(); ++i) {
            if (i) sentence.push_back(' ');
            sentence += out[i];
        }

        if (!sentence.empty()) {
            sentence[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(sentence[0])));
            char last = sentence.back();
            if (last != '.' && last != '!' && last != '?') {
                sentence.push_back('.');
            }
        }

        return sentence;
    }

private:
    static constexpr const char* START_TOKEN = "<START>";

    Chain chain;
    int min_length;
    int max_length;
    std::mt19937 rng;

    static void clean_word(std::string& w) {
        auto keep = [](unsigned char c) {
            return std::isalnum(c) || c == '\'' || c == '-' || c == '_';
        };

        while (!w.empty() && !keep(static_cast<unsigned char>(w.front()))) {
            w.erase(w.begin());
        }
        while (!w.empty() && !keep(static_cast<unsigned char>(w.back()))) {
            w.pop_back();
        }

        std::transform(w.begin(), w.end(), w.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
    }

    std::string pick_start_state() {
        if (chain.find(START_TOKEN) != chain.end()) return START_TOKEN;
        if (chain.find("") != chain.end()) return "";
        return pick_random_state();
    }

    std::string pick_random_state() {
        if (chain.empty()) return START_TOKEN;

        std::uniform_int_distribution<size_t> dist(0, chain.size() - 1);
        size_t idx = dist(rng);

        auto it = chain.begin();
        std::advance(it, static_cast<long>(idx));
        return it->first;
    }
};

static void print_help(const char* exe) {
    std::cout
        << "Usage: " << exe << " [options]\n\n"
        << "Options:\n"
        << "  --load <file>       Load existing Markov chain JSON.\n"
        << "  --train <file>      Train from a plain text file.\n"
        << "  --save <file>       Save Markov chain JSON.\n"
        << "  --min <n>           Minimum words per sentence (default 6).\n"
        << "  --max <n>           Maximum words per sentence (default 18).\n"
        << "  --count <n>         How many sentences to generate (default 20).\n"
        << "  --interactive       Enter a simple REPL.\n"
        << "  --help              Show this help.\n\n"
        << "If you provide neither --load nor --train, the program will try:\n"
        << "  1) mutarkov_brain.json\n"
        << "  2) text_data.txt\n"
        << "and then will save to markov_memory.json.\n";
}

int main(int argc, char** argv) {
    std::string load_file;
    std::string train_file;
    std::string save_file;

    int min_len = 6;
    int max_len = 18;
    int count = 20;

    bool interactive = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];

        auto need_value = [&](const char* opt) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error(std::string("Missing value for ") + opt);
            }
            return argv[++i];
        };

        if (a == "--load") {
            load_file = need_value("--load");
        } else if (a == "--train") {
            train_file = need_value("--train");
        } else if (a == "--save") {
            save_file = need_value("--save");
        } else if (a == "--min") {
            min_len = std::stoi(need_value("--min"));
        } else if (a == "--max") {
            max_len = std::stoi(need_value("--max"));
        } else if (a == "--count") {
            count = std::stoi(need_value("--count"));
        } else if (a == "--interactive") {
            interactive = true;
        } else if (a == "--help" || a == "-h") {
            print_help(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown argument: " << a << "\n";
            print_help(argv[0]);
            return 2;
        }
    }

    if (min_len < 1) min_len = 1;
    if (max_len < min_len) max_len = min_len;

    MarkovBot bot(min_len, max_len);

    if (load_file.empty() && train_file.empty()) {
        if (!bot.load_json("mutarkov_brain.json")) {
            try {
                bot.train_from_file("text_data.txt");
            } catch (...) {
                // If defaults don't exist, we'll just generate nothing.
            }
        }

        if (save_file.empty()) {
            save_file = "markov_memory.json";
        }
    }

    if (!load_file.empty()) {
        if (!bot.load_json(load_file)) {
            std::cerr << "Failed to load JSON from " << load_file << "\n";
        }
    }

    if (!train_file.empty()) {
        try {
            bot.train_from_file(train_file);
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n";
            return 1;
        }
    }

    if (interactive) {
        std::cout
            << "Mutarkov interactive mode.\n"
            << "Commands:\n"
            << "  say [n]            Generate n sentences (default 1)\n"
            << "  train <file>      Train from text file\n"
            << "  load <file>       Load chain JSON\n"
            << "  save <file>       Save chain JSON\n"
            << "  clear             Clear chain\n"
            << "  help              Show commands\n"
            << "  quit              Exit\n";

        std::string line;

        while (true) {
            std::cout << "mutarkov> " << std::flush;
            if (!std::getline(std::cin, line)) break;

            std::istringstream iss(line);
            std::string cmd;
            iss >> cmd;

            if (cmd.empty()) continue;

            if (cmd == "quit" || cmd == "exit") {
                break;
            } else if (cmd == "help") {
                std::cout << "say [n], train <file>, load <file>, save <file>, clear, quit\n";
            } else if (cmd == "clear") {
                bot.clear();
                std::cout << "Chain cleared.\n";
            } else if (cmd == "say") {
                int n = 1;
                if (iss >> n) {
                    if (n < 1) n = 1;
                }
                for (int k = 0; k < n; ++k) {
                    std::cout << bot.generate_sentence() << "\n";
                }
            } else if (cmd == "train") {
                std::string f;
                if (!(iss >> f)) {
                    std::cout << "Usage: train <file>\n";
                    continue;
                }
                try {
                    bot.train_from_file(f);
                    std::cout << "Trained from " << f << "\n";
                } catch (const std::exception& e) {
                    std::cout << e.what() << "\n";
                }
            } else if (cmd == "load") {
                std::string f;
                if (!(iss >> f)) {
                    std::cout << "Usage: load <file>\n";
                    continue;
                }
                if (bot.load_json(f)) {
                    std::cout << "Loaded " << f << "\n";
                } else {
                    std::cout << "Failed to load " << f << "\n";
                }
            } else if (cmd == "save") {
                std::string f;
                if (!(iss >> f)) {
                    std::cout << "Usage: save <file>\n";
                    continue;
                }
                if (bot.save_json(f)) {
                    std::cout << "Saved " << f << "\n";
                } else {
                    std::cout << "Failed to save " << f << "\n";
                }
            } else {
                std::cout << "Unknown command: " << cmd << "\n";
            }
        }
    } else {
        std::cout << "Generating sentences...\n";
        for (int i = 0; i < count; ++i) {
            std::cout << "Generated sentence: " << bot.generate_sentence() << "\n";
        }
    }

    if (!save_file.empty()) {
        if (!bot.save_json(save_file)) {
            std::cerr << "Warning: could not save to " << save_file << "\n";
        }
    }

    return 0;
}
