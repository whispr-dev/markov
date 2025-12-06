#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <random>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <thread>

// nlohmann single-header JSON (place json.hpp alongside this file)
#include "json.hpp"
using json = nlohmann::json;

struct Args {
    std::string brain_in  = "mutarkov_brain.json";
    std::string brain_out = "mutarkov_brain.json";
    std::string corpus;                  // optional
    std::string pos_lex_path = "custom_pos_library.txt";
    std::vector<std::string> pos_allow;  // e.g. NOUN,VERB,ADJ
    std::vector<std::string> pos_block;  // e.g. PRON,NUM
    int min_len = 6;
    int max_len = 15;
    int delay_ms = 500;
    int count = -1;                      // -1 = infinite
    bool save_after_train = true;

    // entropy / fragility / reversibility knobs
    double temperature = 1.0;            // 0.5 = more stable, 1.0 = neutral, 2.0 = wilder
    double entropy_pressure = 0.00;      // 0..1: prob to inject random vocab word (noise)
    double repetition_decay = 0.50;      // 0..1: how strongly to avoid recent words
    int    repeat_window = 25;           // recent window size for repetition penalty
    int    undo_window = 3;              // how many tokens we can roll back if cornered
    unsigned seed = (unsigned)std::time(nullptr);
};

static inline std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){return std::tolower(c);});
    return s;
}

static inline void trim(std::string &s) {
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    size_t i = 0; while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    if (i) s.erase(0, i);
}

class Markov {
public:
    // state -> multiset(next) implemented as vector (duplicates = frequency)
    std::unordered_map<std::string, std::vector<std::string>> nexts;
    std::unordered_set<std::string> vocab;
    std::mt19937 rng{std::random_device{}()};

    void seed(unsigned s) { rng.seed(s); }

    void train_file(const std::string& filename) {
        std::ifstream f(filename);
        if (!f) {
            std::cerr << "[WARN] Could not open corpus: " << filename << "\n";
            return;
        }
        std::string prev = "", word;
        size_t added = 0;
        while (f >> word) {
            clean_word(word);
            if (word.empty()) continue;
            nexts[prev].push_back(word);
            vocab.insert(word);
            prev = word;
            ++added;
        }
        // ensure terminal state exists
        nexts[prev];
        std::cerr << "[INFO] Trained from " << filename << " (" << added << " tokens)\n";
    }

    bool load(const std::string& filename) {
        std::ifstream f(filename);
        if (!f) return false;
        json j; f >> j;
        nexts = j.get<std::unordered_map<std::string, std::vector<std::string>>>();
        // rebuild vocab
        vocab.clear();
        for (auto &kv : nexts) {
            for (auto &w : kv.second) vocab.insert(w);
            if (!kv.first.empty()) vocab.insert(kv.first);
        }
        std::cerr << "[INFO] Loaded brain: " << filename
                  << " (states=" << nexts.size() << ", vocab=" << vocab.size() << ")\n";
        return true;
    }

    bool save(const std::string& filename) {
        std::ofstream f(filename);
        if (!f) return false;
        f << json(nexts);
        std::cerr << "[INFO] Saved brain: " << filename
                  << " (states=" << nexts.size() << ")\n";
        return true;
    }

    // select a solid starting key (not empty, with outgoing edges), fallbacks included
    std::string choose_start() {
        if (nexts.empty()) return "";
        for (int tries = 0; tries < 64; ++tries) {
            auto it = nexts.begin();
            std::advance(it, rng() % nexts.size());
            if (!it->first.empty() && !it->second.empty()) return it->first;
        }
        if (!nexts[""].empty()) return "";
        for (auto &kv : nexts) if (!kv.second.empty()) return kv.first;
        return "";
    }

private:
    void clean_word(std::string &w) {
        // keep alnum, apostrophe, hyphen
        std::string out; out.reserve(w.size());
        for (unsigned char c : w) {
            if (std::isalnum(c) || c=='\'' || c=='-') out.push_back((char)c);
        }
        w.swap(out);
    }
};

// Simple POS lexicon built from "== TAG ==" sections in custom_pos_library.txt
class POSLex {
public:
    // word -> set of tags
    std::unordered_map<std::string, std::unordered_set<std::string>> tags_of;

