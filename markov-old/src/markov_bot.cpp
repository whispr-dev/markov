#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <random>
#include <algorithm>
#include <regex>

using namespace std;

class MarkovChain {
public:
    void train(const string& text) {
        istringstream iss(text);
        string prev, word;
        while (iss >> word) {
            chain[prev].push_back(word);
            prev = word;
        }
        chain[prev]; // ensure last word is in the map
    }

    string generate(size_t target_words = 100) {
        string output, current = "";
        random_device rd;
        mt19937 gen(rd());

        for (size_t i = 0; i < target_words; ++i) {
            const auto& next_words = chain[current];
            if (next_words.empty()) break;

            uniform_int_distribution<> dis(0, next_words.size() - 1);
            string next = next_words[dis(gen)];
            output += next + " ";
            current = next;
        }
        return output;
    }

private:
    unordered_map<string, vector<string>> chain;
};

vector<string> split_sentences(const string& text) {
    vector<string> sentences;
    regex re(R"([^\.!?]+)");
    sregex_iterator it(text.begin(), text.end(), re), end;
    while (it != end) {
        string s = it->str();
        s.erase(remove_if(s.begin(), s.end(), ::isspace), s.end());
        if (!s.empty()) sentences.push_back(it->str());
        ++it;
    }
    return sentences;
}

string filter_by_length(const string& text, size_t min_len = 7, size_t max_len = 10) {
    auto sentences = split_sentences(text);
    stringstream ss;
    for (const auto& sent : sentences) {
        size_t word_count = count_if(sent.begin(), sent.end(), [](char c) { return c == ' '; }) + 1;
        if (word_count >= min_len && word_count <= max_len) {
            ss << sent << ". ";
        }
    }
    return ss.str();
}

string filter_by_keywords(const string& text, const vector<string>& include, const vector<string>& exclude) {
    auto sentences = split_sentences(text);
    stringstream ss;
    for (const auto& sent : sentences) {
        bool include_match = include.empty() || any_of(include.begin(), include.end(), [&](const string& kw) {
            return regex_search(sent, regex("\\b" + kw + "\\b", regex::icase));
        });
        bool exclude_match = any_of(exclude.begin(), exclude.end(), [&](const string& kw) {
            return regex_search(sent, regex("\\b" + kw + "\\b", regex::icase));
        });
        if (include_match && !exclude_match) {
            ss << sent << ". ";
        }
    }
    return ss.str();
}

// Naive POS filter: only keeps alphabetic words (placeholder for real POS tagger)
string filter_by_pos(const string& text, const vector<string>& allowed = {"NOUN", "VERB", "ADJ"}) {
    stringstream ss(text);
    string word, result;
    while (ss >> word) {
        if (all_of(word.begin(), word.end(), ::isalpha)) {
            result += word + " ";
        }
    }
    return result;
}

int main() {
    ifstream file("text_data.txt");
    if (!file) {
        cerr << "Failed to open text_data.txt\n";
        return 1;
    }

    stringstream buffer;
    buffer << file.rdbuf();
    string text = buffer.str();

    MarkovChain markov;
    markov.train(text);

    string gen_text = markov.generate(200);
    cout << "Generated:\n" << gen_text << "\n\n";

    string length_filtered = filter_by_length(gen_text, 3, 50);
    cout << "After length filter:\n" << length_filtered << "\n\n";

    vector<string> exclude = {"moon", "gun", "ice", "glowing"};
    string keyword_filtered = filter_by_keywords(length_filtered, {}, exclude);
    cout << "After keyword filter:\n" << keyword_filtered << "\n\n";

    string final_text = filter_by_pos(keyword_filtered);
    cout << "Final POS filtered:\n" << final_text << "\n";

    return 0;
}
