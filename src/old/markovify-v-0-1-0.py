import markovify

def generate_longer_text(text_model, length=100):
  """Generates a longer text by iteratively concatenating sentences.

  Args:
    text_model: A Markovify Text model.
    length: The desired length of the generated text in words.

  Returns:
    A string containing the generated text.
  """

  generated_text = ""
  word_count = 0

  while word_count < length:
    sentence = text_model.make_sentence()
    if sentence:
      generated_text += sentence + " "
      word_count += len(sentence.split())

  return generated_text

# Example usage:
try:
    with open('text_data2.txt', 'r', encoding='utf-8') as f:
        text = f.read()
except UnicodeDecodeError:
    # Handle the error, e.g., print a warning, try a different encoding, etc.
    print("Error decoding file")

text_model = markovify.Text(text)

longer_text = generate_longer_text(text_model, length=200)
print(longer_text)