    bool load(const std::string& path) {
        std::ifstream f(path);
        if (!f) {
            std::cerr << "[WARN] No POS lexicon file: " << path << " (continuing)\n";
            return false;
        }
        std::string line, current_tag;
        size_t add_count = 0;
        while (std::getline(f, line)) {
            std::string s = line;
            trim(s);
            if (s.size() >= 6 && s.rfind("==",0)==0) {
                // format: "== TAG =="
                // extract TAG
                size_t p1 = s.find_first_not_of("= ");
                size_t p2 = s.find("==", p1);
                std::string mid = (p1!=std::string::npos && p2!=std::string::npos) ? s.substr(p1, p2 - p1) : "";
                trim(mid);
                current_tag = mid;
                continue;
            }
            if (current_tag.empty() || s.empty()) continue;

            // comma-separated words, possibly line-wrapped
            std::stringstream ss(s);
            std::string token;
            while (std::getline(ss, token, ',')) {
                trim(token);
                if (token.empty()) continue;
                std::string w = lower(token);
                tags_of[w].insert(current_tag);
                ++add_count;
            }
        }
        std::cerr << "[INFO] Loaded POS lexicon entries: " << add_count << "\n";
        return !tags_of.empty();
    }

    // Return POS tags for a word; if unknown, heuristic guess
    std::unordered_set<std::string> get(const std::string& word) const {
        std::string w = lower(word);
        auto it = tags_of.find(w);
        if (it != tags_of.end()) return it->second;

        // cheap heuristics
        std::unordered_set<std::string> guess;
        if (ends_with(w, "ly")) guess.insert("ADV");
        if (ends_with(w, "ing") || ends_with(w, "ed") || ends_with(w, "s")) guess.insert("VERB");
        if (guess.empty()) guess.insert("NOUN");
        return guess;
    }

private:
    static bool ends_with(const std::string& s, const std::string& suf) {
        return s.size()>=suf.size() && std::equal(suf.rbegin(), suf.rend(), s.rbegin());
    }
};

// ——— Selection Engine with Entropy/Fragility/Reversibility ———

struct Selector {
    const Markov& model;
    const POSLex& plex;
    const Args& args;
    std::mt19937 rng;

    // recent history for repetition decay / reversibility
    std::vector<std::string> history;
    std::unordered_map<std::string,int> recent_count;

    Selector(const Markov& m, const POSLex& p, const Args& a)
    : model(m), plex(p), args(a), rng(a.seed) {}

    void history_push(const std::string& w) {
        history.push_back(w);
        recent_count[w]++;
        if ((int)history.size() > args.repeat_window) {
            const std::string& old = history.front();
            if (--recent_count[old] <= 0) recent_count.erase(old);
            history.erase(history.begin());
        }
    }

    bool pos_allowed(const std::string& w) const {
        auto tags = plex.get(w);
        if (!args.pos_allow.empty()) {
            bool any = false;
            for (auto& t : args.pos_allow) {
                if (tags.count(t)) { any = true; break; }
            }
            if (!any) return false;
        }
        if (!args.pos_block.empty()) {
            for (auto& t : args.pos_block) {
                if (tags.count(t)) return false;
            }
        }
        return true;
    }

    // Build filtered candidate list from current state
    std::vector<std::string> candidates_for(const std::string& state) {
        auto it = model.nexts.find(state);
        if (it==model.nexts.end() || it->second.empty()) return {};
        // Frequency is represented by duplicates already; keep that behavior
        std::vector<std::string> pool;
        pool.reserve(it->second.size());
        for (auto &w : it->second) {
            if (!pos_allowed(w)) continue;
            // repetition decay: probabilistically drop recent words
            double decay = args.repetition_decay;
            int rc = 0;
            auto rci = recent_count.find(w);
            if (rci != recent_count.end()) rc = rci->second;
            if (rc > 0 && decay > 0.0) {
                // Drop with probability proportional to rc * decay (capped)
                double pdrop = std::min(0.9, rc * decay * 0.25);
                std::uniform_real_distribution<> U(0.0, 1.0);
                if (U(rng) < pdrop) continue;
            }
            pool.push_back(w);
        }
        return pool;
    }

