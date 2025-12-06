// mutarkov_grammar_pos.cpp
// Markov + POS filters + entropy/fragility/reversibility + lightweight grammar & adjective ordering
// fren + woflfren

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
#include <climits>

#include "json.hpp"
using json = nlohmann::json;

// -------------------- args --------------------
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
    double temperature = 1.0;            // 0.5 calmer, 1.0 neutral, 2.0 wild
    double entropy_pressure = 0.00;      // 0..1: noise injection chance
    double repetition_decay = 0.50;      // 0..1: avoid recently used words
    int    repeat_window = 25;           // history length
    int    undo_window = 3;              // steps we can roll back
    unsigned seed = (unsigned)std::time(nullptr);

    // grammar
    std::string grammar_mode = "svo";    // "svo" or "off"
    int max_adj = 3;                     // max adjectives in an NP
};

static inline std::string lower(std::string s){ std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){return std::tolower(c);}); return s; }
static inline void trim(std::string &s){
    while(!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    size_t i=0; while(i<s.size() && std::isspace((unsigned char)s[i])) ++i;
    if(i) s.erase(0,i);
}

// -------------------- Markov model --------------------
class Markov {
public:
    // state -> multiset(next) as vector (dup = freq)
    std::unordered_map<std::string, std::vector<std::string>> nexts;
    std::unordered_set<std::string> vocab;
    std::mt19937 rng{std::random_device{}()};

    void seed(unsigned s){ rng.seed(s); }

    void train_file(const std::string& filename){
        std::ifstream f(filename);
        if(!f){ std::cerr << "[WARN] Could not open corpus: " << filename << "\n"; return; }
        std::string prev = "", word;
        size_t added = 0;
        while(f >> word){
            clean_word(word);
            if(word.empty()) continue;
            nexts[prev].push_back(word);
            vocab.insert(word);
            prev = word;
            ++added;
        }
        nexts[prev]; // ensure terminal state
        std::cerr << "[INFO] Trained from " << filename << " (" << added << " tokens)\n";
    }

    bool load(const std::string& filename){
        std::ifstream f(filename);
        if(!f) return false;
        json j; f >> j;
        nexts = j.get<std::unordered_map<std::string, std::vector<std::string>>>();
        vocab.clear();
        for(auto &kv : nexts){
            for(auto &w : kv.second) vocab.insert(w);
            if(!kv.first.empty()) vocab.insert(kv.first);
        }
        std::cerr << "[INFO] Loaded brain: " << filename << " (states=" << nexts.size() << ", vocab=" << vocab.size() << ")\n";
        return true;
    }

    bool save(const std::string& filename){
        std::ofstream f(filename);
        if(!f) return false;
        f << json(nexts);
        std::cerr << "[INFO] Saved brain: " << filename << " (states=" << nexts.size() << ")\n";
        return true;
    }

    std::string choose_start(){
        if(nexts.empty()) return "";
        for(int tries=0; tries<64; ++tries){
            auto it = nexts.begin();
            std::advance(it, rng() % nexts.size());
            if(!it->first.empty() && !it->second.empty()) return it->first;
        }
        if(!nexts[""].empty()) return "";
        for(auto &kv : nexts) if(!kv.second.empty()) return kv.first;
        return "";
    }

private:
    static void clean_word(std::string &w){
        std::string out; out.reserve(w.size());
        for(unsigned char c : w){
            if(std::isalnum(c) || c=='\'' || c=='-') out.push_back((char)c);
        }
        w.swap(out);
    }
};

// -------------------- POS lexicon --------------------
class POSLex {
public:
    // word -> set of tags (e.g., NOUN/VERB/ADJ/DET/ADV/PRON/...)
    std::unordered_map<std::string, std::unordered_set<std::string>> tags_of;

    bool load(const std::string& path){
        std::ifstream f(path);
        if(!f){ std::cerr << "[WARN] No POS lexicon file: " << path << " (continuing)\n"; return false; }
        std::string line, current_tag;
        size_t add_count = 0;
        while(std::getline(f, line)){
            std::string s = line; trim(s);
            if(s.size()>=6 && s.rfind("==",0)==0){
                // expect "== TAG =="
                size_t p1 = s.find_first_not_of("= ");
                size_t p2 = s.find("==", p1);
                std::string mid = (p1!=std::string::npos && p2!=std::string::npos) ? s.substr(p1, p2 - p1) : "";
                trim(mid);
                current_tag = mid;
                continue;
            }
            if(current_tag.empty() || s.empty()) continue;

            std::stringstream ss(s);
            std::string token;
            while(std::getline(ss, token, ',')){
                trim(token);
                if(token.empty()) continue;
                std::string w = lower(token);
                tags_of[w].insert(current_tag);
                ++add_count;
            }
        }
        std::cerr << "[INFO] Loaded POS lexicon entries: " << add_count << "\n";
        // add some determiners if missing (safety net)
        for(const std::string& d : {"the","a","an","this","that","these","those"}) tags_of[d].insert("DET");
        return !tags_of.empty();
    }

    std::unordered_set<std::string> get(const std::string& word) const {
        std::string w = lower(word);
        auto it = tags_of.find(w);
        if(it != tags_of.end()) return it->second;
        // heuristics if unknown
        std::unordered_set<std::string> guess;
        if(ends_with(w,"ly")) guess.insert("ADV");
        if(ends_with(w,"ing") || ends_with(w,"ed") || ends_with(w,"s")) guess.insert("VERB");
        if(guess.empty()) guess.insert("NOUN");
        return guess;
    }

private:
    static bool ends_with(const std::string& s, const std::string& suf){
        return s.size()>=suf.size() && std::equal(suf.rbegin(), suf.rend(), s.rbegin());
    }
};

// -------------------- Adjective ordering (Opinion→Size→Age→Shape→Colour→Origin→Material) --------------------
// Based on the convention described in the provided grammar PDF. :contentReference[oaicite:4]{index=4}
enum AdjCat { OPINION=0, SIZE=1, AGE=2, SHAPE=3, COLOUR=4, ORIGIN=5, MATERIAL=6, OTHER=7 };

struct AdjOrder {
    std::unordered_set<std::string> opinion = {
        "nice","lovely","beautiful","wonderful","awful","terrible","cute","amazing","horrible","great","fine","weird","strange","odd","perfect"
    };
    std::unordered_set<std::string> size = {
        "big","small","little","large","huge","tiny","massive","giant","miniature","tall","short","long","wide","narrow"
    };
    std::unordered_set<std::string> age = {
        "old","young","new","ancient","modern","recent","vintage","antique","brand-new"
    };
    std::unordered_set<std::string> shape = {
        "round","square","flat","thin","thick","circular","oval","rectangular","triangular","cubic","spherical","long","short"
    };
    std::unordered_set<std::string> colour = {
        "red","blue","green","black","white","yellow","brown","purple","pink","orange","grey","gray","golden","silver"
    };
    std::unordered_set<std::string> material = {
        "wooden","wood","metal","metallic","steel","iron","gold","silver","plastic","glass","cotton","wool","silk","leather","paper","stone","bronze","copper"
    };
    // simple origin heuristic
    bool looks_origin(const std::string& w) const {
        static const std::unordered_set<std::string> origin_words = {
            "french","english","american","german","chinese","japanese","russian","greek","italian","spanish","dutch","belgian","haitian","arabic","indian","scottish","irish","welsh","swiss","swedish","norwegian","danish","polish","turkish","korean","vietnamese","portuguese","brazilian","mexican","canadian","australian"
        };
        if(origin_words.count(w)) return true;
        // suffix heuristics: -ese, -ish, -ian, -an, -ic
        if(w.size()>=3){
            if(ends_with(w,"ese") || ends_with(w,"ish")) return true;
        }
        if(w.size()>=2){
            if(ends_with(w,"ian") || ends_with(w,"an")) return true;
        }
        if(w.size()>=2 && ends_with(w,"ic")) return true;
        return false;
    }
    static bool ends_with(const std::string& s, const std::string& suf){
        return s.size()>=suf.size() && std::equal(suf.rbegin(), suf.rend(), s.rbegin());
    }

    AdjCat categorize(const std::string& w_in) const {
        std::string w = lower(w_in);
        if(opinion.count(w)) return OPINION;
        if(size.count(w)) return SIZE;
        if(age.count(w)) return AGE;
        if(shape.count(w)) return SHAPE;
        if(colour.count(w)) return COLOUR;
        if(looks_origin(w)) return ORIGIN;
        if(material.count(w)) return MATERIAL;
        return OTHER;
    }

    void sort_in_place(std::vector<std::string>& adjs) const {
        std::stable_sort(adjs.begin(), adjs.end(), [&](const std::string& a, const std::string& b){
            return (int)categorize(a) < (int)categorize(b);
        });
    }
};

// -------------------- Selector (now with target POS) --------------------
struct Selector {
    const Markov& model;
    const POSLex& plex;
    const Args& args;
    std::mt19937 rng;

    // hist for repetition and reversibility
    std::vector<std::string> history;
    std::unordered_map<std::string,int> recent_count;

    // POS→words index (from vocab/lexicon) for fallbacks
    std::unordered_map<std::string, std::vector<std::string>> pos_index;

    Selector(const Markov& m, const POSLex& p, const Args& a)
    : model(m), plex(p), args(a), rng(a.seed)
    {
        // build a crude POS index from vocab using lexicon tags (heuristics if unknown)
        for(const auto& w : model.vocab){
            auto tags = plex.get(w);
            for(const auto& t : tags){
                pos_index[t].push_back(w);
            }
        }
        // Ensure we have at least a few determiners
        if(pos_index["DET"].empty()){
            for(const std::string& d : {"the","a","an","this","that","these","those"})
                pos_index["DET"].push_back(d);
        }
    }

    void history_push(const std::string& w){
        history.push_back(w);
        recent_count[w]++;
        if((int)history.size() > args.repeat_window){
            const std::string& old = history.front();
            if(--recent_count[old] <= 0) recent_count.erase(old);
            history.erase(history.begin());
        }
    }

    bool pos_allowed_by_user(const std::string& w) const {
        auto tags = plex.get(w);
        if(!args.pos_allow.empty()){
            bool any=false;
            for(auto& t : args.pos_allow){ if(tags.count(t)){ any=true; break; } }
            if(!any) return false;
        }
        if(!args.pos_block.empty()){
            for(auto& t : args.pos_block){ if(tags.count(t)) return false; }
        }
        return true;
    }

    bool has_pos(const std::string& w, const std::string& need) const {
        if(need.empty()) return true;
        auto tags = plex.get(w);
        return tags.count(need) > 0;
    }

    std::vector<std::string> candidates_for(const std::string& state, const std::string& need_pos=""){
        auto it = model.nexts.find(state);
        if(it==model.nexts.end() || it->second.empty()) return {};
        std::vector<std::string> pool;
        pool.reserve(it->second.size());
        for(const auto &w : it->second){
            if(!pos_allowed_by_user(w)) continue;
            if(!need_pos.empty() && !has_pos(w, need_pos)) continue;

            // repetition decay
            int rc = 0;
            auto rci = recent_count.find(w);
            if(rci != recent_count.end()) rc = rci->second;
            if(rc>0 && args.repetition_decay>0.0){
                double pdrop = std::min(0.9, rc * args.repetition_decay * 0.25);
                std::uniform_real_distribution<> U(0.0, 1.0);
                if(U(rng) < pdrop) continue;
            }
            pool.push_back(w);
        }
        return pool;
    }

    std::string sample_from(std::vector<std::string>& pool){
        if(pool.empty()) return {};
        int k = std::max(1, (int)std::ceil(args.temperature * 2.0));
        std::uniform_int_distribution<> D(0, (int)pool.size()-1);
        std::string best; int best_rc = INT_MAX;
        for(int i=0;i<k;i++){
            const std::string& w = pool[D(rng)];
            int rc = 0;
            auto it = recent_count.find(w);
            if(it != recent_count.end()) rc = it->second;
            if(rc < best_rc){ best_rc = rc; best = w; }
        }
        return best;
    }

    std::string random_from_pos(const std::string& need_pos){
        auto it = pos_index.find(need_pos);
        if(it==pos_index.end() || it->second.empty()) return {};
        std::uniform_int_distribution<> D(0, (int)it->second.size()-1);
        return it->second[D(rng)];
    }

    std::string maybe_noise(const std::string& need_pos=""){
        if(model.vocab.empty()) return {};
        std::uniform_real_distribution<> U(0.0, 1.0);
        if(U(rng) < args.entropy_pressure){
            // choose random vocab word that matches need_pos & user POS allow/block
            auto idx = rng() % model.vocab.size();
            auto it = model.vocab.begin(); std::advance(it, idx);
            const std::string& cand = *it;
            if((need_pos.empty() || has_pos(cand, need_pos)) && pos_allowed_by_user(cand))
                return cand;
        }
        return {};
    }

    // pick next word that matches need_pos; can rollback if cornered
    bool pick_next(std::string& state, std::string& out_word, const std::string& need_pos=""){
        // normal transition
        auto pool = candidates_for(state, need_pos);

        // try entropy injection
        if(pool.empty()){
            std::string noise = maybe_noise(need_pos);
            if(!noise.empty()){ out_word = noise; return true; }
        }

        // rollback if stuck
        if(pool.empty() && args.undo_window>0 && history.size()>0){
            double collapse_p = std::min(0.6, (args.temperature-1.0)*0.2 + args.entropy_pressure*0.4);
            std::uniform_real_distribution<> U(0.0, 1.0);
            if(U(rng) < collapse_p){
                return false; // end sentence early
            }
            int steps = std::min(args.undo_window, (int)history.size());
            for(int i=0;i<steps;i++){
                const std::string& last = history.back();
                if(--recent_count[last] <= 0) recent_count.erase(last);
                history.pop_back();
            }
            if(!history.empty()) state = history.back(); else state = "";
            pool = candidates_for(state, need_pos);
        }

        if(pool.empty()){
            // last resort: draw from POS bucket directly
            std::string fallback = random_from_pos(need_pos);
            if(!fallback.empty()){ out_word = fallback; return true; }
            return false;
        }

        out_word = sample_from(pool);
        return !out_word.empty();
    }
};

// -------------------- CLI --------------------
static std::vector<std::string> split_csv(const std::string& s){
    std::vector<std::string> out; std::stringstream ss(s); std::string tok;
    while(std::getline(ss, tok, ',')){ trim(tok); if(!tok.empty()) out.push_back(tok); }
    return out;
}

static Args parse_args(int argc, char** argv){
    Args a;
    for(int i=1;i<argc;i++){
        std::string s = argv[i];
        auto need = [&](const char* name){ if(i+1>=argc){ std::cerr<<"[ERR] Missing value for "<<name<<"\n"; std::exit(2);} return std::string(argv[++i]); };
        if      (s=="--brain-in")         a.brain_in = need(s.c_str());
        else if (s=="--brain-out")        a.brain_out = need(s.c_str());
        else if (s=="--corpus")           a.corpus = need(s.c_str());
        else if (s=="--pos-lex")          a.pos_lex_path = need(s.c_str());
        else if (s=="--pos-allow")        a.pos_allow = split_csv(need(s.c_str()));
        else if (s=="--pos-block")        a.pos_block = split_csv(need(s.c_str()));
        else if (s=="--min")              a.min_len = std::stoi(need(s.c_str()));
        else if (s=="--max")              a.max_len = std::stoi(need(s.c_str()));
        else if (s=="--delay-ms")         a.delay_ms = std::stoi(need(s.c_str()));
        else if (s=="--count")            a.count = std::stoi(need(s.c_str()));
        else if (s=="--no-save")          a.save_after_train = false;
        else if (s=="--temperature")      a.temperature = std::stod(need(s.c_str()));
        else if (s=="--entropy-pressure") a.entropy_pressure = std::stod(need(s.c_str()));
        else if (s=="--repetition-decay") a.repetition_decay = std::stod(need(s.c_str()));
        else if (s=="--repeat-window")    a.repeat_window = std::stoi(need(s.c_str()));
        else if (s=="--undo-window")      a.undo_window = std::stoi(need(s.c_str()));
        else if (s=="--seed")             a.seed = (unsigned)std::stoul(need(s.c_str()));
        else if (s=="--grammar")          a.grammar_mode = lower(need(s.c_str())); // "svo" or "off"
        else if (s=="--max-adj")          a.max_adj = std::stoi(need(s.c_str()));
        else if (s=="--help" || s=="-h"){
            std::cout <<
R"(Mutarkov+Grammar — Markov bot with POS filters, entropy/fragility, and CFG scaffolding

Usage:
  mutarkov_grammar_pos [--brain-in FILE] [--brain-out FILE]
                       [--corpus FILE] [--pos-lex FILE]
                       [--pos-allow CSV] [--pos-block CSV]
                       [--min N] [--max N] [--delay-ms MS] [--count N]
                       [--temperature F] [--entropy-pressure F]
                       [--repetition-decay F] [--repeat-window N]
                       [--undo-window N] [--seed N]
                       [--grammar svo|off] [--max-adj N] [--no-save]

Defaults:
  --brain-in/out    mutarkov_brain.json
  --pos-lex         custom_pos_library.txt
  --min/max         6 / 15
  --delay-ms        500
  --count           -1   (infinite)
  --temperature     1.0
  --entropy-pressure 0.00
  --repetition-decay 0.50
  --repeat-window   25
  --undo-window     3
  --grammar         svo
  --max-adj         3

Examples:
  # Train and run with grammar (S->NP VP; NP->DET [Adj*] NOUN)
  ./mutarkov_grammar_pos --corpus text_data_filtered.txt --pos-allow NOUN,VERB,ADJ,DET

  # Wilder and noisier, 40 lines:
  ./mutarkov_grammar_pos --temperature 1.8 --entropy-pressure 0.05 --count 40 --delay-ms 60

  # Pure Markov (no grammar scaffolding):
  ./mutarkov_grammar_pos --grammar off
)";
            std::exit(0);
        } else {
            std::cerr << "[WARN] Unknown arg: " << s << "\n";
        }
    }
    if(a.min_len < 1) a.min_len = 1;
    if(a.max_len < a.min_len) a.max_len = a.min_len;
    if(a.temperature < 0.1) a.temperature = 0.1;
    if(a.entropy_pressure < 0.0) a.entropy_pressure = 0.0;
    if(a.entropy_pressure > 1.0) a.entropy_pressure = 1.0;
    if(a.repetition_decay < 0.0) a.repetition_decay = 0.0;
    if(a.repetition_decay > 1.0) a.repetition_decay = 1.0;
    if(a.repeat_window < 1) a.repeat_window = 1;
    if(a.undo_window < 0) a.undo_window = 0;
    if(a.max_adj < 0) a.max_adj = 0;
    return a;
}

