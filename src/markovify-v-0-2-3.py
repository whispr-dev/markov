import markovify
import spacy
import re

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

def filter_by_length(text, min_length=7, max_length=10):
  sentences = text.split('.')
  filtered_sentences = [sent for sent in sentences if min_length <= len(sent.split()) <= max_length]
  return ' '.join(filtered_sentences)

def filter_by_keywords(text, include_keywords=[], exclude_keywords=[]):
  sentences = text.split('.')
  filtered_sentences = []

  include_patterns = [re.compile(r'\b' + keyword + r'\b', re.IGNORECASE) for keyword in include_keywords]
  exclude_patterns = [re.compile(r'\b' + keyword + r'\b', re.IGNORECASE) for keyword in exclude_keywords]

  for sentence in sentences:
    if any(pattern.search(sentence) for pattern in include_patterns) and not any(pattern.search(sentence) for pattern in exclude_patterns):
      filtered_sentences.append(sentence)
  return ' '.join(filtered_sentences)

def filter_by_pos(text, allowed_pos=['NOUN', 'VERB', 'ADJ']):
  nlp = spacy.load("en_core_web_sm")
  doc = nlp(text)
  filtered_text = ' '.join([token.text for token in doc if token.pos_ in allowed_pos])
  return filtered_text

# Example usage:
try:
    with open('text_data.txt', 'r', encoding='utf-8') as f:
        text = f.read()
except UnicodeDecodeError:
    # Handle the error, e.g., print a warning, try a different encoding, etc.
    print("Error decoding file")

text_model = markovify.Text(text)

longer_text = generate_longer_text(text_model, length=200)

print("Generated text:", longer_text)

filtered_text = filter_by_length(longer_text, min_length=3, max_length=50)
print("After length filter:", filtered_text)

filtered_text = filter_by_keywords(filtered_text, exclude_keywords=['moon', 'gun', 'ice', 'glowing'])
print("After keyword filter:", filtered_text)

filtered_text = filter_by_pos(filtered_text)
print("After POS filter:", filtered_text)

print("Final filtered text:", filtered_text)
