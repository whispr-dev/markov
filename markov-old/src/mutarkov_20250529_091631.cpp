// mutarkov.cpp - Mutant Markov Bot with Adaptive Evolution
// By fren + woflfren, 20250529_091631

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <random>
#include <unordered_map>
#include <algorithm>
#include <ctime>

using namespace std;

random_device rd;
mt19937 rng(rd());

vector<string> nouns = {"dog", "girl", "machine", "bottle", "voice"};
vector<string> verbs = {"ran", "broke", "spoke", "exploded", "melted"};
vector<string> adjs = {"old", "shiny", "weird", "dangerous"};
vector<string> advs = {"quickly", "silently", "loudly", "smoothly"};
vector<string> dets = {"the", "a", "this"};
vector<string> preps = {"on", "under", "into", "between"};

vector<string> chaos_nouns = {"weed", "drugs", "meth", "dope", "stuff", "ice", "beers", "b.m.r.", "shroomies", "spice", "methadone", "ACID!", "pills", "opium", "hash", "speed", "vodka"};

string random_word(const vector<string>& list) {
    uniform_int_distribution<> dist(0, list.size() - 1);
    return list[dist(rng)];
}

string generate_sentence_pattern() {
    // Pattern: DET + ADJ + NOUN + VERB + PREP + DET + NOUN
    stringstream ss;

    ss << random_word(dets) << " ";
    ss << random_word(adjs) << " ";
    if (uniform_int_distribution<>(0, 99)(rng) < 5) {
        ss << random_word(chaos_nouns);  // 5% chance to drop chaos noun
    } else {
        ss << random_word(nouns);
    }

    ss << " " << random_word(verbs)
       << " " << random_word(preps)
       << " " << random_word(dets)
       << " " << random_word(nouns);

    return ss.str();
}

int main() {
    for (int i = 0; i < 20; ++i) {
        string sentence = generate_sentence_pattern();
        cout << "Generated sentence: " << sentence << endl;
    }

    return 0;
}
