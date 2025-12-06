import markovify

try:
    with open('text_data3.txt', 'r', encoding='utf-8') as f:
        text = f.read()
except UnicodeDecodeError:
    # Handle the error, e.g., print a warning, try a different encoding, etc.
    print("Error decoding file")

text_model = markovify.Text(text, state_size=2)  # Adjust state size

for i in range(5):
    sentence = text_model.make_sentence()
    if sentence:
        print(sentence)