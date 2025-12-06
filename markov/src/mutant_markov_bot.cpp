
#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <ctime>

using namespace std;

// -- POS Fallback Builder --
vector<string> build_fallback_sentence() {
    static vector<string> det = {"the", "a", "this"};
    static vector<string> adj = {"weird", "shiny", "old", "dangerous"};
    static vector<string> noun = {"dog", "girl", "machine", "bottle", "voice"};
    static vector<string> verb = {"ran", "melted", "spoke", "broke", "exploded"};
    static vector<string> prep = {"into", "on", "under", "between"};

    vector<string> sentence;
    sentence.push_back(det[rand() % det.size()]);
    sentence.push_back(adj[rand() % adj.size()]);
    sentence.push_back(noun[rand() % noun.size()]);
    sentence.push_back(verb[rand() % verb.size()]);
    sentence.push_back(prep[rand() % prep.size()]);
    sentence.push_back(det[rand() % det.size()]);
    sentence.push_back(noun[rand() % noun.size()]);
    return sentence;
}

int main() {
    srand(static_cast<unsigned>(time(nullptr)));

    vector<string> sentence = build_fallback_sentence();

    if (!sentence.empty()) {
        cout << "Generated sentence: ";
        for (const auto& word : sentence) cout << word << " ";
        cout << endl;
    } else {
        cout << "Could not build fallback sentence." << endl;
    }

    return 0;
}