    // k-sampling "temperature-ish": k = ceil(max(1, temperature*2))
    std::string sample_from(std::vector<std::string>& pool) {
        if (pool.empty()) return {};
        int k = std::max(1, (int)std::ceil(args.temperature * 2.0));
        std::uniform_int_distribution<> D(0, (int)pool.size()-1);

        // gather k candidates; prefer least-recently used among them
        std::string best;
        int best_rc = INT_MAX;
        for (int i=0;i<k;i++) {
            const std::string& w = pool[D(rng)];
            int rc = 0;
            auto it = recent_count.find(w);
            if (it != recent_count.end()) rc = it->second;
            if (rc < best_rc) { best_rc = rc; best = w; }
        }
        return best;
    }

    // entropy pressure: inject random vocab word with probability p
    std::string maybe_inject_noise() {
        if (model.vocab.empty()) return {};
        std::uniform_real_distribution<> U(0.0, 1.0);
        if (U(rng) < args.entropy_pressure) {
            // pull a random vocab word that passes POS
            auto idx = rng() % model.vocab.size();
            auto it = model.vocab.begin();
            std::advance(it, idx);
            if (pos_allowed(*it)) return *it;
        }
        return {};
    }

    // attempt to pick next word; may rollback if cornered (reversibility)
    bool pick_next(std::string& state, std::string& out_word) {
        // try normal transition
        auto pool = candidates_for(state);

        // If pool empty, try entropy injection
        if (pool.empty()) {
            std::string noise = maybe_inject_noise();
            if (!noise.empty()) {
                out_word = noise;
                return true;
            }
        }

        // If still empty and we can undo, roll back
        if (pool.empty() && args.undo_window > 0 && history.size() > 0) {
            int steps = std::min(args.undo_window, (int)history.size());
            // Fragility: collapse chance increases with temperature/noise
            // If collapse triggers, end sentence by refusing further picks.
            double collapse_p = std::min(0.6, (args.temperature-1.0)*0.2 + args.entropy_pressure*0.4);
            std::uniform_real_distribution<> U(0.0, 1.0);
            if (U(rng) < collapse_p) {
                return false; // end sequence early (fabric "tears")
            }

            // otherwise undo a couple of steps and try again from the earlier state
            for (int i=0;i<steps;i++) {
                const std::string& last = history.back();
                if (--recent_count[last] <= 0) recent_count.erase(last);
                history.pop_back();
            }
            // new state is previous token or "" if none
            if (!history.empty()) state = history.back();
            else state = "";
            pool = candidates_for(state);
        }

        if (pool.empty()) return false;

        // temperature-ish selection
        out_word = sample_from(pool);
        return !out_word.empty();
    }
};

// -------------------- CLI parsing --------------------

static std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        trim(tok);
        if (!tok.empty()) out.push_back(tok);
    }
    return out;
}

