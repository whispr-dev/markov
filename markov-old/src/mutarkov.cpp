#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <random>

using namespace std;


vector<string> determiners = {"the", "a", "this"};
vector<string> adjectives = {"old", "shiny", "weird", "dangerous"};
vector<string> nouns = {"dog", "girl", "machine", "voice", "bottle"};
vector<string> chaos_nouns = {"weed", "drugs", "meth", "dope", "stuff", "ice", "beers", "b.m.r.", "shroomies", "spice", "methadone", "ACID!", "pills", "opium", "hash", "speed", "vodka"};
vector<string> verbs = {"ran", "broke", "exploded", "spoke", "melted"};
vector<string> prepositions = {"under", "on", "into", "between"};

random_device rd;
mt19937 rng(rd());
uniform_int_distribution<> chaos_chance(1, 100);

string choose_random(const vector<string>& list) {
    uniform_int_distribution<> dist(0, list.size() - 1);
    return list[dist(rng)];
}

string maybe_chaos_noun() {
    return chaos_chance(rng) <= 5 ? choose_random(chaos_nouns) : choose_random(nouns);
}

string generate_sentence() {
    string det1 = choose_random(determiners);
    string adj1 = choose_random(adjectives);
    string noun1 = maybe_chaos_noun();
    string verb = choose_random(verbs);
    string prep = choose_random(prepositions);
    string det2 = choose_random(determiners);
    string noun2 = maybe_chaos_noun();

    return det1 + " " + adj1 + " " + noun1 + " " + verb + " " + prep + " " + det2 + " " + noun2;
}

int main() {
    for (int i = 0; i < 20; ++i) {
        cout << "Generated sentence: " << generate_sentence() << endl;
    }
    return 0;
}