// -------------------- Grammar-driven generation --------------------
static bool flip(std::mt19937& rng, int pct){ std::uniform_int_distribution<> D(1,100); return D(rng) <= pct; }

std::vector<std::string> build_template_svo(std::mt19937& rng, int max_adj){
    // S -> NP VP; NP -> DET (Adj*) NOUN; VP -> VERB NP
    // Allow determiners probabilistically.
    std::vector<std::string> tpl;
    if(flip(rng, 85)) tpl.push_back("DET");
    tpl.push_back("ADJ*");
    tpl.push_back("NOUN");
    tpl.push_back("VERB");
    if(flip(rng, 85)) tpl.push_back("DET");
    tpl.push_back("ADJ*");
    tpl.push_back("NOUN");
    // ADJ* is a marker: generator will pick 0..max_adj adjectives and order them.
    // The overall structure mirrors the sample grammar in the PDF. :contentReference[oaicite:5]{index=5}
    return tpl;
}

std::string capitalize_first(std::string s){
    if(!s.empty() && std::isalpha((unsigned char)s[0])) s[0] = (char)std::toupper(s[0]);
    return s;
}

int main(int argc, char** argv){
    Args args = parse_args(argc, argv);
    Markov mk; mk.seed(args.seed);
    mk.load(args.brain_in);
    if(!args.corpus.empty()){
        mk.train_file(args.corpus);
        if(args.save_after_train) mk.save(args.brain_out);
    }
    POSLex plex; plex.load(args.pos_lex_path);
    AdjOrder adj_order;
    std::uniform_int_distribution<int> L(args.min_len, args.max_len);
    Selector sel{mk, plex, args};

    auto gen_markov_only = [&](){
        std::string state = mk.choose_start();
        if(mk.nexts.empty()) return std::string("[Empty model]");
        int target = L(sel.rng);
        std::string out;
        for(int i=0;i<target;++i){
            std::string next;
            if(!sel.pick_next(state, next, "")) break;
            if(next.empty()) break;
            out += next + " ";
            sel.history_push(next);
            state = next;
        }
        if(!out.empty()){
            out.pop_back();
            out = capitalize_first(out) + ".";
        } else out = "[No sentence generated!]";
        return out;
    };

    auto gen_with_grammar = [&](){
        if(mk.nexts.empty()) return std::string("[Empty model]");
        std::string state = mk.choose_start();
        std::string out;
        std::vector<std::string> tpl = build_template_svo(sel.rng, args.max_adj);

        for(size_t i=0;i<tpl.size(); ++i){
            const std::string slot = tpl[i];
            if(slot == "ADJ*"){
                // choose 0..max_adj adjectives, order them, append
                int how_many = (args.max_adj==0) ? 0 : std::uniform_int_distribution<>(0, args.max_adj)(sel.rng);
                std::vector<std::string> adjs;
                for(int k=0;k<how_many;++k){
                    std::string w;
                    // try Markov-consistent ADJ first
                    if(!sel.pick_next(state, w, "ADJ")){
                        // fallback: random ADJ
                        w = sel.random_from_pos("ADJ");
                        if(w.empty()) break;
                    }
                    adjs.push_back(w);
                    sel.history_push(w);
                    state = w;
                }
                // order adjectives by category (Opinion→Size→Age→Shape→Colour→Origin→Material) :contentReference[oaicite:6]{index=6}
                adj_order.sort_in_place(adjs);
                for(const auto& w : adjs) out += w + " ";
                continue;
            }

            std::string next;
            if(!sel.pick_next(state, next, slot)){
                // last-ditch: try grabbing from POS bucket
                next = sel.random_from_pos(slot);
                if(next.empty()){
                    // try noise
                    next = sel.maybe_noise(slot);
                    if(next.empty()) break;
                }
            }
            out += next + " ";
            sel.history_push(next);
            state = next;
        }

        trim(out);
        if(!out.empty()) out = capitalize_first(out) + ".";
        else out = "[No sentence generated!]";
        return out;
    };

    auto gen_once = [&](){
        if(args.grammar_mode=="off") return gen_markov_only();
        return gen_with_grammar();
    };

    if(args.count < 0){
        while(true){
            std::cout << "Generated sentence: " << gen_once() << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(args.delay_ms));
        }
    } else {
        for(int i=0;i<args.count;++i){
            std::cout << "Generated sentence: " << gen_once() << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(args.delay_ms));
        }
    }
    return 0;
}