static Args parse_args(int argc, char** argv) {
    Args a;
    for (int i=1;i<argc;i++) {
        std::string s = argv[i];
        auto need = [&](const char* name){
            if (i+1>=argc) { std::cerr << "[ERR] Missing value for " << name << "\n"; std::exit(2); }
            return std::string(argv[++i]);
        };
        if      (s=="--brain-in")        a.brain_in = need(s.c_str());
        else if (s=="--brain-out")       a.brain_out = need(s.c_str());
        else if (s=="--corpus")          a.corpus = need(s.c_str());
        else if (s=="--pos-lex")         a.pos_lex_path = need(s.c_str());
        else if (s=="--pos-allow")       a.pos_allow = split_csv(need(s.c_str()));
        else if (s=="--pos-block")       a.pos_block = split_csv(need(s.c_str()));
        else if (s=="--min")             a.min_len = std::stoi(need(s.c_str()));
        else if (s=="--max")             a.max_len = std::stoi(need(s.c_str()));
        else if (s=="--delay-ms")        a.delay_ms = std::stoi(need(s.c_str()));
        else if (s=="--count")           a.count = std::stoi(need(s.c_str()));
        else if (s=="--no-save")         a.save_after_train = false;
        else if (s=="--temperature")     a.temperature = std::stod(need(s.c_str()));
        else if (s=="--entropy-pressure")a.entropy_pressure = std::stod(need(s.c_str()));
        else if (s=="--repetition-decay")a.repetition_decay = std::stod(need(s.c_str()));
        else if (s=="--repeat-window")   a.repeat_window = std::stoi(need(s.c_str()));
        else if (s=="--undo-window")     a.undo_window = std::stoi(need(s.c_str()));
        else if (s=="--seed")            a.seed = (unsigned)std::stoul(need(s.c_str()));
        else if (s=="--help" || s=="-h") {
            std::cout <<
R"(Mutarkov+POS — persistent Markov bot with POS filtering + entropy/fragility/reversibility

Usage:
  mutarkov_pos_entropy [--brain-in FILE] [--brain-out FILE]
                       [--corpus FILE]
                       [--pos-lex FILE]
                       [--pos-allow CSV] [--pos-block CSV]
                       [--min N] [--max N] [--delay-ms MS] [--count N]
                       [--temperature F] [--entropy-pressure F]
                       [--repetition-decay F] [--repeat-window N]
                       [--undo-window N] [--seed N]
                       [--no-save]

Defaults:
  --brain-in          mutarkov_brain.json
  --brain-out         mutarkov_brain.json
  --pos-lex           custom_pos_library.txt
  --min               6
  --max               15
  --delay-ms          500
  --count             -1   (infinite)
  --temperature       1.0  (0.5 calmer … 2.0 wilder)
  --entropy-pressure  0.00 (0..1)
  --repetition-decay  0.50 (0..1)
  --repeat-window     25
  --undo-window       3

Examples:
  # Train from corpus, allow only NOUN/VERB/ADJ, block PRON, run forever
  ./mutarkov_pos_entropy --corpus text_data_filtered.txt \
      --pos-allow NOUN,VERB,ADJ --pos-block PRON

  # Load existing brain, wilder sampling with light noise, print 40 lines
  ./mutarkov_pos_entropy --temperature 1.8 --entropy-pressure 0.05 \
      --count 40 --delay-ms 80

  # Calmer output, heavy repetition avoidance, with undo
  ./mutarkov_pos_entropy --temperature 0.8 --repetition-decay 0.8 \
      --undo-window 5
)";
            std::exit(0);
        } else {
            std::cerr << "[WARN] Unknown arg: " << s << "\n";
        }
    }
    // hygiene
    if (a.min_len < 1) a.min_len = 1;
    if (a.max_len < a.min_len) a.max_len = a.min_len;
    if (a.temperature < 0.1) a.temperature = 0.1;
    if (a.entropy_pressure < 0.0) a.entropy_pressure = 0.0;
    if (a.entropy_pressure > 1.0) a.entropy_pressure = 1.0;
    if (a.repetition_decay < 0.0) a.repetition_decay = 0.0;
    if (a.repetition_decay > 1.0) a.repetition_decay = 1.0;
    if (a.repeat_window < 1) a.repeat_window = 1;
    if (a.undo_window < 0) a.undo_window = 0;
    return a;
}

int main(int argc, char** argv) {
    Args args = parse_args(argc, argv);

    Markov mk;
    mk.seed(args.seed);
    mk.load(args.brain_in);  // ok if missing

    if (!args.corpus.empty()) {
        mk.train_file(args.corpus);
        if (args.save_after_train) mk.save(args.brain_out);
    }

    POSLex plex;
    plex.load(args.pos_lex_path); // ok if missing; we’ll use heuristics

    // Generator
    std::uniform_int_distribution<int> L(args.min_len, args.max_len);
    Selector sel{mk, plex, args};

    auto gen_once = [&](){
        std::string state = mk.choose_start();
        if (mk.nexts.empty()) return std::string("[Empty model]");
        int target = L(sel.rng);
        std::string out;
        for (int i=0; i<target; ++i) {
            std::string next;
            if (!sel.pick_next(state, next)) break; // collapse or no options
            if (next.empty()) break;
            out += next + " ";
            sel.history_push(next);
            state = next;
        }
        if (!out.empty()) {
            out.pop_back();
            // Capitalize first letter, add period.
            if (std::isalpha((unsigned char)out[0])) out[0] = (char)std::toupper(out[0]);
            out += ".";
        } else {
            out = "[No sentence generated!]";
        }
        return out;
    };

    if (args.count < 0) {
        while (true) {
            std::cout << "Generated sentence: " << gen_once() << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(args.delay_ms));
        }
    } else {
        for (int i=0; i<args.count; ++i) {
            std::cout << "Generated sentence: " << gen_once() << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(args.delay_ms));
        }
    }
    return 0;
}
